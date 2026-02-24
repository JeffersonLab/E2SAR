#!/bin/bash
# Perlmutter SLURM batch script for E2SAR multi-sender/receiver tests
#
# Usage:
#   EJFAT_URI="ejfat://..." sbatch -N <total_nodes> perlmutter_multi_slurm.sh [OPTIONS]
#
# SLURM Options (must be specified via sbatch):
#   -N <nodes>        Total nodes (receivers + senders)
#   -C cpu            CPU nodes
#   -q debug          Queue (debug or regular)
#   -t 00:30:00       Time limit
#   -A <allocation>   Project allocation
#
# Multi-Instance Options:
#   --receivers N           Total number of receiver instances (default: 1)
#   --senders M             Number of sender instances (default: 1)
#   --receivers-per-node K  Receivers per node (default: 1)
#   --senders-per-node K    Senders per node (default: 1)
#   --base-port PORT        Starting port for receivers (default: 10000)
#   --receiver-delay SEC    Delay in seconds after receivers start (default: 10)
#
# Senders and receivers share the same node pool and may be co-located.
# Required nodes = max(ceil(receivers/receivers-per-node), ceil(senders/senders-per-node))
#
# Test Options (passed to minimal_sender.sh/minimal_receiver.sh):
#   --rate RATE       Sending rate in Gbps (default: 1)
#   --length LENGTH   Event buffer length in bytes (default: 1048576)
#   --num COUNT       Number of events to send (default: 100)
#   --mtu MTU         MTU size in bytes (default: 9000)
#   --threads N       Receive threads per receiver instance (default: 16)
#                     Also determines port stride: each receiver occupies N consecutive ports
#   --image IMAGE     Container image (default: ibaldin/e2sar:0.3.1a3)
#   --ipv6            Use IPv6 (default: false)
#   -v                Skip SSL certificate validation (default: disabled)
#
# Note: Receivers always run with --duration 0 (indefinite) and are terminated
#       with SIGTERM/SIGKILL after all senders complete.
#
# Environment Variables:
#   EJFAT_URI         Required: EJFAT admin URI (not an INSTANCE_URI)
#
# Note: A fresh LB reservation is created for each job and freed on completion.
#
# Example (4 receivers and 4 senders co-located on 2 nodes):
#   EJFAT_URI="ejfat://..." sbatch -N 2 -A <project> perlmutter_multi_slurm.sh \
#       --receivers 4 --receivers-per-node 2 --senders 4 --senders-per-node 2 --rate 1 --num 100
#
# Example (single node running everything):
#   EJFAT_URI="ejfat://..." sbatch -N 1 -A <project> perlmutter_multi_slurm.sh \
#       --receivers 2 --receivers-per-node 2 --senders 2 --senders-per-node 2 --rate 1 --num 100
#

#SBATCH -C cpu
#SBATCH -q debug
#SBATCH -t 00:30:00
#SBATCH --ntasks-per-node=128
#SBATCH -J ejfat_multi
#SBATCH -o ./slurm-%j.out
#SBATCH -e ./slurm-%j.err
#SBATCH --mail-type=BEGIN,END,FAIL
#SBATCH --mail-user=$USER@nersc.gov

set -euo pipefail

#=============================================================================
# Parse command-line arguments
#=============================================================================

# Multi-instance configuration
NUM_RECEIVERS=1
NUM_SENDERS=1
RECEIVERS_PER_NODE=1
SENDERS_PER_NODE=1
BASE_PORT=10000
RECEIVER_DELAY=10

# Test parameters
RATE=""
LENGTH=""
NUM=""
MTU=""
RECV_THREADS=16
IMAGE=""
IPV6_FLAG=""
V_FLAG=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --receivers)
            NUM_RECEIVERS="$2"
            shift 2
            ;;
        --senders)
            NUM_SENDERS="$2"
            shift 2
            ;;
        --receivers-per-node)
            RECEIVERS_PER_NODE="$2"
            shift 2
            ;;
        --senders-per-node)
            SENDERS_PER_NODE="$2"
            shift 2
            ;;
        --base-port)
            BASE_PORT="$2"
            shift 2
            ;;
        --receiver-delay)
            RECEIVER_DELAY="$2"
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
        --threads)
            RECV_THREADS="$2"
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
echo "EJFAT Multi-Instance Test - SLURM Job $SLURM_JOB_ID"
echo "========================================="
echo "Start time: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo ""

# Validate EJFAT_URI
if [[ -z "${EJFAT_URI:-}" ]]; then
    echo "ERROR: EJFAT_URI is required"
    echo "Set via: EJFAT_URI='ejfat://...' sbatch $0"
    exit 1
fi

EJFAT_URI_REDACTED=$(echo "$EJFAT_URI" | sed -E 's|(://)(.{4})[^@]*(.{4})@|\1\2---\3@|')
echo "EJFAT_URI: $EJFAT_URI_REDACTED"
echo "Job nodes: $SLURM_JOB_NODELIST"
echo "Job ID: $SLURM_JOB_ID"
echo ""

# Get script directory (where minimal_*.sh scripts are located)
# Require E2SAR_SCRIPTS_DIR to be set to avoid hardcoded paths
if [[ -z "${E2SAR_SCRIPTS_DIR:-}" ]]; then
    echo "ERROR: E2SAR_SCRIPTS_DIR must be set to the zero_to_hero directory"
    echo "  export E2SAR_SCRIPTS_DIR=/path/to/E2SAR/scripts/zero_to_hero"
    exit 1
fi
SCRIPT_DIR="$E2SAR_SCRIPTS_DIR"
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

# Parse node list
NODE_ARRAY=($(scontrol show hostname $SLURM_JOB_NODELIST))

# Port stride matches receive thread count: each receiver binds RECV_THREADS consecutive ports
PORT_STRIDE=$RECV_THREADS

echo "Configuration:"
echo "  Receivers: $NUM_RECEIVERS ($RECEIVERS_PER_NODE per node)"
echo "  Senders:   $NUM_SENDERS ($SENDERS_PER_NODE per node)"
echo "  Base port: $BASE_PORT (stride: $PORT_STRIDE)"
echo ""

# Calculate required nodes: senders and receivers share the same node pool
NUM_RECEIVER_NODES=$(( (NUM_RECEIVERS + RECEIVERS_PER_NODE - 1) / RECEIVERS_PER_NODE ))
NUM_SENDER_NODES=$(( (NUM_SENDERS + SENDERS_PER_NODE - 1) / SENDERS_PER_NODE ))
REQUIRED_NODES=$(( NUM_RECEIVER_NODES > NUM_SENDER_NODES ? NUM_RECEIVER_NODES : NUM_SENDER_NODES ))

echo "Node allocation:"
echo "  Receiver nodes needed: $NUM_RECEIVER_NODES"
echo "  Sender nodes needed:   $NUM_SENDER_NODES"
echo "  Total nodes required:  $REQUIRED_NODES"
echo "  Nodes allocated:       ${#NODE_ARRAY[@]}"
echo ""

# Validate node count
if [[ ${#NODE_ARRAY[@]} -lt $REQUIRED_NODES ]]; then
    echo "ERROR: Insufficient nodes allocated"
    echo "  Need $REQUIRED_NODES nodes (max of $NUM_RECEIVER_NODES receiver nodes, $NUM_SENDER_NODES sender nodes)"
    echo "  Got ${#NODE_ARRAY[@]} nodes"
    echo "  Use: sbatch -N $REQUIRED_NODES -A <project> $0 --receivers $NUM_RECEIVERS --receivers-per-node $RECEIVERS_PER_NODE --senders $NUM_SENDERS --senders-per-node $SENDERS_PER_NODE"
    exit 1
fi

# Display node assignments (combined per-node view)
echo "Node assignments:"
for ((node_idx=0; node_idx<REQUIRED_NODES; node_idx++)); do
    NODE="${NODE_ARRAY[$node_idx]}"
    LINE="  Node $node_idx ($NODE):"

    # Receivers on this node
    RECV_START=$((node_idx * RECEIVERS_PER_NODE))
    RECV_END=$(( (node_idx + 1) * RECEIVERS_PER_NODE - 1 ))
    if [[ $RECV_START -lt $NUM_RECEIVERS ]]; then
        [[ $RECV_END -ge $NUM_RECEIVERS ]] && RECV_END=$((NUM_RECEIVERS - 1))
        PORTS=""
        for ((r=RECV_START; r<=RECV_END; r++)); do
            [[ -n "$PORTS" ]] && PORTS="$PORTS, "
            PORTS="${PORTS}$((BASE_PORT + r * PORT_STRIDE))"
        done
        LINE="$LINE receivers $RECV_START-$RECV_END (ports: $PORTS)"
    fi

    # Senders on this node
    SEND_START=$((node_idx * SENDERS_PER_NODE))
    SEND_END=$(( (node_idx + 1) * SENDERS_PER_NODE - 1 ))
    if [[ $SEND_START -lt $NUM_SENDERS ]]; then
        [[ $SEND_END -ge $NUM_SENDERS ]] && SEND_END=$((NUM_SENDERS - 1))
        LINE="$LINE senders $SEND_START-$SEND_END"
    fi

    echo "$LINE"
done
echo ""

#=============================================================================
# Build command arguments for sender and receiver
#=============================================================================

SENDER_ARGS=()
RECEIVER_ARGS=()

# Force receiver to run indefinitely (duration 0) - will be terminated after senders complete
RECEIVER_ARGS+=(--duration 0)

RECEIVER_ARGS+=(--threads "$RECV_THREADS")
[[ -n "$IMAGE" ]] && SENDER_ARGS+=(--image "$IMAGE") && RECEIVER_ARGS+=(--image "$IMAGE")
[[ -n "$RATE" ]] && SENDER_ARGS+=(--rate "$RATE")
[[ -n "$LENGTH" ]] && SENDER_ARGS+=(--length "$LENGTH")
[[ -n "$NUM" ]] && SENDER_ARGS+=(--num "$NUM")
[[ -n "$MTU" ]] && SENDER_ARGS+=(--mtu "$MTU")
[[ -n "$IPV6_FLAG" ]] && SENDER_ARGS+=("$IPV6_FLAG") && RECEIVER_ARGS+=("$IPV6_FLAG")
[[ -n "$V_FLAG" ]] && SENDER_ARGS+=("$V_FLAG") && RECEIVER_ARGS+=("$V_FLAG")

echo "Sender arguments: ${SENDER_ARGS[*]:-<none>}"
echo "Receiver arguments (per instance): ${RECEIVER_ARGS[*]:-<none>}"
echo ""

#=============================================================================
# Install cleanup trap to free reservation on early exit or job cancellation
#=============================================================================

CLEANUP_DONE=false

cleanup() {
    # Only run cleanup once, and only if INSTANCE_URI exists
    if [[ "$CLEANUP_DONE" == "false" && -f "$JOB_DIR/INSTANCE_URI" ]]; then
        CLEANUP_DONE=true
        echo ""
        echo "========================================="
        echo "Cleanup: Freeing Load Balancer"
        echo "========================================="
        cd "$JOB_DIR" || return
        "$SCRIPT_DIR/minimal_free.sh" ${V_FLAG:+"$V_FLAG"} 2>/dev/null || echo "WARNING: Failed to free load balancer reservation"
    fi
}

trap cleanup EXIT

#=============================================================================
# Phase 1: Reserve Load Balancer (fresh reservation per job)
#=============================================================================

echo "========================================="
echo "Phase 1: Reserve Load Balancer"
echo "========================================="

export EJFAT_URI

# Always create a fresh reservation in the job directory so each job
# has its own isolated INSTANCE_URI and LB reservation.
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
# Phase 2: Start All Receivers (parallel, background processes)
#=============================================================================

echo "========================================="
echo "Phase 2: Start Receivers"
echo "========================================="

# Arrays to track receiver state
declare -a RECEIVER_PIDS=()
declare -a RECEIVER_NODES=()
declare -a RECEIVER_PORTS=()
declare -a RECEIVER_EXIT_CODES=()

# Start all receivers in parallel
RECV_INDEX=0
for ((node_idx=0; node_idx<NUM_RECEIVER_NODES; node_idx++)); do
    RECV_NODE="${NODE_ARRAY[$node_idx]}"

    for ((j=0; j<RECEIVERS_PER_NODE && RECV_INDEX<NUM_RECEIVERS; j++)); do
        RECV_DIR="$JOB_DIR/receiver_$RECV_INDEX"
        RECV_PORT=$((BASE_PORT + RECV_INDEX * PORT_STRIDE))

        mkdir -p "$RECV_DIR"
        # Copy INSTANCE_URI to receiver directory
        cp "$JOB_DIR/INSTANCE_URI" "$RECV_DIR/"

        echo "Starting receiver $RECV_INDEX on $RECV_NODE (port $RECV_PORT)..."

        # Build receiver command with port
        RECV_ARGS_WITH_PORT=("${RECEIVER_ARGS[@]}" --port "$RECV_PORT")

        srun --nodes=1 --ntasks=1 --exact --nodelist="$RECV_NODE" \
            bash -c "cd '$RECV_DIR' && '$SCRIPT_DIR/minimal_receiver.sh' ${RECV_ARGS_WITH_PORT[*]}" \
            > "$RECV_DIR/receiver_srun.log" 2>&1 &

        RECEIVER_PIDS+=($!)
        RECEIVER_NODES+=("$RECV_NODE")
        RECEIVER_PORTS+=($RECV_PORT)

        RECV_INDEX=$((RECV_INDEX + 1))
    done
done

echo ""
echo "Started $NUM_RECEIVERS receivers"
for ((i=0; i<NUM_RECEIVERS; i++)); do
    echo "  Receiver $i: ${RECEIVER_NODES[$i]}, port ${RECEIVER_PORTS[$i]}, PID ${RECEIVER_PIDS[$i]}"
done
echo ""

# Wait for receivers to register with load balancer
echo "Waiting ${RECEIVER_DELAY} seconds for receivers to register..."
sleep $RECEIVER_DELAY
echo ""

#=============================================================================
# Phase 3: Start All Senders (parallel, background processes)
#=============================================================================

echo "========================================="
echo "Phase 3: Start Senders"
echo "========================================="

# Arrays to track sender state
declare -a SENDER_PIDS=()
declare -a SENDER_NODES=()
declare -a SENDER_EXIT_CODES=()

# Start all senders in parallel
for ((i=0; i<NUM_SENDERS; i++)); do
    SEND_DIR="$JOB_DIR/sender_$i"
    SEND_NODE="${NODE_ARRAY[$((i / SENDERS_PER_NODE))]}"

    mkdir -p "$SEND_DIR"
    # Copy INSTANCE_URI to sender directory
    cp "$JOB_DIR/INSTANCE_URI" "$SEND_DIR/"

    echo "Starting sender $i on $SEND_NODE..."

    srun --nodes=1 --ntasks=1 --exact --nodelist="$SEND_NODE" \
        bash -c "cd '$SEND_DIR' && '$SCRIPT_DIR/minimal_sender.sh' ${SENDER_ARGS[*]:-}" \
        > "$SEND_DIR/sender_srun.log" 2>&1 &

    SENDER_PIDS+=($!)
    SENDER_NODES+=("$SEND_NODE")
done

echo ""
echo "Started $NUM_SENDERS senders"
for ((i=0; i<NUM_SENDERS; i++)); do
    echo "  Sender $i: ${SENDER_NODES[$i]}, PID ${SENDER_PIDS[$i]}"
done
echo ""

#=============================================================================
# Phase 4: Wait for All Senders to Complete
#=============================================================================

echo "========================================="
echo "Phase 4: Wait for Senders"
echo "========================================="

set +e  # Don't exit on error, we want to capture all exit codes

for ((i=0; i<NUM_SENDERS; i++)); do
    echo "Waiting for sender $i (PID ${SENDER_PIDS[$i]})..."
    wait ${SENDER_PIDS[$i]} || true
    SENDER_EXIT_CODES[$i]=$?
    echo "  Sender $i completed with exit code ${SENDER_EXIT_CODES[$i]}"
done

set -e

echo ""
echo "All senders completed"
echo ""

#=============================================================================
# Phase 5: Shutdown All Receivers
#=============================================================================

echo "========================================="
echo "Phase 5: Shutdown Receivers"
echo "========================================="

# Gracefully terminate receivers with SIGTERM first
echo "Sending SIGTERM to all receivers..."
for ((i=0; i<NUM_RECEIVERS; i++)); do
    if kill -0 ${RECEIVER_PIDS[$i]} 2>/dev/null; then
        kill -TERM ${RECEIVER_PIDS[$i]} 2>/dev/null || true
    fi
done

# Grace period for graceful shutdown
echo "Waiting 5 seconds for graceful shutdown..."
sleep 5

# Force kill any remaining receivers with SIGKILL
echo "Sending SIGKILL to any remaining receivers..."
for ((i=0; i<NUM_RECEIVERS; i++)); do
    if kill -0 ${RECEIVER_PIDS[$i]} 2>/dev/null; then
        kill -9 ${RECEIVER_PIDS[$i]} 2>/dev/null || true
    fi
done

# Collect receiver exit codes
set +e
for ((i=0; i<NUM_RECEIVERS; i++)); do
    wait ${RECEIVER_PIDS[$i]} 2>/dev/null || true
    RECEIVER_EXIT_CODES[$i]=$?
done
set -e

echo "All receivers terminated"
echo ""

#=============================================================================
# Phase 6: Free Load Balancer
#=============================================================================

echo "========================================="
echo "Phase 6: Free Load Balancer"
echo "========================================="

# Mark cleanup as done and run it explicitly
CLEANUP_DONE=true
if ! "$SCRIPT_DIR/minimal_free.sh" ${V_FLAG:+"$V_FLAG"}; then
    echo "WARNING: Failed to free load balancer reservation"
fi

echo ""

#=============================================================================
# Summary Report
#=============================================================================

echo "========================================="
echo "Multi-Instance Test Summary"
echo "========================================="
echo "Job ID: $SLURM_JOB_ID"
echo "Job directory: $JOB_DIR"
echo "Configuration: $NUM_RECEIVERS receivers ($RECEIVERS_PER_NODE per node), $NUM_SENDERS senders ($SENDERS_PER_NODE per node)"
echo "Nodes used: $REQUIRED_NODES"
echo ""

echo "Sender Results:"
ALL_SENDERS_SUCCESS=true
for ((i=0; i<NUM_SENDERS; i++)); do
    echo "  Sender $i (${SENDER_NODES[$i]}): exit code ${SENDER_EXIT_CODES[$i]}"
    if [[ ${SENDER_EXIT_CODES[$i]} -ne 0 ]]; then
        ALL_SENDERS_SUCCESS=false
    fi
done
echo ""

echo "Receiver Results:"
for ((i=0; i<NUM_RECEIVERS; i++)); do
    EXIT_CODE=${RECEIVER_EXIT_CODES[$i]}
    EXIT_MSG="exit code $EXIT_CODE"

    # Exit code 137 = 128 + 9 (SIGKILL), 143 = 128 + 15 (SIGTERM)
    if [[ $EXIT_CODE -eq 137 ]]; then
        EXIT_MSG="$EXIT_MSG (SIGKILL - expected)"
    elif [[ $EXIT_CODE -eq 143 ]]; then
        EXIT_MSG="$EXIT_MSG (SIGTERM - expected)"
    fi

    echo "  Receiver $i (${RECEIVER_NODES[$i]}, port ${RECEIVER_PORTS[$i]}): $EXIT_MSG"
done
echo ""

# Overall result
if $ALL_SENDERS_SUCCESS; then
    echo "Overall: SUCCESS (all senders completed successfully)"
    OVERALL_EXIT_CODE=0
else
    echo "Overall: FAILURE (one or more senders failed)"
    OVERALL_EXIT_CODE=1
fi
echo ""

echo "Logs available at:"
for ((i=0; i<NUM_RECEIVERS; i++)); do
    echo "  Receiver $i: $JOB_DIR/receiver_$i/"
done
for ((i=0; i<NUM_SENDERS; i++)); do
    echo "  Sender $i: $JOB_DIR/sender_$i/"
done
echo ""

echo "========================================="
echo "End time: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo "========================================="

# Exit with overall result
exit $OVERALL_EXIT_CODE
