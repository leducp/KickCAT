#!/bin/bash

# Arduino Due Deploy Script
# Usage: ./deploy.sh <binary_file.bin>

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <binary_file.bin>"
    exit 1
fi

BIN_FILE="$1"
BOSSAC_VERSION="1.9.1"

# Check if binary file exists
if [ ! -f "$BIN_FILE" ]; then
    echo "Error: Binary file '$BIN_FILE' not found"
    exit 1
fi

# Store bossac in the same directory as the binary
BUILD_DIR=$(dirname "$(realpath "$BIN_FILE")")
BOSSAC_DIR="${BUILD_DIR}"
BOSSAC_BIN="${BOSSAC_DIR}/bossac"

# Function to download and setup bossac
setup_bossac() {
    echo "Setting up bossac ${BOSSAC_VERSION}..."
    mkdir -p "${BUILD_DIR}"
    
    cd "${BUILD_DIR}"
    
    # Detect OS and architecture
    OS=$(uname -s)
    ARCH=$(uname -m)
    
    if [ "$OS" = "Linux" ]; then
        BOSSAC_URL="http://downloads.arduino.cc/tools/bossac-${BOSSAC_VERSION}-arduino1-linux64.tar.gz"
    else
        echo "Error: Unsupported OS: $OS"
        exit 1
    fi
    
    echo "Downloading bossac from $BOSSAC_URL"
    curl -L -o bossac.tar.gz "$BOSSAC_URL"
    
    tar -xzf bossac.tar.gz --strip-components=1 -C "${BUILD_DIR}"
    rm bossac.tar.gz
    
    echo "bossac installed successfully"
}

# Check if bossac exists, if not download it
if [ ! -f "$BOSSAC_BIN" ]; then
    echo "bossac not found at $BOSSAC_BIN"
    setup_bossac
fi

# Auto-detect the Arduino Due port
PORT=""
for port in /dev/ttyACM* /dev/ttyUSB*; do
    if [ -e "$port" ]; then
        PORT=$(basename "$port")
        echo "Found Arduino Due at $port"
        break
    fi
done

if [ -z "$PORT" ]; then
    echo "Error: No Arduino Due found. Please connect the board."
    exit 1
fi

# Flash the binary
echo "Flashing $BIN_FILE to Arduino Due..."
"${BOSSAC_BIN}" -i --port="$PORT" -e -w -b "$BIN_FILE" -R

echo "Deploy complete!"
