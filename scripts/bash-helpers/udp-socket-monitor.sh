#!/bin/bash

# UDP Socket Monitor - High-frequency monitoring of UDP socket statistics
# Monitors memory usage and packet statistics for specified UDP ports

SCRIPT_NAME="udp-socket-monitor.sh"
VERSION="1.0"

# Global variables
PORTS=""
PERIOD=""
OUTPUT_FILE=""
TEMP_FILE=""
VERBOSE=false

show_help() {
    cat << EOF
$SCRIPT_NAME v$VERSION - UDP Socket Statistics Monitor

USAGE:
    $SCRIPT_NAME -p <port_list> -t <period> -o <output_file> [OPTIONS]

REQUIRED PARAMETERS:
    -p, --ports PORT_LIST    UDP ports to monitor
                            Examples: 20000, 20000-20010, 20000,25000,30000-30005
    -t, --period SECONDS     Sampling period in seconds (supports fractional)
                            Examples: 0.1, 0.5, 1.0, 2.5
    -o, --output FILE        Output CSV file path

OPTIONAL PARAMETERS:
    -v, --verbose           Show progress messages to stderr
    -h, --help             Show this help message

EXAMPLES:
    # Monitor ports 20000-20010 every 0.1 seconds
    $SCRIPT_NAME -p 20000-20010 -t 0.1 -o udp_stats.csv

    # Monitor specific ports with verbose output
    $SCRIPT_NAME -p 20000,25000,30000 -t 0.2 -o stats.csv -v

    # Run until Ctrl-C is pressed to save data
    $SCRIPT_NAME -p 20000 -t 0.5 -o monitoring.csv

OUTPUT FORMAT:
    CSV with columns: timestamp,udp_port,pid,process_name,local_addr,state,inode,
    recv_q,send_q,rmem_alloc,rcv_buf,wmem_alloc,snd_buf,fwd_alloc,wmem_queued,
    opt_mem,back_log,sock_drop,mem_rss_kb,mem_vsz_kb

NOTES:
    - Data is temporarily stored in /dev/shm for performance
    - Press Ctrl-C to stop monitoring and save final results
    - Recommended sampling period: 0.1-0.2 seconds for high-frequency monitoring

EOF
}

log_verbose() {
    if [ "$VERBOSE" = true ]; then
        echo "[$(date '+%H:%M:%S')] $1" >&2
    fi
}

log_error() {
    echo "ERROR: $1" >&2
}

cleanup_and_exit() {
    log_verbose "Received signal, finalizing data..."
    if [ -f "$TEMP_FILE" ]; then
        cp "$TEMP_FILE" "$OUTPUT_FILE" 2>/dev/null
        rm -f "$TEMP_FILE"
        log_verbose "Data saved to $OUTPUT_FILE"
    fi
    exit 0
}

# Parse port list into ss filter format
parse_ports() {
    local port_list="$1"
    local ss_filter=""
    
    # Split by comma
    IFS=',' read -ra PORT_SPECS <<< "$port_list"
    
    for spec in "${PORT_SPECS[@]}"; do
        if [[ "$spec" =~ ^([0-9]+)-([0-9]+)$ ]]; then
            # Port range
            local start="${BASH_REMATCH[1]}"
            local end="${BASH_REMATCH[2]}"
            for ((port=start; port<=end; port++)); do
                if [ -n "$ss_filter" ]; then
                    ss_filter="$ss_filter or "
                fi
                ss_filter="${ss_filter}sport = :$port"
            done
        elif [[ "$spec" =~ ^[0-9]+$ ]]; then
            # Single port
            if [ -n "$ss_filter" ]; then
                ss_filter="$ss_filter or "
            fi
            ss_filter="${ss_filter}sport = :$spec"
        else
            log_error "Invalid port specification: $spec"
            exit 1
        fi
    done
    
    echo "$ss_filter"
}

# Extract process memory info
get_process_memory() {
    local pid="$1"
    local rss="0"
    local vsz="0"
    
    if [ "$pid" != "0" ] && [ -f "/proc/$pid/status" ]; then
        rss=$(grep '^VmRSS:' "/proc/$pid/status" 2>/dev/null | awk '{print $2}' || echo "0")
        vsz=$(grep '^VmSize:' "/proc/$pid/status" 2>/dev/null | awk '{print $2}' || echo "0")
    fi
    
    echo "$rss,$vsz"
}

# Parse skmem field
parse_skmem() {
    local skmem="$1"
    # Extract values from skmem:(r0,rb212992,t0,tb212992,f0,w0,o0,bl0,d0)
    if [[ "$skmem" =~ skmem:\(r([0-9]+),rb([0-9]+),t([0-9]+),tb([0-9]+),f([0-9]+),w([0-9]+),o([0-9]+),bl([0-9]+),d([0-9]+)\) ]]; then
        echo "${BASH_REMATCH[1]},${BASH_REMATCH[2]},${BASH_REMATCH[3]},${BASH_REMATCH[4]},${BASH_REMATCH[5]},${BASH_REMATCH[6]},${BASH_REMATCH[7]},${BASH_REMATCH[8]},${BASH_REMATCH[9]}"
    else
        echo "0,0,0,0,0,0,0,0,0"
    fi
}

# Sample UDP socket data
sample_data() {
    local timestamp=$(date -Iseconds)
    local ss_filter="$1"
    
    # Run ss command with filter
    ss -u -n -p -e -m -O "$ss_filter" 2>/dev/null | while read -r line; do
        # Skip empty lines
        [ -z "$line" ] && continue
        
        # Parse ss output line
        # Format: STATE RECV-Q SEND-Q LOCAL-ADDR PEER-ADDR PROCESS-INFO INODE SKMEM
        local fields=($line)
        local state="${fields[0]}"
        local recv_q="${fields[1]}"
        local send_q="${fields[2]}"
        local local_addr="${fields[3]}"
        local peer_addr="${fields[4]}"
        
        # Extract port from local address
        local port=$(echo "$local_addr" | sed 's/.*://')
        
        # Extract process info
        local pid="0"
        local process_name=""
        if [[ "$line" =~ users:\(\(\"([^\"]+)\",pid=([0-9]+) ]]; then
            process_name="${BASH_REMATCH[1]}"
            pid="${BASH_REMATCH[2]}"
        fi
        
        # Extract inode
        local inode="0"
        if [[ "$line" =~ ino:([0-9]+) ]]; then
            inode="${BASH_REMATCH[1]}"
        fi
        
        # Parse skmem
        local skmem_values=$(parse_skmem "$line")
        
        # Get process memory
        local mem_info=$(get_process_memory "$pid")
        
        # Output CSV line
        echo "$timestamp,$port,$pid,$process_name,$local_addr,$state,$inode,$recv_q,$send_q,$skmem_values,$mem_info"
    done
}

# Validate parameters
validate_params() {
    if [ -z "$PORTS" ]; then
        log_error "Port list is required (-p)"
        exit 1
    fi
    
    if [ -z "$PERIOD" ]; then
        log_error "Sampling period is required (-t)"
        exit 1
    fi
    
    if [ -z "$OUTPUT_FILE" ]; then
        log_error "Output file is required (-o)"
        exit 1
    fi
    
    # Validate period is a positive number
    if ! [[ "$PERIOD" =~ ^[0-9]*\.?[0-9]+$ ]] || (( $(echo "$PERIOD <= 0" | bc -l) )); then
        log_error "Period must be a positive number"
        exit 1
    fi
    
    # Check if /dev/shm is available
    if [ ! -d "/dev/shm" ]; then
        log_error "/dev/shm is not available"
        exit 1
    fi
    
    # Check if output directory is writable
    local output_dir=$(dirname "$OUTPUT_FILE")
    if [ ! -w "$output_dir" ]; then
        log_error "Cannot write to output directory: $output_dir"
        exit 1
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--ports)
            PORTS="$2"
            shift 2
            ;;
        -t|--period)
            PERIOD="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Validate parameters
validate_params

# Setup temporary file
TEMP_FILE="/dev/shm/udp_monitor_$$.csv"

# Setup signal handling
trap cleanup_and_exit INT TERM

# Create CSV header
echo "timestamp,udp_port,pid,process_name,local_addr,state,inode,recv_q,send_q,rmem_alloc,rcv_buf,wmem_alloc,snd_buf,fwd_alloc,wmem_queued,opt_mem,back_log,sock_drop,mem_rss_kb,mem_vsz_kb" > "$TEMP_FILE"

# Parse ports into ss filter format
SS_FILTER=$(parse_ports "$PORTS")

log_verbose "Starting UDP socket monitoring..."
log_verbose "Ports: $PORTS"
log_verbose "Period: ${PERIOD}s"
log_verbose "Output: $OUTPUT_FILE"
log_verbose "Temporary file: $TEMP_FILE"
log_verbose "Press Ctrl-C to stop and save data"

# Main monitoring loop
while true; do
    sample_data "$SS_FILTER" >> "$TEMP_FILE"
    sleep "$PERIOD"
done