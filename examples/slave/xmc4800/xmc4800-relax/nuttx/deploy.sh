#!/bin/bash

# XMC4800 Deploy Script
# Usage: ./deploy.sh <binary_file.bin>

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <binary_file.bin>"
    exit 1
fi

BIN_FILE="$1"

# Check if binary file exists
if [ ! -f "$BIN_FILE" ]; then
    echo "Error: Binary file '$BIN_FILE' not found"
    exit 1
fi

# Create J-Link command file
echo "Flashing $BIN_FILE to XMC4800..."
touch flashBin.jlink
echo "loadfile $BIN_FILE 0x08000000" > flashBin.jlink
echo "reset" >> flashBin.jlink
echo "quit"  >> flashBin.jlink

# Flash using J-Link
JLinkExe -device XMC4800-2048 -if swd -speed 4000 -autoconnect 1 -commandfile flashBin.jlink

echo "Deploy complete!"
