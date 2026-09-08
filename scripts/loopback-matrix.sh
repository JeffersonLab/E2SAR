#!/usr/bin/env bash
# Drive perf-loopback.sh across the full dataplane regime matrix (§2 of
# notes/loopback_test_scaffold.md) and tally PASS/FAIL/SKIP.
#
# Each regime is one back-to-back sender+receiver run over 127.0.0.1 with the
# control plane off. Regimes whose optimization is not compiled into this
# e2sar_perf binary are SKIPped (both proactively, from the "Available
# Optimizations" probe, and defensively, from the per-run select() error that
# perf-loopback.sh reports as status=SKIP).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENGINE="$SCRIPT_DIR/perf-loopback.sh"

# --- Color helpers (disabled when piped) ---
if [ -t 1 ]; then
    C_GREEN='\033[0;32m' C_RED='\033[0;31m' C_YELLOW='\033[0;33m'
    C_BLUE='\033[0;34m' C_BOLD='\033[1m' C_RESET='\033[0m'
else
    C_GREEN='' C_RED='' C_YELLOW='' C_BLUE='' C_BOLD='' C_RESET=''
fi
log_info()  { echo -e "${C_GREEN}[INFO]${C_RESET} $*"; }
log_warn()  { echo -e "${C_YELLOW}[WARN]${C_RESET} $*" >&2; }
log_error() { echo -e "${C_RED}[ERROR]${C_RESET} $*" >&2; }
log_head()  { echo -e "${C_BOLD}${C_BLUE}==== $* ====${C_RESET}"; }

# --- Defaults (§3.2) ---
BUILD_DIR="${E2SAR_BUILD_DIR:-$REPO_ROOT/build}"
RATE=1.0
MTU=1500
LENGTH=1000000       # 1 MB base event for a*/b*/c*
NUM=100
THREADS=4            # multi-thread regimes -> 4 ports
BIG_LENGTH=2097152   # 2 MB  (s1/s3)
HUGE_LENGTH=4194304  # 4 MB  (s2)
TIMEOUT=2000         # reassembly timeout (ms)
BUFSIZE=3145728      # 3 MB socket buffers
SPECIAL_MTU=1500     # fixed MTU for IOV_MAX/ring regimes
OUT_TSV=""
LOSS_TOL=""          # forwarded to the engine's --loss-tol if set
ALLOW_LOSS=0         # forwarded to the engine's --allow-loss if set
PORT_START=""        # base UDP port; randomized if empty
PORT_STEP=64         # per-regime port offset to dodge TIME_WAIT reuse
REGIMES_ARG=""
ONLY_SPECIAL=0
ONLY_MULTI=0
INCLUDE_N2=0
INTER_REGIME_SLEEP=1

# Full default matrix order (n2 is opt-in only; see §2).
DEFAULT_REGIMES="a1 b1 c1 a2 b2 c2 s1 s2 s3 s4 s5 n1"
MULTI_REGIMES="a2 b2 c2"
SPECIAL_REGIMES="s1 s2 s3 s4 s5"

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Run the e2sar_perf loopback dataplane regime matrix (control plane off).

REGIME SELECTION:
  --regimes LIST   Comma/space list to run (default: all of $DEFAULT_REGIMES)
  --only-multi     Run only the multi-thread regimes ($MULTI_REGIMES)
  --only-special   Run only the special-condition regimes ($SPECIAL_REGIMES)
  --with-n2        Also run the optional smooth-guard negative regime (n2)

GLOBAL KNOBS (sane defaults, all overridable):
  --rate F         Sender rate in Gbps (default: $RATE, negative=unlimited)
  --mtu N          MTU for a*/b*/c* regimes (default: $MTU)
  --length N       Base event size for a*/b*/c* (default: $LENGTH)
  --num N          Events per run (default: $NUM)
  --threads N      Threads for multi-thread regimes (default: $THREADS)
  --big-length N   Event size for s1/s3 (default: $BIG_LENGTH)
  --huge-length N  Event size for s2 (default: $HUGE_LENGTH)
  --timeout N      Reassembly timeout in ms (default: $TIMEOUT)
  --bufsize N      Socket buffer size in bytes (default: $BUFSIZE)
  --loss-tol N     Allowed missing fragments per run (forwarded to the engine)
  --allow-loss     Downgrade a fragment shortfall to a warning (forwarded)
  --port N         Base UDP port (default: random; +$PORT_STEP per regime)
  --build-dir DIR  Build directory holding bin/e2sar_perf (default: $BUILD_DIR)
  --out FILE       Write a machine-readable TSV summary to FILE
  -h, --help       Show this help

EXIT: non-zero if any non-skipped regime FAILED.
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --regimes)     REGIMES_ARG="$2"; shift 2 ;;
        --only-multi)  ONLY_MULTI=1; shift ;;
        --only-special) ONLY_SPECIAL=1; shift ;;
        --with-n2)     INCLUDE_N2=1; shift ;;
        --rate)        RATE="$2"; shift 2 ;;
        --mtu)         MTU="$2"; shift 2 ;;
        --length)      LENGTH="$2"; shift 2 ;;
        --num)         NUM="$2"; shift 2 ;;
        --threads)     THREADS="$2"; shift 2 ;;
        --big-length)  BIG_LENGTH="$2"; shift 2 ;;
        --huge-length) HUGE_LENGTH="$2"; shift 2 ;;
        --timeout)     TIMEOUT="$2"; shift 2 ;;
        --bufsize)     BUFSIZE="$2"; shift 2 ;;
        --loss-tol)    LOSS_TOL="$2"; shift 2 ;;
        --allow-loss)  ALLOW_LOSS=1; shift ;;
        --port)        PORT_START="$2"; shift 2 ;;
        --build-dir)   BUILD_DIR="$2"; shift 2 ;;
        --out)         OUT_TSV="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *)             log_error "Unknown option: $1"; usage >&2; exit 2 ;;
    esac
done

# Per-run knobs forwarded verbatim to perf-loopback.sh.
ENGINE_EXTRA=()
if [[ -n "$LOSS_TOL" ]]; then ENGINE_EXTRA+=(--loss-tol "$LOSS_TOL"); fi
if [[ "$ALLOW_LOSS" -eq 1 ]]; then ENGINE_EXTRA+=(--allow-loss); fi

PERF="$BUILD_DIR/bin/e2sar_perf"
if [[ ! -x "$PERF" ]]; then
    log_error "$PERF not found or not executable (set --build-dir / E2SAR_BUILD_DIR)."
    exit 1
fi
if [[ ! -x "$ENGINE" ]]; then
    log_error "Engine script $ENGINE not found or not executable."
    exit 1
fi

# --- Base port ---
if [[ -z "$PORT_START" ]]; then
    PORT_START=$(( 22000 + (RANDOM % 2000) ))
fi

# --- Resolve regime list ---
if [[ "$ONLY_MULTI" -eq 1 ]]; then
    REGIMES="$MULTI_REGIMES"
elif [[ "$ONLY_SPECIAL" -eq 1 ]]; then
    REGIMES="$SPECIAL_REGIMES"
elif [[ -n "$REGIMES_ARG" ]]; then
    REGIMES="${REGIMES_ARG//,/ }"
else
    REGIMES="$DEFAULT_REGIMES"
    if [[ "$INCLUDE_N2" -eq 1 ]]; then REGIMES="$REGIMES n2"; fi
fi

# --- Probe available optimizations (§3.2 step 1, pre-filter) ---
AVAIL=""
probe_available() {
    local help line
    help="$("$PERF" --help 2>&1 || true)"
    line="$(echo "$help" | grep -i 'Available Optimizations' | head -1)"
    # Strip label, normalize commas to spaces, collapse whitespace.
    AVAIL=" $(echo "${line#*:}" | tr ',' ' ' | tr -s ' ') "
}
opt_available() {
    case "$AVAIL" in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

probe_available
log_head "E2SAR Loopback Regime Matrix"
log_info "Binary:           $PERF"
log_info "Available opts:   ${AVAIL:-<none parsed>}"
log_info "Regimes:          $REGIMES"
log_info "Base port:        $PORT_START (+$PORT_STEP per regime)"
log_info "rate=$RATE mtu=$MTU len=$LENGTH num=$NUM threads=$THREADS timeout=$TIMEOUT"
echo ""

# --- Optimizations each regime requires ---
regime_required_opts() {
    case "$1" in
        a1|a2)          echo "" ;;
        b1|b2|s1|s2)    echo "sendmmsg recvmmsg" ;;
        c1|c2|s3)       echo "liburing_send recvmmsg" ;;
        s4|s5)          echo "recvmmsg" ;;
        n1)             echo "sendmmsg liburing_send" ;;
        n2)             echo "sendmmsg" ;;
        *)              echo "__unknown__" ;;
    esac
}

# --- Build the perf-loopback.sh argument list for a regime ---
build_args() {
    # populates global ARGS array; uses regime-specific mtu/length/threads
    local name="$1" port="$2"
    local mtu="$MTU" length="$LENGTH" threads=1
    ARGS=(--regime "$name" --build-dir "$BUILD_DIR" --port "$port"
          --rate "$RATE" --num "$NUM" --timeout "$TIMEOUT" --bufsize "$BUFSIZE")
    case "$name" in
        a1) ;;
        b1) ARGS+=(--send-opt sendmmsg --recv-opt recvmmsg) ;;
        c1) ARGS+=(--send-opt liburing_send --recv-opt recvmmsg) ;;
        a2) threads="$THREADS" ;;
        b2) threads="$THREADS"; ARGS+=(--send-opt sendmmsg --recv-opt recvmmsg) ;;
        c2) threads="$THREADS"; ARGS+=(--send-opt liburing_send --recv-opt recvmmsg) ;;
        s1) mtu="$SPECIAL_MTU"; length="$BIG_LENGTH";  ARGS+=(--send-opt sendmmsg --recv-opt recvmmsg) ;;
        s2) mtu="$SPECIAL_MTU"; length="$HUGE_LENGTH"; ARGS+=(--send-opt sendmmsg --recv-opt recvmmsg) ;;
        s3) mtu="$SPECIAL_MTU"; length="$BIG_LENGTH";  ARGS+=(--send-opt liburing_send --recv-opt recvmmsg) ;;
        s4) ARGS+=(--recv-opt recvmmsg --rcviovecsize 1) ;;
        s5) ARGS+=(--recv-opt recvmmsg --rcviovecsize 2048) ;;
        n1) ARGS+=(--send-opt "sendmmsg liburing_send" --expect-fail "are incompatible") ;;
        n2) ARGS+=(--send-opt sendmmsg --smooth --expect-fail "incompatible") ;;
    esac
    ARGS+=(--mtu "$mtu" --length "$length" --threads "$threads")

    # §3.2 sizing note: warn if an IOV_MAX regime won't actually cross the boundary.
    case "$name" in
        s1|s2)
            local maxpld=$(( mtu - 64 ))
            local threshold=$(( 1024 * maxpld ))
            if [[ "$length" -le "$threshold" ]]; then
                log_warn "$name: length $length <= 1024*(mtu-64)=$threshold; sendmmsg IOV_MAX loop not exercised"
            fi
            ;;
    esac
}

# --- Tallies and TSV ---
declare -a SUMMARY_ROWS=()
N_PASS=0 N_FAIL=0 N_SKIP=0
if [[ -n "$OUT_TSV" ]]; then
    printf 'regime\tstatus\tpkts\terrs\ttput_gbps\tgoodput_gbps\n' > "$OUT_TSV"
fi

record() {
    local regime="$1" status="$2" pkts="$3" errs="$4" tput="$5" goodput="$6"
    SUMMARY_ROWS+=("$(printf '%-6s %-6s pkts=%-10s errs=%-6s tput=%-8s goodput=%-8s' \
        "$regime" "$status" "$pkts" "$errs" "$tput" "$goodput")")
    if [[ -n "$OUT_TSV" ]]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$regime" "$status" "$pkts" "$errs" "$tput" "$goodput" >> "$OUT_TSV"
    fi
    case "$status" in
        PASS) N_PASS=$((N_PASS + 1)) ;;
        SKIP) N_SKIP=$((N_SKIP + 1)) ;;
        *)    N_FAIL=$((N_FAIL + 1)) ;;
    esac
}

# --- Run the matrix ---
idx=0
for regime in $REGIMES; do
    req="$(regime_required_opts "$regime")"
    if [[ "$req" == "__unknown__" ]]; then
        log_warn "Unknown regime '$regime' — skipping"
        record "$regime" SKIP - - - -
        continue
    fi

    port=$(( PORT_START + idx * PORT_STEP ))
    idx=$((idx + 1))

    # Pre-filter: SKIP regimes whose optimizations are not compiled in.
    missing=""
    for opt in $req; do
        if ! opt_available "$opt"; then missing="${missing:+$missing }$opt"; fi
    done
    if [[ -n "$missing" ]]; then
        log_head "Regime $regime — SKIP (not available: $missing)"
        record "$regime" SKIP - - - -
        echo ""
        continue
    fi

    log_head "Regime $regime  (port $port)"
    build_args "$regime" "$port"

    run_log="$(mktemp /tmp/e2sar-regime-"$regime"-XXXXXX.log)"
    "$ENGINE" "${ARGS[@]}" ${ENGINE_EXTRA[@]+"${ENGINE_EXTRA[@]}"} 2>&1 | tee "$run_log"

    # Parse the single RESULT line the engine emits.
    result_line="$(grep '^RESULT ' "$run_log" | tail -1)"
    if [[ -z "$result_line" ]]; then
        log_error "Regime $regime produced no RESULT line — marking FAIL"
        record "$regime" FAIL - - - -
    else
        status=$(echo "$result_line"  | sed -n 's/.*status=\([^ ]*\).*/\1/p')
        pkts=$(echo "$result_line"    | sed -n 's/.*pkts=\([^ ]*\).*/\1/p')
        errs=$(echo "$result_line"    | sed -n 's/.*errs=\([^ ]*\).*/\1/p')
        tput=$(echo "$result_line"    | sed -n 's/.*tput=\([^ ]*\).*/\1/p')
        goodput=$(echo "$result_line" | sed -n 's/.*goodput=\([^ ]*\).*/\1/p')
        record "$regime" "${status:-FAIL}" "${pkts:-N/A}" "${errs:-N/A}" "${tput:-N/A}" "${goodput:-N/A}"
    fi
    rm -f "$run_log"
    echo ""

    # Space out runs to let TIME_WAIT sockets clear before port reuse.
    if [[ "$INTER_REGIME_SLEEP" -gt 0 ]]; then sleep "$INTER_REGIME_SLEEP"; fi
done

# --- Summary ---
log_head "Matrix Summary"
for row in ${SUMMARY_ROWS[@]+"${SUMMARY_ROWS[@]}"}; do
    echo "  $row"
done
echo ""
echo -e "  ${C_GREEN}PASS=$N_PASS${C_RESET}  ${C_RED}FAIL=$N_FAIL${C_RESET}  ${C_YELLOW}SKIP=$N_SKIP${C_RESET}"
if [[ -n "$OUT_TSV" ]]; then log_info "TSV written to $OUT_TSV"; fi

if [[ "$N_FAIL" -gt 0 ]]; then
    exit 1
fi
exit 0
