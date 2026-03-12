#!/bin/bash
# hw_test_deploy.sh
# Deploys KickCAT hardware test components: slave firmware and/or master binary.
# Run from PC with slave boards connected via J-Link.
#
# Usage: ./release/hw_test_deploy.sh [options]
# At least one of -m, -f, -x must be specified. -a is required when deploying master (-m).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$REPO_ROOT/scripts/lib/log.sh"

# --- Defaults ---
PI_ADDR=""
MASTER_INPUT=""
FREEDOM_INPUT=""
XMC_INPUT=""
PI_USER="pi"
PI_WORK_DIR="hw_test"

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

# --- Usage ---
usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Deploys slave firmware (via J-Link) and/or master binary to the Raspberry Pi."
    echo "At least one of -m, -f, -x must be specified."
    echo ""
    echo "Options:"
    echo "  -m, --master <zip/bin>   Master test bench artifact (zip or binary)"
    echo "  -f, --freedom <zip>      Freedom-K64F firmware artifact (zip)"
    echo "  -x, --xmc <zip>          XMC4800-Relax firmware artifact (zip)"
    echo "  -a, --addr <ip>          Raspberry Pi address (required with -m)"
    echo "  -u, --user <name>        Pi SSH user (default: pi)"
    echo ""
    echo "Examples:"
    echo "  Deploy everything:"
    echo "    $0 -m build.zip -f freedom.zip -x xmc.zip -a 192.168.0.93"
    echo "  Deploy master only:"
    echo "    $0 -m build.zip -a 192.168.0.93"
    echo "  Flash freedom only:"
    echo "    $0 -f freedom.zip"
    echo "  Flash both slaves, no master:"
    echo "    $0 -f freedom.zip -x xmc.zip"
    exit 1
}

# --- Argument Parsing ---
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -m|--master)  MASTER_INPUT="$2";  shift ;;
        -f|--freedom) FREEDOM_INPUT="$2"; shift ;;
        -x|--xmc)     XMC_INPUT="$2";     shift ;;
        -a|--addr)    PI_ADDR="$2";       shift ;;
        -u|--user)    PI_USER="$2";       shift ;;
        *) usage ;;
    esac
    shift
done

if [[ -z "$MASTER_INPUT" && -z "$FREEDOM_INPUT" && -z "$XMC_INPUT" ]]; then
    error "At least one of -m, -f, -x must be specified."
    usage
fi

if [[ -n "$MASTER_INPUT" && -z "$PI_ADDR" ]]; then
    error "-a/--addr is required when deploying master (-m)."
    usage
fi

# --- Helpers ---
setup_ssh_access() {
    info "Checking SSH access to ${PI_USER}@${PI_ADDR}..."

    if ssh -q -o BatchMode=yes -o ConnectTimeout=5 "${PI_USER}@${PI_ADDR}" exit; then
        success "Passwordless SSH access already configured."
        return 0
    fi

    warn "Passwordless access not detected. Setting up SSH keys..."
    local key_file="$HOME/.ssh/id_rsa"
    if [ ! -f "$key_file" ]; then
        info "Generating new SSH key: $key_file"
        ssh-keygen -t rsa -b 4096 -f "$key_file" -N ""
    fi

    info "Deploying public key to Pi... (Password may be required)"
    if command -v ssh-copy-id >/dev/null 2>&1; then
        ssh-copy-id -i "$key_file.pub" "${PI_USER}@${PI_ADDR}"
    else
        cat "$key_file.pub" | ssh "${PI_USER}@${PI_ADDR}" \
            "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
    fi

    if ssh -q -o BatchMode=yes -o ConnectTimeout=5 "${PI_USER}@${PI_ADDR}" exit; then
        success "SSH setup successful!"
    else
        error "SSH setup failed. Please check manual connectivity."
        exit 1
    fi
}

validate_file() {
    local input="$1"
    if [ ! -f "$input" ]; then
        error "File not found: $input"
        exit 1
    fi
    realpath "$input"
}

# --- Execution ---

# 1. SSH setup (only needed when deploying master to Pi)
if [[ -n "$MASTER_INPUT" ]]; then
    step "SSH setup"
    setup_ssh_access
fi

# 2. Validate provided artifacts
step "Validating artifacts"
if [[ -n "$MASTER_INPUT" ]]; then
    MASTER_PATH=$(validate_file "$MASTER_INPUT")
    info "Master: $MASTER_PATH"
fi
if [[ -n "$FREEDOM_INPUT" ]]; then
    FREEDOM_ZIP=$(validate_file "$FREEDOM_INPUT")
    info "Freedom: $FREEDOM_ZIP"
fi
if [[ -n "$XMC_INPUT" ]]; then
    XMC_ZIP=$(validate_file "$XMC_INPUT")
    info "XMC: $XMC_ZIP"
fi
success "All artifacts found."

# 3. Flash slave firmware (only the ones requested)
if [[ -n "$FREEDOM_INPUT" || -n "$XMC_INPUT" ]]; then
    step "Flashing slave firmware"
    if [[ -n "$FREEDOM_INPUT" ]]; then
        "$REPO_ROOT/scripts/deploy_artifacts.sh" freedom-k64f "$FREEDOM_ZIP"
    fi
    if [[ -n "$XMC_INPUT" ]]; then
        "$REPO_ROOT/scripts/deploy_artifacts.sh" xmc4800-relax "$XMC_ZIP"
    fi
    success "Slave firmware deployed."
fi

# 4. Deploy master to Pi (only if requested)
if [[ -n "$MASTER_INPUT" ]]; then
    step "Preparing master binary"
    if [[ "$MASTER_PATH" == *.zip ]]; then
        info "Extracting master binary from zip..."
        unzip -q "$MASTER_PATH" -d "$TEMP_DIR/master_unzipped"
        MASTER_BIN=$(find "$TEMP_DIR/master_unzipped" -name "hw_test_bench" -type f | head -n 1)
        if [ -z "$MASTER_BIN" ]; then
            error "Could not find 'hw_test_bench' in zip."
            exit 1
        fi
    else
        MASTER_BIN="$MASTER_PATH"
    fi

    step "Deploying to Pi (${PI_USER}@${PI_ADDR})"
    ssh "${PI_USER}@${PI_ADDR}" "mkdir -p ~/${PI_WORK_DIR}/lib"

    scp "$MASTER_BIN" "${PI_USER}@${PI_ADDR}:~/${PI_WORK_DIR}/hw_test_bench"
    scp "$SCRIPT_DIR/hw_test_run.sh" "${PI_USER}@${PI_ADDR}:~/${PI_WORK_DIR}/hw_test_run.sh"
    scp "$SCRIPT_DIR/hw_test_status.sh" "${PI_USER}@${PI_ADDR}:~/${PI_WORK_DIR}/hw_test_status.sh"
    scp "$REPO_ROOT/scripts/lib/log.sh" "${PI_USER}@${PI_ADDR}:~/${PI_WORK_DIR}/lib/log.sh"

    ssh "${PI_USER}@${PI_ADDR}" "chmod +x ~/${PI_WORK_DIR}/hw_test_bench ~/${PI_WORK_DIR}/hw_test_run.sh ~/${PI_WORK_DIR}/hw_test_status.sh"

    step "Setting network capabilities"
    info "Applying cap_net_raw,cap_net_admin on hw_test_bench..."
    ssh "${PI_USER}@${PI_ADDR}" "sudo setcap 'cap_net_raw,cap_net_admin=+ep' ~/${PI_WORK_DIR}/hw_test_bench"
    success "Capabilities set."
fi

echo ""
printf "${GREEN}${BOLD}Done!${RESET} Deployment complete.\n"
echo ""
if [[ -n "$MASTER_INPUT" ]]; then
    info "Next steps:"
    info "  1. Disconnect the slave boards from this PC"
    info "  2. Connect them to the Raspberry Pi's EtherCAT network"
    info "  3. SSH into the Pi:  ssh ${PI_USER}@${PI_ADDR}"
    info "  4. Start the test:   cd ~/${PI_WORK_DIR} && ./hw_test_run.sh -i eth0"
    info "  5. Check status:     cd ~/${PI_WORK_DIR} && ./hw_test_status.sh"
    info "     Or from this PC:  $SCRIPT_DIR/hw_test_status.sh -a ${PI_ADDR}"
    echo ""
fi
