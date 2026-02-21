# Running the Minimal Scripts: From Zero to Hero

## Introduction

Welcome to the E2SAR Minimal Scripts tutorial! This guide will walk you through using the streamlined shell scripts that make network performance testing with E2SAR straightforward and accessible.

**CAUTION:** Perlmutter login nodes are only for lightweight workflow setup and orientation exercises. When testing these scripts on a login node, keep the data rate below 5Gbps, and do not send more than 60 seconds worth of traffic.

The minimal scripts are a set of bash wrappers around the E2SAR containerized tools. They simplify the process of:

- Creating load balancer reservations
- Sending network traffic for performance measurements
- Receiving and analyzing network data
- Running distributed tests on HPC systems like Perlmutter

By the end of this guide, you'll be able to run your own network performance tests and understand how to tune parameters for different scenarios.

### What You'll Need

Before starting, make sure you have:

1. **An EJFAT_URI**: This is your authentication token and load balancer connection string (format: `ejfat://token@hostname:port/lb/1?sync=...`)
2. **podman-hpc**: The containerized environment manager
3. **Network access**: Connectivity to the EJFAT load balancer
4. **The minimal scripts**: All scripts should be in the same directory

### Optional: Setup for Easy Access

You can add the scripts to your PATH for convenient access from any directory:

```bash
# Option 1: Temporary setup (current shell only)
source /path/to/zero_to_hero/setup_env.sh

# Option 2: Permanent setup (add to ~/.bashrc or ~/.zshrc)
echo 'source /path/to/zero_to_hero/setup_env.sh' >> ~/.bashrc
# Then start a new shell or: source ~/.bashrc
```

**After setup:**
- Run scripts from any directory without full paths
- All artifacts (INSTANCE_URI, logs) are created in your current directory
- Example:
  ```bash
  mkdir -p ~/e2sar_tests && cd ~/e2sar_tests
  minimal_reserve.sh  # Works! Artifacts created here
  ```

**Without setup:**
- Use full paths: `/path/to/minimal_sender.sh`
- Or run from the script directory: `cd /path/to/zero_to_hero && ./minimal_sender.sh`
- Artifacts are still created in your current working directory

### Quick Overview of the Workflow

The E2SAR testing workflow follows a simple four-step pattern:

```
Reserve → Run Receiver/Sender → Analyze Results → Free Resources
```

Each step has a dedicated script:

| Script | Purpose |
|--------|---------|
| `minimal_reserve.sh` | Create load balancer reservations |
| `minimal_receiver.sh` | Receive and measure network traffic |
| `minimal_sender.sh` | Send network traffic for testing |
| `minimal_free.sh` | Release reserved resources |
| `perlmutter_slurm.sh` | SLURM batch script for HPC environments |

---

## Part 1: Hello World - Your First Test

Let's start with the simplest possible test: sending 100 events at 1 Gbps.

### Step 1: Create a Reservation

First, you need to reserve resources on the load balancer. This creates a session that both senders and receivers will use.

```bash
EJFAT_URI="ejfat://your_token@lb.hostname:19522/lb/1?sync=..." ./minimal_reserve.sh
```

**What happens:**
- The script validates your EJFAT_URI
- Checks if a valid reservation already exists
- If needed, creates a new reservation and saves it to `INSTANCE_URI`

**Expected output:**
```
Checking for existing reservation...
Creating new reservation...
Reservation created and saved to INSTANCE_URI
EJFAT_URI=ejfat://...
```

**Important:** The `INSTANCE_URI` file is created in your current directory. This file is required by the sender and receiver scripts, so keep it safe!

### Step 2: Start the Receiver

In a terminal window, start the receiver. This will wait indefinitely for data:

```bash
./minimal_receiver.sh
```

**Expected output:**
```
Loading EJFAT_URI from INSTANCE_URI...
Starting E2SAR receiver...
Auto-detecting receiver IP...
LB Host: lb.hostname
LB IP: 192.168.1.100
Receiver IP: 10.0.0.50
Data Port: 10000
Receive Threads: 16
Dequeue Threads: 16
Buffer Size: 134217728

Running: podman-hpc run --rm --network host ...
START_TIME (UTC): 2026-02-17 14:30:00

Waiting for data...
```

The receiver is now ready to accept traffic. Leave this terminal open and running.

### Step 3: Run the Sender

In a new terminal (same directory), start the sender:

```bash
./minimal_sender.sh
```

**Expected output:**
```
Loading EJFAT_URI from INSTANCE_URI...
Starting E2SAR sender...
Auto-detecting sender IP...
Sender IP: 10.0.0.51
Rate: 1 Gbps
Event Length: 1048576 bytes
Number of Events: 100

Running: podman-hpc run --rm --network host ...
START_TIME (UTC): 2026-02-17 14:31:00

Sending 100 events at 1 Gbps...
Progress: 100/100 events sent
Performance: 1.02 Gbps, 0.95 Gbps payload

END_TIME (UTC): 2026-02-17 14:31:15
EXIT_CODE: 0
```

The sender will complete and exit. Check back in the receiver terminal - you should see it received the events!

### Step 4: Stop the Receiver

Go back to the receiver terminal and press `Ctrl+C` to stop it. You'll see final statistics:

```
Received 100 events
Total data: 104857600 bytes (100 MB)
Average rate: 1.01 Gbps
Packet loss: 0%

END_TIME (UTC): 2026-02-17 14:31:20
EXIT_CODE: 0
```

### Step 5: Free the Reservation

When you're done testing, release the load balancer resources:

```bash
./minimal_free.sh
```

**Expected output:**
```
Found INSTANCE_URI
Freeing load balancer reservation...
Reservation freed successfully
Removed INSTANCE_URI
```

**Congratulations!** You've just completed your first E2SAR network performance test.

---

## Part 2: Exploring the Scripts

Now that you've run a basic test, let's dive deeper into what each script can do.

### 2.1 minimal_reserve.sh - Reservation Management

The reservation script is your entry point. It creates a session on the load balancer that coordinates traffic between senders and receivers.

**Basic usage:**
```bash
EJFAT_URI="ejfat://..." ./minimal_reserve.sh
```

**Smart behavior:**
- If `INSTANCE_URI` already exists and is valid, the script does nothing (idempotent)
- If the reservation expired, it automatically creates a new one
- The reservation details are saved to `INSTANCE_URI` for other scripts to use

**Checking reservation status:**

You can manually verify your reservation is active:

```bash
source INSTANCE_URI
podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview
```

This shows detailed load balancer status including registered receivers.

### 2.2 minimal_sender.sh - Sending Traffic

The sender script transmits network events through the load balancer to registered receivers.

**Command-line options:**

| Option | Description | Default |
|--------|-------------|---------|
| `--rate RATE` | Sending rate in Gbps | 1 |
| `--length LENGTH` | Event buffer size in bytes | 1048576 (1 MB) |
| `--num COUNT` | Number of events to send | 100 |
| `--mtu MTU` | MTU size in bytes | 9000 |
| `--ipv6` | Use IPv6 instead of IPv4 | false |
| `-v` | Skip SSL certificate validation | disabled |
| `--no-monitor` | Disable memory monitoring | false (monitoring enabled) |
| `--image IMAGE` | Container image override | ibaldin/e2sar:0.3.1a3 |
| `--help` | Show help message | - |

**Example: High-rate test with larger buffers**

```bash
./minimal_sender.sh --rate 10 --length 2097152 --num 1000
```

This sends 1000 events at 10 Gbps with 2 MB buffers (total: ~2 GB of data).

**Example: Custom MTU for jumbo frames**

```bash
./minimal_sender.sh --rate 5 --mtu 9000
```

Uses 9000-byte MTU (jumbo frames) for improved efficiency on networks that support it.

**Example: Skip SSL certificate validation (testing/dev)**

```bash
./minimal_sender.sh -v --rate 5
```

Skips SSL certificate validation - useful for testing environments with self-signed certificates.

**What the sender does:**

1. Loads `EJFAT_URI` from the `INSTANCE_URI` file
2. Auto-detects the appropriate sender IP address by:
   - Extracting the load balancer hostname from `EJFAT_URI`
   - Resolving it to an IP address
   - Using `ip route get` to find the correct source IP
3. Runs the containerized `e2sar_perf --send` command
4. Logs all output to `minimal_sender.log` with timestamps

**Performance tuning:**

The sender uses several optimizations:
- `--optimize=sendmmsg`: Batched send syscalls for efficiency
- `--mtu=9000`: Jumbo frames (configurable via `--mtu` option)
- `--bufsize=134217728`: 128 MB socket buffer
- `MALLOC_ARENA_MAX=32`: Memory allocator tuning

### 2.3 minimal_receiver.sh - Receiving Traffic

The receiver script waits for and processes incoming network events.

**Command-line options:**

| Option | Description | Default |
|--------|-------------|---------|
| `--port PORT` | Data receiving port | 10000 |
| `--duration SEC` | Run duration in seconds (0 = indefinite) | 0 |
| `--threads NUM` | Number of receive threads | 16 |
| `--deq NUM` | Number of dequeue threads | 16 |
| `--bufsize SIZE` | Socket buffer size in bytes | 134217728 |
| `--ipv6` | Use IPv6 instead of IPv4 | false |
| `-v` | Skip SSL certificate validation | disabled |
| `--image IMAGE` | Container image override | ibaldin/e2sar:0.3.1a3 |
| `--help` | Show help message | - |

**Example: Time-limited receiver**

```bash
./minimal_receiver.sh --duration 60
```

The receiver will automatically stop after 60 seconds.

**Example: Custom port**

```bash
./minimal_receiver.sh --port 20000
```

This uses a custom data port for receiving.

**Example: High-throughput configuration**

```bash
./minimal_receiver.sh --threads 32 --deq 32 --bufsize 268435456
```

Doubles the thread count and socket buffer size for maximum throughput.

**Example: Skip SSL certificate validation (testing/dev)**

```bash
./minimal_receiver.sh -v --duration 60
```

Skips SSL certificate validation - useful for testing environments with self-signed certificates.

**Understanding receiver behavior:**

- With `--duration 0` (default), the receiver runs indefinitely until you press Ctrl+C
- The receiver registers itself with the load balancer automatically
- It uses a 3000ms timeout - if no packets arrive for 3 seconds, it reports this but continues waiting
- All output is logged to `minimal_receiver.log` with timestamps

**IP address auto-detection:**

Like the sender, the receiver automatically detects the correct IP address:
1. Extracts LB hostname from `EJFAT_URI`
2. Resolves to an IP address
3. Uses `ip route get` to find the source IP for that route

This ensures correct operation on multi-homed systems.

### 2.4 minimal_free.sh - Cleanup

The cleanup script releases your load balancer reservation.

**Basic usage:**
```bash
./minimal_free.sh
```

**What it does:**
1. Reads `EJFAT_URI` from the `INSTANCE_URI` file
2. Calls `lbadm --free` to release the reservation
3. Removes the `INSTANCE_URI` file

**When to use it:**
- Always run this when you're done testing
- If a reservation expires, just create a new one with `minimal_reserve.sh`
- If `minimal_free.sh` fails (network issue), you can manually delete `INSTANCE_URI` and create a fresh reservation

---

## Part 3: Advanced Scenarios

### 3.1 Multiple Receivers

You can run multiple receivers simultaneously to test load distribution.

**Terminal 1 - Receiver A:**
```bash
./minimal_receiver.sh --port 10000 --duration 120
```

**Terminal 2 - Receiver B:**
```bash
./minimal_receiver.sh --port 10001 --duration 120
```

**Terminal 3 - Sender:**
```bash
./minimal_sender.sh --rate 5 --num 5000
```

The load balancer will distribute events across both receivers. Check the logs to see the distribution!

### 3.2 Sequential Testing

For repeated tests, the reservation script makes things easy:

```bash
# Create reservation once
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# Run test 1
./minimal_receiver.sh --duration 60 &
sleep 5
./minimal_sender.sh --rate 1 --num 100
wait

# Run test 2 (same reservation!)
./minimal_receiver.sh --duration 60 &
sleep 5
./minimal_sender.sh --rate 10 --num 1000
wait

# Cleanup
./minimal_free.sh
```

The reservation persists across multiple sender/receiver invocations.

### 3.3 Log Analysis

All scripts generate detailed logs:

**Sender log analysis:**
```bash
# View performance summary
tail -20 minimal_sender.log

# Extract timing information
grep -E "(START_TIME|END_TIME|EXIT_CODE)" minimal_sender.log

# Check for errors
grep -i error minimal_sender.log
```

**Receiver log analysis:**
```bash
# View final statistics
tail -20 minimal_receiver.log

# Check data reception rate
grep -i "rate\|throughput" minimal_receiver.log
```

### 3.4 Environment Variable Configuration

You can override the container image for all scripts:

```bash
export E2SAR_IMAGE="ibaldin/e2sar:0.4.0"

./minimal_reserve.sh
./minimal_sender.sh --rate 5
```

Or per-command:
```bash
./minimal_sender.sh --image "ibaldin/e2sar:0.4.0" --rate 5
```

---


## Appendix: Tips and Troubleshooting

### Common Issues and Solutions

#### Problem: "ERROR: EJFAT_URI is required"

**Cause:** The `EJFAT_URI` environment variable is not set.

**Solution:**
```bash
EJFAT_URI="ejfat://your_token@hostname:port/lb/1?sync=..." ./minimal_reserve.sh
```

Make sure to include the full URI string in quotes.

---

#### Problem: "ERROR: INSTANCE_URI not found"

**Cause:** You're trying to run sender/receiver without creating a reservation first.

**Solution:**
```bash
# Create reservation first
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# Then run sender/receiver
./minimal_sender.sh
```

---

#### Problem: "ERROR: Failed to detect sender/receiver IP"

**Cause:** Network routing issue or load balancer hostname not resolvable.

**Solution:**
1. Verify LB hostname resolves:
   ```bash
   getent ahosts lb.hostname.net
   ```

2. Check network connectivity:
   ```bash
   ping lb.hostname.net
   ```

3. Verify routing:
   ```bash
   ip route get <LB_IP>
   ```

---

#### Problem: Receiver shows "Timeout waiting for packets"

**Cause:** No traffic arriving, or sender/receiver not using the same reservation.

**Solution:**
1. Verify both use the same `INSTANCE_URI` file
2. Check receiver registered with LB:
   ```bash
   source INSTANCE_URI
   podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview
   ```
3. Ensure sender ran successfully (check `minimal_sender.log`)

---

#### Problem: "ERROR: Existing reservation is invalid"

**Cause:** Reservation expired or was freed on the load balancer.

**Solution:** This is automatically handled - the script creates a new reservation. Just verify the new `INSTANCE_URI` is created.

---

#### Problem: SLURM job fails with reservation error

**Cause:** Load balancer reservation failed after job started, wasting queue time.

**Solution:** Pre-create the reservation on the login node to avoid waiting for job allocation:
```bash
# On login node
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# Then submit job (EJFAT_URI still needed for environment)
EJFAT_URI="ejfat://..." sbatch -A <project> perlmutter_slurm.sh
```

---

### Memory Monitoring

The `minimal_sender.sh` script includes automatic memory monitoring to help track resource usage during tests.

#### How It Works

When the sender runs, it automatically:
1. Starts a background memory monitor process
2. Samples memory usage every second
3. Logs data to `minimal_sender_memory.log`
4. Generates a summary when the test completes

#### Memory Log Location

- **Local runs:** `minimal_sender_memory.log` in the current directory
- **SLURM runs:** `runs/slurm_job_${SLURM_JOB_ID}/minimal_sender_memory.log`

#### Viewing Memory Results

After a test completes, check the memory log:

```bash
# View the summary (at the end of the file)
tail minimal_sender_memory.log

# Example output:
# Memory Summary:
#   Peak RSS: 245 MB (251392 KB)
#   Min RSS:  89 MB (91136 KB)
#   Growth:   156 MB
```

#### Analyzing Memory Data

The log file is CSV format with these columns:
- `TIMESTAMP` - ISO8601 timestamp
- `PID` - Process ID
- `RSS_KB` - Resident Set Size in KB
- `VSZ_KB` - Virtual memory Size in KB
- `%MEM` - Memory percentage
- `%CPU` - CPU percentage
- `ELAPSED_TIME` - Process elapsed time
- `COMMAND` - Full command line

You can analyze it with standard tools:

```bash
# Plot memory over time (requires gnuplot)
grep -v '^#' minimal_sender_memory.log | \
  awk -F', ' '{print NR, $3/1024}' | \
  gnuplot -e "set terminal dumb; plot '-' with lines title 'RSS (MB)'"

# Find peak memory usage
grep -v '^#' minimal_sender_memory.log | \
  awk -F', ' '{print $3}' | \
  sort -n | \
  tail -1 | \
  awk '{print $1/1024 " MB"}'
```

#### Disabling Memory Monitoring

If you don't need memory tracking (e.g., for benchmarking), disable it:

```bash
./minimal_sender.sh --no-monitor --rate 10 --num 1000
```

#### Why Monitor Memory?

Memory monitoring helps you:
- Detect memory leaks or accumulation issues
- Optimize buffer sizes and event counts
- Compare different optimization modes (sendmsg vs sendmmsg vs liburing)
- Validate memory usage stays within system limits

For detailed analysis of memory behavior with different configurations, see [E2SAR_MEMORY_ANALYSIS.md](E2SAR_MEMORY_ANALYSIS.md).

---

### Performance Tuning Tips

#### Maximizing Throughput

For high-rate tests (>10 Gbps):

**Sender:**
```bash
./minimal_sender.sh --rate 25 --length 8388608 --num 10000
```

**Receiver:**
```bash
./minimal_receiver.sh --threads 32 --deq 32 --bufsize 268435456
```

**Key parameters:**
- Larger `--length` (event size) reduces packet overhead
- More `--threads` and `--deq` increase parallelism
- Larger `--bufsize` prevents socket buffer overflow

#### Testing Packet Loss

To test under stress conditions:

```bash
# Receiver with limited resources
./minimal_receiver.sh --threads 4 --deq 2 --bufsize 33554432 &

# High-rate sender
./minimal_sender.sh --rate 40 --num 100000 --length 1048576
```

Check receiver logs for dropped packets.

---

### Log File Reference

#### minimal_sender.log Format

```
START_TIME (UTC): 2026-02-17 14:30:00

[Container initialization output]
Sending 100 events at 1 Gbps...
Progress: 100/100 events sent
Performance: 1.02 Gbps, 0.95 Gbps payload

END_TIME (UTC): 2026-02-17 14:30:15
EXIT_CODE: 0
```

**Key metrics:**
- **Performance**: Raw rate (including headers) vs payload rate
- **EXIT_CODE**: 0 = success, non-zero = error

#### minimal_receiver.log Format

```
START_TIME (UTC): 2026-02-17 14:30:00

[Container initialization output]
Waiting for data...
Received 100 events
Total data: 104857600 bytes (100 MB)
Average rate: 1.01 Gbps
Packet loss: 0%

END_TIME (UTC): 2026-02-17 14:30:20
EXIT_CODE: 0
```

**Key metrics:**
- **Total data**: Bytes received (payload)
- **Average rate**: Sustained receive rate
- **Packet loss**: Percentage of events dropped

---

### Quick Reference Commands

**Complete test sequence:**
```bash
# Setup
EJFAT_URI="ejfat://..." ./minimal_reserve.sh

# Test
./minimal_receiver.sh --duration 60 &
sleep 5
./minimal_sender.sh --rate 5 --num 1000

# Cleanup
./minimal_free.sh
```

**Check reservation status:**
```bash
source INSTANCE_URI
podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview
```

**View help for any script:**
```bash
./minimal_sender.sh --help
./minimal_receiver.sh --help
```

**Emergency cleanup:**
```bash
# If minimal_free.sh fails
rm -f INSTANCE_URI
rm -f *.log

# Start fresh
EJFAT_URI="ejfat://..." ./minimal_reserve.sh
```

---

## Conclusion

You now have all the tools you need to run E2SAR network performance tests!

**Remember the basic workflow:**
1. **Reserve** resources with `minimal_reserve.sh`
2. **Start receiver** with `minimal_receiver.sh`
3. **Run sender** with `minimal_sender.sh`
4. **Analyze logs** to see performance results
5. **Free resources** with `minimal_free.sh`

For HPC environments with SLURM, see **[RunningSlurmOnPerlmutter.md](RunningSlurmOnPerlmutter.md)** for batch job submission.

**Key takeaways:**
- Always create a reservation first
- The `INSTANCE_URI` file is shared between all scripts
- Logs contain detailed timestamps and performance metrics
- On HPC systems, pre-create reservations on login nodes
- Use `--help` on any script to see all options

Happy testing!
