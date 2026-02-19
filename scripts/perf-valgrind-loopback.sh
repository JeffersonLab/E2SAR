#!/usr/bin/env bash
# Run e2sar_perf loopback under Valgrind. Collects separate XML for sender/receiver.
# Usage: run-perf-valgrind.sh [BUILD_DIR] [REPORT_DIR] [SUPP_FILE]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/${1:-build-valgrind}"
REPORT_DIR="${2:-/tmp/valgrind-reports}"
SUPP="${3:-$REPO_ROOT/scripts/valgrind.supp}"
PERF="$BUILD_DIR/bin/e2sar_perf"
mkdir -p "$REPORT_DIR"

if [[ ! -x "$PERF" ]]; then
    echo "ERROR: $PERF not found." >&2
    exit 1
fi

BASE_PORT=$(( 22000 + (RANDOM % 3000) ))
URI="ejfat://token@127.0.0.1:18020/lb/1?data=127.0.0.1:${BASE_PORT}"

VALGRIND_COMMON=(
    --tool=memcheck
    --leak-check=full
    --show-leak-kinds=definite,indirect
    --track-origins=yes
    --trace-children=yes
    --fair-sched=yes
    --num-callers=30
    --suppressions="$SUPP"
    --error-exitcode=1
)

echo "--- Receiver under Valgrind (port $BASE_PORT) ---"
valgrind "${VALGRIND_COMMON[@]}" \
    --xml=yes \
    --xml-file="$REPORT_DIR/perf_recv.xml" \
    "$PERF" -r \
        --ip 127.0.0.1 \
        --port "$BASE_PORT" \
        --duration 60 \
        --timeout 2000 \
        --quiet \
        -u "$URI" \
    >"$REPORT_DIR/perf_recv.log" 2>&1 &
RECV_PID=$!

# Valgrind startup is slower — give it more time
sleep 3

echo "--- Sender under Valgrind ---"
valgrind "${VALGRIND_COMMON[@]}" \
    --xml=yes \
    --xml-file="$REPORT_DIR/perf_send.xml" \
    "$PERF" -s \
        --ip 127.0.0.1 \
        -n 1000 \
        --length 1000000\
        --mtu 9000 \
        --rate 10 \
        -u "$URI" \
    >"$REPORT_DIR/perf_send.log" 2>&1
SEND_EXIT=$?

wait "$RECV_PID"
RECV_EXIT=$?

echo "Sender:   exit=$SEND_EXIT  xml=$REPORT_DIR/perf_send.xml"
echo "Receiver: exit=$RECV_EXIT  xml=$REPORT_DIR/perf_recv.xml"

[[ $SEND_EXIT -eq 0 && $RECV_EXIT -eq 0 ]] || exit 1

