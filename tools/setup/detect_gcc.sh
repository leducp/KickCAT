#!/bin/bash

echo "Scanning for GCC installations..."

declare -A GCC_MAP

# Collect all GCC executables
GCC_LIST=$(compgen -c | grep -E '^gcc(-[0-9]+(\.[0-9]+)*)?$' | sort -u)

if [ -z "$GCC_LIST" ]; then
    echo "!!! No GCC installations found !!!"
    exit 1
fi

for gcc_bin in $GCC_LIST; do
    if command -v "$gcc_bin" &>/dev/null; then
        version=$("$gcc_bin" -dumpfullversion -dumpversion 2>/dev/null)
        if [[ -z "$version" ]]; then
            version=$("$gcc_bin" --version | head -n1 | awk '{print $3}')
        fi

        echo " * Found: $gcc_bin ($version)"
        GCC_MAP["$version"]=$gcc_bin
    fi
done

# Find greatest version using version sorting
GREATEST_VERSION_FULL=$(printf "%s\n" "${!GCC_MAP[@]}" | sort -V | tail -n1)
GREATEST_CC="${GCC_MAP[$GREATEST_VERSION_FULL]}"

# Convert full version -> major.minor
IFS='.' read -r major minor patch <<< "$GREATEST_VERSION_FULL"
GREATEST_VERSION="$major.$minor"

# Match g++
if command -v "${GREATEST_CC/gcc/g++}" &>/dev/null; then
    GREATEST_CXX="${GREATEST_CC/gcc/g++}"
else
    GREATEST_CXX="g++"
fi

echo
echo "--> Greatest GCC version detected: $GREATEST_VERSION ($GREATEST_CC/$GREATEST_CXX)"
echo ""
