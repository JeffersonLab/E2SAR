# Notebook: udplbd2 native mock mode (2026-09-08)

## What was done

Reworked the `scripts/notebooks/EJFAT/E2SAR-development-tester.ipynb` control-plane
sections to build and run **udplbd2 natively (no docker) in mock mode** on the
`cpnode`, and made the E2SAR live-test / lbadm / lbmon cells consistent with that
setup. Also added the mock config the notebook uploads.

## Why

udplbd2 **mock mode is loopback-only by design** (see
[E2SAR#185](https://github.com/JeffersonLab/E2SAR/issues/185)). In mock mode
udplbd rewrites the addresses it returns to the client:

- **data address** — from `mock.address_map`, e.g. `192.0.2.1 -> 127.0.0.1:19522`
- **sync address** — first IPv4 in `server.listen`, i.e. `127.0.0.1`

So the E2SAR Segmenter/Reassembler send data+sync to `127.0.0.1`, which means the
mock and the E2SAR test process **must run on the same host** sharing loopback.
The old docker approach (`docker run` + port publishing) cannot work: publishing
the gRPC port doesn't fix the loopback data/sync addresses, and bridge networking
breaks the loopback path. Running udplbd2 natively on the cpnode is the simplest
correct option — it only needs the Rust toolchain (protoc + C toolchain are
already installed for E2SAR). The `cpnode.sh` post-boot script now installs
rustup **and `sqlx-cli`** (see separate change).

### sqlx: the build must be online, not offline

udplbd2 uses **sqlx compile-time-checked queries** (`sqlx::query!`), which
validate SQL against a schema at build time. The checked-in `.sqlx/` offline
cache is **stale on this branch**: an `SQLX_OFFLINE=true` build fails with
`there is no cached data for this query` (`src/api/handlers/lb.rs`,
`src/snp4/metrics_collector.rs`) plus column-nullability type errors in
`src/db/*.rs` (cached metadata disagrees with the current source). So the build
must run **online** against a live SQLite schema, mirroring the upstream
`udplbd2/Dockerfile`:

1. `cargo install --locked sqlx-cli --no-default-features --features rustls,sqlite`
   (sqlite-only, rustls — no OpenSSL dev headers needed).
2. `cargo sqlx database setup` — creates the build DB and applies `migrations/`
   (`0001_init.sql` … `0004_upstream_chain.sql`).
3. `cargo build --release` with `DATABASE_URL` pointing at that DB — the
   `query!` macros validate against the live schema and compile cleanly.

The build DB (`$HOME/udplbd-build.db`) is kept separate from the runtime DB
(`/tmp/udplbd.db` from the mock config, which the server self-migrates on start).

## Changes

### New file: `scripts/notebooks/EJFAT/config/udplbd2_mock.yml`

The mock config the notebook uploads to the cpnode. Loopback-only, TLS disabled,
gRPC on `127.0.0.1:19523`, auth token `udplbd2changeme`, `allow_loopback: true`
(so the Reassembler may register a worker from 127.0.0.1), plus the full
`mock.address_map`. Lives in the notebook's `config/` dir (matching the existing
upload pattern) because `udplbd2` is a submodule cloned fresh from esnet's remote
and will not carry our local copy. The only functional delta vs. udplbd2's
`etc/example-config.yml` is `allow_loopback: true`.

### `scripts/notebooks/EJFAT/E2SAR-development-tester.ipynb`

- **Preamble:** `udplbd_config = 'udplbd2_mock.yml'`.
- **Intro + "Build and start udplbd2" markdown:** reworded from containerized to
  native mock; documents loopback-only and that live tests run on cpnode.
- **Prereq-check cell** (was docker/compose/buildx checks): now checks `cargo`
  (via `. "$HOME/.cargo/env"`), `protoc`, `cc`.
- **Clone cell:** also uploads `config/udplbd2_mock.yml` -> `~/udplbd2_mock.yml`.
- **Build cell** (was `docker build`): online sqlx recipe — installs `sqlx-cli`
  (guarded by `command -v sqlx`), then `cargo sqlx database setup` +
  `cargo build --release` against `DATABASE_URL=sqlite://$HOME/udplbd-build.db`
  (the checked-in `.sqlx/` offline cache is stale on this branch, so an offline
  build fails; see "sqlx: the build must be online" above).
- **Start cell** (was `docker run`): `UDPLBD_CONFIG=$HOME/udplbd2_mock.yml nohup
  ./target/release/udplbd mock > ~/udplbd2.log 2>&1 &`, guarded by
  `pkill`/`pgrep -f "udplbd mock"`. The SQLite DB self-creates and migrates at
  `/tmp/udplbd.db` on first start.
- **Logs cell** (was `docker logs`): `pgrep -a -f "udplbd mock"` + `tail ~/udplbd2.log`.
- **Stop cell** (was `docker stop/rm`): `pkill -f "udplbd mock"`.
- **Deleted** the old `docker image rm` cell (it also had an unterminated-string
  syntax error; irrelevant natively).
- **Live-tests cell:** `EJFAT_URI='ejfat://udplbd2changeme@127.0.0.1:19523/'`,
  now run on **cpnode** (was `ejfats://udplbd@...:18347` on sender). Plaintext
  `ejfat://` (TLS disabled), port 19523, token `udplbd2changeme`.
- **lbadm / lbmon cells:** same loopback URI, run on cpnode (were
  `ejfats://...:18347` on sender — would fail against the loopback mock).

Left untouched by design: the perf / header-format sections (back-to-back, don't
use the control plane) and `udplbd2/etc/mock-docker-config.yml` (the scoped
rename follow-up).

## Native run recap (on cpnode after re-provision)

```bash
# sqlx-cli is pre-installed by cpnode.sh; this is the fallback if missing:
command -v sqlx >/dev/null 2>&1 || cargo install --locked sqlx-cli --no-default-features --features rustls,sqlite
cd udplbd2
export DATABASE_URL="sqlite://$HOME/udplbd-build.db"
cargo sqlx database setup && cargo build --release
UDPLBD_CONFIG=$HOME/udplbd2_mock.yml ./target/release/udplbd mock   # 127.0.0.1:19523
# then, on the SAME node:
EJFAT_URI="ejfat://udplbd2changeme@127.0.0.1:19523/" meson test -C build --suite live --timeout 0 -j 1
```

## Known wrinkle

In `mock.address_map`, instance #2's data address (`127.0.0.1:19523`) collides
with the gRPC listen port. The common live tests use the first instance
(`:19522`), so it is fine in practice, but exercising multiple LB instances would
need the data-port base shifted off 19523.
