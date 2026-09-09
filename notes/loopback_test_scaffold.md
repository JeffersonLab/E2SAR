# Loopback Dataplane Test Scaffold — Implementation Plan

## Goal

Extend the existing single-shot `scripts/perf-loopback.sh` into a **regime matrix**
that exercises every dataplane send/receive code path in `bin/e2sar_perf.cpp` over the
loopback interface (no real load balancer, `useCP=false`), and can be driven inside a
**Docker or Podman** container on a Linux host (where `sendmmsg`/`recvmmsg` and
`liburing` are actually compiled in).

Rates, MTU, and event/message size are all selectable with sane defaults that
deliberately land on the tricky code paths.

---

## 1. Background: what the code actually does (evidence)

These are the branch points the scaffold must cover. Line references are current as of
this plan.

### 1.1 Optimizations are a process-global singleton, selected per process

- `Optimizations::select()` (`src/e2sarUtil.cpp:81`) sets a global bitmask.
- `e2sar_perf` exposes it via `--optimize/-o` (multitoken) — `bin/e2sar_perf.cpp:419`.
- Sender and receiver are **separate `e2sar_perf` processes**, so each gets its own
  `-o` list. Sender uses `sendmmsg` **or** `liburing_send`; receiver uses `recvmmsg`.
- Two hard constraints enforced in `select()` (`src/e2sarUtil.cpp:99`):
  - `sendmmsg` + `liburing_send` → **error** ("incompatible").
  - `recvmmsg` + `liburing_recv` → **error**.
- `liburing_recv` is defined in the enum (`include/e2sarUtil.hpp:729`) but **there is no
  receiver-side liburing implementation** — the recv path only branches on `recvmmsg`
  (`src/e2sarDPReassembler.cpp:422`). So regime "c" pairs `liburing_send` (tx) with
  `recvmmsg` (rx).

### 1.2 Availability is compile-time gated → must be checked at runtime

- `SENDMMSG_AVAILABLE` is set only if `sendmmsg()` compiles (`meson.build:71`); it gates
  **both** `sendmmsg` (tx) and `recvmmsg` (rx) — `src/e2sarDPSegmenter.cpp:707`,
  `src/e2sarDPReassembler.cpp:421`, and the `available` list in `src/e2sarUtil.cpp:29`.
- `LIBURING_AVAILABLE` requires `liburing.h` **and** `linux/io_uring.h` (`meson.build:22`).
- On **macOS**: neither is available → only `none` compiled in. That is exactly why the
  matrix must be runnable in a Linux container.
- **The default `Dockerfile.cli` image does NOT have `liburing`.** `liburing.h`/`-luring`
  come only from `liburing-dev`, which the build stage does not install, and the bundled
  `e2sar-deps` .deb contains **only Boost and gRPC — nothing else** (confirmed). So
  `LIBURING_AVAILABLE` is off in that image and regimes c*/s3 cannot run there until the
  image is rebuilt with `liburing-dev`. `sendmmsg`/`recvmmsg`, by contrast, are a libc
  compile-check (`meson.build:71`) with no package dependency and are always present on
  the Ubuntu 24.04 base → regimes a*/b*/s1/s2/s4/s5 run out of the box.
- `e2sar_perf` prints the compiled-in set on startup:
  `E2SAR Available Optimizations: ...` (`bin/e2sar_perf.cpp:478`). **The scaffold must
  parse this line and skip regimes whose optimizations are not present**, marking them
  `SKIP (not compiled in)` rather than failing.

### 1.3 Header/fragment math (for choosing sizes)

- `getTotalHeaderLength()` (`include/e2sarHeaders.hpp:419`):
  IPv4 = 20(IP) + 8(UDP) + 16(LBHdrV2) + 20(REHdr) = **64 bytes**; IPv6 = 84 bytes.
- `maxPldLen = MTU - totalHeader` (`e2sarDPSegmenter.hpp:238`):
  - MTU 1500 → **1436** usable payload/frag.
  - MTU 9000 → **8936**.
- Fragments per event `numBuffers = ceil(eventLen / maxPldLen)` (`e2sarDPSegmenter.cpp:703`).
- Sanity limits: MTU capped at **9000** (`e2sarDPSegmenter.hpp:304`); receiver buffer
  `RECV_BUFFER_SIZE = 9000` fixed (`e2sarDPReassembler.hpp:40`). Do not exceed MTU 9000.

### 1.4 The `sendmmsg` IOV_MAX batching loop (special condition #1)

`src/e2sarDPSegmenter.cpp:867-902`: `sendmmsg` sends the whole event's frames in one
`mmsghdr` vector, but loops in chunks of **`IOV_MAX` (1024)**:

```
numBuffersThisBatch = min(IOV_MAX, numBuffers - sentOut);
sendmmsg(sendSocket, &mmsgvec[sentOut], numBuffersThisBatch, 0);
```

To force `numBuffers > IOV_MAX` you need `eventLen > 1024 * maxPldLen`:
- MTU 1500 → `1024 * 1436 = 1,470,464` B. A **2 MB** event → 1393 frags → **2 batches**.
- For **≥3 batches** (crossing the boundary twice): `> 2048 * 1436 = 2.94 MB`. A **4 MB**
  event → 2786 frags → **3 batches**.

### 1.5 The `liburing` ring depth (special condition #2)

`e2sarDPSegmenter.hpp:106`: `uringSize = 1000` SQEs per ring, with a comment that it
"want[s] to put at least `2*eventSize/bufferSize` entries" — but the size is a **fixed
constant, not derived from event size**. The submit path busy-waits for a free SQE
(`e2sarDPSegmenter.cpp:821` `while(not(sqe = io_uring_get_sqe(...)))`) and relies on a
background CQE reaper (`_reap`, `e2sarDPSegmenter.cpp:198`) to drain completions. An event
that fragments into **> 1000 frames** (e.g. 2 MB at MTU 1500 = 1393 frames) stresses this
back-pressure/reaping loop. Worth an explicit regime.

### 1.6 The `recvmmsg` iovec vector + IOV_MAX clamp (special condition #3)

- `rcvIovecSize` is clamped to `IOV_MAX` at construction
  (`src/e2sarDPReassembler.cpp:63` etc.), settable via `--rcviovecsize`
  (`bin/e2sar_perf.cpp:425`, default 100).
- Recv path allocates one aligned block of
  `mmsghdr*N + iovec*N + N*RECV_BUFFER_SIZE` per socket-ready event
  (`e2sarDPReassembler.cpp:426`), calls `recvmmsg(..., MSG_DONTWAIT)`.
- Edge values to test: `--rcviovecsize 1` (degenerate batch), a default (100), and a
  value **> 1024** to confirm the clamp doesn't crash.

### 1.7 Multi-port distribution (correctness detail the current script gets wrong)

- **Receiver**: number of ports opened is `numRecvPorts = 2^portRange`, and
  `portRange = get_PortRange(numThreads)` (`e2sarCP.hpp:772`) which rounds the thread
  count **up to the next power of two**:
  | threads | portRange | ports |
  |---|---|---|
  | 1 | 0 | 1 |
  | 2 | 1 | 2 |
  | 3–4 | 2 | 4 |
  | 5–8 | 3 | 8 |
  Ports opened are `dataPort … dataPort + numRecvPorts - 1`.
- **Sender**: spreads its `numSendSockets` sockets' **destination** ports across the URI
  `data=` range with `stride = rangeSize / numSendSockets`, dest port
  `= minP + (fdCount*stride) % rangeSize` (`e2sarDPSegmenter.cpp:486,651`).
- **Implication for the matrix**:
  - The URI `data=host:START-END` range must span **exactly `numRecvPorts` ports**
    (`END = START + 2^ceil(log2(threads)) - 1`), *not* `START + threads - 1` as
    `perf-loopback.sh:100` currently computes (wrong for non-power-of-2 thread counts).
  - To actually hit every receive port, set `numSendSockets >= numRecvPorts` so
    `stride == 1`. The single-thread regimes use 1 port, so any socket count works.

### 1.8 Other conditions worth a (negative/guard) test

- `smooth` shaping is **incompatible** with `sendmmsg`/`liburing` and only valid at low
  rate (`e2sarDPSegmenter.hpp:72`, guard at `bin/e2sar_perf.cpp:540`). Optional negative
  test: `--smooth` + `-o sendmmsg` should be rejected/ignored, not silently corrupt.
- MTU just above header length (e.g. 100) → tiny payload → huge fragment counts; good for
  a stress/soak variant but slow — keep opt-in.
- Optimization conflict (`-o sendmmsg -o liburing_send`) → `select()` returns error;
  a negative test asserts non-zero exit and the "incompatible" message.

---

## 2. Test regimes to implement

Primary matrix (each run = one sender process + one receiver process, back-to-back,
CP off):

| # | Name | Sender `-o` | Receiver `-o` | Threads / Sockets | Purpose |
|---|------|-------------|---------------|-------------------|---------|
| a1 | plain-st | *(none)* | *(none)* | 1 / 1 | baseline `sendmsg`+`recvfrom`, single thread |
| b1 | mmsg-st | `sendmmsg` | `recvmmsg` | 1 / 1 | batched tx/rx, single thread |
| c1 | uring-st | `liburing_send` | `recvmmsg` | 1 / 1 | io_uring tx + recvmmsg rx, single thread |
| a2 | plain-mt | *(none)* | *(none)* | N / ≥ports | baseline, multi thread + port range |
| b2 | mmsg-mt | `sendmmsg` | `recvmmsg` | N / ≥ports | batched, multi thread + port range |
| c2 | uring-mt | `liburing_send` | `recvmmsg` | N / ≥ports | io_uring, multi thread + port range |

Special-condition regimes (single thread unless noted):

| # | Name | Config | Exercises |
|---|------|--------|-----------|
| s1 | mmsg-iovmax2 | `sendmmsg`, MTU 1500, event **2 MB** | `sendmmsg` IOV_MAX loop, 2 batches (§1.4) |
| s2 | mmsg-iovmax3 | `sendmmsg`, MTU 1500, event **4 MB** | IOV_MAX loop, 3 batches |
| s3 | uring-deep | `liburing_send`, MTU 1500, event **2 MB** | ring back-pressure > uringSize (§1.5) |
| s4 | recvmmsg-iov1 | `recvmmsg`, `--rcviovecsize 1` | degenerate recv batch (§1.6) |
| s5 | recvmmsg-iovbig | `recvmmsg`, `--rcviovecsize 2048` | clamp to IOV_MAX, no crash (§1.6) |
| n1 | conflict (neg) | sender `-o sendmmsg -o liburing_send` | `select()` rejects, non-zero exit |
| n2 | smooth-guard (neg, opt) | `--smooth -o sendmmsg` | incompatibility guard |

`N` defaults to **4** (→ 4 ports). Regimes b*/c*/s1-3/s5 auto-`SKIP` when the required
optimization is absent from the runtime "Available Optimizations" line.

---

## 3. Script structure

Three files, layered so the engine stays reusable:

```
scripts/
  perf-loopback.sh          # EXISTING single run — refactor lightly (see §3.1)
  loopback-matrix.sh        # NEW orchestrator: iterates regimes, tallies pass/fail
  loopback-in-container.sh  # NEW docker/podman wrapper around the matrix
```

### 3.1 `perf-loopback.sh` (refactor, keep backward compatible)

Add pass-through options; keep all existing defaults/flags working:

- `--send-opt "sendmmsg"` → appends `-o sendmmsg` (repeatable / space list) to sender.
- `--recv-opt "recvmmsg"` → appends `-o recvmmsg` to receiver.
- `--rcviovecsize N` → passes `--rcviovecsize N` to receiver.
- `--smooth` → passes `--smooth` to sender (for negative test n2).
- `--expect-fail "SUBSTRING"` → negative regimes (n1/n2): success = sender exits non-zero
  **and** its stderr contains the given substring (e.g. `are incompatible` for n1). Matching
  the message, not just the non-zero exit, is required — a not-compiled-in optimization also
  exits 255 but means SKIP, not pass (see §4).
- **Fix the port-range math** (§1.7): compute
  `NUM_PORTS = 2^ceil(log2(RECV_THREADS))`, `END_PORT = BASE_PORT + NUM_PORTS - 1`, and
  default `SEND_SOCKETS = max(SEND_SOCKETS, NUM_PORTS)`. Emit both in the config summary.
- Emit a machine-readable one-line result at the end, e.g.
  `RESULT regime=<name> status=PASS pkts=<n> errs=<n> tput=<g> goodput=<g>` so the
  orchestrator can parse without re-implementing the log scraping already in
  `perf-loopback.sh:213-227`.
- Keep the existing SIGINT-drain-and-stop receiver logic (`perf-loopback.sh:187-200`) and
  the `EVENT_TIMEOUT`-derived `DRAIN_WAIT` (which matters for large events per CLAUDE.md).

Everything else (URI construction, temp logs, cleanup trap, stat parsing) is reused as-is.

### 3.2 `loopback-matrix.sh` (new orchestrator)

Responsibilities:

1. **Probe availability once (pre-filter)**: run `e2sar_perf --help` and capture the
   `E2SAR Available Optimizations:` line (`bin/e2sar_perf.cpp:478`). Build a set
   `{sendmmsg?, recvmmsg?, liburing_send?}` and skip regimes whose optimization is absent.
   This is a proactive filter; the **authoritative** signal is the per-run `select()` error
   (see §4) — the runner must handle that too, because a user could override `--image` to
   one whose `--help` we didn't probe, or the probe output could drift.
2. **Select regimes**: default = all of §2; allow `--regimes a1,b1,s1` to subset, and
   `--only-special` / `--only-multi` shortcuts.
3. For each regime, translate the table row into a `perf-loopback.sh` invocation with the
   right `--send-opt/--recv-opt/--threads/--sockets/--mtu/--length/--rate/--rcviovecsize`.
   Randomize/space out `--port` per regime (offset base port per regime index) to dodge
   macOS/Linux TIME_WAIT reuse (CLAUDE.md "Port reuse" note); or add a short inter-regime
   `sleep`.
4. **Skip** unavailable regimes with a clear `SKIP` line (don't count as fail).
5. Tally results into a summary table + a machine-readable TSV/CSV
   (`--out results.tsv`), and exit non-zero if any *non-skipped* regime failed.

Global knobs (sane defaults, all overridable):

| Flag | Default | Notes |
|------|---------|-------|
| `--rate` | `1.0` Gbps | negative = unlimited; passed through |
| `--mtu` | `1500` | special regimes override to force fragment counts |
| `--length` | `1000000` (1 MB) | base event size for a*/b*/c* |
| `--num` | `100` | events per run |
| `--threads` | `4` | for the multi-thread regimes (→ 4 ports) |
| `--big-length` | `2097152` (2 MB) | s1/s3 |
| `--huge-length` | `4194304` (4 MB) | s2 |
| `--timeout` | `2000` ms | reassembly timeout; large events may need more |
| `--build-dir` | `$E2SAR_BUILD_DIR` or `build/` | forwarded |
| `--regimes` | all | subset selector |

**Sizing note baked into defaults**: for the IOV_MAX regimes the orchestrator asserts
`length > 1024*(mtu-64)` and, if the user overrides `--mtu`/`--big-length` such that the
condition no longer holds, prints a warning that the special path won't be exercised.

### 3.3 `loopback-in-container.sh` (new container wrapper)

1. **Pick a runtime**: prefer `podman`, fall back to `docker`
   (`command -v podman || command -v docker`); error if neither.
2. **Image (pull a pre-built image; do NOT rebuild per run)**: default to the published
   DockerHub image **`ibaldin/e2sar:${E2SAR_IMAGE_VERSION:-0.4.0a1}`**. The wrapper pulls it
   once (`$RUNTIME pull <image>` unless already present / `--no-pull` given) and reuses it
   for the whole matrix. Overrides:
   - `--version X.Y.Z` → selects `ibaldin/e2sar:X.Y.Z` (default `0.4.0a1`).
   - `--image NAME[:TAG]` → use an arbitrary image/registry instead of the `ibaldin/e2sar`
     default (e.g. a locally built one).
   - `--build` → *optional, for local iteration only*: build from `Dockerfile.cli`
     (`$RUNTIME build -f Dockerfile.cli -t e2sar-perf:local .`) and use that tag. Not the
     default path.
   `e2sar_perf` lives on `PATH` in the published image (installed at
   `$E2SARINSTALL=/e2sar-install`, `Dockerfile.cli:85`), so the in-container `--build-dir`
   should default to `/e2sar-install` and no host build dir needs mounting.
   **liburing precondition (handled separately)**: `ibaldin/e2sar:0.4.0a1` is being
   (re)published with liburing compiled in as a **separate step, outside this scaffold**
   (see §3.4 for the how). The scripts therefore assume the default image contains
   `sendmmsg`, `recvmmsg`, and `liburing_send`, so all regimes run by default. The
   availability probe (step 0) is kept only as a **defensive fallback**: if a user points
   `--image`/`--version` at an image lacking an optimization, the matrix `SKIP`s the
   affected regimes instead of failing.
0. **Availability probe (first action)**: run
   `$RUNTIME run --rm <image> e2sar_perf --help | grep "Available Optimizations"` and pass
   the result into `loopback-matrix.sh` (or let the matrix run it) so unavailable regimes
   are skipped cleanly rather than failing.
3. **Run**: always pass **`--network=host`** for **both** podman and docker. Even though the
   sender and receiver both live on `127.0.0.1` inside the container, the default bridge/NAT
   networking routes even loopback-adjacent traffic through a `veth` pair and the container
   network namespace, which caps throughput and adds latency/jitter — defeating the purpose
   of a *performance* test. `--network=host` puts the container directly on the host network
   stack (host `lo` and real NICs), giving the proper high-performance path from the
   container to the network and making in-container loopback numbers comparable to the bare
   `perf-loopback.sh` run on the host. It is also required if the matrix is ever pointed at a
   real off-host receiver (a future non-loopback axis). Mount the scripts and (optionally)
   the build dir:
   ```
   IMAGE="ibaldin/e2sar:${E2SAR_IMAGE_VERSION:-0.4.0a1}"
   $RUNTIME run --rm --network=host \
     -v "$REPO_ROOT/scripts:/scripts:ro" \
     "$IMAGE" \
     /scripts/loopback-matrix.sh --build-dir /e2sar-install [args...]
   ```
   Only the scripts are mounted; the binaries come from the pulled image at
   `/e2sar-install` (`--build-dir` default). No host build dir is needed.
   Notes: with `--network=host`, container ports bind directly on the host, so the matrix's
   per-regime base-port offsetting (§3.2) also avoids colliding with host services; on
   rootless **podman**, `--network=host` uses the host netns directly and additionally
   sidesteps the slow rootless (pasta/slirp4netns) datapath.
4. **Optional tuning**: the 3 MB socket buffers (`--bufsize`) need
   `net.core.rmem_max`/`net.core.wmem_max` high enough or they are silently clamped. With
   `--network=host` these are the **host's** sysctls (the container shares the host net
   namespace, so `--sysctl` cannot override them from `run`); raise them on the host
   beforehand (`sudo sysctl -w net.core.rmem_max=... net.core.wmem_max=...`). The wrapper
   should read the current values and warn if they are below `--bufsize`.
5. Pass every unrecognized flag straight through to `loopback-matrix.sh`.

### 3.4 `Dockerfile.cli` — how the liburing-enabled image is produced (reference; done separately)

> **Out of scope for the scripting work.** Publishing a liburing-enabled
> `ibaldin/e2sar:0.4.0a1` is a **separate step the maintainer handles**. This section is
> retained only as the reference recipe for that step; the scaffold consumes the resulting
> image and does not build it.

This governs how the **published `ibaldin/e2sar:<version>` image** is built; the test
wrapper (§3.3) only pulls it. `liburing_send` must be present **at compile time**
(`meson.build:22` needs `liburing.h` + `-luring`), so it has to be baked into the image at
build/publish time — the wrapper cannot add it, and no local rebuild happens during a test
run. Two small, surgical edits to `Dockerfile.cli`:

1. **Build stage (`build-base`, the `apt-get install` list, `Dockerfile.cli:31-48`)** — add
   the dev package so the header/lib exist when meson runs:
   ```diff
            protobuf-compiler \
            libre2-dev \
   +        liburing-dev \
            wget \
            ca-certificates && \
   ```
   No meson/ninja change is needed: `meson.build:22` auto-detects `liburing.h` and turns on
   `-DLIBURING_AVAILABLE` + `-luring`. After the build, confirm in the `compile` stage log
   that meson reports the liburing check passing.

2. **Runtime stage (`deploy`, the `apt-get install` list, `Dockerfile.cli:89-107`)** — add
   the shared library so the copied `e2sar_perf`/`e2sar` binaries can load `-luring` at
   runtime (the build-stage `liburing-dev` does not carry into `deploy`):
   ```diff
            libprotobuf32t64 \
   +        liburing2 \
            netcat-traditional \
   ```
   On Ubuntu 24.04 the runtime package is `liburing2`; the dev package is `liburing-dev`.

**Verification after the image is rebuilt & pushed** (identical to the wrapper's
availability probe, §3.3 step 0):
```
docker run --rm ibaldin/e2sar:0.4.0a1 e2sar_perf --help | grep "Available Optimizations"
# expect: ... sendmmsg recvmmsg liburing_send liburing_recv
```
If `liburing_send` appears, regimes c1/c2/s3 are live against that image. If it does not,
the image predates this change (or the build stage did not see `liburing.h`) and the matrix
will skip c*/s3 until a corrected image is published.

**Image-size note**: `liburing2` is tiny (~50 KB), so the runtime image barely grows; the
dev package stays in the discarded build stage. No multi-stage restructuring required.

**Ownership note**: publishing the `ibaldin/e2sar` image is a maintainer step outside this
scaffold (build `Dockerfile.cli`, tag `ibaldin/e2sar:<version>`, `docker push`). The plan's
deliverable is the `Dockerfile.cli` edit; rebuilding/pushing the DockerHub image so the
`0.4.0a1` tag includes liburing is a follow-up the image owner performs.

---

## 4. Pass/fail criteria per run

**First, classify the run by the optimization-selection outcome.** `e2sar_perf` validates
`-o` before doing anything: `Optimizations::select()` (`bin/e2sar_perf.cpp:494-498`) prints
the error to **stderr** and `return -1` (exit 255) in two distinct cases, which share the
same exit code and so **must be distinguished by message substring** (from
`src/e2sarUtil.cpp:92,105`):

| stderr contains | meaning | runner verdict |
|---|---|---|
| `is not available on this platform` | optimization not compiled into this image | **SKIP** (not a failure) — even if the §3.2 pre-filter missed it |
| `are incompatible` | conflicting `-o` set (e.g. `sendmmsg`+`liburing_send`) | **PASS** for the negative regime **n1** (this is exactly what it asserts); **FAIL** for any other regime |
| *(neither; select succeeded)* | optimizations accepted | proceed to the throughput/loss checks below |

The runner keys off the message, not the bare 255, so it never (a) reports a
not-compiled-in optimization as a failure, nor (b) reports the intended n1 conflict as a
failure. `perf-loopback.sh` should surface the sender's stderr (already captured in
`SEND_LOG`) to the matrix, and the matrix greps these substrings before applying the
numeric criteria.

For runs where `select()` succeeded, reuse and extend the existing logic in
`perf-loopback.sh:229-254`:

- Sender exit code 0 (or non-zero for `--expect-fail` regimes).
- Receiver exit code 0 (SIGINT → 130 normalized to 0, already handled).
- Sender `errors == 0` (parsed, already handled).
- **New**: receiver `Events Received` should be ~`--num` (allow small loss tolerance,
  configurable `--loss-tol`, default 0 for loopback). Parse from the receiver stats block
  (`bin/e2sar_perf.cpp:333`) — but note `--quiet` suppresses the periodic stats thread;
  the final `Port Stats`/`Total` fragment counts (`bin/e2sar_perf.cpp:84-91`) are always
  printed and can be checked instead (total fragments ≈ expected frames).
- **New**: `Events Mangled == 0` (payload sentinel check, `bin/e2sar_perf.cpp:295`).

---

## 5. Implementation order

**Precondition (external, not part of this scripting work):** a liburing-enabled
`ibaldin/e2sar:0.4.0a1` image is (re)built and pushed to DockerHub per the §3.4 recipe. The
scaffold below is written against that image.

1. Refactor `perf-loopback.sh`: add `--send-opt/--recv-opt/--rcviovecsize/--smooth/
   --expect-fail`, fix port-range math, emit `RESULT ...` line. Verify old CLI still works.
2. Write `loopback-matrix.sh` with the regime table, availability probe, subsetting,
   summary + TSV, correct exit code. Test locally on macOS (only `a1`/`a2` run; the rest
   `SKIP` — validates the skip logic).
3. Write `loopback-in-container.sh`: podman/docker detect, **pull** `ibaldin/e2sar:0.4.0a1`
   (overridable via `--version`/`--image`), run with `--network=host`, mount only scripts.
   Test the full matrix on a Linux host; against the liburing-enabled image **all** regimes
   run (a*/b*/c*/s1–s5, n1–n2). The c*/s3 `SKIP` path only triggers as a fallback if pointed
   at an image lacking liburing.

## 6. Open questions / assumptions

- **Image source** (decided): the test wrapper **pulls a pre-built image**, default
  `ibaldin/e2sar:0.4.0a1` (override via `--version`/`--image`); it does **not** rebuild per
  run. Local `--build` from `Dockerfile.cli` remains only for iteration.
- **liburing in the image** (decided; handled separately): a liburing-enabled
  `ibaldin/e2sar:0.4.0a1` will be published as a **separate maintainer step** (via the §3.4
  `Dockerfile.cli` recipe), so the scaffold assumes `liburing_send` is present by default.
  The availability probe / c*/s3 `SKIP` remains only as a fallback for images that lack it.
- **Loss tolerance on loopback**: assume 0 by default; large-event regimes at high rate
  may need a small tolerance or a higher `--timeout` (see CLAUDE.md B2B notes about the
  500 ms default GC discarding partially-assembled large events).
- **IPv6 dataplane** (`--dpv6`) is out of scope for v1 but the engine already supports it;
  can be added as a `--v6` matrix axis later.
