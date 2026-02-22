# E2SAR Zero to Hero - Network Performance Testing Framework

This repository contains a containerized E2SAR (Enhanced End-to-End Send And Receive) framework for high-performance network testing in HPC environments.

## Optional: Setup for Easy Access

Add scripts to your PATH to run them from any directory:

```bash
# Temporary (current shell)
source /path/to/zero_to_hero/setup_env.sh

# Permanent (add to ~/.bashrc or ~/.zshrc)
echo 'source /path/to/zero_to_hero/setup_env.sh' >> ~/.bashrc
```

After setup, create working directories and run scripts from anywhere - all artifacts are created in your current directory.

## Quick Start

### 1. Create Reservation
```bash
EJFAT_URI="ejfat://token@host:port/lb/1?sync=..." ./minimal_reserve.sh
```

### 2. Run Sender
```bash
./minimal_sender.sh --rate 5 --num 1000
```

### 3. Run Receiver (in another terminal/node)
```bash
./minimal_receiver.sh --duration 60
```

### 4. Clean Up
```bash
./minimal_free.sh
```

## Documentation

- **[CLAUDE.md](../CLAUDE.md)** - Project overview and core architecture
- **[RunningMinimalScripts.md](RunningMinimalScripts.md)** - Detailed usage guide

## Scripts

### Reservation Management
| Script | Purpose |
|--------|---------|
| `minimal_reserve.sh` | Create LB reservation (required first step) |
| `minimal_free.sh` | Release reservation and cleanup |

### Network Testing
| Script | Purpose |
|--------|---------|
| `minimal_sender.sh` | Sender with flexible options and memory monitoring |
| `minimal_receiver.sh` | Receiver with configurable duration |
| `perlmutter_slurm.sh` | SLURM batch job template (single sender/receiver) |
| `perlmutter_multi_slurm.sh` | SLURM batch job for multiple concurrent senders/receivers |

### Monitoring
| Script | Purpose |
|--------|---------|
| `monitor_memory.sh` | Real-time memory usage monitoring |

## Memory-Efficient Usage

### Why Memory Matters

E2SAR's send mode can accumulate memory depending on configuration. The analysis identified three key factors:

1. **Optimization mode** - `liburing_send` defers cleanup, `sendmmsg` is synchronous
2. **Buffer allocation** - `--realmalloc` allocates before queue checks
3. **Rate limiting** - Applied after queueing, not before

### Recommended Configurations

**Lowest memory (safest):**
```bash
./minimal_sender.sh --rate 5 --optimize sendmmsg
# Uses: sendmmsg optimization (synchronous, lower memory), reusable buffers
```

**Balanced performance:**
```bash
./minimal_sender.sh --rate 10 --optimize sendmmsg
```

**Avoid these combinations:**
- `--optimize liburing_send` + large events (high accumulation)
- `--realmalloc` + high rate (pre-allocation issues)
- `--smooth` mode (increases thread pool pressure)

## Monitoring Memory Usage

### Start monitoring before running tests
```bash
# Terminal 1: Monitor memory
./monitor_memory.sh 1

# Terminal 2: Run sender
./minimal_sender.sh --rate 5 --num 10000
```

## Typical Workflow

### Local Testing
```bash
# 1. Reserve resources
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# 2. Monitor memory (optional)
./monitor_memory.sh 1 &

# 3. Run sender
./minimal_sender.sh --rate 5 --num 1000

# 4. Check logs
tail -f minimal_sender.log

# 5. Clean up
./minimal_free.sh
```

### HPC Cluster (SLURM)
```bash
# Submit batch job
sbatch perlmutter_slurm.sh

# Monitor job
squeue -u $USER
tail -f slurm-*.out
```

## Troubleshooting

### Reservation Issues
**Problem:** Missing INSTANCE_URI file
- **Solution:** Run `minimal_reserve.sh` first

**Problem:** Connection refused or timeout
- **Solution:** Verify EJFAT_URI is valid and LB is accessible

### Network Detection Issues
**Problem:** Cannot determine source IP
- **Solution:** Check connectivity to LB hostname: `ping <LB_HOST>`

### Exit Code Issues
**Problem:** Exit code 141 (SIGPIPE) in sender or receiver logs
- **Note:** This was a known issue caused by the `tee` pipeline receiving SIGPIPE when the container process exited before all output was flushed. It has been fixed in the current scripts using `|| true` guards and `PIPESTATUS[0]` to capture the container's actual exit code. If you see exit code 141 in older script versions, update to the latest scripts.

## Performance Parameters

### Sender Key Options
- `--rate GBPS` - Target sending rate (default: 1)
- `--num EVENTS` - Number of events to send (default: 100)
- `--length BYTES` - Event buffer size (default: 1048576 = 1MB)
- `--optimize MODE` - sendmsg (default), sendmmsg (recommended), liburing_send (high risk)
- `--realmalloc` - Allocate buffers per event (increases memory, avoid if possible)

### Receiver Key Options
- `--port PORT` - Data receiving port (default: 10000)
- `--duration SECONDS` - Run time in seconds (default: 0 = indefinite)

## Log Files

| File | Contains |
|------|----------|
| `minimal_sender.log` | Sender execution log with timestamps |
| `minimal_sender_memory.log` | Automatic memory usage monitoring (CSV format) |
| `minimal_receiver.log` | Receiver execution log with timestamps |
| `memory_monitor.log` | Real-time memory usage data from monitor_memory.sh |

## Container Configuration

**Image:** `ibaldin/e2sar:0.3.1a3` (configurable via `E2SAR_IMAGE`)

**Optimizations:**
- `--network host` - Direct host networking
- `MALLOC_ARENA_MAX=32` - Memory allocation tuning
- `--mtu=8500` - Jumbo frame support
- `--bufsize=134217728` - 128MB buffer size

## Contributing

When modifying scripts or adding features, please:
1. Test with `monitor_memory.sh` to verify memory behavior
2. Update relevant documentation (CLAUDE.md, this README, or memory docs)
3. Use `set -euo pipefail` for strict error handling
4. Follow existing logging conventions

## Additional Resources

- [E2SAR GitHub Repository](https://github.com/JeffersonLab/E2SAR)
- [E2SAR Documentation](https://jeffersonlab.github.io/E2SAR/)
- EJFAT Project Documentation (contact your site administrator)

## License

This framework follows E2SAR's licensing. See the [E2SAR repository](https://github.com/JeffersonLab/E2SAR) for details.
