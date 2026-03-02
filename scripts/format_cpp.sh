#!/bin/bash

# KickCAT C++ Formatting Script
# This script is used to check or apply C++ formatting using clang-format.

# Get the project root directory (one level up from the scripts directory)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_FORMAT_FILE="$PROJECT_ROOT/.clang-format"

# Directories to check by default
# TODO: Add Complete check later DEFAULT_DIRS=("lib" "examples" "tools" "unit" "simulation" "py_bindings")
DEFAULT_DIRS=("examples" "simulation")
# Extensions to check
EXTENSIONS=("cc" "h" "cpp" "hpp")

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo "ERROR: clang-format is not installed."
    exit 1
fi

# Check if .clang-format exists
if [[ ! -f "$CLANG_FORMAT_FILE" ]]; then
    echo "ERROR: .clang-format configuration file not found at $CLANG_FORMAT_FILE"
    exit 1
fi

print_usage() {
    echo "Usage: $0 [check|apply] [paths...]"
    echo ""
    echo "Commands:"
    echo "  check   Verify that files are correctly formatted (used in CI)."
    echo "  apply   Apply formatting to files in place."
    echo ""
    echo "If no paths are provided, it defaults to: ${DEFAULT_DIRS[*]}"
}

# Function to find all C++ files in the given paths
find_files() {
    local search_paths=("$@")
    if [ ${#search_paths[@]} -eq 0 ]; then
        search_paths=("${DEFAULT_DIRS[@]}")
    fi

    local find_args=()
    for i in "${!search_paths[@]}"; do
        # Convert relative paths to absolute if they exist relative to PROJECT_ROOT
        if [[ ! -e "${search_paths[$i]}" && -e "$PROJECT_ROOT/${search_paths[$i]}" ]]; then
            search_paths[$i]="$PROJECT_ROOT/${search_paths[$i]}"
        fi
    done

    # Build the extension filter for find
    local name_filter=()
    for ext in "${EXTENSIONS[@]}"; do
        if [ ${#name_filter[@]} -gt 0 ]; then
            name_filter+=("-o")
        fi
        name_filter+=("-name" "*.$ext")
    done

    find "${search_paths[@]}" -type f \( "${name_filter[@]}" \) -not -path "*/build/*"
}

check_format() {
    local files_to_check
    files_to_check=$(find_files "$@")
    local exit_code=0
    local count=0
    local failed_files=()

    echo "Checking C++ formatting..."

    while read -r file; do
        if [ -z "$file" ]; then continue; fi
        ((count++))
        if ! clang-format --dry-run --Werror --style=file:"$CLANG_FORMAT_FILE" "$file" >/dev/null 2>&1; then
            echo "INCORRECT FORMAT: $file"
            failed_files+=("$file")
            exit_code=1
        fi
    done <<< "$files_to_check"

    if [ $exit_code -eq 0 ]; then
        echo "Success: $count files checked, all correctly formatted."
    else
        echo "Failure: ${#failed_files[@]} files need formatting."
    fi

    return $exit_code
}

apply_format() {
    local files_to_format
    files_to_format=$(find_files "$@")
    local count=0

    echo "Applying C++ formatting..."

    while read -r file; do
        if [ -z "$file" ]; then continue; fi
        ((count++))
        clang-format -i --style=file:"$CLANG_FORMAT_FILE" "$file"
        echo "Formatted: $file"
    done <<< "$files_to_format"

    echo "Done: $count files formatted."
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
