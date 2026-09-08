#!/usr/bin/env bash
# Run an e2sar_perf loopback performance test over 127.0.0.1 (no control plane).
#
# This is the single-run engine used by loopback-matrix.sh. It can also be run
# standalone. In addition to running one sender/receiver pair it:
#   - accepts sender/receiver optimization selections (--send-opt/--recv-opt)
#   - computes the receiver port range the way the Reassembler does
#     (2^ceil(log2(threads))) so multi-thread runs open the right ports
#   - classifies the run as PASS / FAIL / SKIP, where SKIP means an optimization
#     was not compiled into this binary (e2sar_perf prints
#     "is not available on this platform" and exits non-zero before sending)
#   - supports negative regimes via --expect-fail "SUBSTRING"
#   - emits a single machine-readable RESULT line for the orchestrator to parse
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${E2SAR_BUILD_DIR:-$REPO_ROOT/build}"
PERF="$BUILD_DIR/bin/e2sar_perf"

# --- Color helpers (disabled when piped) ---
if [ -t 1 ]; then
    C_GREEN='\033[0;32m' C_RED='\033[0;31m' C_YELLOW='\033[0;33m'
    C_BOLD='\033[1m' C_RESET='\033[0m'
else
    C_GREEN='' C_RED='' C_YELLOW='' C_BOLD='' C_RESET=''
fi

log_info()  { echo -e "${C_GREEN}[INFO]${C_RESET} $*"; }
log_warn()  { echo -e "${C_YELLOW}[WARN]${C_RESET} $*" >&2; }
log_error() { echo -e "${C_RED}[ERROR]${C_RESET} $*" >&2; }
log_pass()  { echo -e "${C_BOLD}${C_GREEN}[PASS]${C_RESET} $*"; }
log_fail()  { echo -e "${C_BOLD}${C_RED}[FAIL]${C_RESET} $*"; }
log_skip()  { echo -e "${C_BOLD}${C_YELLOW}[SKIP]${C_RESET} $*"; }

# Strings e2sar_perf prints from Optimizations::select() (src/e2sarUtil.cpp).
NOT_AVAIL_MSG="is not available on this platform"
INCOMPATIBLE_MSG="are incompatible"

# --- Defaults ---
MTU=9000
RECV_THREADS=1
SEND_RATE=1.0
SEND_SOCKETS=4
EVENT_LENGTH=1000000
NUM_EVENTS=100
RECV_DURATION=60
EVENT_TIMEOUT=2000
SOCK_BUFSIZE=3145728
BASE_PORT=""
RECV_STARTUP_WAIT=1
DRAIN_WAIT=""            # default: derived from EVENT_TIMEOUT
SEND_OPTS=""            # space-separated optimization names for the sender
RECV_OPTS=""            # space-separated optimization names for the receiver
RCV_IOVEC_SIZE=""       # if set, passed to receiver as --rcviovecsize
SMOOTH=0                # pass --smooth to sender
EXPECT_FAIL=""          # negative regime: substring the sender stderr must contain
REGIME="single"        # label used in the RESULT line
RECV_LOSS_TOL=0         # allowed missing fragments before the recv check fails
ALLOW_LOSS=0            # downgrade a receiver fragment shortfall from FAIL to WARN

# The IPv4 on-wire header e2sar prepends per fragment (IP+UDP+LB+RE); see
# getTotalHeaderLength() in include/e2sarHeaders.hpp. Used for fragment math only.
HDR_LEN=64

# --- Usage ---
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run an e2sar_perf loopback performance test over 127.0.0.1 (no control plane).

OPTIONS:
  --mtu N          MTU size in bytes (default: $MTU)
  --threads N      Number of receiver threads (default: $RECV_THREADS)
  --rate F         Sender rate in Gbps (default: $SEND_RATE, negative=unlimited)
  --sockets N      Number of sender sockets (default: $SEND_SOCKETS, auto-raised
                   to the receiver port count so every port is exercised)
  --length N       Event buffer size in bytes (default: $EVENT_LENGTH)
  --num N          Number of events to send (default: $NUM_EVENTS)
  --duration N     Receiver max runtime in seconds (default: $RECV_DURATION)
  --timeout N      Event reassembly timeout in ms (default: $EVENT_TIMEOUT)
  --bufsize N      Socket buffer size in bytes (default: $SOCK_BUFSIZE)
  --port N         Starting UDP port (default: random in 22000-24999)
  --wait N         Seconds to wait for receiver startup (default: $RECV_STARTUP_WAIT)
  --drain N        Seconds to wait after sender finishes before stopping receiver
                   (default: EVENT_TIMEOUT/1000 + 1)
  --send-opt LIST  Space-separated sender optimizations (e.g. "sendmmsg" or
                   "liburing_send", or "sendmmsg liburing_send" for the conflict test)
  --recv-opt LIST  Space-separated receiver optimizations (e.g. "recvmmsg")
  --rcviovecsize N recvmmsg iovec batch size passed to the receiver
  --smooth         Pass --smooth to the sender
  --expect-fail S  Negative regime: PASS iff the sender exits non-zero AND its
                   stderr contains substring S (e.g. "are incompatible")
  --regime NAME    Label emitted in the RESULT line (default: $REGIME)
  --loss-tol N     Allowed missing fragments in the receiver check (default: $RECV_LOSS_TOL)
  --allow-loss     Downgrade a receiver fragment shortfall to a warning (default: FAIL)
  --build-dir DIR  Path to build directory (default: \$E2SAR_BUILD_DIR or build/)
  -h, --help       Show this help message

EXIT / RESULT:
  Emits a final line:
    RESULT regime=<name> status=<PASS|FAIL|SKIP> pkts=<n> errs=<n> tput=<g> goodput=<g>
  Exit code is 0 for PASS and SKIP, 1 for FAIL.

EXAMPLES:
  $(basename "$0")                                  # defaults, plain sendmsg/recvfrom
  $(basename "$0") --send-opt sendmmsg --recv-opt recvmmsg
  $(basename "$0") --send-opt liburing_send --recv-opt recvmmsg --threads 4
  $(basename "$0") --send-opt "sendmmsg liburing_send" --expect-fail "are incompatible"
EOF
}

# --- Argument parsing ---
while [[ $# -gt 0 ]]; do
    case $1 in
        --mtu)         MTU="$2";               shift 2 ;;
        --threads)     RECV_THREADS="$2";       shift 2 ;;
        --rate)        SEND_RATE="$2";          shift 2 ;;
        --sockets)     SEND_SOCKETS="$2";       shift 2 ;;
        --length)      EVENT_LENGTH="$2";       shift 2 ;;
        --num)         NUM_EVENTS="$2";         shift 2 ;;
        --duration)    RECV_DURATION="$2";      shift 2 ;;
        --timeout)     EVENT_TIMEOUT="$2";      shift 2 ;;
        --bufsize)     SOCK_BUFSIZE="$2";       shift 2 ;;
        --port)        BASE_PORT="$2";          shift 2 ;;
        --wait)        RECV_STARTUP_WAIT="$2";  shift 2 ;;
        --drain)       DRAIN_WAIT="$2";         shift 2 ;;
        --send-opt)    SEND_OPTS="${SEND_OPTS:+$SEND_OPTS }$2"; shift 2 ;;
        --recv-opt)    RECV_OPTS="${RECV_OPTS:+$RECV_OPTS }$2"; shift 2 ;;
        --rcviovecsize) RCV_IOVEC_SIZE="$2";    shift 2 ;;
        --smooth)      SMOOTH=1;                shift ;;
        --expect-fail) EXPECT_FAIL="$2";        shift 2 ;;
        --regime)      REGIME="$2";             shift 2 ;;
        --loss-tol)    RECV_LOSS_TOL="$2";      shift 2 ;;
        --allow-loss)  ALLOW_LOSS=1;            shift ;;
        --build-dir)   BUILD_DIR="$2"; PERF="$BUILD_DIR/bin/e2sar_perf"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *)             log_error "Unknown option: $1"; usage >&2; exit 2 ;;
    esac
done

# --- Smallest power of two >= threads (matches Reassembler get_PortRange) ---
num_ports_for_threads() {
    local t="$1" p=1
    if [[ "$t" -le 1 ]]; then echo 1; return; fi
    while [[ "$p" -lt "$t" ]]; do p=$((p * 2)); done
    echo "$p"
}

NUM_PORTS=$(num_ports_for_threads "$RECV_THREADS")

# To spread the sender across every receive port the sender needs at least as
# many sockets as there are ports (stride = rangeSize / numSendSockets).
if [[ "$SEND_SOCKETS" -lt "$NUM_PORTS" ]]; then
    log_warn "Raising sender sockets from $SEND_SOCKETS to $NUM_PORTS to cover all receive ports"
    SEND_SOCKETS="$NUM_PORTS"
fi

# --- Port randomization ---
if [[ -z "$BASE_PORT" ]]; then
    BASE_PORT=$(( 22000 + (RANDOM % 3000) ))
fi

# --- Drain wait: default to event timeout (ms->s) + 1s headroom ---
if [[ -z "$DRAIN_WAIT" ]]; then
    DRAIN_WAIT=$(( EVENT_TIMEOUT / 1000 + 1 ))
fi

END_PORT=$(( BASE_PORT + NUM_PORTS - 1 ))
URI="ejfat://token@127.0.0.1:18020/lb/1?data=127.0.0.1:${BASE_PORT}-${END_PORT}"

# --- Expected fragment math (best-effort; used only for the recv-side check) ---
MAX_PLD=$(( MTU - HDR_LEN ))
if [[ "$MAX_PLD" -lt 1 ]]; then MAX_PLD=1; fi
FRAGS_PER_EVENT=$(( (EVENT_LENGTH + MAX_PLD - 1) / MAX_PLD ))
EXPECTED_FRAGS=$(( NUM_EVENTS * FRAGS_PER_EVENT ))

# --- Validate binary ---
if [[ ! -x "$PERF" ]]; then
    log_error "$PERF not found or not executable."
    log_error "Build the project first, or set --build-dir / E2SAR_BUILD_DIR."
    exit 1
fi

# --- Build optimization argument arrays (word-splitting is intentional) ---
SEND_OPT_ARGS=()
if [[ -n "$SEND_OPTS" ]]; then SEND_OPT_ARGS=(-o $SEND_OPTS); fi
RECV_OPT_ARGS=()
if [[ -n "$RECV_OPTS" ]]; then RECV_OPT_ARGS=(-o $RECV_OPTS); fi
SEND_EXTRA_ARGS=()
if [[ "$SMOOTH" -eq 1 ]]; then SEND_EXTRA_ARGS+=(--smooth); fi
RECV_EXTRA_ARGS=()
if [[ -n "$RCV_IOVEC_SIZE" ]]; then RECV_EXTRA_ARGS+=(--rcviovecsize "$RCV_IOVEC_SIZE"); fi

# --- Configuration summary ---
log_info "=== E2SAR Loopback Performance Test (regime: $REGIME) ==="
log_info "Binary:                $PERF"
log_info "URI:                   $URI"
log_info "Control plane:         OFF"
log_info "--- Sender ---"
log_info "  MTU:                 $MTU"
log_info "  Rate (Gbps):         $SEND_RATE"
log_info "  Sockets:             $SEND_SOCKETS"
log_info "  Event size (bytes):  $EVENT_LENGTH"
log_info "  Num events:          $NUM_EVENTS"
log_info "  Optimizations:       ${SEND_OPTS:-none}${SMOOTH:+ (smooth)}"
log_info "  Socket buffer:       $SOCK_BUFSIZE"
log_info "--- Receiver ---"
log_info "  Threads:             $RECV_THREADS"
log_info "  Ports:               $BASE_PORT-$END_PORT ($NUM_PORTS)"
log_info "  Optimizations:       ${RECV_OPTS:-none}${RCV_IOVEC_SIZE:+ (rcviovecsize=$RCV_IOVEC_SIZE)}"
log_info "  Duration (sec):      $RECV_DURATION"
log_info "  Event timeout (ms):  $EVENT_TIMEOUT"
log_info "  Socket buffer:       $SOCK_BUFSIZE"
log_info "  Drain wait (sec):    $DRAIN_WAIT"
if [[ -n "$EXPECT_FAIL" ]]; then
    log_info "  Expect failure:      substring \"$EXPECT_FAIL\""
fi
log_info "  Expected fragments:  ~$EXPECTED_FRAGS"
log_info "========================================="

# --- Temp files for output capture ---
RECV_LOG=$(mktemp /tmp/e2sar-recv-XXXXXX.log)
SEND_LOG=$(mktemp /tmp/e2sar-send-XXXXXX.log)

# --- Cleanup trap ---
RECV_PID=""
cleanup() {
    if [[ -n "$RECV_PID" ]] && kill -0 "$RECV_PID" 2>/dev/null; then
        kill -INT "$RECV_PID" 2>/dev/null || true
        wait "$RECV_PID" 2>/dev/null || true
    fi
    rm -f "$RECV_LOG" "$SEND_LOG"
}
trap cleanup EXIT

# --- Emit the machine-readable RESULT line and exit ---
# Usage: finalize <PASS|FAIL|SKIP>
finalize() {
    local status="$1"
    local pkts="${SEND_PKTS:-N/A}" errs="${SEND_ERRS:-N/A}"
    local tput="${SEND_THROUGHPUT:-N/A}" goodput="${SEND_GOODPUT:-N/A}"
    echo ""
    echo "RESULT regime=${REGIME} status=${status} pkts=${pkts} errs=${errs} tput=${tput} goodput=${goodput}"
    case "$status" in
        PASS) log_pass "Regime '$REGIME' completed successfully"; exit 0 ;;
        SKIP) log_skip "Regime '$REGIME' skipped"; exit 0 ;;
        *)    log_fail "Regime '$REGIME' FAILED"; exit 1 ;;
    esac
}

# --- Start receiver in background ---
log_info "Starting receiver..."
"$PERF" -r \
    --ip 127.0.0.1 \
    --port "$BASE_PORT" \
    --threads "$RECV_THREADS" \
    --duration "$RECV_DURATION" \
    --timeout "$EVENT_TIMEOUT" \
    --bufsize "$SOCK_BUFSIZE" \
    ${RECV_OPT_ARGS[@]+"${RECV_OPT_ARGS[@]}"} \
    ${RECV_EXTRA_ARGS[@]+"${RECV_EXTRA_ARGS[@]}"} \
    --quiet \
    -u "$URI" \
    >"$RECV_LOG" 2>&1 &
RECV_PID=$!

sleep "$RECV_STARTUP_WAIT"

if ! kill -0 "$RECV_PID" 2>/dev/null; then
    # Receiver died during startup. Distinguish "optimization not compiled in"
    # (SKIP) from a genuine startup failure (FAIL).
    wait "$RECV_PID" 2>/dev/null || true
    RECV_PID=""
    log_info "=== Receiver Output ==="
    cat "$RECV_LOG"
    if grep -qF "$NOT_AVAIL_MSG" "$RECV_LOG"; then
        log_skip "Receiver optimization not available on this platform"
        finalize SKIP
    fi
    log_error "Receiver failed to start."
    finalize FAIL
fi
log_info "Receiver running (PID $RECV_PID)"

# --- Run sender synchronously ---
log_info "Starting sender..."
set +e
"$PERF" -s \
    --ip 127.0.0.1 \
    --mtu "$MTU" \
    --rate "$SEND_RATE" \
    --sockets "$SEND_SOCKETS" \
    --length "$EVENT_LENGTH" \
    -n "$NUM_EVENTS" \
    --bufsize "$SOCK_BUFSIZE" \
    ${SEND_OPT_ARGS[@]+"${SEND_OPT_ARGS[@]}"} \
    ${SEND_EXTRA_ARGS[@]+"${SEND_EXTRA_ARGS[@]}"} \
    -u "$URI" \
    >"$SEND_LOG" 2>&1
SEND_EXIT=$?
set -e

# --- Stop receiver gracefully ---
log_info "Sender finished (exit=$SEND_EXIT). Waiting ${DRAIN_WAIT}s for reassembly to drain..."
sleep "$DRAIN_WAIT"
if kill -0 "$RECV_PID" 2>/dev/null; then
    kill -INT "$RECV_PID" 2>/dev/null || true
fi
set +e
wait "$RECV_PID" 2>/dev/null
RECV_EXIT=$?
set -e
# SIGINT causes exit code 130 (128+2) which is expected
if [[ $RECV_EXIT -eq 130 ]]; then RECV_EXIT=0; fi
RECV_PID=""
log_info "Receiver finished (exit=$RECV_EXIT)"

# --- Display logs ---
echo ""
log_info "=== Sender Output ==="
cat "$SEND_LOG"
echo ""
log_info "=== Receiver Output ==="
cat "$RECV_LOG"
echo ""

# --- Parse stats (portable: no GNU grep -P) ---
SEND_PKTS=$(sed -n 's/Completed, \([0-9,]*\) packets sent.*/\1/p' "$SEND_LOG" || echo "N/A")
SEND_ERRS=$(sed -n 's/.*Completed,.* \([0-9,]*\) errors/\1/p' "$SEND_LOG" || echo "N/A")
SEND_THROUGHPUT=$(awk '/Estimated effective throughput/{print $NF}' "$SEND_LOG" || echo "N/A")
SEND_GOODPUT=$(awk '/Estimated goodput/{print $NF}' "$SEND_LOG" || echo "N/A")
RECV_TOTAL=$(sed -n 's/^Total: \([0-9,]*\).*/\1/p' "$RECV_LOG" | tail -1 || echo "")

: "${SEND_PKTS:=N/A}"
: "${SEND_ERRS:=N/A}"
: "${SEND_THROUGHPUT:=N/A}"
: "${SEND_GOODPUT:=N/A}"

# --- Negative regime: PASS iff sender failed with the expected message ---
if [[ -n "$EXPECT_FAIL" ]]; then
    if [[ $SEND_EXIT -ne 0 ]] && grep -qF "$EXPECT_FAIL" "$SEND_LOG"; then
        log_pass "Sender failed as expected (matched \"$EXPECT_FAIL\")"
        finalize PASS
    fi
    log_fail "Expected sender failure matching \"$EXPECT_FAIL\" did not occur"
    finalize FAIL
fi

# --- SKIP: an optimization was not compiled into this binary ---
if grep -qF "$NOT_AVAIL_MSG" "$SEND_LOG" || grep -qF "$NOT_AVAIL_MSG" "$RECV_LOG"; then
    log_skip "An optimization is not available on this platform"
    finalize SKIP
fi

# --- Normal pass/fail ---
log_info "=== Results Summary ==="
log_info "  Packets sent:              $SEND_PKTS"
log_info "  Send errors:               $SEND_ERRS"
log_info "  Effective throughput:       $SEND_THROUGHPUT Gbps"
log_info "  Goodput:                    $SEND_GOODPUT Gbps"
log_info "  Fragments received:         ${RECV_TOTAL:-N/A} (expected ~$EXPECTED_FRAGS)"

RESULT=0

if [[ $SEND_EXIT -ne 0 ]]; then
    log_fail "Sender exited with error code $SEND_EXIT"
    RESULT=1
fi

if [[ $RECV_EXIT -ne 0 ]]; then
    log_fail "Receiver exited with error code $RECV_EXIT"
    RESULT=1
fi

# e2sar_perf returns exit 0 even when the Segmenter/Reassembler throws (e.g. a
# socket it could not open because the system buffer limit was too low); the
# only reliable signal is this log line. Treat it as a hard failure.
if grep -qF "encountered an error" "$SEND_LOG"; then
    log_fail "Sender reported a dataplane error (see output above)"
    RESULT=1
fi
if grep -qF "encountered an error" "$RECV_LOG"; then
    log_fail "Receiver reported a dataplane error (see output above)"
    RESULT=1
fi

SEND_ERRS_CLEAN=$(echo "$SEND_ERRS" | tr -d ',')
if [[ "$SEND_ERRS_CLEAN" =~ ^[0-9]+$ ]] && [[ "$SEND_ERRS_CLEAN" -gt 0 ]]; then
    log_fail "Sender reported $SEND_ERRS errors"
    RESULT=1
fi

# The sender must have completed and reported a packet count; "N/A" means it
# aborted before finishing (and did not print a numeric "errors" count either).
if [[ "$SEND_PKTS" == "N/A" ]]; then
    log_fail "Sender did not report a completion/packet count (aborted early)"
    RESULT=1
fi

# --- Receiver fragment-count check (FAIL by default, WARN with --allow-loss) ---
RECV_TOTAL_CLEAN=$(echo "${RECV_TOTAL:-}" | tr -d ',')
if [[ "$RECV_TOTAL_CLEAN" =~ ^[0-9]+$ ]]; then
    MIN_OK=$(( EXPECTED_FRAGS - RECV_LOSS_TOL ))
    if [[ "$RECV_TOTAL_CLEAN" -lt "$MIN_OK" ]]; then
        if [[ "$ALLOW_LOSS" -eq 1 ]]; then
            log_warn "Receiver got $RECV_TOTAL_CLEAN fragments, expected >= $MIN_OK (tolerated via --allow-loss)"
        else
            log_fail "Receiver got $RECV_TOTAL_CLEAN fragments, expected >= $MIN_OK"
            RESULT=1
        fi
    fi
else
    log_warn "Could not parse receiver fragment total; skipping loss check"
fi

if [[ $RESULT -eq 0 ]]; then
    finalize PASS
else
    finalize FAIL
fi
