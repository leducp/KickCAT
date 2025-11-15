#!/bin/bash
set -euo pipefail

# Usage
if [ $# -lt 2 ]; then
    echo "Usage: $0 <board-name> <nuttx-src-path> [build-name]"
    echo "Boards supported: xmc4800-relax, arduino-due, freedom-k64f"
    exit 1
fi

BOARD="$1"
nuttx_src="$(realpath "$2")"
BUILD_NAME="${3:-${BOARD}}"

kickcat_src="$(realpath "$(dirname "$0")/..")"
build_dir="${kickcat_src}/build_${BUILD_NAME}"

# Board-to-path + config mapping
case "$BOARD" in

"xmc4800-relax")
    BOARD_PATH="arm/xmc4/xmc4800-relax"
    DEFCONFIG_SRC="${kickcat_src}/examples/slave/nuttx/xmc4800/boards/relax/defconfig"
    CONFIG_NAME="kickcat"
    ;;

"arduino-due")
    BOARD_PATH="arm/sam34/arduino-due"
    DEFCONFIG_SRC="${kickcat_src}/examples/slave/nuttx/lan9252/arduino-due/board/defconfig"
    CONFIG_NAME="kickcat"
    ;;

"freedom-k64f")
    BOARD_PATH="arm/kinetis/freedom-k64f"
    DEFCONFIG_SRC="${kickcat_src}/examples/slave/nuttx/lan9252/freedom-k64f/board/defconfig"
    CONFIG_NAME="kickcat"
    ;;

*)
    echo "Unknown board: $BOARD"
    exit 1
    ;;
esac

# Build
echo "- Board:        $BOARD"
echo "- Config Name:  $CONFIG_NAME"
echo "- Defconfig:    $DEFCONFIG_SRC"
echo "- NuttX src:    $nuttx_src"
echo "- Build dir:    $build_dir"

mkdir -p "$build_dir"
make -C "$nuttx_src" distclean || true

# Copy configuration file
NUTTX_CONFIG_PATH="$nuttx_src/boards/${BOARD_PATH}/configs/${CONFIG_NAME}"
mkdir -p "$NUTTX_CONFIG_PATH"
cp "$DEFCONFIG_SRC" "$NUTTX_CONFIG_PATH/defconfig"

# Configure NuttX
"$nuttx_src/tools/configure.sh" -l -E "${BOARD}:${CONFIG_NAME}"

# Export NuttX
make -C "$nuttx_src" export -j$(nproc)

rm -rf "${build_dir}/nuttx-export"
mkdir -p "${build_dir}/nuttx-export"

tar xf "$nuttx_src"/nuttx-export-*.tar.gz \
    --strip-components=1 \
    -C "${build_dir}/nuttx-export"

# Build KickCAT
cmake \
    -B "${build_dir}" \
    -S "${kickcat_src}" \
    -DCMAKE_TOOLCHAIN_FILE="${build_dir}/nuttx-export/scripts/toolchain.cmake"

cmake --build "${build_dir}" -- -j$(nproc)

echo "Build SUCCESS — Output: ${build_dir}"
