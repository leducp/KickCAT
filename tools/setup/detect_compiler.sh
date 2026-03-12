#!/bin/bash
# Detect the highest-versioned GCC on this system.
# Meant to be sourced; exports GREATEST_CC, GREATEST_CXX, GREATEST_VERSION.

# Ensure log helpers are available (idempotent if already sourced)
if ! command -v info &>/dev/null; then
    _DETECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    source "$_DETECT_DIR/../../scripts/lib/log.sh"
fi

info "Scanning for GCC installations..."

declare -A GCC_MAP

GCC_LIST=$(compgen -c | grep -E '^gcc(-[0-9]+(\.[0-9]+)*)?$' | sort -u)

if [ -z "$GCC_LIST" ]; then
    error "No GCC installations found!"
    exit 1
fi

for gcc_bin in $GCC_LIST; do
    if command -v "$gcc_bin" &>/dev/null; then
        version=$("$gcc_bin" -dumpfullversion -dumpversion 2>/dev/null)
        if [[ -z "$version" ]]; then
            version=$("$gcc_bin" --version | head -n1 | awk '{print $3}')
        fi

        info "  Found: $gcc_bin ($version)"
        GCC_MAP["$version"]=$gcc_bin
    fi
done

GREATEST_VERSION_FULL=$(printf "%s\n" "${!GCC_MAP[@]}" | sort -V | tail -n1)
GREATEST_CC="${GCC_MAP[$GREATEST_VERSION_FULL]}"

IFS='.' read -r major minor patch <<< "$GREATEST_VERSION_FULL"
GREATEST_VERSION="$major.$minor"

if command -v "${GREATEST_CC/gcc/g++}" &>/dev/null; then
    GREATEST_CXX="${GREATEST_CC/gcc/g++}"
else
    GREATEST_CXX="g++"
fi

success "Selected GCC $GREATEST_VERSION ($GREATEST_CC/$GREATEST_CXX)"
