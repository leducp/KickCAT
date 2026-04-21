#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Accepted tag scheme: vX.Y.Z or vX.Y.Z-rcN. Anything else -> 0.0.0.
# pyproject.toml / pkg-config get the raw tag (PyPI will reject -rc forms,
# which is fine: those builds aren't release artifacts anyway).
# CMake's project(VERSION) needs strict X[.Y[.Z[.T]]], so the rc number
# rides in the tweak slot: vX.Y.Z-rcN -> X.Y.Z.N.
VERSION="0.0.0"
CMAKE_VERSION="0.0.0"

if git -C "$ROOT_DIR" rev-parse --git-dir > /dev/null 2>&1; then
    TAG=$(git -C "$ROOT_DIR" describe --tags --exact-match 2>/dev/null || true)

    if [[ "$TAG" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-rc([0-9]+))?$ ]]; then
        VERSION="$TAG"
        BASE="${BASH_REMATCH[1]}"
        RC="${BASH_REMATCH[3]:-}"
        if [[ -n "$RC" ]]; then
            CMAKE_VERSION="${BASE}.${RC}"
        else
            CMAKE_VERSION="$BASE"
        fi
    fi
fi

# Anchor to the actual version lines so re-runs (e.g. checkout of a different
# tag on an already-substituted tree) overwrite the prior value instead of
# no-op'ing because the '0.0.0' literal is gone.
sed -i -E "s/^version = \".+\"$/version = \"${VERSION}\"/" \
    "$ROOT_DIR/pyproject.toml"
sed -i -E "s/^project\(KickCAT VERSION [^ )]+/project(KickCAT VERSION ${CMAKE_VERSION}/" \
    "$ROOT_DIR/CMakeLists.txt"

echo "VERSION set to: $VERSION (CMake: $CMAKE_VERSION)"
