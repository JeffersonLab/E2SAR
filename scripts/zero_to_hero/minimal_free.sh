#!/bin/bash
# Minimal E2SAR load balancer free script
#
# Usage:
#   ./minimal_free.sh
#
# Frees the load balancer reservation using INSTANCE_URI file

set -euo pipefail

# Script location (for finding sibling scripts if needed)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifacts are created in the current working directory (not script directory)

INSTANCE_URI_FILE="INSTANCE_URI"

# Check if INSTANCE_URI file exists
if [[ ! -f "$INSTANCE_URI_FILE" ]]; then
    echo "ERROR: $INSTANCE_URI_FILE not found"
    echo "No reservation to free"
    exit 1
fi

echo "Found $INSTANCE_URI_FILE"

# Source the INSTANCE_URI file to get the EJFAT_URI
source "$INSTANCE_URI_FILE"

# Validate EJFAT_URI was set
if [[ -z "${EJFAT_URI:-}" ]]; then
    echo "ERROR: EJFAT_URI not found in $INSTANCE_URI_FILE"
    exit 1
fi

echo "Freeing load balancer reservation..."
echo "EJFAT_URI: $EJFAT_URI"

# Run lbadm --free
if podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --free; then
    echo "Reservation freed successfully"

    # Remove the INSTANCE_URI file
    rm -f "$INSTANCE_URI_FILE"
    echo "Removed $INSTANCE_URI_FILE"
else
    echo "ERROR: Failed to free reservation"
    exit 1
fi
