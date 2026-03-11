#!/bin/bash
# hw_test_orchestrator.sh
# Semi-automated deployment and long-duration hardware test for KickCAT releases.
#
# Usage: ./release/hw_test_orchestrator.sh -m <master_bin> -f <freedom_zip> -x <xmc_zip> -a <pi_addr> [-i <interface>] [-d <duration_sec>]

set -euo pipefail

# Force C locale for numeric operations to ensure dots are used as decimal separators
export LC_NUMERIC=C

# --- ANSI Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }

# --- Defaults ---
INTERFACE="eth0"
DURATION=259200         # 72 hours
LOGGING_INTERVAL=600    # Log to file every 10 minutes
SAMPLE_INTERVAL=5       # Sample CPU/MEM every 5 seconds
WINDOW_SIZE=12          # Rolling average window (12 samples = 60s)
PI_ADDR=""
MASTER_INPUT=""
FREEDOM_INPUT=""
XMC_INPUT=""
PI_USER="pi"
RESULT_FILE="hw_test_results.txt"
PI_PID=""

TEMP_DIR=$(mktemp -d)

# --- Cleanup ---
cleanup() {
    local exit_code=$?
    trap - INT TERM EXIT

    echo ""
    log_warn "Cleaning up..."

    if [ -n "$PI_PID" ] && [ -n "$PI_ADDR" ]; then
        log_info "Stopping master process on Pi (PID: $PI_PID)..."
        ssh "${PI_USER}@${PI_ADDR}" "sudo kill $PI_PID 2>/dev/null || true"

        log_info "Recovering master logs from Pi..."
        if scp "${PI_USER}@${PI_ADDR}:~/hw_test.log" "$TEMP_DIR/hw_test_master.log" 2>/dev/null; then
            echo -e "\n--- Master Process Logs (Synchronized) ---" >> "$RESULT_FILE"
            cat "$TEMP_DIR/hw_test_master.log" >> "$RESULT_FILE"
            log_success "Master logs merged to $RESULT_FILE"
        else
            log_warn "Could not recover master logs from Pi."
        fi
    fi

    [ -d "$TEMP_DIR" ] && rm -rf "$TEMP_DIR"
    exit $exit_code
}

trap cleanup INT TERM EXIT

# --- Usage ---
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -m, --master <zip/bin>        Master test bench artifact (local path)"
    echo "  -f, --freedom <zip>           Freedom-K64F firmware artifact (local path)"
    echo "  -x, --xmc <zip>               XMC4800-Relax firmware artifact (local path)"
    echo "  -a, --addr <ip>               Raspberry Pi address"
    echo "  -i, --interface <name>        Pi network interface (default: eth0)"
    echo "  -d, --duration <sec>          Test duration in seconds (default: 21600 / 6h)"
    echo "  -l, --logging-interval <sec>  Logging interval in seconds (default: 300 / 5min)"
    echo "  -u, --user <name>             Pi SSH user (default: pi)"
    exit 1
}

# --- Argument Parsing ---
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -m|--master)           MASTER_INPUT="$2";    shift ;;
        -f|--freedom)          FREEDOM_INPUT="$2";   shift ;;
        -x|--xmc)              XMC_INPUT="$2";       shift ;;
        -a|--addr)             PI_ADDR="$2";         shift ;;
        -i|--interface)        INTERFACE="$2";       shift ;;
        -d|--duration)         DURATION="$2";        shift ;;
        -l|--logging-interval) LOGGING_INTERVAL="$2"; shift ;;
        -u|--user)             PI_USER="$2";         shift ;;
        *) usage ;;
    esac
    shift
done

if [[ -z "$MASTER_INPUT" || -z "$FREEDOM_INPUT" || -z "$XMC_INPUT" || -z "$PI_ADDR" ]]; then
    log_error "Missing required arguments."
    usage
fi

# --- Helpers ---
setup_ssh_access() {
    log_info "Checking SSH access to ${PI_USER}@${PI_ADDR}..."

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
        cat "$key_file.pub" | ssh "${PI_USER}@${PI_ADDR}" \
            "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
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

# Returns "<cpu> <rss_kb>" for PI_PID in one SSH call
get_stats() {
    ssh "${PI_USER}@${PI_ADDR}" "ps -p $PI_PID -o %cpu=,rss=" 2>/dev/null || echo "ERROR ERROR"
}

# Averages a bash array of numbers
get_avg_bash() {
    local arr=("$@")
    printf "%s\n" "${arr[@]}" | awk '{sum+=$1} END {if (NR>0) print sum/NR; else print 0}'
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

# 5. Start Remote Process
log_info "Step 4: Starting hardware test for $DURATION seconds..."
PI_PID=$(ssh "${PI_USER}@${PI_ADDR}" "sudo nohup ~/hw_test_bench -i $INTERFACE -s 2 > ~/hw_test.log 2>&1 & echo \$!")

if [ -z "$PI_PID" ]; then
    log_error "Failed to start master process on Pi."
    exit 1
fi

log_info "Master process started on Pi with PID: $PI_PID, sleeping for 60s while the communication stack calibrates..."

sleep 60 # Wait a bit for the communication stack to calibrate before starting measurements

# --- Calibration Phase ---
log_info "Starting calibration phase (12 samples x 5s = 60s)..."
CAL_SAMPLES_CPU=()
CAL_SAMPLES_MEM=()

for i in {1..12}; do
    read -r cpu_val mem_val < <(get_stats)
    if [[ "$cpu_val" == "ERROR" ]]; then
        log_error "Process died during calibration!"
        exit 1
    fi
    CAL_SAMPLES_CPU+=("$cpu_val")
    CAL_SAMPLES_MEM+=("$mem_val")
    echo -ne "\rCalibration: $((i*100/12))% complete... (CPU: ${cpu_val}%, MEM: ${mem_val} KB)"
    sleep "$SAMPLE_INTERVAL"
done
echo ""

BASELINE_CPU=$(get_avg_bash "${CAL_SAMPLES_CPU[@]}")
BASELINE_MEM=$(get_avg_bash "${CAL_SAMPLES_MEM[@]}")
THRESHOLD_CPU=$(awk "BEGIN {print $BASELINE_CPU * 1.05}")
THRESHOLD_MEM=$(awk "BEGIN {print $BASELINE_MEM * 1.05}")

log_success "Calibration complete."
log_info "Baselines  - CPU: ${BASELINE_CPU}%, MEM: ${BASELINE_MEM} KB"
log_info "Thresholds - CPU: ${THRESHOLD_CPU}%, MEM: ${THRESHOLD_MEM} KB"

# --- Result File Header ---
{
    echo "Test started: $(date)"
    echo "Target Duration: $DURATION seconds"
    echo "Profile Interval: $LOGGING_INTERVAL seconds"
    echo "Sample Interval: $SAMPLE_INTERVAL seconds (rolling window: $WINDOW_SIZE samples)"
    echo "Baselines  - CPU: ${BASELINE_CPU}%, MEM: ${BASELINE_MEM} KB"
    echo "Thresholds - CPU: ${THRESHOLD_CPU}%, MEM: ${THRESHOLD_MEM} KB"
    echo "---------------------------------------------------"
} > "$RESULT_FILE"

# --- Monitoring Loop ---
START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION))
LAST_LOG_TIME=$START_TIME

CPU_WINDOW=()
MEM_WINDOW=()

while [ "$(date +%s)" -lt "$END_TIME" ]; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    REMAINING=$((END_TIME - CURRENT_TIME))

    # Sample stats
    read -r SAMPLE_CPU SAMPLE_MEM < <(get_stats)
    if [[ "$SAMPLE_CPU" == "ERROR" || -z "${SAMPLE_CPU// /}" ]]; then
        log_error "Master process on Pi has stopped unexpectedly!"
        echo "$(date): [CRASH] Process $PI_PID not found." >> "$RESULT_FILE"
        exit 1
    fi

    # Update rolling window
    CPU_WINDOW+=("$SAMPLE_CPU")
    MEM_WINDOW+=("$SAMPLE_MEM")
    if [ "${#CPU_WINDOW[@]}" -gt "$WINDOW_SIZE" ]; then
        CPU_WINDOW=("${CPU_WINDOW[@]:1}")
        MEM_WINDOW=("${MEM_WINDOW[@]:1}")
    fi

    AVG_CPU=$(get_avg_bash "${CPU_WINDOW[@]}")
    AVG_MEM=$(get_avg_bash "${MEM_WINDOW[@]}")

    # Threshold check
    CPU_BREACH=$(awk "BEGIN {print ($AVG_CPU > $THRESHOLD_CPU) ? 1 : 0}")
    MEM_BREACH=$(awk "BEGIN {print ($AVG_MEM > $THRESHOLD_MEM) ? 1 : 0}")

    if [[ "$CPU_BREACH" == "1" || "$MEM_BREACH" == "1" ]]; then
        echo ""
        log_error "Threshold breached! Avg CPU: ${AVG_CPU}% (limit: ${THRESHOLD_CPU}%), Avg MEM: ${AVG_MEM} KB (limit: ${THRESHOLD_MEM} KB)"
        echo "$(date): [THRESHOLD BREACH] Avg CPU: ${AVG_CPU}%, Avg MEM: ${AVG_MEM} KB" >> "$RESULT_FILE"
        exit 1
    fi

    # Log at LOGGING_INTERVAL
    TIME_SINCE_LOG=$((CURRENT_TIME - LAST_LOG_TIME))
    if [ "$TIME_SINCE_LOG" -ge "$LOGGING_INTERVAL" ]; then
        TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
        LOG_LINE="[$TIMESTAMP] Elapsed: ${ELAPSED}s | Remaining: ${REMAINING}s | Avg CPU: ${AVG_CPU}% | Avg MEM: ${AVG_MEM} KB (window: ${#CPU_WINDOW[@]}/${WINDOW_SIZE})"
        echo "$LOG_LINE" >> "$RESULT_FILE"
        echo -ne "\r$LOG_LINE"
        LAST_LOG_TIME=$CURRENT_TIME
    fi

    sleep "$SAMPLE_INTERVAL"
done

log_success "Hardware test completed successfully."
log_info "Final results saved to $RESULT_FILE"
log_info "Full test log available on Pi at ~/hw_test.log"
