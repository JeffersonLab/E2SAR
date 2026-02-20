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
- `--no-monitor`: Disable automatic memory monitoring
- `--image`: Override container image

### Receiver Parameters
- `--port`: Data receiving port (default: 10000)
- `--duration`: Run duration in seconds (default: 0 = indefinite)
- `--threads`: Number of receive threads (default: 16)
- `--deq`: Number of dequeue threads (default: 16)
- `--bufsize`: Socket buffer size in bytes (default: 134217728 = 128MB)
- `--ipv6`: Use IPv6 instead of IPv4
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