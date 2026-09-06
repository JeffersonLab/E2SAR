#!/usr/bin/env bash
# Run an e2sar_perf loopback performance test over 127.0.0.1 (no control plane).
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
DRAIN_WAIT=""   # default: derived from EVENT_TIMEOUT

# --- Usage ---
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run an e2sar_perf loopback performance test over 127.0.0.1 (no control plane).

OPTIONS:
  --mtu N          MTU size in bytes (default: $MTU)
  --threads N      Number of receiver threads (default: $RECV_THREADS)
  --rate F         Sender rate in Gbps (default: $SEND_RATE, negative=unlimited)
  --sockets N      Number of sender sockets (default: $SEND_SOCKETS)
  --length N       Event buffer size in bytes (default: $EVENT_LENGTH)
  --num N          Number of events to send (default: $NUM_EVENTS)
  --duration N     Receiver max runtime in seconds (default: $RECV_DURATION)
  --timeout N      Event reassembly timeout in ms (default: $EVENT_TIMEOUT)
  --bufsize N      Socket buffer size in bytes (default: $SOCK_BUFSIZE)
  --port N         Starting UDP port (default: random in 22000-24999)
  --wait N         Seconds to wait for receiver startup (default: $RECV_STARTUP_WAIT)
  --drain N        Seconds to wait after sender finishes before stopping receiver
                   (default: EVENT_TIMEOUT/1000 + 1)
  --build-dir DIR  Path to build directory (default: \$E2SAR_BUILD_DIR or build/)
  -h, --help       Show this help message

EXAMPLES:
  $(basename "$0")                          # Run with all defaults
  $(basename "$0") --rate 10 --mtu 9000     # 10 Gbps with jumbo frames
  $(basename "$0") --num 1000 --threads 4   # More events, 4 recv threads
EOF
    exit 0
}

# --- Argument parsing ---
while [[ $# -gt 0 ]]; do
    case $1 in
        --mtu)       MTU="$2";               shift 2 ;;
        --threads)   RECV_THREADS="$2";       shift 2 ;;
        --rate)      SEND_RATE="$2";          shift 2 ;;
        --sockets)   SEND_SOCKETS="$2";       shift 2 ;;
        --length)    EVENT_LENGTH="$2";       shift 2 ;;
        --num)       NUM_EVENTS="$2";         shift 2 ;;
        --duration)  RECV_DURATION="$2";      shift 2 ;;
        --timeout)   EVENT_TIMEOUT="$2";      shift 2 ;;
        --bufsize)   SOCK_BUFSIZE="$2";       shift 2 ;;
        --port)      BASE_PORT="$2";          shift 2 ;;
        --wait)      RECV_STARTUP_WAIT="$2";  shift 2 ;;
        --drain)     DRAIN_WAIT="$2";         shift 2 ;;
        --build-dir) BUILD_DIR="$2"; PERF="$BUILD_DIR/bin/e2sar_perf"; shift 2 ;;
        -h|--help)   usage ;;
        *)           log_error "Unknown option: $1"; usage ;;
    esac
done

# --- Port randomization ---
if [[ -z "$BASE_PORT" ]]; then
    BASE_PORT=$(( 22000 + (RANDOM % 3000) ))
fi

# --- Drain wait: default to event timeout (ms→s) + 1s headroom ---
if [[ -z "$DRAIN_WAIT" ]]; then
    DRAIN_WAIT=$(( EVENT_TIMEOUT / 1000 + 1 ))
fi

END_PORT=$(( BASE_PORT + RECV_THREADS - 1 ))
URI="ejfat://token@127.0.0.1:18020/lb/1?data=127.0.0.1:${BASE_PORT}-${END_PORT}"

# --- Validate binary ---
if [[ ! -x "$PERF" ]]; then
    log_error "$PERF not found or not executable."
    log_error "Build the project first, or set --build-dir / E2SAR_BUILD_DIR."
    exit 1
fi

# --- Configuration summary ---
log_info "=== E2SAR Loopback Performance Test ==="
log_info "Binary:                $PERF"
log_info "URI:                   $URI"
log_info "Control plane:         OFF"
log_info "--- Sender ---"
log_info "  MTU:                 $MTU"
log_info "  Rate (Gbps):         $SEND_RATE"
log_info "  Sockets:             $SEND_SOCKETS"
log_info "  Event size (bytes):  $EVENT_LENGTH"
log_info "  Num events:          $NUM_EVENTS"
log_info "  Socket buffer:       $SOCK_BUFSIZE"
log_info "--- Receiver ---"
log_info "  Threads:             $RECV_THREADS"
log_info "  Start port:          $BASE_PORT"
log_info "  Duration (sec):      $RECV_DURATION"
log_info "  Event timeout (ms):  $EVENT_TIMEOUT"
log_info "  Socket buffer:       $SOCK_BUFSIZE"
log_info "  Drain wait (sec):    $DRAIN_WAIT"
log_info "========================================="

# --- Temp files for output capture ---
RECV_LOG=$(mktemp /tmp/e2sar-recv-XXXXXX.log)
SEND_LOG=$(mktemp /tmp/e2sar-send-XXXXXX.log)

# --- Cleanup trap ---
RECV_PID=""
cleanup() {
    if [[ -n "$RECV_PID" ]] && kill -0 "$RECV_PID" 2>/dev/null; then
        log_warn "Cleaning up: sending SIGINT to receiver (PID $RECV_PID)"
        kill -INT "$RECV_PID" 2>/dev/null || true
        wait "$RECV_PID" 2>/dev/null || true
    fi
    rm -f "$RECV_LOG" "$SEND_LOG"
}
trap cleanup EXIT

# --- Start receiver in background ---
log_info "Starting receiver..."
"$PERF" -r \
    --ip 127.0.0.1 \
    --port "$BASE_PORT" \
    --threads "$RECV_THREADS" \
    --duration "$RECV_DURATION" \
    --timeout "$EVENT_TIMEOUT" \
    --bufsize "$SOCK_BUFSIZE" \
    --quiet \
    -u "$URI" \
    >"$RECV_LOG" 2>&1 &
RECV_PID=$!

sleep "$RECV_STARTUP_WAIT"

if ! kill -0 "$RECV_PID" 2>/dev/null; then
    log_error "Receiver failed to start. Log output:"
    cat "$RECV_LOG" >&2
    RECV_PID=""
    exit 1
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
    -u "$URI" \
    >"$SEND_LOG" 2>&1
SEND_EXIT=$?
set -e

# --- Stop receiver gracefully ---
log_info "Sender finished (exit=$SEND_EXIT). Waiting ${DRAIN_WAIT}s for reassembly to drain..."
sleep "$DRAIN_WAIT"
log_info "Stopping receiver..."
if kill -0 "$RECV_PID" 2>/dev/null; then
    kill -INT "$RECV_PID" 2>/dev/null || true
fi
wait "$RECV_PID" 2>/dev/null || true
RECV_EXIT=$?
# SIGINT causes exit code 130 (128+2) which is expected
if [[ $RECV_EXIT -eq 130 ]]; then
    RECV_EXIT=0
fi
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

: "${SEND_PKTS:=N/A}"
: "${SEND_ERRS:=N/A}"
: "${SEND_THROUGHPUT:=N/A}"
: "${SEND_GOODPUT:=N/A}"

log_info "=== Results Summary ==="
log_info "  Packets sent:              $SEND_PKTS"
log_info "  Send errors:               $SEND_ERRS"
log_info "  Effective throughput:       $SEND_THROUGHPUT Gbps"
log_info "  Goodput:                    $SEND_GOODPUT Gbps"

# --- Pass/fail ---
RESULT=0

if [[ $SEND_EXIT -ne 0 ]]; then
    log_fail "Sender exited with error code $SEND_EXIT"
    RESULT=1
fi

if [[ $RECV_EXIT -ne 0 ]]; then
    log_fail "Receiver exited with error code $RECV_EXIT"
    RESULT=1
fi

SEND_ERRS_CLEAN=$(echo "$SEND_ERRS" | tr -d ',')
if [[ "$SEND_ERRS_CLEAN" =~ ^[0-9]+$ ]] && [[ "$SEND_ERRS_CLEAN" -gt 0 ]]; then
    log_fail "Sender reported $SEND_ERRS errors"
    RESULT=1
fi

if [[ $RESULT -eq 0 ]]; then
    log_pass "Loopback performance test completed successfully"
else
    log_fail "Loopback performance test FAILED"
fi

exit $RESULT
