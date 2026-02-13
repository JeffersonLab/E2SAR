# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

E2SAR (EJFAT Event Segmentation And Reassembly) is a high-performance C++ library with Python bindings for streaming large scientific event data through FPGA-based hardware-assisted load balancers. It handles segmentation/reassembly of events at 10s to 100s of Gbps for scientific instruments.

## Build System and Dependencies

**Build Tool**: Meson with Ninja backend
**Language**: C++17 standard
**Python**: 3.9+ required for bindings

### Core Build Commands
```bash
# Setup build directory
meson setup build

# reconfigure build environment in place
meson setup --reconfigure build

# Compile
meson compile -C build

# Install
meson install -C build

# Create distribution
meson dist -C build --no-tests
```

### Key Dependencies
- **gRPC++ (≥1.51.1)**: Control plane communication - often needs to be built from source
- **Boost (1.83.0-1.86.0)**: Core C++ utilities - may need custom build
- **Protocol Buffers (≥3.21.12)**: Serialization
- **pybind11**: Python bindings (install via pip)

## Testing

### C++ Tests (Boost Test Framework)
```bash
# Unit tests (no UDPLBd required)
meson test -C build --suite unit --timeout 0
```

### Python Tests (pytest)
```bash
# Set environment
export PYTHONPATH=/path/to/build/src/pybind
export E2SARCONFIGDIR=/path/to/E2SAR

# Run tests
cd test/py_test
pytest -m unit      # Unit tests
pytest -m b2b       # Back-to-back tests  
pytest -m cp        # Control plane tests
pytest -m lb-b2b    # Tests with real EJFAT FPGA load balancer
```

**Test Markers**:
- `unit`: Standalone tests without external dependencies
- `b2b`: Back-to-back segmentation/reassembly tests
- `cp`: Control plane functionality tests
- `lb-b2b`: Tests with real EJFAT FPGA load balancer

### B2B Test Notes
The b2b tests (`test_b2b_DP.py`) send and receive events over localhost UDP using Segmenter and Reassembler with the control plane disabled. Common failure modes:

- **Event timeout**: The default `ReassemblerFlags.eventTimeout_ms` is 500ms. Large payloads (e.g. 200MB numpy arrays) fragmented at small MTUs produce many fragments, and reassembly may not complete before the GC thread discards the event. Fix by increasing the MTU or the event timeout.
- **Port reuse ("Address already in use")**: The Reassembler does not set `SO_REUSEADDR` on its receive sockets. After a test closes port 10000, the OS keeps it in TIME_WAIT (~60s on macOS). Subsequent tests binding the same port will fail. Larger MTUs reduce fragment count and send/receive time, which helps mitigate this.
- **Fragment math**: With MTU=1500 (~1400 bytes usable payload), a 200MB event requires ~142,857 fragments. At 1 Gbps rate limiting, sending takes ~1.6s — well beyond the 500ms default timeout.

## Code Architecture

### Core Classes
- **EjfatURI** (`include/e2sarURI.hpp`): Parses EJFAT URI format (`ejfats://udplbd@host:port/lb/id?sync=...&data=...`)
- **LBManager** (`include/e2sarCP.hpp`): gRPC control plane client for load balancer communication
- **Segmenter** (`include/e2sarDPSegmenter.hpp`): Event fragmentation with background sync
- **Reassembler** (`include/e2sarDPReassembler.hpp`): Event reassembly and worker registration
- **Affinity** (`include/e2sarAffinityMgr.hpp`): Thread/process/NUMA affinity management
- **Optimizations** (`include/e2sarOptimizations.hpp`): Performance optimization tracking

### Error Handling Pattern
- Constructors throw `E2SARException` on failure
- Methods return `result<T>` type - check with `has_error()` before accessing value
- Always validate URI format and connection parameters

### Key Design Patterns
- **Multi-threading**: Thread pools with configurable affinity binding
- **Memory Management**: Static pools for high-performance packet handling
- **Rate Limiting**: Configurable Gbps limits with precise pacing
- **Fragmentation**: MTU-aware segmentation for large event payloads

## Development Tools

### Performance Testing
```bash
# E2SAR performance tool (iperf-like)
./build/bin/e2sar_perf -h

# Load balancer administration
./build/bin/lbadm --version -u "ejfats://udplbd@host:18347/" -v

# Packet sniffing/generation (requires root)
sudo python3 scripts/scapy/snifgen.py -l --sync --port 19522
```

### SSL/TLS Configuration
UDPLBd uses gRPC over TLS. For testing:
- Use `-v` flag to disable certificate validation
- Use `-o /path/to/cert.pem` to specify certificate
- Use hostname (not IP) when validating certificates

## Multi-threaded Segmenter (Current Development)

**Current Branch**: `v0.2.2-wip` - developing file sender/receiver applications

### Key Features in Development
- Static memory pools for zero-allocation packet creation
- sendmmsg() support for batch packet transmission
- io_uring integration for zero-copy operations (Linux)
- NUMA-aware thread placement

### Configuration Options
- `--threads N`: Number of receive threads
- `--affinity CPU_LIST`: Bind threads to specific CPUs
- `--rate GBPS`: Rate limiting in Gbps, negative rate means no limit
- `--mtu SIZE`: Maximum transmission unit

## Environment Variables

**Required for testing**:
- `EJFAT_URI`: Complete EJFAT URI with load balancer and data endpoints
- `PYTHONPATH`: Path to Python bindings (build/src/pybind)
- `E2SARCONFIGDIR`: Absolute path to E2SAR repository root

**Build environment** (set in setup_compile_env.sh):
- `GRPC_ROOT`: gRPC installation path
- `BOOST_ROOT`: Boost installation path
- `PKG_CONFIG_PATH`: Path to .pc files
- `LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH`: Runtime library paths

## Git Workflow

**Prerequisites**: 
- Git LFS must be installed before cloning
- Clone with `--recurse-submodules` to include UDPLBd, wiki, and docs

**Submodules**:
- `udplbd/`: Control plane implementation (separate repo)
- `wiki/`: User documentation (uses `master` branch)
- `docs/`: Doxygen API docs (uses `main` branch)

**Version Management**:
- Version defined in `VERSION.txt`
- Tags follow `vX.Y.Z` format
- Current version: 0.2.2a1 (alpha release)

## Platform-Specific Notes

### macOS (Homebrew)
Dependencies typically in `/opt/homebrew`, build should work with both Clang and GCC.

### Linux (Ubuntu/RHEL)
- **Ubuntu 20/22/24**: Supported with binary dependency packages available
- **RHEL8**: Requires `sed -i 's/-std=c++11//g' build/build.ninja` after meson setup due to older GCC

### Optional Features
- **liburing**: Linux-specific io_uring support for zero-copy operations
- **libnuma**: NUMA affinity management
- **netlink**: Linux kernel networking capabilities

## Documentation

- **User/Adopter docs**: `wiki/` submodule (GitHub wiki)
- **API docs**: `docs/` submodule (Doxygen-generated)
- **Developer docs**: This file and README.md
- **Examples**: Jupyter notebooks in `scripts/notebooks/`
