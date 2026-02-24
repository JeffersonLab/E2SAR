#!/bin/bash
# Minimal E2SAR sender startup script
#
# Usage:
#   ./minimal_reserve.sh        # First, create a reservation
#   ./minimal_sender.sh [OPTIONS]
#
# Requires INSTANCE_URI file (created by minimal_reserve.sh)
#
# Options:
#   --image IMAGE     Container image (default: ibaldin/e2sar:0.3.1a3)
#   --rate RATE       Sending rate in Gbps (default: 1)
#   --length LENGTH   Event buffer length in bytes (default: 1048576)
#   --num COUNT       Number of events to send (default: 100)
#   --mtu MTU         MTU size in bytes (default: 9000)
#   --ipv6            Use IPv6 (default: false)
#   -v                Skip SSL certificate validation (default: disabled)
#   --no-monitor      Disable memory monitoring (default: enabled)
#   --help            Show this help message

set -euo pipefail

# Script location (for finding sibling scripts if needed)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifacts are created in the current working directory (not script directory)

# Memory monitoring process ID
MEMORY_MONITOR_PID=""

# Default values
EJFAT_URI="${EJFAT_URI:-}"
E2SAR_IMAGE="${E2SAR_IMAGE:-ibaldin/e2sar:0.3.1a3}"
RATE="1"
LENGTH="1048576"
NUM="100"
MTU="9000"
USE_IPV6="false"
SKIP_SSL_VERIFY="false"
ENABLE_MONITOR="true"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --image)
            E2SAR_IMAGE="$2"
            shift 2
            ;;
        --rate)
            RATE="$2"
            shift 2
            ;;
        --length)
            LENGTH="$2"
            shift 2
            ;;
        --num)
            NUM="$2"
            shift 2
            ;;
        --mtu)
            MTU="$2"
            shift 2
            ;;
        --ipv6)
            USE_IPV6="true"
            shift
            ;;
        -v)
            SKIP_SSL_VERIFY="true"
            shift
            ;;
        --no-monitor)
            ENABLE_MONITOR="false"
            shift
            ;;
        --help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Load EJFAT_URI from INSTANCE_URI file
INSTANCE_URI_FILE="INSTANCE_URI"
if [[ ! -f "$INSTANCE_URI_FILE" ]]; then
    echo "ERROR: $INSTANCE_URI_FILE not found"
    echo "Run minimal_reserve.sh first to create a reservation"
    exit 1
fi

echo "Loading EJFAT_URI from $INSTANCE_URI_FILE..."
# Extract EJFAT_URI safely without sourcing the entire file
EJFAT_URI=$(grep -E '^export EJFAT_URI=' "$INSTANCE_URI_FILE" | head -1 | sed "s/^export EJFAT_URI=//; s/^['\"]//; s/['\"]$//")

# Validate EJFAT_URI was loaded
if [[ -z "$EJFAT_URI" ]]; then
    echo "ERROR: EJFAT_URI not found in $INSTANCE_URI_FILE"
    exit 1
fi

echo "Starting E2SAR sender..."
EJFAT_URI_REDACTED=$(echo "$EJFAT_URI" | sed -E 's|(://)(.{4})[^@]*(.{4})@|\1\2---\3@|')
echo "EJFAT_URI: $EJFAT_URI_REDACTED"
echo "Container Image: $E2SAR_IMAGE"

# Auto-detect sender IP
echo "Auto-detecting sender IP..."

# Extract LB hostname from EJFAT_URI
# Format: ejfat://token@hostname:port/lb/1?sync=...
LB_HOST=$(echo "$EJFAT_URI" | sed 's|.*@\([^:]*\):.*|\1|')
echo "LB Host: $LB_HOST"

# Resolve LB hostname to IP
if [[ "$USE_IPV6" == "true" ]]; then
    LB_IP=$(getent ahostsv6 "$LB_HOST" | head -1 | awk '{print $1}')
else
    LB_IP=$(getent ahostsv4 "$LB_HOST" | head -1 | awk '{print $1}')
fi

if [[ -z "$LB_IP" ]]; then
    echo "ERROR: Failed to resolve LB host: $LB_HOST"
    exit 1
fi
echo "LB IP: $LB_IP"

# Find source IP for route to LB
SENDER_IP=$(ip route get "$LB_IP" | head -1 | sed 's/^.*src//' | awk '{print $1}')

if [[ -z "$SENDER_IP" ]]; then
    echo "ERROR: Failed to detect sender IP"
    exit 1
fi

echo "Sender IP: $SENDER_IP"
echo "Rate: $RATE Gbps"
echo "Event Length: $LENGTH bytes"
echo "Number of Events: $NUM"
echo "MTU: $MTU bytes"
echo ""

#=============================================================================
# Memory monitoring function
#=============================================================================

start_memory_monitor() {
    local log_file="$1"
    local interval="${2:-1}"

    # Create log file with header
    {
        echo "# E2SAR Memory Monitor"
        echo "# Started: $(date -Iseconds)"
        echo "# Interval: ${interval} second(s)"
        echo "#"
        echo "# Columns: TIMESTAMP, PID, RSS_KB, VSZ_KB, %MEM, %CPU, ELAPSED_TIME, COMMAND"
    } > "$log_file"

    # Start monitoring loop in background
    (
        while true; do
            # Find all e2sar_perf processes
            PIDS=$(pgrep -f "e2sar_perf.*--send" 2>/dev/null || true)

            if [ -n "$PIDS" ]; then
                for PID in $PIDS; do
                    # Get process info
                    PS_INFO=$(ps -p "$PID" -o pid=,rss=,vsz=,%mem=,%cpu=,etime=,args= 2>/dev/null || true)

                    if [ -n "$PS_INFO" ]; then
                        # Parse and log
                        read -r P_PID RSS VSZ MEM CPU ETIME ARGS <<< "$PS_INFO"
                        echo "$(date -Iseconds), $P_PID, $RSS, $VSZ, $MEM, $CPU, $ETIME, $ARGS" >> "$log_file"
                    fi
                done
            fi

            sleep "$interval"
        done
    ) >/dev/null 2>&1 &

    # Return the PID of the monitoring process
    echo $!
}

stop_memory_monitor() {
    local monitor_pid="$1"
    local log_file="$2"

    if [ -n "$monitor_pid" ] && kill -0 "$monitor_pid" 2>/dev/null; then
        kill "$monitor_pid" 2>/dev/null || true
        wait "$monitor_pid" 2>/dev/null || true

        # Add footer to log
        {
            echo "#"
            echo "# Stopped: $(date -Iseconds)"
        } >> "$log_file"

        # Generate summary if log has data
        if [ -f "$log_file" ] && [ -s "$log_file" ]; then
            MAX_RSS=$(grep -v '^#' "$log_file" | awk -F', ' '{print $3}' | sort -n | tail -1)
            MIN_RSS=$(grep -v '^#' "$log_file" | awk -F', ' '{print $3}' | sort -n | head -1)

            if [ -n "$MAX_RSS" ] && [ -n "$MIN_RSS" ]; then
                {
                    echo "#"
                    echo "# Memory Summary:"
                    echo "#   Peak RSS: $((MAX_RSS / 1024)) MB ($MAX_RSS KB)"
                    echo "#   Min RSS:  $((MIN_RSS / 1024)) MB ($MIN_RSS KB)"
                    echo "#   Growth:   $(((MAX_RSS - MIN_RSS) / 1024)) MB"
                } >> "$log_file"
            fi
        fi
    fi
}

#=============================================================================
# Build podman-hpc command
#=============================================================================

# Export EJFAT_URI so it can be passed to container without exposing in process list
export EJFAT_URI

CMD=(
    podman-hpc
    run
    --rm
    --network host
    --env EJFAT_URI
    -e "MALLOC_ARENA_MAX=32"
    "$E2SAR_IMAGE"
    e2sar_perf
    --send
    --withcp
    --optimize=sendmmsg
    --sockets=16
    --ip="$SENDER_IP"
    --rate="$RATE"
    --length="$LENGTH"
    --num="$NUM"
    --mtu="$MTU"
    --bufsize=134217728
)

# Add -v flag if SSL verification should be skipped
if [[ "$SKIP_SSL_VERIFY" == "true" ]]; then
    CMD+=(-v)
fi

echo ""
echo "Running: ${CMD[*]}"
echo ""

# Function to write end timestamp and stop monitoring
write_end_time() {
    local exit_code=$?

    # Stop memory monitor if running
    if [[ "$ENABLE_MONITOR" == "true" ]] && [[ -n "$MEMORY_MONITOR_PID" ]]; then
        stop_memory_monitor "$MEMORY_MONITOR_PID" "minimal_sender_memory.log"
    fi

    echo "" >> minimal_sender.log
    echo "END_TIME (UTC): $(date -u '+%Y-%m-%d %H:%M:%S')" >> minimal_sender.log
    echo "EXIT_CODE: $exit_code" >> minimal_sender.log
    return $exit_code
}

# Trap signals to ensure END_TIME is written even on interrupt/termination
trap 'write_end_time' EXIT INT TERM

# Run podman-hpc with timestamps logged to file
{
    echo "START_TIME (UTC): $(date -u '+%Y-%m-%d %H:%M:%S')"
    echo ""
} | tee minimal_sender.log || true

# Start memory monitoring if enabled
if [[ "$ENABLE_MONITOR" == "true" ]]; then
    echo "Starting memory monitor (logging to minimal_sender_memory.log)..."
    MEMORY_MONITOR_PID=$(start_memory_monitor "minimal_sender_memory.log" 1)
    echo "Memory monitor started (PID: $MEMORY_MONITOR_PID)"
    echo ""
fi

# Run the container and append output
# Use || true to prevent pipefail from failing on SIGPIPE in tee
# Use PIPESTATUS[0] to capture container's exit code (not tee's)
"${CMD[@]}" 2>&1 | tee -a minimal_sender.log || true
CONTAINER_EXIT_CODE=${PIPESTATUS[0]}

# Exit with container's exit code (trap will handle END_TIME/EXIT_CODE logging)
exit $CONTAINER_EXIT_CODE
