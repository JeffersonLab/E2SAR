#!/bin/bash
# Minimal E2SAR load balancer reservation script
#
# Usage:
#   EJFAT_URI="ejfat://token@host:port/lb/1?sync=..." ./minimal_reserve.sh
#
# Creates/updates INSTANCE_URI file with reservation details

set -euo pipefail

INSTANCE_URI_FILE="INSTANCE_URI"

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

    # Try to run lbadm --overview to check if the reservation is valid
    if podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --overview &>/dev/null; then
        echo "Existing reservation is valid, skipping reserve"
        exit 0
    else
        echo "Existing reservation is invalid, will create new reservation"
    fi
fi

echo "Creating new reservation..."

# Run lbadm --reserve and save output to INSTANCE_URI
podman-hpc run -e EJFAT_URI="$EJFAT_URI" --rm --network host ibaldin/e2sar:0.3.1a3 lbadm --reserve --lbname "yk_test" --export > "$INSTANCE_URI_FILE"

echo "Reservation created and saved to $INSTANCE_URI_FILE"
cat "$INSTANCE_URI_FILE"
