# E2SAR Zero to Hero

This directory contains minimal wrapper scripts for running E2SAR network performance tests on HPC systems, specifically designed for use with Perlmutter and the ESnet load balancer.

## Getting Started

Please see the documentation in the `docs/` directory:

- **[docs/ZeroToHeroStart.md](docs/ZeroToHeroStart.md)** - First tutorial: Simple loopback testing on your laptop
- **[docs/QuickStartMinimalScripts.md](docs/QuickStartMinimalScripts.md)** - Second tutorial: Running traffic on Perlmutter
- **[docs/RunningMinimalScripts.md](docs/RunningMinimalScripts.md)** - Comprehensive guide with detailed examples
- **[docs/RunningSlurmOnPerlmutter.md](docs/RunningSlurmOnPerlmutter.md)** - Third tutorial: Running SLURM batch jobs on Perlmutter

## Quick Start

```bash
# 1. Reserve load balancer
EJFAT_URI="ejfat://token@host:port/lb/1?sync=..." ./minimal_reserve.sh

# 2. Run sender and receiver
./minimal_sender.sh --rate 5 --num 1000
./minimal_receiver.sh --duration 60

# 3. Cleanup
./minimal_free.sh
```
