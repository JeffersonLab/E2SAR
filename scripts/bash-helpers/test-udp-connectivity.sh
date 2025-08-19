#!/bin/bash

# test-udp-connectivity.sh - Test UDP connectivity to a given host
# Usage: ./test-udp-connectivity.sh -a <host> -p <port> [-6] [-h]

set -euo pipefail

# Function to display usage
usage() {
    echo "Usage: $0 -a <host> -p <port> [-6] [-h]"
    echo ""
    echo "Options:"
    echo "  -a <host>           IP address or hostname (required)"
    echo "  -p <port>           UDP port number (required)"
    echo "  -6                  Use IPv6 (optional, defaults to IPv4)"
    echo "  -h                  Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 -a google.com -p 53                    # Test IPv4 UDP connectivity (DNS)"
    echo "  $0 -a google.com -p 53 -6                 # Test IPv6 UDP connectivity (DNS)"
    echo "  $0 -a 8.8.8.8 -p 53                       # Test IPv4 IP address (DNS)"
    echo "  $0 -a 2001:4860:4860::8888 -p 53 -6       # Test IPv6 IP address (DNS)"
    echo ""
    echo "Note: UDP is connectionless, so port testing uses timeout-based probing"
    echo "      and may show false negatives for filtered/firewalled ports."
    exit 1
}

# Initialize variables
HOST=""
PORT=""
USE_IPV6=false
RESOLVED_IP=""

# Parse command line options
while [[ $# -gt 0 ]]; do
    case $1 in
        -a)
            HOST="$2"
            shift 2
            ;;
        -p)
            PORT="$2"
            shift 2
            ;;
        -6)
            USE_IPV6=true
            shift
            ;;
        -h)
            usage
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            ;;
    esac
done

# Check for required arguments
if [ -z "$HOST" ] || [ -z "$PORT" ]; then
    echo "Error: Missing required arguments"
    echo ""
    usage
fi

# Validate port range
if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    echo "Error: Port must be a number between 1 and 65535"
    exit 2
fi

# Detect OS for command variations
OS_TYPE=$(uname)

# Check if input is already an IP address
is_ipv4() {
    [[ $1 =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]
}

is_ipv6() {
    [[ $1 =~ ^([0-9a-fA-F]{0,4}:){1,7}[0-9a-fA-F]{0,4}$ ]] || [[ $1 == *"::"* ]]
}

# Function to resolve hostname
resolve_hostname() {
    local host="$1"
    local ipv6="$2"
    
    if [ "$ipv6" = true ]; then
        if is_ipv6 "$host"; then
            RESOLVED_IP="$host"
            return 0
        fi
        
        # Try dig first, then nslookup
        if command -v dig >/dev/null 2>&1; then
            RESOLVED_IP=$(dig +short AAAA "$host" | head -1)
        elif command -v nslookup >/dev/null 2>&1; then
            RESOLVED_IP=$(nslookup -type=AAAA "$host" 2>/dev/null | grep -A 10 "Name:" | grep "Address:" | head -1 | awk '{print $2}')
        else
            echo "Error: Neither dig nor nslookup found for DNS resolution"
            exit 2
        fi
    else
        if is_ipv4 "$host"; then
            RESOLVED_IP="$host"
            return 0
        fi
        
        # Try dig first, then nslookup
        if command -v dig >/dev/null 2>&1; then
            RESOLVED_IP=$(dig +short A "$host" | head -1)
        elif command -v nslookup >/dev/null 2>&1; then
            RESOLVED_IP=$(nslookup -type=A "$host" 2>/dev/null | grep -A 10 "Name:" | grep "Address:" | head -1 | awk '{print $2}')
        else
            echo "Error: Neither dig nor nslookup found for DNS resolution"
            exit 2
        fi
    fi
    
    if [ -z "$RESOLVED_IP" ]; then
        return 1
    fi
    
    return 0
}

# Function to test ping connectivity
test_ping() {
    local host="$1"
    local ipv6="$2"
    
    if [ "$ipv6" = true ]; then
        if [ "$OS_TYPE" = "Darwin" ]; then
            # macOS
            if ping -6 -c 3 -W 3000 "$host" >/dev/null 2>&1; then
                local stats=$(ping -6 -c 3 -W 3000 "$host" 2>/dev/null | tail -2)
                local avg_time=$(echo "$stats" | grep "round-trip" | cut -d'/' -f5)
                local packet_loss=$(echo "$stats" | grep "packet loss" | awk '{print $6}')
                echo "✓ Host is reachable (avg: ${avg_time}ms, $packet_loss packet loss)"
                return 0
            fi
        else
            # Linux
            if ping6 -c 3 -W 3 "$host" >/dev/null 2>&1; then
                local stats=$(ping6 -c 3 -W 3 "$host" 2>/dev/null | tail -2)
                local avg_time=$(echo "$stats" | grep "rtt" | cut -d'/' -f5)
                local packet_loss=$(echo "$stats" | grep "packet loss" | awk '{print $6}')
                echo "✓ Host is reachable (avg: ${avg_time}ms, $packet_loss packet loss)"
                return 0
            fi
        fi
    else
        if ping -c 3 -W 3 "$host" >/dev/null 2>&1; then
            local stats=$(ping -c 3 -W 3 "$host" 2>/dev/null | tail -2)
            local avg_time=$(echo "$stats" | grep -E "(round-trip|rtt)" | cut -d'/' -f5)
            local packet_loss=$(echo "$stats" | grep "packet loss" | awk '{print $6}')
            echo "✓ Host is reachable (avg: ${avg_time}ms, $packet_loss packet loss)"
            return 0
        fi
    fi
    
    echo "✗ Host is not reachable via ICMP"
    return 1
}

# Function to test UDP port connectivity
test_udp_port() {
    local host="$1"
    local port="$2"
    local ipv6="$3"
    
    if ! command -v nc >/dev/null 2>&1; then
        echo "✗ netcat (nc) not found - cannot test UDP connectivity"
        return 1
    fi
    
    echo "ℹ Testing UDP port (connectionless protocol - results may vary)"
    
    # Method 1: Try netcat UDP test
    local nc_result=false
    if [ "$ipv6" = true ]; then
        # For IPv6, send a test packet and check if connection is refused
        if timeout 3 bash -c "echo 'test' | nc -6 -u -w 2 '$host' '$port'" >/dev/null 2>&1; then
            nc_result=true
        fi
    else
        # For IPv4, send a test packet and check if connection is refused
        if timeout 3 bash -c "echo 'test' | nc -u -w 2 '$host' '$port'" >/dev/null 2>&1; then
            nc_result=true
        fi
    fi
    
    # Method 2: For common ports, try a service-specific test
    local service_result=false
    local service_name=""
    
    case "$port" in
        53)
            service_name="DNS"
            if command -v dig >/dev/null 2>&1; then
                if [ "$ipv6" = true ]; then
                    if dig @"$host" google.com AAAA +time=3 >/dev/null 2>&1; then
                        service_result=true
                    fi
                else
                    if dig @"$host" google.com A +time=3 >/dev/null 2>&1; then
                        service_result=true
                    fi
                fi
            fi
            ;;
        123)
            service_name="NTP"
            if command -v ntpdate >/dev/null 2>&1; then
                if timeout 5 ntpdate -q "$host" >/dev/null 2>&1; then
                    service_result=true
                fi
            fi
            ;;
        161)
            service_name="SNMP"
            if command -v snmpget >/dev/null 2>&1; then
                if timeout 5 snmpget -v1 -c public "$host" sysDescr.0 >/dev/null 2>&1; then
                    service_result=true
                fi
            fi
            ;;
    esac
    
    # Report results
    if [ "$service_result" = true ]; then
        echo "✓ Port $port is responding ($service_name service confirmed)"
        return 0
    elif [ "$nc_result" = true ]; then
        echo "✓ Port $port appears to be open (UDP packet accepted)"
        return 0
    else
        echo "✗ Port $port is not responding or filtered"
        if [ -n "$service_name" ]; then
            echo "  ($service_name service test failed)"
        fi
        echo "  Note: UDP is connectionless - port may be open but not responding to test packets"
        return 1
    fi
}

# Main execution
PROTOCOL="IPv4"
if [ "$USE_IPV6" = true ]; then
    PROTOCOL="IPv6"
fi

echo "Testing UDP connectivity to $HOST:$PORT ($PROTOCOL)"
echo "====================================================="
echo ""

# Step 1: DNS Resolution
echo "[1/3] DNS Resolution:"
if resolve_hostname "$HOST" "$USE_IPV6"; then
    if [ "$RESOLVED_IP" != "$HOST" ]; then
        echo "✓ Resolved $HOST to $RESOLVED_IP"
    else
        echo "✓ Using IP address: $RESOLVED_IP"
    fi
    TARGET_HOST="$RESOLVED_IP"
else
    echo "✗ Failed to resolve $HOST"
    echo ""
    echo "Overall Result: FAILURE - DNS resolution failed"
    exit 1
fi
echo ""

# Step 2: Ping Test
echo "[2/3] ICMP Ping Test:"
PING_SUCCESS=false
if test_ping "$TARGET_HOST" "$USE_IPV6"; then
    PING_SUCCESS=true
fi
echo ""

# Step 3: UDP Port Test
echo "[3/3] UDP Port Test:"
UDP_SUCCESS=false
if test_udp_port "$TARGET_HOST" "$PORT" "$USE_IPV6"; then
    UDP_SUCCESS=true
fi
echo ""

# Overall Result
if [ "$UDP_SUCCESS" = true ]; then
    echo "Overall Result: SUCCESS - Host is reachable and responding on UDP port $PORT"
    exit 0
elif [ "$PING_SUCCESS" = true ]; then
    echo "Overall Result: PARTIAL - Host is reachable but UDP port $PORT is not responding"
    echo "Note: This may be normal for UDP services that don't respond to arbitrary packets"
    exit 1
else
    echo "Overall Result: FAILURE - Host is not reachable"
    exit 1
fi