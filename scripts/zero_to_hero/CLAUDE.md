# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an E2SAR (Enhanced End-to-End Send And Receive) network performance testing framework designed for high-performance computing environments. The system uses containerized bash scripts to orchestrate network performance measurements between sender and receiver nodes through a load balancer.

## Core Architecture

The framework follows a reservation-based workflow using four main components:

1. **Load Balancer (LB) Reservation**: `minimal_reserve.sh` creates network resource reservations
2. **Sender**: `minimal_sender.sh` sends network traffic for performance testing
3. **Receiver**: `minimal_receiver.sh` receives and measures network traffic
4. **Cleanup**: `minimal_free.sh` releases reserved network resources

All operations use the `ibaldin/e2sar:0.3.1a3` container image via `podman-hpc`.

## Required Workflow Sequence

**Always follow this sequence:**

1. **Reserve resources first:**
   ```bash
   EJFAT_URI="ejfat://token@host:port/lb/1?sync=..." ./minimal_reserve.sh
   ```

2. **Run sender and/or receiver** (can run simultaneously):
   ```bash
   ./minimal_sender.sh [OPTIONS]
   ./minimal_receiver.sh [OPTIONS]
   ```

3. **Free resources when done:**
   ```bash
   ./minimal_free.sh
   ```

The reservation creates an `INSTANCE_URI` file that contains the `EJFAT_URI` needed by sender and receiver scripts.

## Setup and Environment Configuration

### Directory-Independent Operation

The scripts can be run from any directory. All artifacts (INSTANCE_URI, log files) are created in the current working directory, not the script directory.

### One-Time Setup (Optional)

Add the scripts to your PATH for easy access:

```bash
# Option 1: Source directly (temporary, current shell only)
source /path/to/zero_to_hero/setup_env.sh

# Option 2: Add to shell config (permanent)
echo 'source /path/to/zero_to_hero/setup_env.sh' >> ~/.bashrc  # or ~/.zshrc
```

After sourcing the setup script:

```bash
# Create your working directory
mkdir -p ~/my_tests && cd ~/my_tests

# Scripts are now in your PATH - run from anywhere
minimal_reserve.sh
minimal_sender.sh --rate 5
minimal_receiver.sh --duration 60

# All artifacts are created in the current directory
ls  # Shows: INSTANCE_URI, minimal_sender.log, minimal_receiver.log, etc.
```

### Running Without Setup Script

You can also invoke scripts with full paths:

```bash
cd /tmp/my_test
EJFAT_URI="..." /path/to/zero_to_hero/minimal_reserve.sh
/path/to/zero_to_hero/minimal_sender.sh --rate 5
# Artifacts are still created in /tmp/my_test
```

## Common Commands

### Reservation Management
```bash
# Create reservation (required first step)
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# Check reservation status
podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview

# Free reservation (cleanup)
./minimal_free.sh
```

### Sender Operations
```bash
# Basic sender (1 Gbps, 100 events, 1MB buffers)
# Includes automatic memory monitoring
./minimal_sender.sh

# High-rate sender with custom parameters
./minimal_sender.sh --rate 10 --length 2097152 --num 1000

# IPv6 sender
./minimal_sender.sh --ipv6 --rate 5

# Custom MTU (e.g., jumbo frames)
./minimal_sender.sh --mtu 9000 --rate 10

# Disable memory monitoring (for pure performance benchmarking)
./minimal_sender.sh --no-monitor --rate 10

# Skip SSL certificate validation (for testing/dev environments)
./minimal_sender.sh -v --rate 5
```

### Receiver Operations
```bash
# Basic receiver (indefinite duration)
./minimal_receiver.sh

# Receiver with time limit
./minimal_receiver.sh --duration 60

# Custom port receiver
./minimal_receiver.sh --port 20000 --duration 120

# IPv6 receiver
./minimal_receiver.sh --ipv6 --duration 60

# High-throughput receiver (more threads and buffer)
./minimal_receiver.sh --threads 32 --deq 32 --bufsize 268435456

# Skip SSL certificate validation (for testing/dev environments)
./minimal_receiver.sh -v --duration 60
```

## Configuration System

### Environment Variables
- `EJFAT_URI`: The primary configuration URI containing authentication token and load balancer details
- `E2SAR_IMAGE`: Container image override (default: `ibaldin/e2sar:0.3.1a3`)

### State Management
- `INSTANCE_URI`: File containing reservation details shared between scripts
- `minimal_sender.log`: Detailed sender execution log with timestamps and exit codes
- `minimal_sender_memory.log`: Automatic memory usage monitoring data (CSV format)
- `minimal_receiver.log`: Detailed receiver execution log with timestamps and exit codes

### Network Auto-Detection
The system automatically detects appropriate IP addresses by:
1. Extracting LB hostname from `EJFAT_URI`
2. Resolving hostname to IP (IPv4/IPv6)
3. Using `ip route get` to find the local source IP for that destination
4. This ensures correct network interface selection in multi-homed systems

## Key Performance Parameters

### Sender Parameters
- `--rate`: Sending rate in Gbps (default: 1)
- `--length`: Event buffer size in bytes (default: 1048576 = 1MB)
- `--num`: Number of events to send (default: 100)
- `--mtu`: MTU size in bytes (default: 9000)
- `--ipv6`: Use IPv6 instead of IPv4
- `-v`: Skip SSL certificate validation (default: disabled)
- `--no-monitor`: Disable automatic memory monitoring
- `--image`: Override container image

### Receiver Parameters
- `--port`: Data receiving port (default: 10000)
- `--duration`: Run duration in seconds (default: 0 = indefinite)
- `--threads`: Number of receive threads (default: 16)
- `--deq`: Number of dequeue threads (default: 16)
- `--bufsize`: Socket buffer size in bytes (default: 134217728 = 128MB)
- `--ipv6`: Use IPv6 instead of IPv4
- `-v`: Skip SSL certificate validation (default: disabled)
- `--image`: Override container image

### Container Optimizations
Both scripts use these optimizations:
- `--network host`: Direct host networking for performance
- `MALLOC_ARENA_MAX=32`: Memory allocation tuning
- `--mtu=8500`: Jumbo frame support
- `--bufsize=134217728`: 128MB buffer size
- `--optimize=sendmmsg`: Sender-side syscall optimization

## Monitoring and Debugging

### Log Analysis
Check log files for performance metrics and troubleshooting:
```bash
# View sender results
tail -f minimal_sender.log

# Check receiver performance
tail -f minimal_receiver.log

# View memory usage summary
tail minimal_sender_memory.log

# Analyze timestamps and exit codes
grep -E "(START_TIME|END_TIME|EXIT_CODE)" *.log
```

### Memory Monitoring
The sender automatically monitors memory usage and logs to `minimal_sender_memory.log`:
```bash
# View memory summary (at end of file)
tail minimal_sender_memory.log
# Shows: Peak RSS, Min RSS, Growth

# Track memory in real-time (during test)
tail -f minimal_sender_memory.log | grep -v '^#'

# Disable monitoring if not needed
./minimal_sender.sh --no-monitor
```

### Standalone Memory Monitor
For detailed memory tracking across all e2sar_perf processes:
```bash
# Start monitoring in separate terminal (1 second interval)
./monitor_memory.sh 1

# View collected data
tail -f memory_monitor.log

# Analyze CSV data: TIMESTAMP, PID, RSS_KB, VSZ_KB, %MEM, %CPU, ELAPSED_TIME, COMMAND
grep -v '^#' memory_monitor.log
```

### Validation Commands
```bash
# Verify reservation is active
source INSTANCE_URI && podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview

# Test container connectivity
podman-hpc run --rm --network host ibaldin/e2sar:0.3.1a3 e2sar_perf --help
```

## Error Recovery

### Common Issues
1. **Missing INSTANCE_URI**: Always run `minimal_reserve.sh` first
2. **Invalid reservation**: Re-run `minimal_reserve.sh` to create new reservation
3. **Network detection failure**: Check connectivity to load balancer hostname
4. **Container issues**: Verify `podman-hpc` and image availability

### Cleanup and Reset
```bash
# Force cleanup if minimal_free.sh fails
rm -f INSTANCE_URI
rm -f *.log

# Start fresh
EJFAT_URI="..." ./minimal_reserve.sh
```

## SLURM Batch Processing (Perlmutter)

The `perlmutter_slurm.sh` script orchestrates distributed tests on Perlmutter HPC system:

```bash
# Basic SLURM submission
EJFAT_URI="ejfat://..." sbatch -A <project> perlmutter_slurm.sh

# With custom test parameters
EJFAT_URI="ejfat://..." sbatch -A <project> perlmutter_slurm.sh --rate 10 --num 5000 --length 2097152

# Override SLURM parameters
sbatch -A <project> -q regular -t 01:00:00 perlmutter_slurm.sh --rate 20 --mtu 9000

# Skip SSL certificate validation
EJFAT_URI="ejfat://..." sbatch -A <project> perlmutter_slurm.sh -v --rate 10
```

**Key features:**
- Designed specifically for Perlmutter at NERSC
- Uses exactly 2 nodes (Node 0: receiver, Node 1: sender)
- Creates isolated working directory: `runs/slurm_job_<JOBID>/`
- Handles reservation, execution, and cleanup automatically
- Collects all logs in job-specific directory

**Pre-create reservation (recommended):**
```bash
# On login node before submitting job
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# Then submit (avoids waiting for allocation to discover reservation issues)
EJFAT_URI="ejfat://..." sbatch -A <project> perlmutter_slurm.sh
```

### Multi-Instance SLURM Testing (Perlmutter)

The `perlmutter_multi_slurm.sh` script enables testing with multiple concurrent senders and receivers. Senders and receivers share the same node pool and can be co-located on the same nodes.

```bash
# 4 receivers + 4 senders co-located on 2 nodes (2 of each per node)
EJFAT_URI="ejfat://..." sbatch -N 2 -A <project> perlmutter_multi_slurm.sh \
    --receivers 4 --receivers-per-node 2 \
    --senders 4 --senders-per-node 2 \
    --rate 1 --num 10000

# Single node running all instances
EJFAT_URI="ejfat://..." sbatch -N 1 -A <project> perlmutter_multi_slurm.sh \
    --receivers 2 --receivers-per-node 2 \
    --senders 2 --senders-per-node 2 \
    --rate 1 --num 1000

# 2 receivers + 2 senders, one per node (no co-location)
EJFAT_URI="ejfat://..." sbatch -N 2 -A <project> perlmutter_multi_slurm.sh \
    --receivers 2 --senders 2 --rate 1 --num 10000
```

**Multi-instance options:**
- `--receivers N`: Total number of receiver instances (default: 1)
- `--senders M`: Total number of sender instances (default: 1)
- `--receivers-per-node K`: Receiver instances per node (default: 1)
- `--senders-per-node K`: Sender instances per node (default: 1)
- `--threads N`: Receive threads per receiver instance; also sets port stride (default: 16)
- `--base-port PORT`: Starting port for receiver 0 (default: 10000)
- `--receiver-delay SEC`: Wait time after starting receivers (default: 10)

**Node allocation formula:**
```
Receiver nodes = ceil(receivers / receivers-per-node)
Sender nodes   = ceil(senders  / senders-per-node)
Total nodes    = max(Receiver nodes, Sender nodes)
```

**Port assignment:**

Each receiver uses `--threads` consecutive ports. Receiver `i` gets ports `base_port + i * threads` through `base_port + i * threads + threads - 1`. With defaults (`--base-port 10000 --threads 16`):
- Receiver 0: 10000–10015
- Receiver 1: 10016–10031
- Receiver 2: 10032–10047

**Key features:**
- Senders and receivers share the same node pool (co-location supported)
- Each instance runs in an isolated subdirectory with its own logs
- All senders and receivers start in parallel
- Script waits for all senders to complete before shutting down receivers
- Graceful receiver shutdown with SIGTERM then SIGKILL
- Comprehensive summary report with all exit codes

**Log structure:**
```
runs/slurm_job_<JOBID>/
├── receiver_0/
│   ├── minimal_receiver.log
│   └── receiver_srun.log
├── receiver_1/
│   ├── minimal_receiver.log
│   └── receiver_srun.log
├── sender_0/
│   ├── minimal_sender.log
│   ├── minimal_sender_memory.log
│   └── sender_srun.log
├── sender_1/
│   ├── minimal_sender.log
│   ├── minimal_sender_memory.log
│   └── sender_srun.log
└── INSTANCE_URI
```

## Additional Scripts

### monitor_memory.sh
Standalone memory monitoring for all e2sar_perf processes:
- Logs to `memory_monitor.log` in CSV format
- Samples at configurable interval (default: 1 second)
- Tracks: RSS, VSZ, %MEM, %CPU, elapsed time
- Use in parallel with tests for detailed memory analysis

## Development Notes

- All scripts use `set -euo pipefail` for strict error handling
- IP detection logic is shared between sender and receiver scripts
- Container commands are built as arrays to handle complex parameter passing
- Signal trapping ensures proper log completion even on interruption
- The framework supports both IPv4 and IPv6 operation modes
- Memory monitoring is automatic in sender (disable with `--no-monitor`)