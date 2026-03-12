#!/bin/bash
# KickCAT Python Formatting and Linting Script
# Checks or applies Python formatting and linting using ruff.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/lib/log.sh"

DEFAULT_DIRS=("py_bindings" "tools" "scripts" "simulation" "unit")

if ! command -v ruff &> /dev/null; then
    error "ruff is not installed or not in PATH."
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

    step "Checking Python formatting"
    if ! ruff format --check $paths; then
        error "Python files are not correctly formatted. Run '$0 apply' to fix."
        return 1
    fi
    success "Formatting OK."

    step "Checking Python linting"
    if ! ruff check $paths; then
        error "Python linting issues found."
        return 1
    fi
    success "Linting OK."

    echo ""
    printf "${GREEN}${BOLD}Done!${RESET} All Python files are correctly formatted and linted.\n"
    return 0
}

apply_format() {
    local paths
    paths=$(get_search_paths "$@")

    step "Applying Python formatting"
    ruff format $paths
    success "Formatting applied."

    step "Applying Python lint fixes"
    ruff check --fix $paths
    success "Lint fixes applied."

    echo ""
    printf "${GREEN}${BOLD}Done!${RESET} Python formatting and lint fixes applied.\n"
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
