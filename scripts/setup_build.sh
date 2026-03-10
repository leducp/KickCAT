#!/bin/bash

KICKCAT_DIR=$(dirname "$(realpath $0)")/../

usage() {
    cat <<EOF
Usage: $0 <build_dir> [OPTIONS]

Set up the build environment: detect (or select) a compiler, generate a Conan
profile and a CMake toolchain file, then install dependencies via Conan.

Arguments:
  build_dir                 Output directory for generated files (required)

Options:
  --target <os-arch>        Cross-compile for the given platform instead of
                            building natively. Supported targets:
                              linux-aarch64   ARM64 Linux (RPi3/4/5, Rockchip, …)
  -h, --help                Show this help message and exit

Examples:
  $0 build                             # native build
  $0 build --target linux-aarch64      # cross-compile for ARM64 Linux
EOF
}

# Parse arguments: <build_dir> [--target <os-arch>] [-h|--help]
build_dir=""
CROSS_TARGET=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --target)
            CROSS_TARGET="$2"
            shift 2
            ;;
        *)
            if [ -z "$build_dir" ]; then
                build_dir="$1"
            else
                echo "Unknown argument: $1"
                echo ""
                usage
                exit 1
            fi
            shift
            ;;
    esac
done

if [ -z "$build_dir" ]; then
    usage
    exit 1
fi

# Minimum required version
MIN_CONAN_VERSION="2.10.0"

# Check if conan is installed
if ! command -v conan >/dev/null 2>&1; then
    echo "Conan is not installed. Aborting..."
    exit 1
fi

# Get installed version
INSTALLED_CONAN_VERSION=$(conan --version | awk '{print $3}')

# Compare versions
if [[ "$(printf '%s\n' "$MIN_CONAN_VERSION" "$INSTALLED_CONAN_VERSION" | sort -V | head -n1)" != "$MIN_CONAN_VERSION" ]]; then
    echo "Conan version $MIN_CONAN_VERSION or higher is required (found $INSTALLED_CONAN_VERSION)"
    exit 1
fi

mkdir -p "$build_dir"
echo "$build_dir"

TEMPLATE_CMAKE_TOOLCHAIN="$KICKCAT_DIR/cmake/toolchain.cmake.template"
OUTPUT_CMAKE_TOOLCHAIN="$build_dir/toolchain.cmake"
TEMPLATE_CONAN_PROFILE="$KICKCAT_DIR/conan/profile.txt.template"
OUTPUT_CONAN_PROFILE="$build_dir/profile.txt"

if [ -n "$CROSS_TARGET" ]; then
    # ── Cross-compilation ───────────────────────────────────────────────
    echo "Cross-compiling for target: $CROSS_TARGET"

    case "$CROSS_TARGET" in
        linux-aarch64)
            CROSS_CC="aarch64-linux-gnu-gcc"
            CROSS_CXX="aarch64-linux-gnu-g++"
            ARCH_NAME="armv8"
            SYSTEM_PROCESSOR="aarch64"
            OS_NAME="Linux"
            SYSTEM_NAME="Linux"
            COMPILER_NAME="gcc"
            LIBCXX_NAME="libstdc++11"
            ;;
        *)
            echo "Unsupported cross-compilation target: $CROSS_TARGET"
            echo "Supported targets: linux-aarch64"
            exit 1
            ;;
    esac

    if ! command -v "$CROSS_CC" >/dev/null 2>&1; then
        echo "Cross-compiler $CROSS_CC not found. Install it with:"
        echo "  sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
        exit 1
    fi

    CROSS_VERSION_FULL=$("$CROSS_CC" -dumpfullversion -dumpversion 2>/dev/null)
    IFS='.' read -r major minor patch <<< "$CROSS_VERSION_FULL"
    CROSS_VERSION="$major.$minor"

    echo "Cross-compiler: $CROSS_CC ($CROSS_VERSION_FULL)"

    # Generate the native build profile (used by Conan for build-time tools)
    source "$KICKCAT_DIR/tools/setup/detect_compiler.sh"

    OUTPUT_BUILD_PROFILE="$build_dir/profile_build.txt"
    sed \
      -e "s|@OS_NAME@|Linux|g" \
      -e "s|@ARCH_NAME@|x86_64|g" \
      -e "s|@COMPILER_NAME@|gcc|g" \
      -e "s|@LIBCXX_NAME@|libstdc++11|g" \
      -e "s|@MAJOR_VERSION@|${GREATEST_VERSION}|g" \
      -e "s|@BINARY_PATH_CC@|$(command -v $GREATEST_CC)|g" \
      -e "s|@BINARY_PATH_CXX@|$(command -v $GREATEST_CXX)|g" \
      "$TEMPLATE_CONAN_PROFILE" > "$OUTPUT_BUILD_PROFILE"

    # Generate the host profile (targets the cross-compilation platform)
    sed \
      -e "s|@OS_NAME@|${OS_NAME}|g" \
      -e "s|@ARCH_NAME@|${ARCH_NAME}|g" \
      -e "s|@COMPILER_NAME@|${COMPILER_NAME}|g" \
      -e "s|@LIBCXX_NAME@|${LIBCXX_NAME}|g" \
      -e "s|@MAJOR_VERSION@|${CROSS_VERSION}|g" \
      -e "s|@BINARY_PATH_CC@|$(command -v $CROSS_CC)|g" \
      -e "s|@BINARY_PATH_CXX@|$(command -v $CROSS_CXX)|g" \
      "$TEMPLATE_CONAN_PROFILE" > "$OUTPUT_CONAN_PROFILE"

    # Generate cmake toolchain for cross-compilation
    sed \
      -e "s|@SYSTEM_NAME@|${SYSTEM_NAME}|g" \
      -e "s|@BINARY_PATH_CC@|$(command -v $CROSS_CC)|g" \
      -e "s|@BINARY_PATH_CXX@|$(command -v $CROSS_CXX)|g" \
      "$TEMPLATE_CMAKE_TOOLCHAIN" > "$OUTPUT_CMAKE_TOOLCHAIN"

    cat >> "$OUTPUT_CMAKE_TOOLCHAIN" <<CROSS_EOF
set(CMAKE_SYSTEM_PROCESSOR ${SYSTEM_PROCESSOR})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
CROSS_EOF

    echo ""
    echo "=== Host profile (target: $CROSS_TARGET) ==="
    cat "$OUTPUT_CONAN_PROFILE"
    echo ""
    echo "=== Build profile (native) ==="
    cat "$OUTPUT_BUILD_PROFILE"

    CONAN_PROFILE_ARGS="-pr:h $OUTPUT_CONAN_PROFILE -pr:b $OUTPUT_BUILD_PROFILE"

else
    # ── Native build ────────────────────────────────────────────────────
    source "$KICKCAT_DIR/tools/setup/detect_compiler.sh"

    # Detect architecture and map to Conan arch value
    detect_conan_arch() {
        local machine
        machine=$(uname -m)
        case "$machine" in
            x86_64)             echo "x86_64" ;;
            aarch64|arm64)      echo "armv8" ;;
            armv7l)             echo "armv7hf" ;;
            armv6l)             echo "armv6" ;;
            i686|i386)          echo "x86" ;;
            *)
                echo "Unknown architecture: $machine. Aborting..." >&2
                exit 1
                ;;
        esac
    }

    ARCH_NAME=$(detect_conan_arch)
    echo "Detected architecture: $(uname -m) -> Conan arch: $ARCH_NAME"

    # Generate cmake toolchain
    sed \
      -e "s|@SYSTEM_NAME@|Linux|g" \
      -e "s|@BINARY_PATH_CC@|$(command -v $GREATEST_CC)|g" \
      -e "s|@BINARY_PATH_CXX@|$(command -v $GREATEST_CXX)|g" \
      "$TEMPLATE_CMAKE_TOOLCHAIN" > "$OUTPUT_CMAKE_TOOLCHAIN"

    # Generate conan profile
    sed \
      -e "s|@OS_NAME@|Linux|g" \
      -e "s|@ARCH_NAME@|${ARCH_NAME}|g" \
      -e "s|@COMPILER_NAME@|gcc|g" \
      -e "s|@LIBCXX_NAME@|libstdc++11|g" \
      -e "s|@MAJOR_VERSION@|${GREATEST_VERSION}|g" \
      -e "s|@BINARY_PATH_CC@|$(command -v $GREATEST_CC)|g" \
      -e "s|@BINARY_PATH_CXX@|$(command -v $GREATEST_CXX)|g" \
      "$TEMPLATE_CONAN_PROFILE" > "$OUTPUT_CONAN_PROFILE"

    cat "$OUTPUT_CONAN_PROFILE"

    CONAN_PROFILE_ARGS="-pr $OUTPUT_CONAN_PROFILE -pr:b $OUTPUT_CONAN_PROFILE"
fi

# Prepare debug dependencies only for local call, not in CI
if [[ $CIBUILDWHEEL != "1" ]]; then
    conan install "$KICKCAT_DIR/conan/conanfile_linux.txt" -of="$build_dir" $CONAN_PROFILE_ARGS --build=missing -s build_type=Debug
fi
conan install "$KICKCAT_DIR/conan/conanfile_linux.txt" -of="$build_dir" $CONAN_PROFILE_ARGS --build=missing -s build_type=Release
