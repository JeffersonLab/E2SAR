#!/usr/bin/env bash
# Run the e2sar_perf loopback regime matrix, either inside a Docker/Podman
# container on a Linux host (where sendmmsg/recvmmsg and liburing are compiled
# in) or "bare" against a local meson build on this host.
#
# Model (§3.3 of notes/loopback_test_scaffold.md):
#   - Runtime is one of: podman, docker (containerized) or bare (native).
#     Default: prefer podman, fall back to docker; pass --bare (or
#     --runtime bare) to skip containers and use a local build instead.
#   Containerized (podman/docker):
#     - PULL a pre-built image (default ibaldin/e2sar:0.4.0rc1); do NOT rebuild
#       per run. The published image is expected to include
#       sendmmsg/recvmmsg/liburing_send.
#     - Always run with --network=host so loopback traffic uses the real host
#       network stack (proper high-performance path).
#     - Mount only the scripts dir; binaries come from the image at /e2sar-install.
#   Bare (native):
#     - No image, pull or build. Drives loopback-matrix.sh directly against
#       $E2SAR_BUILD_DIR / --build-dir (default: <repo>/build). Useful for
#       validating a local build, including on macOS where only the plain
#       sendmsg/recvfrom regimes (a1/a2) are available.
#   - Any unrecognized flag is passed straight through to loopback-matrix.sh.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Color helpers ---
if [ -t 1 ]; then
    C_GREEN='\033[0;32m' C_RED='\033[0;31m' C_YELLOW='\033[0;33m'
    C_BOLD='\033[1m' C_RESET='\033[0m'
else
    C_GREEN='' C_RED='' C_YELLOW='' C_BOLD='' C_RESET=''
fi
log_info()  { echo -e "${C_GREEN}[INFO]${C_RESET} $*"; }
log_warn()  { echo -e "${C_YELLOW}[WARN]${C_RESET} $*" >&2; }
log_error() { echo -e "${C_RED}[ERROR]${C_RESET} $*" >&2; }

# --- Defaults ---
IMAGE_VERSION="${E2SAR_IMAGE_VERSION:-0.4.0rc1}"
IMAGE=""                 # if set via --image, overrides the ibaldin/e2sar default
DO_BUILD=0               # --build: build locally from Dockerfile.cli (iteration only)
NO_PULL=0                # --no-pull: use whatever image is already present
IN_BUILD_DIR="/e2sar-install"   # binaries live here in the published image
BUFSIZE=3145728          # kept in sync with the matrix default (for sysctl check)
RUNTIME=""
# Bare (native) mode: local build directory holding bin/e2sar_perf.
HOST_BUILD_DIR="${E2SAR_BUILD_DIR:-$REPO_ROOT/build}"
PASSTHROUGH=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] [-- MATRIX_ARGS...]

Run the loopback regime matrix in a Docker/Podman container or bare on the host.

RUNTIME SELECTION:
  --runtime R       Runtime: podman | docker | bare (default: auto-detect
                    podman then docker)
  --bare            Shorthand for --runtime bare: run against a local build,
                    no container (only regimes compiled into the local binary
                    run; the rest SKIP)

CONTAINER OPTIONS (ignored in bare mode):
  --version X.Y.Z   Use ibaldin/e2sar:X.Y.Z (default: $IMAGE_VERSION)
  --image NAME[:TAG] Use an arbitrary image instead of the ibaldin/e2sar default
  --build           Build locally from Dockerfile.cli (tag e2sar-perf:local); iteration only
  --no-pull         Do not pull; use the image already present locally

BARE OPTION (ignored in container mode):
  --build-dir DIR   Local build dir holding bin/e2sar_perf
                    (default: \$E2SAR_BUILD_DIR or $HOST_BUILD_DIR)

COMMON:
  --bufsize N       Socket buffer size for the sysctl check (default: $BUFSIZE)
  -h, --help        Show this help

Any other flags are forwarded to loopback-matrix.sh, e.g.:
  $(basename "$0") --regimes a1,b1,c1 --rate 5.0
  $(basename "$0") --only-special --num 50
  $(basename "$0") --bare --regimes a1,a2 --num 20
  $(basename "$0") --version 0.4.0rc1 -- --out /scripts/results.tsv
EOF
    exit 0
}

# --- Parse args: known container flags handled here; everything else forwarded ---
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)  IMAGE_VERSION="$2"; shift 2 ;;
        --image)    IMAGE="$2"; shift 2 ;;
        --build)    DO_BUILD=1; shift ;;
        --no-pull)  NO_PULL=1; shift ;;
        --bufsize)  BUFSIZE="$2"; PASSTHROUGH+=(--bufsize "$2"); shift 2 ;;
        --runtime)  RUNTIME="$2"; shift 2 ;;
        --bare)     RUNTIME="bare"; shift ;;
        --build-dir) HOST_BUILD_DIR="$2"; shift 2 ;;
        -h|--help)  usage ;;
        --)         shift; while [[ $# -gt 0 ]]; do PASSTHROUGH+=("$1"); shift; done ;;
        *)          PASSTHROUGH+=("$1"); shift ;;
    esac
done

# --- Bare (native) mode: no container, drive the matrix against a local build ---
if [[ "$RUNTIME" == "bare" ]]; then
    log_info "Runtime: bare (native, no container)"
    PERF="$HOST_BUILD_DIR/bin/e2sar_perf"
    if [[ ! -x "$PERF" ]]; then
        log_error "$PERF not found or not executable."
        log_error "Build the project first, or pass --build-dir / set E2SAR_BUILD_DIR."
        exit 1
    fi
    log_info "Build dir: $HOST_BUILD_DIR"

    # --- Socket-buffer limit advisory (native equivalent of the container check) ---
    # Running bare, the effective limit is this host's own kernel, not a VM.
    check_bare_sysctls() {
        local os key cur
        os="$(uname -s)"
        case "$os" in
            Linux)
                for key in net.core.rmem_max net.core.wmem_max; do
                    cur="$(cat "/proc/sys/${key//.//}" 2>/dev/null)" || continue
                    if [[ "$cur" =~ ^[0-9]+$ && "$cur" -lt "$BUFSIZE" ]]; then
                        log_warn "$key=$cur is below --bufsize=$BUFSIZE; sockets will FAIL to open."
                        log_warn "  Raise it:   sudo sysctl -w $key=$BUFSIZE"
                        log_warn "  ...or lower the test buffer: pass --bufsize $cur (or smaller)."
                    fi
                done
                ;;
            Darwin)
                # macOS clamps SO_SND/RCVBUF to kern.ipc.maxsockbuf.
                cur="$(sysctl -n kern.ipc.maxsockbuf 2>/dev/null)" || return 0
                if [[ "$cur" =~ ^[0-9]+$ && "$cur" -lt "$BUFSIZE" ]]; then
                    log_warn "kern.ipc.maxsockbuf=$cur is below --bufsize=$BUFSIZE; sockets may FAIL to open."
                    log_warn "  Raise it:   sudo sysctl -w kern.ipc.maxsockbuf=$BUFSIZE"
                    log_warn "  ...or lower the test buffer: pass --bufsize $cur (or smaller)."
                fi
                ;;
        esac
    }
    check_bare_sysctls

    # --- Availability probe (defensive; the matrix SKIPs unavailable regimes) ---
    log_info "Probing available optimizations in $PERF ..."
    if probe="$("$PERF" --help 2>&1)"; then
        avail_line="$(echo "$probe" | grep -i 'Available Optimizations' | head -1)"
        if [[ -n "$avail_line" ]]; then
            log_info "  ${avail_line#*Available}"
        else
            log_warn "Could not find 'Available Optimizations' in e2sar_perf --help output."
        fi
    else
        log_warn "Availability probe failed; the matrix will still SKIP unavailable regimes per run."
    fi

    log_info "Running matrix bare on the host ..."
    echo ""
    set -x
    "$SCRIPT_DIR/loopback-matrix.sh" \
        --build-dir "$HOST_BUILD_DIR" ${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}
    rc=$?
    set +x

    echo ""
    if [[ $rc -eq 0 ]]; then
        log_info "Matrix completed: all non-skipped regimes passed."
    else
        log_error "Matrix reported failures (exit $rc)."
    fi
    exit $rc
fi

# --- Pick a runtime (§3.3 step 1) ---
if [[ -z "$RUNTIME" ]]; then
    if command -v podman >/dev/null 2>&1; then
        RUNTIME="podman"
    elif command -v docker >/dev/null 2>&1; then
        RUNTIME="docker"
    else
        log_error "Neither podman nor docker found on PATH."
        exit 1
    fi
fi
if [[ "$RUNTIME" != "podman" && "$RUNTIME" != "docker" ]]; then
    log_error "Unknown --runtime '$RUNTIME' (expected podman, docker, or bare)."
    exit 1
fi
log_info "Container runtime: $RUNTIME"

# --- Resolve image (§3.3 step 2) ---
if [[ "$DO_BUILD" -eq 1 ]]; then
    IMAGE="e2sar-perf:local"
    log_info "Building image $IMAGE from Dockerfile.cli (local iteration)..."
    if ! "$RUNTIME" build -f "$REPO_ROOT/Dockerfile.cli" -t "$IMAGE" "$REPO_ROOT"; then
        log_error "Image build failed."
        exit 1
    fi
elif [[ -z "$IMAGE" ]]; then
    IMAGE="ibaldin/e2sar:${IMAGE_VERSION}"
fi
log_info "Image: $IMAGE"

# --- Pull unless already present or suppressed ---
if [[ "$DO_BUILD" -eq 0 && "$NO_PULL" -eq 0 ]]; then
    if "$RUNTIME" image inspect "$IMAGE" >/dev/null 2>&1; then
        log_info "Image already present locally; skipping pull (use --no-pull to force)."
    else
        log_info "Pulling $IMAGE ..."
        if ! "$RUNTIME" pull "$IMAGE"; then
            log_error "Failed to pull $IMAGE."
            exit 1
        fi
    fi
fi

# --- Socket-buffer limit advisory (§3.3 step 4) ---
# With --network=host the effective limits are the host/VM global sysctls, and
# --sysctl on run cannot override them. On macOS (colima / Docker Desktop) these
# live in the Linux VM, NOT on the Mac, so read them the way the test will see
# them: from inside a --network=host container. If they are below --bufsize the
# sender/receiver sockets will fail to open outright ("System socket buffer set
# too low"), so this is a hard prerequisite, not a soft tuning knob.
check_host_sysctls() {
    local vals rmem wmem key cur pair
    vals="$("$RUNTIME" run --rm --network=host "$IMAGE" \
        sh -c 'cat /proc/sys/net/core/rmem_max /proc/sys/net/core/wmem_max' 2>/dev/null)" || return 0
    rmem="$(echo "$vals" | sed -n '1p')"
    wmem="$(echo "$vals" | sed -n '2p')"
    for pair in "net.core.rmem_max:$rmem" "net.core.wmem_max:$wmem"; do
        key="${pair%%:*}"; cur="${pair#*:}"
        if [[ "$cur" =~ ^[0-9]+$ && "$cur" -lt "$BUFSIZE" ]]; then
            log_warn "$key=$cur is below --bufsize=$BUFSIZE; sockets will FAIL to open."
            log_warn "  Raise it where the container's network stack lives, then retry:"
            log_warn "    Linux host:  sudo sysctl -w $key=$BUFSIZE"
            log_warn "    colima VM:   colima ssh -- sudo sysctl -w $key=$BUFSIZE"
            log_warn "  ...or lower the test buffer: pass --bufsize $cur (or smaller)."
        fi
    done
}
check_host_sysctls

# --- Availability probe (§3.3 step 0, defensive) ---
log_info "Probing available optimizations in $IMAGE ..."
if probe="$("$RUNTIME" run --rm "$IMAGE" e2sar_perf --help 2>&1)"; then
    avail_line="$(echo "$probe" | grep -i 'Available Optimizations' | head -1)"
    if [[ -n "$avail_line" ]]; then
        log_info "  ${avail_line#*Available}"
    else
        log_warn "Could not find 'Available Optimizations' in e2sar_perf --help output."
    fi
else
    log_warn "Availability probe failed; the matrix will still SKIP unavailable regimes per run."
fi

# --- Run the matrix inside the container (§3.3 step 3) ---
log_info "Running matrix with --network=host ..."
echo ""
set -x
"$RUNTIME" run --rm --network=host \
    -v "$REPO_ROOT/scripts:/scripts:ro" \
    "$IMAGE" \
    /scripts/loopback-matrix.sh --build-dir "$IN_BUILD_DIR" ${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}
rc=$?
set +x

echo ""
if [[ $rc -eq 0 ]]; then
    log_info "Matrix completed: all non-skipped regimes passed."
else
    log_error "Matrix reported failures (exit $rc)."
fi
exit $rc
