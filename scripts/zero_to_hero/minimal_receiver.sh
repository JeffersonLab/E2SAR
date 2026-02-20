#!/bin/bash
# Minimal E2SAR receiver startup script
#
# Usage:
#   ./minimal_reserve.sh        # First, create a reservation
#   ./minimal_receiver.sh [OPTIONS]
#
# Requires INSTANCE_URI file (created by minimal_reserve.sh)
#
# Options:
#   --image IMAGE     Container image (default: ibaldin/e2sar:0.3.1a3)
#   --port PORT       Data port (default: 10000)
#   --duration SEC    Run duration in seconds (default: 0 = indefinite)
#   --ipv6            Use IPv6 (default: false)
#   --threads NUM     Number of receive threads (default: 8)
#   --deq NUM         Number of dequeue threads (default: 4)
#   --bufsize SIZE    Socket buffer size in bytes (default: 134217728)
#   --help            Show this help message

set -euo pipefail

# Default values
EJFAT_URI="${EJFAT_URI:-}"
E2SAR_IMAGE="${E2SAR_IMAGE:-ibaldin/e2sar:0.3.1a3}"
DATA_PORT="10000"
DURATION="0"
USE_IPV6="false"
RECV_THREADS="16"
DEQUEUE_THREADS="16"
BUFFER_SIZE="134217728"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --image)
            E2SAR_IMAGE="$2"
            shift 2
            ;;
        --port)
            DATA_PORT="$2"
            shift 2
            ;;
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --ipv6)
            USE_IPV6="true"
            shift
            ;;
        --threads)
            RECV_THREADS="$2"
            shift 2
            ;;
        --deq)
            DEQUEUE_THREADS="$2"
            shift 2
            ;;
        --bufsize)
            BUFFER_SIZE="$2"
            shift 2
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
source "$INSTANCE_URI_FILE"

# Validate EJFAT_URI was loaded
if [[ -z "$EJFAT_URI" ]]; then
    echo "ERROR: EJFAT_URI not found in $INSTANCE_URI_FILE"
    exit 1
fi

echo "Starting E2SAR receiver..."
echo "EJFAT_URI: $EJFAT_URI"
echo "Container Image: $E2SAR_IMAGE"

# Auto-detect receiver IP
echo "Auto-detecting receiver IP..."

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
RECEIVER_IP=$(ip route get "$LB_IP" | head -1 | sed 's/^.*src//' | awk '{print $1}')

if [[ -z "$RECEIVER_IP" ]]; then
    echo "ERROR: Failed to detect receiver IP"
    exit 1
fi

echo "Receiver IP: $RECEIVER_IP"
echo "Data Port: $DATA_PORT"
echo "Receive Threads: $RECV_THREADS"
echo "Dequeue Threads: $DEQUEUE_THREADS"
echo "Buffer Size: $BUFFER_SIZE"

# Build podman-hpc command
CMD=(
    podman-hpc
    run
    --rm
    --network host
    -e "EJFAT_URI=$EJFAT_URI"
    -e "MALLOC_ARENA_MAX=32"
    "$E2SAR_IMAGE"
    e2sar_perf
    --recv
    --withcp
    --ip="$RECEIVER_IP"
    --port="$DATA_PORT"
    --duration="$DURATION"
    --timeout=3000
    --threads="$RECV_THREADS"
    --deq="$DEQUEUE_THREADS"
    --bufsize="$BUFFER_SIZE"
)

echo ""
echo "Running: ${CMD[*]}"
echo ""

# Function to write end timestamp
write_end_time() {
    local exit_code=$?
    echo "" >> minimal_receiver.log
    echo "END_TIME (UTC): $(date -u '+%Y-%m-%d %H:%M:%S')" >> minimal_receiver.log
    echo "EXIT_CODE: $exit_code" >> minimal_receiver.log
    return $exit_code
}

# Trap signals to ensure END_TIME is written even on interrupt/termination
trap 'write_end_time' EXIT INT TERM

# Run podman-hpc with timestamps logged to file
{
    echo "START_TIME (UTC): $(date -u '+%Y-%m-%d %H:%M:%S')"
    echo ""
} | tee minimal_receiver.log

# Run the container and append output
"${CMD[@]}" 2>&1 | tee -a minimal_receiver.log

# Capture exit code
CONTAINER_EXIT_CODE=$?

# Write end time and exit code (trap will also write, but this ensures it's in the tee'd output)
{
    echo ""
    echo "END_TIME (UTC): $(date -u '+%Y-%m-%d %H:%M:%S')"
    echo "EXIT_CODE: $CONTAINER_EXIT_CODE"
} | tee -a minimal_receiver.log

# Exit with container's exit code
exit $CONTAINER_EXIT_CODE
