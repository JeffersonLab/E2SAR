#!/bin/bash

# generate-random-files.sh - Generate N random files with specified parameters
# Usage: ./generate-random-files.sh -s <size_MB> -p <prefix> -e <extension> [-d <directory>] -n <count>

set -euo pipefail

# Function to display usage
usage() {
    echo "Usage: $0 -s <size_MB> -p <prefix> -e <extension> [-d <directory>] -n <count>"
    echo ""
    echo "Options:"
    echo "  -s <size_MB>        Size of each file in MB (required)"
    echo "  -p <prefix>         Prefix for generated file names (required)"
    echo "  -e <extension>      File extension without dot (required)"
    echo "  -d <directory>      Directory to create files (default: /dev/shm)"
    echo "  -n <count>          Number of files to generate (required)"
    echo "  -h                  Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 -s 100 -p testfile -e txt -n 5                    # 5 files in /dev/shm"
    echo "  $0 -s 50 -p data -e bin -d /tmp -n 10                # 10 files in /tmp"
    exit 1
}

# Initialize variables with defaults
FILE_LENGTH_MB=""
FILE_NAME_PREFIX=""
FILE_EXTENSION=""
DESTINATION_DIR="/dev/shm"
NUMBER_OF_FILES=""

# Parse command line options
while getopts "s:p:e:d:n:h" opt; do
    case $opt in
        s)
            FILE_LENGTH_MB="$OPTARG"
            ;;
        p)
            FILE_NAME_PREFIX="$OPTARG"
            ;;
        e)
            FILE_EXTENSION="$OPTARG"
            ;;
        d)
            DESTINATION_DIR="$OPTARG"
            ;;
        n)
            NUMBER_OF_FILES="$OPTARG"
            ;;
        h)
            usage
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            usage
            ;;
        :)
            echo "Option -$OPTARG requires an argument." >&2
            usage
            ;;
    esac
done

# Check for required arguments
if [ -z "$FILE_LENGTH_MB" ] || [ -z "$FILE_NAME_PREFIX" ] || [ -z "$FILE_EXTENSION" ] || [ -z "$NUMBER_OF_FILES" ]; then
    echo "Error: Missing required arguments"
    echo ""
    usage
fi

# Validate inputs
if ! [[ "$FILE_LENGTH_MB" =~ ^[0-9]+$ ]] || [ "$FILE_LENGTH_MB" -le 0 ]; then
    echo "Error: file_length_MB must be a positive integer"
    exit 1
fi

if ! [[ "$NUMBER_OF_FILES" =~ ^[0-9]+$ ]] || [ "$NUMBER_OF_FILES" -le 0 ]; then
    echo "Error: number_of_files must be a positive integer"
    exit 1
fi

# Calculate byte count (MB to bytes)
BYTE_COUNT=$((FILE_LENGTH_MB * 1024 * 1024))

# Check if destination directory exists and is writable
if [ ! -d "$DESTINATION_DIR" ]; then
    echo "Error: Destination directory does not exist: $DESTINATION_DIR"
    exit 1
fi

if [ ! -w "$DESTINATION_DIR" ]; then
    echo "Error: Cannot write to destination directory: $DESTINATION_DIR"
    exit 1
fi

echo "Generating $NUMBER_OF_FILES files of ${FILE_LENGTH_MB}MB each in $DESTINATION_DIR"
echo "File pattern: ${FILE_NAME_PREFIX}-N.${FILE_EXTENSION}"

# Generate files
for i in $(seq 1 "$NUMBER_OF_FILES"); do
    filename="${DESTINATION_DIR}/${FILE_NAME_PREFIX}-${i}.${FILE_EXTENSION}"
    echo "Generating file $i/$NUMBER_OF_FILES: $(basename "$filename")"
    
    # Use head to read from /dev/urandom and generate random file
    # Use byte count instead of M suffix for macOS compatibility
    head -c "$BYTE_COUNT" /dev/urandom > "$filename"
    
    if [ $? -eq 0 ]; then
        echo "  ✓ Generated: $filename ($(du -h "$filename" | cut -f1))"
    else
        echo "  ✗ Failed to generate: $filename"
        exit 1
    fi
done

echo ""
echo "Successfully generated $NUMBER_OF_FILES files in $DESTINATION_DIR"
echo "Total space used: $(du -sh "$DESTINATION_DIR"/"${FILE_NAME_PREFIX}"-*.${FILE_EXTENSION} 2>/dev/null | tail -1 | cut -f1 || echo "Unknown")"