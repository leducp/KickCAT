#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

source "$SCRIPT_DIR/lib/log.sh"

# Usage
if [ $# -lt 2 ]; then
    echo "Usage: $0 <board-name> <artifact-zip-path>"
    echo "Boards supported: xmc4800-relax, arduino-due, freedom-k64f"
    echo ""
    echo "Example: $0 xmc4800-relax ./firmware-xmc4800-relax-nuttx-stable.zip"
    exit 1
fi

BOARD="$1"
ARTIFACT_ZIP="$(realpath "$2")"
kickcat_src="$(realpath "$SCRIPT_DIR/..")"
temp_dir=$(mktemp -d)
trap "rm -rf $temp_dir" EXIT

# Validate artifact exists
if [ ! -f "$ARTIFACT_ZIP" ]; then
    error "Artifact file not found: $ARTIFACT_ZIP"
    exit 1
fi

# Board-to-deploy-script and binary name mapping
case "$BOARD" in
    "xmc4800-relax")
        DEPLOY_SCRIPT="${kickcat_src}/examples/slave/nuttx/xmc4800/deploy.sh"
        BINARY_NAME="xmc4800_relax.bin" # Find a better way to identify the binary
        ;;
    "arduino-due")
        DEPLOY_SCRIPT="${kickcat_src}/examples/slave/nuttx/lan9252/arduino-due/deploy.sh"
        BINARY_NAME="easycat_arduino_due.bin" # Find a better way to identify the binary
        ;;
    "freedom-k64f")
        DEPLOY_SCRIPT="${kickcat_src}/examples/slave/nuttx/lan9252/freedom-k64f/deploy.sh"
        BINARY_NAME="easycat_frdm_k64f.bin" # Find a better way to identify the binary
        ;;
    *)
        error "Unknown board: $BOARD"
        info "Supported boards: xmc4800-relax, arduino-due, freedom-k64f"
        exit 1
        ;;
esac

# Validate deploy script exists
if [ ! -f "$DEPLOY_SCRIPT" ]; then
    error "Deploy script not found: $DEPLOY_SCRIPT"
    exit 1
fi

info "Board:         $BOARD"
info "Artifact:      $ARTIFACT_ZIP"
info "Deploy script: $DEPLOY_SCRIPT"

step "Extracting artifact"
unzip -q "$ARTIFACT_ZIP" -d "$temp_dir"

# Find binary
BINARY_PATH=$(find "$temp_dir" -name "$BINARY_NAME" -type f | head -n 1)

if [ -z "$BINARY_PATH" ]; then
    error "Binary '$BINARY_NAME' not found in artifact"
    info "Contents:"
    find "$temp_dir" -type f
    exit 1
fi

success "Found binary: $BINARY_NAME"

step "Flashing $BOARD"
chmod +x "$DEPLOY_SCRIPT"
"$DEPLOY_SCRIPT" "$BINARY_PATH"

success "Deployment completed!"
