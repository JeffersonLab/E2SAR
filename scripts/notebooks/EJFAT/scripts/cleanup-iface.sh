#!/bin/bash
#
# cleanup-iface.sh - Remove IP addresses and routes from a Linux interface
#
# This script removes IPv4/IPv6 addresses assigned from specified subnets
# and removes routes to specified destination subnets.
#

set -e

usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Remove IP addresses and routes from a Linux interface.

Required options:
  -i, --interface IFACE       Network interface name
  -4, --ipv4-subnet SUBNET    IPv4 subnet from which address was assigned (CIDR notation)
  -6, --ipv6-subnet SUBNET    IPv6 subnet from which address was assigned (CIDR notation)
  -r, --ipv4-route SUBNET     IPv4 destination subnet for route removal (CIDR notation)
  -R, --ipv6-route SUBNET     IPv6 destination subnet for route removal (CIDR notation)

Optional:
  -h, --help                  Show this help message
  -n, --dry-run               Show commands without executing them
  -v, --verbose               Enable verbose output

Example:
  $(basename "$0") -i eth1 -4 192.168.1.0/24 -6 fd00::/64 -r 10.0.0.0/8 -R 2001:db8::/32

EOF
    exit 1
}

# Parse command line arguments
INTERFACE=""
IPV4_SUBNET=""
IPV6_SUBNET=""
IPV4_ROUTE_DEST=""
IPV6_ROUTE_DEST=""
DRY_RUN=0
VERBOSE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i|--interface)
            INTERFACE="$2"
            shift 2
            ;;
        -4|--ipv4-subnet)
            IPV4_SUBNET="$2"
            shift 2
            ;;
        -6|--ipv6-subnet)
            IPV6_SUBNET="$2"
            shift 2
            ;;
        -r|--ipv4-route)
            IPV4_ROUTE_DEST="$2"
            shift 2
            ;;
        -R|--ipv6-route)
            IPV6_ROUTE_DEST="$2"
            shift 2
            ;;
        -n|--dry-run)
            DRY_RUN=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Error: Unknown option: $1" >&2
            usage
            ;;
    esac
done

# Validate required parameters
if [[ -z "$INTERFACE" ]]; then
    echo "Error: Interface name is required (-i)" >&2
    usage
fi

if [[ -z "$IPV4_SUBNET" ]]; then
    echo "Error: IPv4 subnet is required (-4)" >&2
    usage
fi

if [[ -z "$IPV6_SUBNET" ]]; then
    echo "Error: IPv6 subnet is required (-6)" >&2
    usage
fi

if [[ -z "$IPV4_ROUTE_DEST" ]]; then
    echo "Error: IPv4 route destination is required (-r)" >&2
    usage
fi

if [[ -z "$IPV6_ROUTE_DEST" ]]; then
    echo "Error: IPv6 route destination is required (-R)" >&2
    usage
fi

# Check if interface exists
if ! ip link show "$INTERFACE" &>/dev/null; then
    echo "Error: Interface '$INTERFACE' does not exist" >&2
    exit 1
fi

log() {
    if [[ $VERBOSE -eq 1 ]]; then
        echo "$@"
    fi
}

run_cmd() {
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "[DRY-RUN] $*"
    else
        log "Executing: $*"
        "$@"
    fi
}

# Extract network prefix from CIDR notation (e.g., 192.168.1.0/24 -> 192.168.1)
# For matching addresses assigned from that subnet
get_ipv4_prefix() {
    local subnet="$1"
    local network="${subnet%/*}"
    local prefix_len="${subnet#*/}"

    # Calculate the network prefix based on prefix length
    # For simplicity, we'll match based on the significant octets
    local octets
    IFS='.' read -ra octets <<< "$network"

    if [[ $prefix_len -ge 24 ]]; then
        echo "${octets[0]}.${octets[1]}.${octets[2]}."
    elif [[ $prefix_len -ge 16 ]]; then
        echo "${octets[0]}.${octets[1]}."
    elif [[ $prefix_len -ge 8 ]]; then
        echo "${octets[0]}."
    else
        echo ""
    fi
}

get_ipv6_prefix() {
    local subnet="$1"
    local network="${subnet%/*}"
    local prefix_len="${subnet#*/}"

    # Expand and normalize the IPv6 address for matching
    # Extract the significant portion based on prefix length
    # For /64, we match the first 4 groups
    local groups
    IFS=':' read -ra groups <<< "$network"

    if [[ $prefix_len -ge 64 ]]; then
        # Match first 4 groups (64 bits)
        echo "${groups[0]}:${groups[1]}:${groups[2]}:${groups[3]}:"
    elif [[ $prefix_len -ge 48 ]]; then
        echo "${groups[0]}:${groups[1]}:${groups[2]}:"
    elif [[ $prefix_len -ge 32 ]]; then
        echo "${groups[0]}:${groups[1]}:"
    else
        echo "${groups[0]}:"
    fi
}

echo "=== Cleaning up interface: $INTERFACE ==="

# Step 1: Find and remove IPv4 addresses from the specified subnet
echo ""
echo "--- Removing IPv4 addresses from subnet $IPV4_SUBNET ---"

IPV4_PREFIX=$(get_ipv4_prefix "$IPV4_SUBNET")
log "Looking for addresses matching prefix: $IPV4_PREFIX"

# Get IPv4 addresses assigned to the interface that match the subnet
IPV4_ADDRS=$(ip -4 addr show dev "$INTERFACE" 2>/dev/null | \
    awk -v prefix="$IPV4_PREFIX" '/inet / {
        split($2, a, "/");
        if (index(a[1], prefix) == 1) print $2
    }')

if [[ -z "$IPV4_ADDRS" ]]; then
    echo "No IPv4 addresses from subnet $IPV4_SUBNET found on $INTERFACE"
else
    for addr in $IPV4_ADDRS; do
        echo "Removing IPv4 address: $addr"
        run_cmd sudo ip -4 address delete "$addr" dev "$INTERFACE"
    done
fi

# Step 2: Find and remove IPv6 addresses from the specified subnet
echo ""
echo "--- Removing IPv6 addresses from subnet $IPV6_SUBNET ---"

IPV6_PREFIX=$(get_ipv6_prefix "$IPV6_SUBNET")
log "Looking for addresses matching prefix: $IPV6_PREFIX"

# Get IPv6 addresses assigned to the interface that match the subnet
# Note: we use tolower() to handle case-insensitive matching for IPv6
IPV6_ADDRS=$(ip -6 addr show dev "$INTERFACE" 2>/dev/null | \
    awk -v prefix="$IPV6_PREFIX" 'BEGIN { prefix = tolower(prefix) } /inet6 / {
        split($2, a, "/");
        addr_lower = tolower(a[1]);
        prefix_lower = tolower(prefix);
        if (index(addr_lower, prefix_lower) == 1) print $2
    }')

if [[ -z "$IPV6_ADDRS" ]]; then
    echo "No IPv6 addresses from subnet $IPV6_SUBNET found on $INTERFACE"
else
    for addr in $IPV6_ADDRS; do
        echo "Removing IPv6 address: $addr"
        run_cmd sudo ip -6 address delete "$addr" dev "$INTERFACE"
    done
fi

# Step 3: Remove IPv4 route to destination subnet
echo ""
echo "--- Removing IPv4 route to $IPV4_ROUTE_DEST ---"

# Get route information using ip route get
IPV4_ROUTE_INFO=$(ip -4 route get "${IPV4_ROUTE_DEST%/*}" 2>/dev/null | head -1) || true

if [[ -n "$IPV4_ROUTE_INFO" ]]; then
    # Extract gateway and device from route info
    # Format: <dest> via <gateway> dev <interface> ...
    GATEWAY=$(echo "$IPV4_ROUTE_INFO" | awk '{for(i=1;i<=NF;i++) if($i=="via") print $(i+1)}')
    ROUTE_DEV=$(echo "$IPV4_ROUTE_INFO" | awk '{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}')

    if [[ "$ROUTE_DEV" == "$INTERFACE" ]]; then
        if [[ -n "$GATEWAY" ]]; then
            echo "Removing route: $IPV4_ROUTE_DEST via $GATEWAY dev $INTERFACE"
            run_cmd sudo ip route delete "$IPV4_ROUTE_DEST" via "$GATEWAY" dev "$INTERFACE"
        else
            echo "Removing route: $IPV4_ROUTE_DEST dev $INTERFACE"
            run_cmd sudo ip route delete "$IPV4_ROUTE_DEST" dev "$INTERFACE"
        fi
    else
        echo "Route to $IPV4_ROUTE_DEST uses different interface ($ROUTE_DEV), skipping"
    fi
else
    echo "No IPv4 route to $IPV4_ROUTE_DEST found"
fi

# Step 4: Remove IPv6 route to destination subnet
echo ""
echo "--- Removing IPv6 route to $IPV6_ROUTE_DEST ---"

# Get route information using ip route get
IPV6_ROUTE_INFO=$(ip -6 route get "${IPV6_ROUTE_DEST%/*}" 2>/dev/null | head -1) || true

if [[ -n "$IPV6_ROUTE_INFO" ]]; then
    # Extract gateway and device from route info
    GATEWAY6=$(echo "$IPV6_ROUTE_INFO" | awk '{for(i=1;i<=NF;i++) if($i=="via") print $(i+1)}')
    ROUTE_DEV6=$(echo "$IPV6_ROUTE_INFO" | awk '{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}')

    if [[ "$ROUTE_DEV6" == "$INTERFACE" ]]; then
        if [[ -n "$GATEWAY6" ]]; then
            echo "Removing route: $IPV6_ROUTE_DEST via $GATEWAY6 dev $INTERFACE"
            run_cmd sudo ip -6 route delete "$IPV6_ROUTE_DEST" via "$GATEWAY6" dev "$INTERFACE"
        else
            echo "Removing route: $IPV6_ROUTE_DEST dev $INTERFACE"
            run_cmd sudo ip -6 route delete "$IPV6_ROUTE_DEST" dev "$INTERFACE"
        fi
    else
        echo "Route to $IPV6_ROUTE_DEST uses different interface ($ROUTE_DEV6), skipping"
    fi
else
    echo "No IPv6 route to $IPV6_ROUTE_DEST found"
fi

echo ""
echo "=== Cleanup complete ==="
