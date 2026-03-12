#!/bin/bash
# Shared logging helpers for KickCAT scripts.
# Source this file at the top of any script:
#
#   SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
#   source "$SCRIPT_DIR/../scripts/lib/log.sh"   # adjust path as needed

BOLD="\033[1m"
GREEN="\033[0;32m"
YELLOW="\033[0;33m"
RED="\033[0;31m"
CYAN="\033[0;36m"
RESET="\033[0m"

info()    { printf "${CYAN}[INFO]${RESET}  %s\n" "$*"; }
success() { printf "${GREEN}[ OK ]${RESET}  %s\n" "$*"; }
warn()    { printf "${YELLOW}[WARN]${RESET}  %s\n" "$*" >&2; }
error()   { printf "${RED}[ERR ]${RESET}  %s\n" "$*" >&2; }

step() {
    printf "\n${BOLD}==> %s${RESET}\n" "$*"
}
