# Loopback c2 throughput characterization (2026-09-09)

## Context

Investigating why regime `c2` (`liburing_send` + `recvmmsg`, multi-thread) reported
a noticeably lower goodput than the other regimes in a default
`scripts/loopback-in-container.sh --bare` matrix run:

```
a1 goodput 1.006   b1 1.006   c1 1.009
a2 1.005   b2 0.994   c2 0.617   <-- outlier
```

All 12 regimes PASSed; `c2` was just slow. Purpose of the run was to characterize
`c2`, not to declare a defect.

## Key finding: c2's send path is fine — the pacer is the ceiling

The apparent slowness is **not** the liburing multi-thread send path. Two runs settle it.

### 1. Paced run (default 1 Gbps, MTU 1500, num=2000)

```
Inter-event sleep (usec) is:  8000
Elapsed usecs: 19529203  for 2000 events   -> 9764.6 usec/event
goodput 0.819286 Gbps
```

The sender's rate limiter sleeps a **fixed 8000 usec** after each 8 Mbit event (8 Mbit
/ 1 Gbps = 8000 usec), but that sleep **does not subtract the time already spent
sending**. Actual per-event send cost is ~1765 usec (9764.6 - 8000), so the real period
is 9765 usec and the achieved rate asymptotes to `8000 / (8000 + T)` where T is the
per-event send cost. Goodput 8 Mbit / 9764.6 usec = 0.819 Gbps — matches exactly.

For fast single-thread paths (a1/b1/c1) T is near zero -> ~1.0 Gbps. For c2 the
liburing + 4-thread submit/complete cost makes T ~1765 usec -> 0.82 Gbps.

The original 0.617 (at num=100) was worse only because fixed startup cost (ring init,
worker registration, thread spin-up) was amortized over just 100 events. At num=2000
startup washes out; **0.82 is the honest steady-state c2 number**. => use larger `--num`
for a truer measurement.

### 2. Unlimited-rate run (MTU 9000, num=2000, --rate -1)

```
Sending average bit rate is: unlimited
Elapsed usecs: 1390419   goodput 11.5073 Gbps
sent 224000, received 162672   (~27% dropped, sender errs=0)
```

**The c2 send path does 11.5 Gbps.** So the paced 0.82 was entirely the fixed-sleep
pacer, not any weakness in the send path. Nothing to fix on the send side — MT-liburing
is the fastest sender measured.

The ~27% drop is receiver-side: silent kernel drops when the UDP receive socket buffer
(3 MB, capped by `net.core.rmem_max=3145728`) overflows because the 4 `recvmmsg` threads
can't drain 11.5 Gbps. UDP gives no backpressure, so the sender reports `0 errors`. This
is the expected memory-to-memory loopback overrun, not a defect.

## The real tuning axis: receivers + rcvIovecSize

A `--rate 5` run (MTU 9000, num=2000) dropped only 1479/224000 (~0.66%) — a marginal
socket overrun the strict 0-loss tolerance flags as FAIL. The knobs that address it:

- **`--threads N`** — receiver thread/port count (matrix + wrapper flag).
- **`--rcviovecsize N`** — recvmmsg iovec batch (`rcvIovecSize`). Exposed on
  `scripts/perf-loopback.sh` (`--rcviovecsize`), but the **matrix only sets it for the
  s4/s5 special regimes** — it is NOT a pass-through for a*/b*/c*. To tune it for c2,
  drop to the single-run engine:

```bash
scripts/perf-loopback.sh \
  --send-opt liburing_send --recv-opt recvmmsg \
  --threads 8 --rcviovecsize 256 \
  --mtu 9000 --rate 5 --num 2000
```

`s5` already runs clean at `--rcviovecsize 2048`, so there is headroom to raise the batch.

## Framing (important)

The regime matrix is a **code-path correctness gate, not a benchmark.** Its intended
verdict is the default paced 1 Gbps sweep (all 12 green). Any run above 1 Gbps is
characterization, where loopback socket overrun is expected and the strict 0-loss
tolerance is the wrong lens. To keep the matrix green while exploring high rates, add
`--allow-loss` (downgrades a fragment shortfall to a warning). To actually tune the
receive side, use `scripts/perf-loopback.sh` directly.

## Tuned run: zero loss confirmed (but not free)

`--threads 8 --rcviovecsize 256`, MTU 9000, rate 5, num 2000:

```
sent 224000, received 224000   (28000/port x 8, 0 loss)  -> PASS
Elapsed usecs: 8656753   goodput 1.84827 Gbps
```

Tuning the receive side (8 drain threads + 256-deep recvmmsg batch) eliminates the loss
entirely — the receiver now keeps up. Note the tradeoff: achieved goodput **fell** from
~3.35 Gbps (4 threads, rate 5, lossy) to ~1.85 Gbps here, because the harness raised the
sender to 8 sockets to cover 8 ports (`Raising sender sockets from 4 to 8`), and per-event
send cost rose (~2728 usec/event vs ~780 at 4 threads). So the zero-loss config trades some
throughput for correctness — expected, and fine, since the point was a clean functional pass,
not peak rate.

**Decision: do NOT change the matrix defaults.** This is an exploratory tuned config, not the
correctness gate. The default paced 1 Gbps sweep stays the intended PASS condition; these
tuned numbers are recorded for reference only.

## Takeaways

- c2 send path ceiling: **~11.5 Gbps** (loopback, MTU 9000).
- Paced runs undershoot the requested rate because the inter-event sleep is a fixed
  `event_bits / rate` and does not discount elapsed send time; the slower the path, the
  bigger the undershoot.
- High-rate loss is **receiver-side** socket overrun, tunable via `--threads` and
  `--rcviovecsize`, and bounded by `net.core.rmem_max` (3 MB here).
- Use larger `--num` for steadier numbers (amortizes startup); the matrix is for path
  coverage, not peak throughput.
