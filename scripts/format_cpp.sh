#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/lib/log.sh"

DEFAULT_DIRS=("lib" "examples")

if ! command -v uncrustify &> /dev/null; then
    error "uncrustify is not installed or not in PATH."
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

get_filenames() {
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
        else
            warn "Skipping non-existing path $path"
        fi
    done

    local filenames=()
    for path in "${final_paths[@]}"; do
        while IFS= read -r -d '' filename; do
            filenames+=("$filename")
        done < <(find "$path" -name '*.cc' -print0)
    done
    echo "${filenames[@]}"
}

check_format() {
    local filenames
    filenames=$(get_filenames "$@")

    step "Checking C++ formatting"
    local total_files=0
    local bad_files=0
    for filename in $filenames; do
        info "Checking $filename ..."
        total_files=$((total_files + 1))
        if uncrustify -c "$PROJECT_ROOT/uncrustify.cfg" --check "$filename"; then
            info "OK"
        else
            bad_files=$((bad_files + 1))
            warn "format not ok"
            diff -u "$filename" <(uncrustify -c "$PROJECT_ROOT/uncrustify.cfg" -f "$filename")
        fi
    done

    if [ $bad_files -gt 0 ]; then
        error "${bad_files} out of ${total_files} C++ files are not correctly formatted. Run '$0 apply' to fix."
        return 1
    else
        success "All ${total_files} C++ files are correctly formatted."
        return 0
    fi
}

apply_format() {
    local filenames
    filenames=$(get_filenames "$@")

    step "Applying C++ formatting"
    for filename in $filenames; do
        info "Formatting $filename"
        uncrustify -c "$PROJECT_ROOT/uncrustify.cfg" --replace --no-backup "$filename"
    done
    success "Formatting applied."
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
