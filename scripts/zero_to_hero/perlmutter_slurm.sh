#!/bin/bash
# Perlmutter SLURM batch script for E2SAR sender/receiver tests
#
# Usage:
#   EJFAT_URI="ejfat://..." sbatch perlmutter_slurm.sh [OPTIONS]
#
# SLURM Options (can be overridden via sbatch):
#   -N 2              Number of nodes (fixed at 2)
#   -C cpu            CPU nodes
#   -q debug          Queue (debug or regular)
#   -t 00:30:00       Time limit
#   -A <allocation>   Project allocation
#
# Test Options (passed to minimal_sender.sh/minimal_receiver.sh):
#   --rate RATE       Sending rate in Gbps (default: 1)
#   --length LENGTH   Event buffer length in bytes (default: 1048576)
#   --num COUNT       Number of events to send (default: 100)
#   --mtu MTU         MTU size in bytes (default: 9000)
#   --port PORT       Receiver data port (default: 10000)
#   --image IMAGE     Container image (default: ibaldin/e2sar:0.3.1a3)
#   --ipv6            Use IPv6 (default: false)
#   -v                Skip SSL certificate validation (default: disabled)
#
# Note: Receivers always run with --duration 0 (indefinite) and are terminated
#       with SIGKILL after the sender completes.
#
# Environment Variables:
#   EJFAT_URI         Required: EJFAT load balancer URI
#
# Example:
#   EJFAT_URI="ejfat://..." sbatch -A <project> perlmutter_slurm.sh --rate 2 --num 1000
#
# Note: A fresh LB reservation is created for each job and freed on completion.
#       EJFAT_URI must be the admin URI (not an INSTANCE_URI).

#SBATCH -N 2
#SBATCH -C cpu
#SBATCH -q debug
#SBATCH -t 00:30:00
#SBATCH -J ejfat_minimal
#SBATCH -o ./slurm-%j.out
#SBATCH -e ./slurm-%j.err
#SBATCH --mail-type=BEGIN,END,FAIL
#SBATCH --mail-user=$USER@nersc.gov

set -euo pipefail

#=============================================================================
# Parse command-line arguments
#=============================================================================

RATE=""
LENGTH=""
NUM=""
MTU=""
PORT=""
IMAGE=""
IPV6_FLAG=""
V_FLAG=""

while [[ $# -gt 0 ]]; do
    case $1 in
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
        --port)
            PORT="$2"
            shift 2
            ;;
        --image)
            IMAGE="$2"
            shift 2
            ;;
        --ipv6)
            IPV6_FLAG="--ipv6"
            shift
            ;;
        -v)
            V_FLAG="-v"
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

#=============================================================================
# Environment setup
#=============================================================================

echo "========================================="
echo "EJFAT Minimal Test - SLURM Job $SLURM_JOB_ID"
echo "========================================="
echo "Start time: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo ""

# Validate EJFAT_URI
if [[ -z "${EJFAT_URI:-}" ]]; then
    echo "ERROR: EJFAT_URI is required"
    echo "Set via: EJFAT_URI='ejfat://...' sbatch $0"
    exit 1
fi

echo "EJFAT_URI: $EJFAT_URI"
echo "Job nodes: $SLURM_JOB_NODELIST"
echo "Job ID: $SLURM_JOB_ID"
echo ""

# Get script directory (where minimal_*.sh scripts are located)
# SLURM copies the script to a temp dir, but we can find the original location
# Use the original script's directory, not the submit directory
if [[ -n "${SLURM_JOB_ID:-}" ]]; then
    # In SLURM environment - find the actual script directory
    SCRIPT_DIR="/global/homes/y/yak/E2SAR/scripts/zero_to_hero"
else
    # Fallback for local testing
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
echo "Script directory: $SCRIPT_DIR"

# Create runs directory if it doesn't exist
RUNS_DIR="${SLURM_SUBMIT_DIR}/runs"
mkdir -p "$RUNS_DIR"
echo "Runs directory: $RUNS_DIR"

# Create job-specific working directory for logs and INSTANCE_URI
JOB_DIR="${RUNS_DIR}/slurm_job_${SLURM_JOB_ID}"
mkdir -p "$JOB_DIR"
cd "$JOB_DIR"
echo "Working directory: $JOB_DIR"
echo ""

# Parse node list - get first two nodes
# SLURM_JOB_NODELIST format examples: "nid[001-002]", "nid001,nid002"
NODE_ARRAY=($(scontrol show hostname $SLURM_JOB_NODELIST))

if [[ ${#NODE_ARRAY[@]} -lt 2 ]]; then
    echo "ERROR: Need at least 2 nodes, got ${#NODE_ARRAY[@]}"
    exit 1
fi

NODE0="${NODE_ARRAY[0]}"
NODE1="${NODE_ARRAY[1]}"

echo "Receiver node (Node 0): $NODE0"
echo "Sender node (Node 1): $NODE1"
echo ""

#=============================================================================
# Build command arguments for sender and receiver
#=============================================================================

SENDER_ARGS=()
RECEIVER_ARGS=()

# Force receiver to run indefinitely (duration 0) - will be terminated after sender completes
RECEIVER_ARGS+=(--duration 0)

[[ -n "$IMAGE" ]] && SENDER_ARGS+=(--image "$IMAGE") && RECEIVER_ARGS+=(--image "$IMAGE")
[[ -n "$RATE" ]] && SENDER_ARGS+=(--rate "$RATE")
[[ -n "$LENGTH" ]] && SENDER_ARGS+=(--length "$LENGTH")
[[ -n "$NUM" ]] && SENDER_ARGS+=(--num "$NUM")
[[ -n "$MTU" ]] && SENDER_ARGS+=(--mtu "$MTU")
[[ -n "$PORT" ]] && RECEIVER_ARGS+=(--port "$PORT")
[[ -n "$IPV6_FLAG" ]] && SENDER_ARGS+=("$IPV6_FLAG") && RECEIVER_ARGS+=("$IPV6_FLAG")
[[ -n "$V_FLAG" ]] && SENDER_ARGS+=("$V_FLAG") && RECEIVER_ARGS+=("$V_FLAG")

echo "Sender arguments: ${SENDER_ARGS[*]:-<none>}"
echo "Receiver arguments: ${RECEIVER_ARGS[*]:-<none>}"
echo ""

#=============================================================================
# Phase 1: Reserve Load Balancer (fresh reservation per job)
#=============================================================================

echo "========================================="
echo "Phase 1: Reserve Load Balancer"
echo "========================================="

export EJFAT_URI

# Create a fresh reservation for this job
echo "Creating new LB reservation for job $SLURM_JOB_ID..."
if ! "$SCRIPT_DIR/minimal_reserve.sh"; then
    echo "ERROR: Failed to reserve load balancer"
    exit 1
fi

# Verify INSTANCE_URI file exists
if [[ ! -f "INSTANCE_URI" ]]; then
    echo "ERROR: INSTANCE_URI file not found after reservation"
    exit 1
fi

echo ""
echo "Reservation ready"
echo ""

#=============================================================================
# Phase 2: Start Receiver (background on Node 0)
#=============================================================================

echo "========================================="
echo "Phase 2: Start Receiver on $NODE0"
echo "========================================="

# Start receiver in background
srun --nodes=1 --ntasks=1 --nodelist="$NODE0" \
    bash -c "cd '$JOB_DIR' && '$SCRIPT_DIR/minimal_receiver.sh' ${RECEIVER_ARGS[*]:-}" \
    > receiver_srun.log 2>&1 &

RECEIVER_PID=$!
echo "Receiver started (PID: $RECEIVER_PID)"
echo ""

# Brief delay to allow receiver to register with load balancer
echo "Waiting 10 seconds for receiver to register..."
sleep 10
echo ""

#=============================================================================
# Phase 3: Start Sender (foreground on Node 1)
#=============================================================================

echo "========================================="
echo "Phase 3: Start Sender on $NODE1"
echo "========================================="

# Start sender in foreground (wait for completion)
set +e  # Don't exit on error, we want to capture exit code
srun --nodes=1 --ntasks=1 --nodelist="$NODE1" \
    bash -c "cd '$JOB_DIR' && '$SCRIPT_DIR/minimal_sender.sh' ${SENDER_ARGS[*]:-}" \
    > sender_srun.log 2>&1

SENDER_EXIT_CODE=$?
set -e

echo ""
echo "Sender completed (exit code: $SENDER_EXIT_CODE)"
echo ""

#=============================================================================
# Phase 4: Shutdown Receiver
#=============================================================================

echo "========================================="
echo "Phase 4: Shutdown Receiver"
echo "========================================="

# Receiver is running with duration=0 (indefinite), terminate it with SIGKILL
if kill -0 $RECEIVER_PID 2>/dev/null; then
    echo "Sending SIGKILL to receiver (PID: $RECEIVER_PID)..."
    kill -9 $RECEIVER_PID 2>/dev/null || true
    sleep 1

    # Wait for receiver to finish
    RECEIVER_EXIT_CODE=0
    if wait $RECEIVER_PID 2>/dev/null; then
        echo "Receiver terminated successfully"
    else
        RECEIVER_EXIT_CODE=$?
        echo "Receiver terminated (exit code: $RECEIVER_EXIT_CODE)"
    fi
else
    echo "Receiver already stopped"
    RECEIVER_EXIT_CODE=0
fi

echo ""

#=============================================================================
# Phase 5: Free Load Balancer
#=============================================================================

echo "========================================="
echo "Phase 5: Free Load Balancer"
echo "========================================="

if ! "$SCRIPT_DIR/minimal_free.sh" ${V_FLAG:+"$V_FLAG"}; then
    echo "WARNING: Failed to free load balancer reservation"
fi

echo ""

#=============================================================================
# Summary and Log Collection
#=============================================================================

echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Job ID: $SLURM_JOB_ID"
echo "Job directory: $JOB_DIR"
echo "Sender exit code: $SENDER_EXIT_CODE"
echo "Receiver exit code: $RECEIVER_EXIT_CODE"
echo ""

echo "Logs available at:"
echo "  - Sender log: $JOB_DIR/minimal_sender.log"
echo "  - Sender memory log: $JOB_DIR/minimal_sender_memory.log"
echo "  - Receiver log: $JOB_DIR/minimal_receiver.log"
echo "  - Sender srun log: $JOB_DIR/sender_srun.log"
echo "  - Receiver srun log: $JOB_DIR/receiver_srun.log"
echo ""

# Display log excerpts if they exist
if [[ -f minimal_sender.log ]]; then
    echo "--- Sender Log (last 20 lines) ---"
    tail -n 20 minimal_sender.log
    echo ""
fi

if [[ -f minimal_sender_memory.log ]]; then
    echo "--- Memory Summary ---"
    grep "^# Memory Summary:" -A 3 minimal_sender_memory.log || echo "Memory monitoring data available in minimal_sender_memory.log"
    echo ""
fi

if [[ -f minimal_receiver.log ]]; then
    echo "--- Receiver Log (last 20 lines) ---"
    tail -n 20 minimal_receiver.log
    echo ""
fi

echo "========================================="
echo "End time: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo "========================================="

# Exit with sender's exit code (most important for test success)
exit $SENDER_EXIT_CODE
