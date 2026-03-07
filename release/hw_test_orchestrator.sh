#!/bin/bash
# hw_test_orchestrator.sh
# Semi-automated deployment and long-duration hardware test for KickCAT releases.
#
# Usage: ./release/hw_test_orchestrator.sh -m <master_bin> -f <freedom_zip> -x <xmc_zip> -a <pi_addr> [-i <interface>] [-d <duration_sec>]

set -euo pipefail

# ANSI Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }

# Default values
INTERFACE="eth0"
DURATION=21600 # 6 hours
PROFILE_INTERVAL=300 # 5 minutes
PI_ADDR=""
MASTER_INPUT=""
FREEDOM_INPUT=""
XMC_INPUT=""
PI_USER="pi"
RESULT_FILE="hw_test_results.txt"
PI_PID=""

# Temporary directory for artifacts
TEMP_DIR=$(mktemp -d)

# Robust cleanup function
cleanup() {
    local exit_code=$?
    trap - INT TERM EXIT # Disable traps to avoid recursion
    
    echo "" # New line for visual separation
    log_warn "Cleaning up..."
    
    if [ -n "$PI_PID" ] && [ -n "$PI_ADDR" ]; then
        log_info "Stopping master process on Pi (PID: $PI_PID)..."
        ssh "${PI_USER}@${PI_ADDR}" "sudo kill $PI_PID 2>/dev/null || true"
    fi
    
    if [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
    
    exit $exit_code
}

trap cleanup INT TERM EXIT

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -m, --master <zip/bin>    Master test bench artifact (local path)"
    echo "  -f, --freedom <zip>       Freedom-K64F firmware artifact (local path)"
    echo "  -x, --xmc <zip>           XMC4800-Relax firmware artifact (local path)"
    echo "  -a, --addr <ip>           Raspberry Pi address"
    echo "  -i, --interface <name>    Pi network interface (default: eth0)"
    echo "  -d, --duration <sec>      Test duration in seconds (default: 21600 / 6h)"
    echo "  -p, --profile-interval <sec>  Interval for profiling logs (default: 300 / 5min)"
    echo "  -u, --user <name>         Pi SSH user (default: pi)"
    exit 1
}

# Argument parsing
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -m|--master) MASTER_INPUT="$2"; shift ;;
        -f|--freedom) FREEDOM_INPUT="$2"; shift ;;
        -x|--xmc) XMC_INPUT="$2"; shift ;;
        -a|--addr) PI_ADDR="$2"; shift ;;
        -i|--interface) INTERFACE="$2"; shift ;;
        -d|--duration) DURATION="$2"; shift ;;
        -p|--profile-interval) PROFILE_INTERVAL="$2"; shift ;;
        -u|--user) PI_USER="$2"; shift ;;
        *) usage ;;
    esac
    shift
done

if [[ -z "$MASTER_INPUT" || -z "$FREEDOM_INPUT" || -z "$XMC_INPUT" || -z "$PI_ADDR" ]]; then
    log_error "Missing required arguments."
    usage
fi

setup_ssh_access() {
    log_info "Checking SSH access to ${PI_USER}@${PI_ADDR}..."
    
    # Check for passwordless access
    if ssh -q -o BatchMode=yes -o ConnectTimeout=5 "${PI_USER}@${PI_ADDR}" exit; then
        log_success "Passwordless SSH access already configured."
        return 0
    fi

    log_warn "Passwordless access not detected. Setting up SSH keys..."
    
    local key_file="$HOME/.ssh/id_rsa"
    if [ ! -f "$key_file" ]; then
        log_info "Generating new SSH key: $key_file"
        ssh-keygen -t rsa -b 4096 -f "$key_file" -N ""
    fi

    log_info "Deploying public key to Pi... (Password may be required)"
    if command -v ssh-copy-id >/dev/null 2>&1; then
        ssh-copy-id -i "$key_file.pub" "${PI_USER}@${PI_ADDR}"
    else
        cat "$key_file.pub" | ssh "${PI_USER}@${PI_ADDR}" "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
    fi

    if ssh -q -o BatchMode=yes -o ConnectTimeout=5 "${PI_USER}@${PI_ADDR}" exit; then
        log_success "SSH setup successful!"
    else
        log_error "SSH setup failed. Please check manual connectivity."
        exit 1
    fi
}

validate_file() {
    local input="$1"
    if [ ! -f "$input" ]; then
        log_error "File not found: $input"
        exit 1
    fi
    echo "$(realpath "$input")"
}

# --- Execution Flow ---

# 1. SSH Setup
setup_ssh_access

# 2. Artifact Preparation
log_info "Step 1: Preparing artifacts..."
MASTER_ZIP=$(validate_file "$MASTER_INPUT")
FREEDOM_ZIP=$(validate_file "$FREEDOM_INPUT")
XMC_ZIP=$(validate_file "$XMC_INPUT")

if [[ "$MASTER_ZIP" == *.zip ]]; then
    log_info "Extracting master binary from zip..."
    unzip -q "$MASTER_ZIP" -d "$TEMP_DIR/master_unzipped"
    MASTER_BIN=$(find "$TEMP_DIR/master_unzipped" -name "hw_test_bench" -type f | head -n 1)
    if [ -z "$MASTER_BIN" ]; then
        log_error "Could not find 'hw_test_bench' in zip."
        exit 1
    fi
else
    MASTER_BIN="$MASTER_ZIP"
fi

# 3. Slave Deployment
log_info "Step 2: Deploying slave firmware..."
./scripts/deploy_artifacts.sh freedom-k64f "$FREEDOM_ZIP"
./scripts/deploy_artifacts.sh xmc4800-relax "$XMC_ZIP"
log_success "Slaves deployed."

# 4. Master Deployment
log_info "Step 3: Deploying master to Pi ($PI_ADDR)..."
scp "$MASTER_BIN" "${PI_USER}@${PI_ADDR}:~/hw_test_bench"
ssh "${PI_USER}@${PI_ADDR}" "chmod +x ~/hw_test_bench"
log_success "Master deployed."

# 5. Long-Duration Test Loop
log_info "Step 4: Starting hardware test for $DURATION seconds..."
log_info "Monitoring memory and status..."

# Start the remote process and capture its PID
PI_PID=$(ssh "${PI_USER}@${PI_ADDR}" "sudo nohup ~/hw_test_bench -i $INTERFACE -s 2 > ~/hw_test.log 2>&1 & echo \$!")

if [ -z "$PI_PID" ]; then
    log_error "Failed to start master process on Pi."
    exit 1
fi

log_info "Master process started on Pi with PID: $PI_PID"
echo "Test started: $(date)" > "$RESULT_FILE"
echo "Target Duration: $DURATION seconds" >> "$RESULT_FILE"
echo "---------------------------------------------------" >> "$RESULT_FILE"

START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION))

while [ $(date +%s) -lt $END_TIME ]; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    REMAINING=$((END_TIME - CURRENT_TIME))
    
    # Check if process is still running and get memory usage
    MEM_USAGE=$(ssh "${PI_USER}@${PI_ADDR}" "ps -o rss= -p $PI_PID" 2>/dev/null || echo "ERROR")
    
    if [[ "$MEM_USAGE" == "ERROR" || -z "${MEM_USAGE// /}" ]]; then
        log_error "Master process on Pi has stopped unexpectedly!"
        echo "$(date): [CRASH] Process $PI_PID not found." >> "$RESULT_FILE"
        exit 1
    fi

    TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
    LOG_LINE="[$TIMESTAMP] Elapsed: ${ELAPSED}s | Remaining: ${REMAINING}s | RSS: ${MEM_USAGE} KB"
    echo "$LOG_LINE" >> "$RESULT_FILE"
    echo -ne "\r$LOG_LINE"
    
    sleep "$PROFILE_INTERVAL"
done

log_success "\nHardware test completed successfully."
log_info "Final results saved to $RESULT_FILE"
log_info "Full test log available on Pi at ~/hw_test.log"
