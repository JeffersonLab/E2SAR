#!/bin/bash
#
# monitor_memory.sh - Monitor e2sar_perf memory usage during testing
#
# Usage:
#   ./monitor_memory.sh [interval_seconds]
#
# Default interval: 1 second
#

set -euo pipefail

# Script location (for finding sibling scripts if needed)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifacts are created in the current working directory (not script directory)

INTERVAL="${1:-1}"
LOG_FILE="memory_monitor.log"

echo "E2SAR Memory Monitor"
echo "===================="
echo "Monitoring e2sar_perf processes every ${INTERVAL} second(s)"
echo "Logging to: ${LOG_FILE}"
echo "Press Ctrl+C to stop"
echo ""

# Create log file with header
cat > "${LOG_FILE}" <<EOF
# E2SAR Memory Monitor Log
# Started: $(date -Iseconds)
# Interval: ${INTERVAL} seconds
#
# Columns: TIMESTAMP, PID, RSS_KB, VSZ_KB, %MEM, %CPU, ELAPSED_TIME, COMMAND
EOF

# Trap Ctrl+C for clean exit
trap 'echo -e "\n\nMonitoring stopped. Log saved to: ${LOG_FILE}"; exit 0' INT

# Monitor loop
while true; do
    # Find all e2sar_perf processes
    PIDS=$(pgrep -f e2sar_perf || true)

    if [ -z "$PIDS" ]; then
        echo -ne "\r[$(date +%H:%M:%S)] No e2sar_perf processes found..."
    else
        for PID in $PIDS; do
            # Get process info
            PS_INFO=$(ps -p "$PID" -o pid=,rss=,vsz=,%mem=,%cpu=,etime=,args= 2>/dev/null || true)

            if [ -n "$PS_INFO" ]; then
                # Parse values
                read -r P_PID RSS VSZ MEM CPU ETIME ARGS <<< "$PS_INFO"

                # Log to file
                echo "$(date -Iseconds), $P_PID, $RSS, $VSZ, $MEM, $CPU, $ETIME, $ARGS" >> "${LOG_FILE}"

                # Display current values
                RSS_MB=$((RSS / 1024))
                VSZ_MB=$((VSZ / 1024))
                echo -ne "\r[$(date +%H:%M:%S)] PID:$P_PID  RSS:${RSS_MB}MB  VSZ:${VSZ_MB}MB  MEM:${MEM}%  CPU:${CPU}%  TIME:${ETIME}    "
            fi
        done
    fi

    sleep "$INTERVAL"
done
