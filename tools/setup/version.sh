#!/bin/bash

# Default version
VERSION="0.0.0"

# Check if we're inside a git repository
if git rev-parse --git-dir > /dev/null 2>&1; then
    # Try to get the exact tag for HEAD
    TAG=$(git describe --tags --exact-match 2>/dev/null)

    if [[ -n "$TAG" ]]; then
        # HEAD is exactly at a tag
        VERSION="$TAG"
    fi
fi

# Export the VERSION variable
export VERSION
echo "VERSION set to: $VERSION"
