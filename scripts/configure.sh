#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KICKCAT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/lib/log.sh"
source "$SCRIPT_DIR/lib/build_options.sh"

run_menu() {
    local tool=""
    if command -v whiptail >/dev/null 2>&1; then
        tool="whiptail"
    elif command -v dialog >/dev/null 2>&1; then
        tool="dialog"
    else
        error "Neither whiptail nor dialog is installed"
        info "Install with: sudo apt install whiptail"
        info "Use -ni flag with --with/--without options instead"
        echo ""
        usage
        exit 1
    fi

    local items=()
    for key in $(echo "${!CONFIG[@]}" | tr ' ' '\n' | sort); do
        local state="OFF"
        [[ "${CONFIG[$key]}" == "ON" ]] && state="ON"
        items+=("$key" "${OPT_DESCRIPTIONS[$key]}" "$state")
    done

    local result
    result=$("$tool" --title "KickCAT Build Options" \
        --checklist "Toggle with SPACE, confirm with ENTER" \
        20 60 ${#OPT_DESCRIPTIONS[@]} \
        "${items[@]}" \
        3>&1 1>&2 2>&3) || return 1

    for key in "${!CONFIG[@]}"; do
        CONFIG[$key]=OFF
    done
    for selected in $result; do
        selected="${selected%\"}"
        selected="${selected#\"}"
        CONFIG[$selected]=ON
    done
}

usage() {
    cat <<EOF
Usage: $0 <build_dir> [OPTIONS]

Manage KickCAT build options. Options are stored in <build_dir>/.buildconfig
and consumed by setup_build.sh (Conan + CMake).

Arguments:
  build_dir                 Build directory (created if it does not exist)

Options:
  --with=<feature>        Enable a feature (use 'all' for every feature)
  --without=<feature>     Disable a feature (use 'all' for every feature)
  --show                  Print current configuration
  --reset                 Reset all options to defaults
  -ni, --non-interactive  Disable interactive menu

Available features:
EOF
    for key in $(echo "${!OPT_DEFAULTS[@]}" | tr ' ' '\n' | sort); do
        printf "  %-22s %s (default: %s)\n" "$key" "${OPT_DESCRIPTIONS[$key]}" "${OPT_DEFAULTS[$key]}"
    done
    cat <<EOF

Examples:
  $0 build --with=unit_tests --without=simulation
  $0 build --without=all --with=unit_tests
  $0 build --show
  $0 build --reset
EOF
}

# Parse build_dir (first positional argument)
build_dir=""
ARGS=()

for arg in "$@"; do
    if [[ -z "$build_dir" && "$arg" != -* ]]; then
        build_dir="$arg"
    else
        ARGS+=("$arg")
    fi
done

if [[ -z "$build_dir" ]]; then
    usage
    exit 1
fi

mkdir -p "$build_dir"
CONFIG_FILE="$build_dir/.buildconfig"

load_buildconfig

ACTION=""
MODIFIED=false
INTERACTIVE=true

set -- "${ARGS[@]+"${ARGS[@]}"}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --show)
            ACTION="show"
            shift
            ;;
        -ni|--non-interactive)
            INTERACTIVE=false
            shift
            ;;
        --reset)
            for key in "${!OPT_DEFAULTS[@]}"; do
                CONFIG[$key]="${OPT_DEFAULTS[$key]}"
            done
            MODIFIED=true
            info "Reset all options to defaults"
            shift
            ;;
        --with=*)
            feature="${1#--with=}"
            if [[ "$feature" == "all" ]]; then
                for key in "${!CONFIG[@]}"; do
                    CONFIG[$key]=ON
                done
            elif [[ -v "OPT_DEFAULTS[$feature]" ]]; then
                CONFIG[$feature]=ON
            else
                error "Unknown feature: $feature"
                echo ""
                usage
                exit 1
            fi
            MODIFIED=true
            shift
            ;;
        --without=*)
            feature="${1#--without=}"
            if [[ "$feature" == "all" ]]; then
                for key in "${!CONFIG[@]}"; do
                    CONFIG[$key]=OFF
                done
            elif [[ -v "OPT_DEFAULTS[$feature]" ]]; then
                CONFIG[$feature]=OFF
            else
                error "Unknown feature: $feature"
                echo ""
                usage
                exit 1
            fi
            MODIFIED=true
            shift
            ;;
        *)
            error "Unknown argument: $1"
            echo ""
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$ACTION" ]] && ! $MODIFIED && $INTERACTIVE; then
    if run_menu; then
        MODIFIED=true
    else
        info "Menu cancelled, no changes made"
        exit 0
    fi
fi

if [[ -z "$ACTION" ]] && ! $MODIFIED; then
    usage
    exit 0
fi

if $MODIFIED; then
    save_buildconfig
    success "Configuration saved to $CONFIG_FILE"
fi

if [[ "$ACTION" == "show" ]] || $MODIFIED; then
    show_buildconfig
fi
