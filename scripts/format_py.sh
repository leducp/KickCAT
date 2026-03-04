#!/bin/bash

# KickCAT Python Formatting and Linting Script
# This script is used to check or apply Python formatting and linting using ruff.

# Get the project root directory
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Directories to check by default
DEFAULT_DIRS=("py_bindings" "tools" "scripts" "simulation" "unit")

# Check if ruff is installed
if ! command -v ruff &> /dev/null; then
    echo "ERROR: ruff is not installed or not in PATH."
    exit 1
fi

print_usage() {
    echo "Usage: $0 [check|apply] [paths...]"
    echo ""
    echo "Commands:"
    echo "  check   Verify that files are correctly formatted and linted (used in CI)."
    echo "  apply   Apply formatting and lint fixes to files in place."
    echo ""
    echo "If no paths are provided, it defaults to: ${DEFAULT_DIRS[*]}"
}

get_search_paths() {
    local search_paths=("$@")
    if [ ${#search_paths[@]} -eq 0 ]; then
        search_paths=("${DEFAULT_DIRS[@]}")
    fi

    local final_paths=()
    for path in "${search_paths[@]}"; do
        if [[ ! -e "$path" && -e "$PROJECT_ROOT/$path" ]]; then
            final_paths+=("$PROJECT_ROOT/$path")
        elif [[ -e "$path" ]]; then
            final_paths+=("$path")
        fi
    done
    echo "${final_paths[@]}"
}

check_format() {
    local paths
    paths=$(get_search_paths "$@")

    echo "Checking Python formatting and linting with ruff..."

    # Check formatting
    if ! ruff format --check $paths; then
        echo "Failure: Python files are not correctly formatted. Run '$0 apply' to fix."
        return 1
    fi

    # Check linting
    if ! ruff check $paths; then
        echo "Failure: Python linting issues found."
        return 1
    fi

    echo "Success: All Python files are correctly formatted and linted."
    return 0
}

apply_format() {
    local paths
    paths=$(get_search_paths "$@")

    echo "Applying Python formatting and lint fixes with ruff..."

    # Apply formatting
    ruff format $paths

    # Apply lint fixes
    ruff check --fix $paths

    echo "Done: Python formatting and lint fixes applied."
}

# Main execution logic
case "$1" in
    check)
        shift
        check_format "$@"
        ;;
    apply)
        shift
        apply_format "$@"
        ;;
    *)
        print_usage
        exit 1
        ;;
esac
