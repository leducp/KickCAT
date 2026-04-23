#!/bin/bash
# Smoke-test the install flow: staged install + find_package and pkg-config
# consumer builds + a KICKCAT_INSTALL=OFF configure as a regression guard.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FIXTURE_DIR="$ROOT_DIR/test/install"

usage() {
    cat <<EOF
Usage: $0 <build_dir>

<build_dir> must be an already-configured, already-built KickCAT build tree
(e.g. the one produced by scripts/setup_build.sh + cmake --build).
EOF
}

if [[ $# -ne 1 ]]; then
    usage
    exit 1
fi

BUILD_DIR="$(cd "$1" && pwd)"
STAGE_DIR="$(mktemp -d /tmp/kickcat-stage-XXXXXX)"
CONSUMER_DIR="$(mktemp -d /tmp/kickcat-consumer-XXXXXX)"

trap 'rm -rf "$STAGE_DIR" "$CONSUMER_DIR"' EXIT

INSTALL_PREFIX="$(awk -F= '/^CMAKE_INSTALL_PREFIX:PATH=/{print $2}' "$BUILD_DIR/CMakeCache.txt")"
PREFIX="$STAGE_DIR$INSTALL_PREFIX"

echo "[test_install] staging install into $PREFIX"
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR" >/dev/null

echo "[test_install] CMake find_package consumer"
# $BUILD_DIR is in the path so Conan-provided transitive deps stay reachable.
cmake -S "$FIXTURE_DIR/cmake" -B "$CONSUMER_DIR/cmake" \
    -DCMAKE_PREFIX_PATH="$PREFIX;$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$CONSUMER_DIR/cmake" >/dev/null
"$CONSUMER_DIR/cmake/consumer" >/dev/null

echo "[test_install] pkg-config consumer"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
if pkg-config --libs --static kickcat >/dev/null 2>&1; then
    make -C "$FIXTURE_DIR/pkgconfig" BUILD_DIR="$CONSUMER_DIR/pkgconfig" >/dev/null
    "$CONSUMER_DIR/pkgconfig/consumer" >/dev/null
else
    echo "[test_install] SKIP pkg-config link test: transitive pkg-config dep not on PKG_CONFIG_PATH"
    echo "                 (e.g. install libtinyxml2-dev for the ESI_PARSER build)"
fi

echo "[test_install] KICKCAT_INSTALL=OFF configure"
cmake -S "$ROOT_DIR" -B "$CONSUMER_DIR/install-off" \
    -DKICKCAT_INSTALL=OFF \
    -DENABLE_ESI_PARSER=OFF \
    -DBUILD_MASTER_EXAMPLES=OFF \
    -DBUILD_SLAVE_EXAMPLES=OFF \
    -DBUILD_SIMULATION=OFF \
    -DBUILD_TOOLS=OFF >/dev/null

echo "[test_install] OK"
