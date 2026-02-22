# Running SLURM Jobs on Perlmutter

The `perlmutter_slurm.sh` script orchestrates distributed E2SAR tests on HPC systems using the SLURM workload manager.

## Understanding the SLURM Script

The SLURM script automates a complete sender/receiver test across two compute nodes:

**What it does:**
1. Reserves load balancer resources (or uses pre-created reservation)
2. Starts receiver on Node 0 (background)
3. Waits 10 seconds for receiver registration
4. Starts sender on Node 1 (foreground)
5. Terminates receiver after sender completes
6. Frees load balancer reservation
7. Collects all logs into a job-specific directory

**Key features:**
- Uses exactly 2 nodes (configurable via SLURM options)
- Creates isolated working directory: `runs/slurm_job_<JOBID>/`
- Generates comprehensive logs for debugging
- Handles cleanup automatically

## Pre-Creating Reservations (Recommended)

It's recommended to create your reservation on the login node before submitting the SLURM job. This avoids waiting for your job to start only to have the reservation fail. The best practice is to create your reservation on the login node:

**On the login node:**
```bash
cd /path/to/minimal_scripts

# Create reservation
EJFAT_URI="ejfat://your_token@lb.hostname:19522/lb/1?sync=..." ./minimal_reserve.sh

# Verify it was created
ls -l INSTANCE_URI
cat INSTANCE_URI
```

This creates an `INSTANCE_URI` file that the SLURM script will use.

## Submitting Jobs

### Finding Your Project Allocation

Before submitting jobs, you need to know your NERSC project allocation. To find your available projects:

```bash
# View all your project allocations and remaining hours
iris

# Alternative: Check your default project
sacctmgr show user $USER format=account%20,defaultaccount
```

The `iris` command shows all your active allocations with remaining compute hours. Common project formats are:
- Repository allocations: `m####` (e.g., `m1234`)
- Startup allocations: `m####_g`
- ALCC/INCITE allocations: Named projects

Use the project name with the `-A` flag when submitting SLURM jobs.

### Basic Job Submission

**Basic job submission:**

```bash
EJFAT_URI="ejfat://..." sbatch -A <your_project> perlmutter_slurm.sh
```

**With custom parameters:**

```bash
EJFAT_URI="ejfat://..." sbatch -A <your_project> perlmutter_slurm.sh --rate 10 --num 5000 --length 2097152
```

**With custom MTU:**

```bash
EJFAT_URI="ejfat://..." sbatch -A <your_project> perlmutter_slurm.sh --rate 20 --mtu 9000
```

**SLURM options you can override:**

```bash
sbatch -A <project> \
       -N 2 \            # Number of nodes (fixed at 2)
       -q debug \        # Queue: debug or regular
       -t 00:30:00 \     # Time limit
       perlmutter_slurm.sh --rate 5
```

**All available test options:**

| Option | Description | Default |
|--------|-------------|---------|
| `--rate RATE` | Sending rate in Gbps | 1 |
| `--length LENGTH` | Event buffer size in bytes | 1048576 |
| `--num COUNT` | Number of events | 100 |
| `--mtu MTU` | MTU size in bytes | 9000 |
| `--port PORT` | Receiver data port | 10000 |
| `--image IMAGE` | Container image | ibaldin/e2sar:0.3.1a3 |

## Monitoring Jobs

**Check job status:**
```bash
squeue -u $USER
```

**Watch live output:**
```bash
tail -f slurm-<JOBID>.out
```

**After job completes:**
```bash
# View SLURM output
cat slurm-<JOBID>.out

# Navigate to job directory
cd runs/slurm_job_<JOBID>/

# Check individual logs
cat minimal_sender.log
cat minimal_receiver.log
cat sender_srun.log
cat receiver_srun.log
```

## Example SLURM Workflow

**Complete example: High-rate test on Perlmutter**

```bash
# 1. Login to Perlmutter
ssh username@perlmutter-p1.nersc.gov

# 2. Navigate to scripts directory
cd /global/homes/u/username/e2sar/zero_to_hero

# 3. Create reservation on login node
EJFAT_URI="ejfat://token@lb.es.net:19522/lb/1?sync=..." ./minimal_reserve.sh

# 4. Submit job (replace m1234 with your project - use 'iris' to find it)
sbatch -A m1234 -q regular -t 01:00:00 perlmutter_slurm.sh \
    --rate 20 \
    --num 10000 \
    --length 4194304 \
    --mtu 9000

# 5. Monitor
squeue -u $USER
tail -f slurm-*.out

# 6. After completion, analyze results
cd runs/slurm_job_*/
grep -E "Performance|rate|throughput" minimal_sender.log minimal_receiver.log
```

## Multi-Instance Testing with `perlmutter_multi_slurm.sh`

The `perlmutter_multi_slurm.sh` script extends the single sender/receiver model to support multiple concurrent senders and receivers. Senders and receivers share the same node pool and may be **co-located** on the same nodes, enabling flexible configurations from a single node running everything to many nodes each running multiple instances.

### How It Works

1. Reserves a fresh load balancer instance for the job
2. Starts all receiver instances in parallel (co-located on shared nodes)
3. Waits a configurable delay for receivers to register with the LB
4. Starts all sender instances in parallel (co-located on the same shared nodes)
5. Waits for all senders to complete
6. Gracefully terminates all receivers (SIGTERM, then SIGKILL after 5s)
7. Frees load balancer reservation
8. Prints a summary report with all exit codes

### Multi-Instance Parameters

| Option | Description | Default |
|--------|-------------|---------|
| `--receivers N` | Total number of receiver instances | 1 |
| `--senders M` | Total number of sender instances | 1 |
| `--receivers-per-node K` | Receiver instances per node | 1 |
| `--senders-per-node K` | Sender instances per node | 1 |
| `--threads N` | Receive threads per receiver (also sets port stride) | 16 |
| `--base-port PORT` | Starting port for receiver 0 | 10000 |
| `--receiver-delay SEC` | Seconds to wait after starting receivers | 10 |

All test options (`--rate`, `--length`, `--num`, `--mtu`, `--image`, `--ipv6`, `-v`) are supported and passed through to the underlying scripts.

### Node Calculation Formula

Senders and receivers share the same pool of nodes. The script requires:

```
Receiver nodes = ceil(receivers / receivers-per-node)
Sender nodes   = ceil(senders  / senders-per-node)
Total nodes    = max(Receiver nodes, Sender nodes)
```

Request exactly this many nodes via `sbatch -N <total>`.

### Port Assignment

Each receiver instance uses `--threads` consecutive ports (default: 16). Receiver ports are assigned as:

```
Receiver 0: base_port + 0 * threads  →  base_port to base_port + threads - 1
Receiver 1: base_port + 1 * threads  →  ...
Receiver i: base_port + i * threads
```

With the defaults (`--base-port 10000 --threads 16`):
- Receiver 0: ports 10000–10015
- Receiver 1: ports 10016–10031
- Receiver 2: ports 10032–10047

If you change `--threads`, the port stride updates automatically to match.

### Example Submissions

**4 receivers and 4 senders co-located on 2 nodes (2 of each per node):**
```bash
EJFAT_URI="ejfat://..." sbatch -N 2 -A <project> perlmutter_multi_slurm.sh \
    --receivers 4 --receivers-per-node 2 \
    --senders 4 --senders-per-node 2 \
    --rate 1 --num 10000
```

**Single node running all instances:**
```bash
EJFAT_URI="ejfat://..." sbatch -N 1 -A <project> perlmutter_multi_slurm.sh \
    --receivers 2 --receivers-per-node 2 \
    --senders 2 --senders-per-node 2 \
    --rate 1 --num 1000
```

**2 receivers + 2 senders, one per node (no co-location):**
```bash
EJFAT_URI="ejfat://..." sbatch -N 2 -A <project> perlmutter_multi_slurm.sh \
    --receivers 2 --senders 2 --rate 1 --num 10000
```

**Custom thread count (port stride updates automatically):**
```bash
EJFAT_URI="ejfat://..." sbatch -N 2 -A <project> perlmutter_multi_slurm.sh \
    --receivers 4 --receivers-per-node 2 \
    --senders 4 --senders-per-node 2 \
    --threads 8 --rate 1 --num 5000
# Receiver ports: 10000-10007, 10008-10015, 10016-10023, 10024-10031
```

### Output Directory Structure

Each job creates an isolated directory under `runs/`:

```
runs/slurm_job_<JOBID>/
├── INSTANCE_URI
├── receiver_0/
│   ├── INSTANCE_URI
│   ├── minimal_receiver.log
│   └── receiver_srun.log
├── receiver_1/
│   ├── INSTANCE_URI
│   ├── minimal_receiver.log
│   └── receiver_srun.log
├── sender_0/
│   ├── INSTANCE_URI
│   ├── minimal_sender.log
│   ├── minimal_sender_memory.log
│   └── sender_srun.log
└── sender_1/
    ├── INSTANCE_URI
    ├── minimal_sender.log
    ├── minimal_sender_memory.log
    └── sender_srun.log
```

Receivers terminated by the script (after senders complete) will show exit codes 137 (SIGKILL) or 143 (SIGTERM) in the SLURM accounting — both are expected and reported as such in the summary.

---

## Understanding SLURM Output

The SLURM script produces detailed output with clear phase markers:

```
=========================================
EJFAT Minimal Test - SLURM Job 12345678
=========================================
Start time: 2026-02-17 10:30:00 UTC

EJFAT_URI: ejfat://...
Job nodes: nid[003201-003202]
Job ID: 12345678

Receiver node (Node 0): nid003201
Sender node (Node 1): nid003202

=========================================
Phase 1: Reserve Load Balancer
=========================================
Found existing INSTANCE_URI in submit directory...
Reservation ready

=========================================
Phase 2: Start Receiver on nid003201
=========================================
Receiver started (PID: 54321)
Waiting 10 seconds for receiver to register...

=========================================
Phase 3: Start Sender on nid003202
=========================================
[sender output...]
Sender completed (exit code: 0)

=========================================
Phase 4: Shutdown Receiver
=========================================
Sending SIGKILL to receiver...
Receiver terminated successfully

=========================================
Phase 5: Free Load Balancer
=========================================
Reservation freed successfully

=========================================
Test Summary
=========================================
Job ID: 12345678
Job directory: /path/to/runs/slurm_job_12345678
Sender exit code: 0
Receiver exit code: 0

Logs available at:
  - Sender log: /path/to/runs/slurm_job_12345678/minimal_sender.log
  - Receiver log: /path/to/runs/slurm_job_12345678/minimal_receiver.log
  ...
```
