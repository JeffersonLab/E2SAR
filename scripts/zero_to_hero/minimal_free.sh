#!/bin/bash
# Minimal E2SAR load balancer free script
#
# Usage:
#   ./minimal_free.sh [-v]
#
# Options:
#   -v    Skip SSL certificate validation
#
# Frees the load balancer reservation using INSTANCE_URI file

set -euo pipefail

# Script location (for finding sibling scripts if needed)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifacts are created in the current working directory (not script directory)

SKIP_SSL_VERIFY="false"
E2SAR_IMAGE="${E2SAR_IMAGE:-ibaldin/e2sar:0.3.1a3}"

while [[ $# -gt 0 ]]; do
    case $1 in
        -v)
            SKIP_SSL_VERIFY="true"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

INSTANCE_URI_FILE="INSTANCE_URI"

# Check if INSTANCE_URI file exists
if [[ ! -f "$INSTANCE_URI_FILE" ]]; then
    echo "ERROR: $INSTANCE_URI_FILE not found"
    echo "No reservation to free"
    exit 1
fi

echo "Found $INSTANCE_URI_FILE"

# Extract EJFAT_URI safely without sourcing the entire file
EJFAT_URI=$(grep -E '^export EJFAT_URI=' "$INSTANCE_URI_FILE" | head -1 | sed "s/^export EJFAT_URI=//; s/^['\"]//; s/['\"]$//")

# Validate EJFAT_URI was set
if [[ -z "${EJFAT_URI:-}" ]]; then
    echo "ERROR: EJFAT_URI not found in $INSTANCE_URI_FILE"
    exit 1
fi

echo "Freeing load balancer reservation..."
EJFAT_URI_REDACTED=$(echo "$EJFAT_URI" | sed -E 's|(://)(.{4})[^@]*(.{4})@|\1\2---\3@|')
echo "EJFAT_URI: $EJFAT_URI_REDACTED"

# Run lbadm --free
LBADM_CMD=(lbadm)
[[ "$SKIP_SSL_VERIFY" == "true" ]] && LBADM_CMD+=(--novalidate)
LBADM_CMD+=(--free)

export EJFAT_URI
if podman-hpc run --env EJFAT_URI --rm --network host "$E2SAR_IMAGE" "${LBADM_CMD[@]}"; then
    echo "Reservation freed successfully"

    # Remove the INSTANCE_URI file
    rm -f "$INSTANCE_URI_FILE"
    echo "Removed $INSTANCE_URI_FILE"
else
    echo "ERROR: Failed to free reservation"
    exit 1
fi
