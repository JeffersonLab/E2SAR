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

```bash
# Example: Create a custom directory for your test runs
mkdir -p ~/my_runs_dir
cd ~/my_runs_dir

# Run scripts - all logs and INSTANCE_URI will be created here
minimal_reserve.sh
minimal_sender.sh --rate 5
minimal_receiver.sh --duration 60

# For runs on login nodes: all artifacts are in your current directory
ls  # Shows: INSTANCE_URI, minimal_sender.log, minimal_receiver.log, minimal_sender_memory.log

# For SLURM jobs: artifacts are organized in runs/slurm_job_<JOBID>/
# - Single instance: runs/slurm_job_12345/{INSTANCE_URI, minimal_sender.log, minimal_receiver.log, ...}
# - Multi-instance: runs/slurm_job_12345/{INSTANCE_URI, sender_0/, sender_1/, receiver_0/, receiver_1/, ...}
```

## Optional: Pre-Installing Containers on Perlmutter

On Perlmutter, you can pre-install container images to avoid re-downloading them on each compute node. This significantly reduces job startup time and network overhead.

### Pull and cache the container image

```bash
# On a login node or in an interactive session
podman-hpc pull ibaldin/e2sar:latest
```

This downloads the image once and stores it in your `$HOME/.local/share/containers` directory, which is accessible from all compute nodes via the shared filesystem.

### Verify the image is cached

```bash
podman-hpc images
```

You should see `ibaldin/e2sar` with tag `latest` in the list.

### Benefits

- **Faster job startup**: No download time when jobs start on compute nodes
- **Reduced network traffic**: Image downloaded once instead of per-node
- **Consistent image version**: All jobs use the same cached image

### Using custom or updated images

If you need to use a different image version:

```bash
# Pull the new image
podman-hpc pull ibaldin/e2sar:0.3.2

# Override in scripts with --image flag
./minimal_sender.sh --image ibaldin/e2sar:0.3.2 --rate 5

# Or set environment variable
export E2SAR_IMAGE=ibaldin/e2sar:0.3.2
./minimal_sender.sh --rate 5
```

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

### SSL Certificate Validation Issues
**Problem:** SSL certificate validation errors when connecting to the load balancer

**Solution:** Use the `-v` flag to skip SSL certificate validation:

```bash
# Skip validation for sender and receiver
./minimal_sender.sh -v --rate 5
./minimal_receiver.sh -v --duration 60

# Skip validation when freeing reservation
./minimal_free.sh -v
```

**Important notes:**
- **Do NOT** pass `-v` to `minimal_reserve.sh` — the reserve operation uses the admin token which skips certificate validation unconditionally
- The `-v` flag is particularly useful when the LB control plane SSL certificate has expired
- For SLURM jobs, pass `-v` to the SLURM script and it will propagate to sender/receiver/free operations:
  ```bash
  sbatch -A <project> perlmutter_slurm.sh -v --rate 10
  ```

**If `minimal_free.sh -v` still fails:** Use the admin `EJFAT_URI` to free reservations directly:

```bash
# View active reservations
podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview

# Free specific reservation by LB ID (replace 302 with actual ID)
podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --free --lbid 302
```

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
- `--mtu=9000` - Jumbo frame support
- `--bufsize=134217728` - 128MB buffer size

## Additional Resources

- [E2SAR GitHub Repository](https://github.com/JeffersonLab/E2SAR)
- [E2SAR Documentation](https://jeffersonlab.github.io/E2SAR/)
- EJFAT Project Documentation (contact your site administrator)

## License

This framework follows E2SAR's licensing. See the [E2SAR repository](https://github.com/JeffersonLab/E2SAR) for details.
