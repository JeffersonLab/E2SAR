#!/bin/bash
# Minimal E2SAR load balancer reservation script
#
# Usage:
#   EJFAT_URI="ejfat://token@host:port/lb/1?sync=..." ./minimal_reserve.sh
#
# Creates/updates INSTANCE_URI file with reservation details
# Note: lbadm --reserve skips SSL cert validation internally; no -v flag needed.

set -euo pipefail

# Script location (for finding sibling scripts if needed)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifacts are created in the current working directory (not script directory)

INSTANCE_URI_FILE="INSTANCE_URI"

# Default configuration
LB_NAME="${LB_NAME:-e2sar_test}"
E2SAR_IMAGE="${E2SAR_IMAGE:-ibaldin/e2sar:0.3.1a3}"

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --lbname)
            LB_NAME="$2"
            shift 2
            ;;
        --image)
            E2SAR_IMAGE="$2"
            shift 2
            ;;
        *)
            echo "ERROR: Unknown argument: $1"
            echo "Usage: $0 [--lbname NAME] [--image IMAGE]"
            exit 1
            ;;
    esac
done

# Validate EJFAT_URI
if [[ -z "${EJFAT_URI:-}" ]]; then
    echo "ERROR: EJFAT_URI is required"
    echo "Set via EJFAT_URI environment variable"
    exit 1
fi

echo "Checking for existing reservation..."

# Check if INSTANCE_URI file exists and is valid
if [[ -f "$INSTANCE_URI_FILE" ]]; then
    echo "Found $INSTANCE_URI_FILE, validating..."

    # Save original admin URI
    ORIGINAL_EJFAT_URI="$EJFAT_URI"

    # Use the instance URI (not the admin URI) to check session validity
    INSTANCE_EJFAT_URI=$(grep -E '^export EJFAT_URI=' "$INSTANCE_URI_FILE" | head -1 | sed "s/^export EJFAT_URI=//; s/^['\"]//; s/['\"]$//")

    # Temporarily use instance URI for validation
    EJFAT_URI="$INSTANCE_EJFAT_URI"
    export EJFAT_URI
    if podman-hpc run --env EJFAT_URI --rm --network host "${E2SAR_IMAGE:-ibaldin/e2sar:0.3.1a3}" lbadm --overview &>/dev/null; then
        echo "Existing reservation is valid, skipping reserve"
        exit 0
    else
        echo "Existing reservation is invalid, will create new reservation"
    fi

    # Restore admin URI for reservation creation below
    EJFAT_URI="$ORIGINAL_EJFAT_URI"
fi

echo "Creating new reservation..."

# Run lbadm --reserve and save output to INSTANCE_URI
# Note: lbadm --reserve skips SSL cert validation internally regardless of --novalidate;
# passing --novalidate interferes with this and causes failures, so it is intentionally omitted.
export EJFAT_URI
podman-hpc run --env EJFAT_URI --rm --network host "$E2SAR_IMAGE" lbadm --reserve --lbname "$LB_NAME" --export > "$INSTANCE_URI_FILE"

echo "Reservation created and saved to $INSTANCE_URI_FILE"
cat "$INSTANCE_URI_FILE"
