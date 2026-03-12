#!/bin/bash
# hw_test_run.sh
# Runs and monitors the KickCAT hardware test bench on the Raspberry Pi.
# Self-daemonizes after calibration to survive SSH disconnection.
#
# Usage: ./hw_test_run.sh -i <interface> [-s <slaves>] [-d <duration>]

set -euo pipefail
export LC_NUMERIC=C

WORK_DIR="$(cd "$(dirname "$0")" && pwd)"

source "$WORK_DIR/lib/log.sh"

# --- State files ---
STATUS_FILE="$WORK_DIR/hw_test_status.txt"
MONITOR_LOG="$WORK_DIR/hw_test_monitor.log"
BENCH_LOG="$WORK_DIR/hw_test.log"
MONITOR_PID_FILE="$WORK_DIR/hw_test.pid"
BENCH_PID_FILE="$WORK_DIR/hw_test_bench.pid"

# --- Helpers ---
get_avg() {
    local arr=("$@")
    printf "%s\n" "${arr[@]}" | awk '{sum+=$1} END {if (NR>0) print sum/NR; else print 0}'
}

get_stats() {
    local pid="$1"
    ps -p "$pid" -o %cpu=,rss= 2>/dev/null || echo "ERROR ERROR"
}

update_status() {
    local state="$1"
    shift
    local tmp="${STATUS_FILE}.tmp"
    {
        echo "STATE='$state'"
        echo "START_TIME='$START_TIME'"
        echo "DURATION='$DURATION'"
        echo "INTERFACE='$INTERFACE'"
        echo "SLAVES='$SLAVES'"
        echo "BENCH_PID='$BENCH_PID'"
        echo "SAMPLE_INTERVAL='$SAMPLE_INTERVAL'"
        echo "LOGGING_INTERVAL='$LOGGING_INTERVAL'"
        echo "WINDOW_SIZE='$WINDOW_SIZE'"
        echo "BASELINE_CPU='$BASELINE_CPU'"
        echo "BASELINE_MEM='$BASELINE_MEM'"
        echo "THRESHOLD_CPU='$THRESHOLD_CPU'"
        echo "THRESHOLD_MEM='$THRESHOLD_MEM'"
        echo "AVG_CPU='${AVG_CPU:-0}'"
        echo "AVG_MEM='${AVG_MEM:-0}'"
        echo "LAST_UPDATE='$(date +%s)'"
        for arg in "$@"; do
            local key="${arg%%=*}"
            local val="${arg#*=}"
            echo "${key}='${val}'"
        done
    } > "$tmp"
    mv "$tmp" "$STATUS_FILE"
}

# ============================================================
# Stop mode: kill a running test gracefully.
# ============================================================
if [[ "${1:-}" == "--stop" ]]; then
    if [ ! -f "$MONITOR_PID_FILE" ]; then
        warn "No running test found (no PID file)."
        exit 0
    fi

    MONITOR_PID=$(cat "$MONITOR_PID_FILE")
    if ! kill -0 "$MONITOR_PID" 2>/dev/null; then
        warn "Monitor process $MONITOR_PID is not running."
        rm -f "$MONITOR_PID_FILE"

        if [ -f "$BENCH_PID_FILE" ]; then
            BENCH_PID=$(cat "$BENCH_PID_FILE")
            if kill -0 "$BENCH_PID" 2>/dev/null; then
                info "Killing orphaned hw_test_bench (PID $BENCH_PID)..."
                kill "$BENCH_PID" 2>/dev/null || true
            fi
            rm -f "$BENCH_PID_FILE"
        fi
        exit 0
    fi

    info "Stopping test (monitor PID: $MONITOR_PID)..."
    kill "$MONITOR_PID"

    # Wait for cleanup to finish (up to 5s)
    for i in {1..10}; do
        kill -0 "$MONITOR_PID" 2>/dev/null || break
        sleep 0.5
    done

    if [ -f "$STATUS_FILE" ]; then
        source "$STATUS_FILE"
        CURRENT_TIME=$(date +%s)
        ELAPSED=$((CURRENT_TIME - START_TIME))
        TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
        echo "[$TIMESTAMP] Test stopped by user after ${ELAPSED}s." >> "$MONITOR_LOG"

        # Rewrite status as STOPPED
        local_tmp="${STATUS_FILE}.tmp"
        {
            while IFS= read -r line; do
                if [[ "$line" == STATE=* ]]; then
                    echo "STATE='STOPPED'"
                else
                    echo "$line"
                fi
            done < "$STATUS_FILE"
            echo "RESULT='Test stopped by user after ${ELAPSED}s'"
        } > "$local_tmp"
        mv "$local_tmp" "$STATUS_FILE"
    fi

    success "Test stopped."
    exit 0
fi

# ============================================================
# Daemon mode: entered via internal re-exec after calibration.
# Reads all config from the status file, runs the monitoring
# loop, and updates status/logs until completion or failure.
# ============================================================
if [[ "${1:-}" == "--daemon" ]]; then
    source "$STATUS_FILE"
    BENCH_PID=$(cat "$BENCH_PID_FILE")

    echo $$ > "$MONITOR_PID_FILE"

    cleanup_daemon() {
        local exit_code=$?
        trap - INT TERM EXIT
        kill "$BENCH_PID" 2>/dev/null || true
        rm -f "$MONITOR_PID_FILE" "$BENCH_PID_FILE"
        exit $exit_code
    }
    trap cleanup_daemon INT TERM EXIT

    END_TIME=$((START_TIME + DURATION))
    LAST_LOG_TIME=$(date +%s)
    CPU_WINDOW=()
    MEM_WINDOW=()

    while [ "$(date +%s)" -lt "$END_TIME" ]; do
        CURRENT_TIME=$(date +%s)
        ELAPSED=$((CURRENT_TIME - START_TIME))
        REMAINING=$((END_TIME - CURRENT_TIME))

        read -r SAMPLE_CPU SAMPLE_MEM < <(get_stats "$BENCH_PID")
        if [[ "$SAMPLE_CPU" == "ERROR" || -z "${SAMPLE_CPU// /}" ]]; then
            TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
            echo "[$TIMESTAMP] [CRASH] hw_test_bench (PID $BENCH_PID) not found." >> "$MONITOR_LOG"
            AVG_CPU="${AVG_CPU:-0}"
            AVG_MEM="${AVG_MEM:-0}"
            update_status "CRASHED" "RESULT=hw_test_bench process died unexpectedly"
            exit 1
        fi

        CPU_WINDOW+=("$SAMPLE_CPU")
        MEM_WINDOW+=("$SAMPLE_MEM")
        if [ "${#CPU_WINDOW[@]}" -gt "$WINDOW_SIZE" ]; then
            CPU_WINDOW=("${CPU_WINDOW[@]:1}")
            MEM_WINDOW=("${MEM_WINDOW[@]:1}")
        fi

        AVG_CPU=$(get_avg "${CPU_WINDOW[@]}")
        AVG_MEM=$(get_avg "${MEM_WINDOW[@]}")

        CPU_BREACH=$(awk "BEGIN {print ($AVG_CPU > $THRESHOLD_CPU) ? 1 : 0}")
        MEM_BREACH=$(awk "BEGIN {print ($AVG_MEM > $THRESHOLD_MEM) ? 1 : 0}")

        if [[ "$CPU_BREACH" == "1" || "$MEM_BREACH" == "1" ]]; then
            TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
            echo "[$TIMESTAMP] [THRESHOLD BREACH] Avg CPU: ${AVG_CPU}% (limit: ${THRESHOLD_CPU}%), Avg MEM: ${AVG_MEM} KB (limit: ${THRESHOLD_MEM} KB)" >> "$MONITOR_LOG"
            update_status "THRESHOLD_BREACH" "RESULT=Avg CPU: ${AVG_CPU}% (limit: ${THRESHOLD_CPU}%), Avg MEM: ${AVG_MEM} KB (limit: ${THRESHOLD_MEM} KB)"
            exit 1
        fi

        update_status "RUNNING"

        TIME_SINCE_LOG=$((CURRENT_TIME - LAST_LOG_TIME))
        if [ "$TIME_SINCE_LOG" -ge "$LOGGING_INTERVAL" ]; then
            TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
            echo "[$TIMESTAMP] Elapsed: ${ELAPSED}s | Remaining: ${REMAINING}s | Avg CPU: ${AVG_CPU}% | Avg MEM: ${AVG_MEM} KB (window: ${#CPU_WINDOW[@]}/${WINDOW_SIZE})" >> "$MONITOR_LOG"
            LAST_LOG_TIME=$CURRENT_TIME
        fi

        sleep "$SAMPLE_INTERVAL"
    done

    TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
    echo "[$TIMESTAMP] Test completed successfully after ${DURATION}s." >> "$MONITOR_LOG"
    update_status "COMPLETED" "RESULT=Test completed successfully after ${DURATION}s"
    exit 0
fi

# ============================================================
# Interactive mode: parse args, start hw_test_bench, calibrate,
# then re-exec as daemon and return control to the user.
# ============================================================

# --- Defaults ---
INTERFACE=""
SLAVES=2
CPU_CORE=1               # isolated core (isolcpus=1-3)
DURATION=2592000         # 30 days
LOGGING_INTERVAL=600     # 10 minutes
SAMPLE_INTERVAL=5        # 5 seconds
WINDOW_SIZE=12           # 12 samples = 60s

# --- Usage ---
usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Starts hw_test_bench, calibrates CPU/MEM baselines, then daemonizes the"
    echo "monitoring loop so it survives SSH disconnection."
    echo ""
    echo "  $0 --stop                     Stop a running test"
    echo ""
    echo "Options:"
    echo "  -i, --interface <name>        Network interface (required)"
    echo "  -s, --slaves <n>              Expected slave count (default: 2)"
    echo "  -c, --cpu <core>              CPU core to pin hw_test_bench to (default: 1)"
    echo "  -d, --duration <sec>          Test duration (default: 2592000 / 30 days)"
    echo "  -l, --logging-interval <sec>  Log interval (default: 600 / 10 min)"
    echo "      --sample-interval <sec>   Sample interval (default: 5)"
    echo "      --window-size <n>         Rolling average window (default: 12)"
    exit 1
}

# --- Argument Parsing ---
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -i|--interface)        INTERFACE="$2";        shift ;;
        -s|--slaves)           SLAVES="$2";           shift ;;
        -c|--cpu)              CPU_CORE="$2";         shift ;;
        -d|--duration)         DURATION="$2";         shift ;;
        -l|--logging-interval) LOGGING_INTERVAL="$2"; shift ;;
        --sample-interval)     SAMPLE_INTERVAL="$2";  shift ;;
        --window-size)         WINDOW_SIZE="$2";      shift ;;
        *) usage ;;
    esac
    shift
done

if [[ -z "$INTERFACE" ]]; then
    error "Missing required argument: -i/--interface"
    usage
fi

# Check for already running test
if [ -f "$MONITOR_PID_FILE" ]; then
    EXISTING_PID=$(cat "$MONITOR_PID_FILE")
    if kill -0 "$EXISTING_PID" 2>/dev/null; then
        error "A test is already running (monitor PID: $EXISTING_PID)."
        error "Use ./hw_test_status.sh to check its state, or kill it first."
        exit 1
    fi
    rm -f "$MONITOR_PID_FILE"
fi

# Verify hw_test_bench exists
if [ ! -x "$WORK_DIR/hw_test_bench" ]; then
    error "hw_test_bench not found or not executable in $WORK_DIR"
    exit 1
fi

step "Starting hw_test_bench"
info "Interface: $INTERFACE, expected slaves: $SLAVES, pinned to CPU $CPU_CORE"
taskset -c "$CPU_CORE" "$WORK_DIR/hw_test_bench" -i "$INTERFACE" -s "$SLAVES" > "$BENCH_LOG" 2>&1 &
BENCH_PID=$!
echo "$BENCH_PID" > "$BENCH_PID_FILE"

sleep 2
if ! kill -0 "$BENCH_PID" 2>/dev/null; then
    error "hw_test_bench failed to start. Log output:"
    cat "$BENCH_LOG" >&2
    rm -f "$BENCH_PID_FILE"
    exit 1
fi

success "hw_test_bench started (PID: $BENCH_PID)"

step "EtherCAT stack calibration (60s)"
sleep 60

if ! kill -0 "$BENCH_PID" 2>/dev/null; then
    error "hw_test_bench died during stack calibration. Log output:"
    cat "$BENCH_LOG" >&2
    rm -f "$BENCH_PID_FILE"
    exit 1
fi

step "CPU/MEM calibration (${WINDOW_SIZE} samples x ${SAMPLE_INTERVAL}s)"
CAL_SAMPLES_CPU=()
CAL_SAMPLES_MEM=()

for ((i = 1; i <= WINDOW_SIZE; i++)); do
    read -r cpu_val mem_val < <(get_stats "$BENCH_PID")
    if [[ "$cpu_val" == "ERROR" ]]; then
        error "hw_test_bench died during calibration!"
        rm -f "$BENCH_PID_FILE"
        exit 1
    fi
    CAL_SAMPLES_CPU+=("$cpu_val")
    CAL_SAMPLES_MEM+=("$mem_val")
    echo -ne "\rCalibration: $((i * 100 / WINDOW_SIZE))% complete... (CPU: ${cpu_val}%, MEM: ${mem_val} KB)"
    sleep "$SAMPLE_INTERVAL"
done
echo ""

BASELINE_CPU=$(get_avg "${CAL_SAMPLES_CPU[@]}")
BASELINE_MEM=$(get_avg "${CAL_SAMPLES_MEM[@]}")
THRESHOLD_CPU=$(awk "BEGIN {print $BASELINE_CPU * 1.05}")
THRESHOLD_MEM=$(awk "BEGIN {print $BASELINE_MEM * 1.05}")

success "Calibration complete."
info "Baselines  - CPU: ${BASELINE_CPU}%, MEM: ${BASELINE_MEM} KB"
info "Thresholds - CPU: ${THRESHOLD_CPU}%, MEM: ${THRESHOLD_MEM} KB"

# --- Write initial status (read by daemon on re-exec) ---
START_TIME=$(date +%s)
AVG_CPU=0
AVG_MEM=0
update_status "CALIBRATED"

# --- Write monitor log header ---
{
    echo "Test started: $(date)"
    echo "Duration: $DURATION seconds"
    echo "Interface: $INTERFACE | Slaves: $SLAVES"
    echo "Sample Interval: ${SAMPLE_INTERVAL}s | Log Interval: ${LOGGING_INTERVAL}s | Window: ${WINDOW_SIZE}"
    echo "Baselines  - CPU: ${BASELINE_CPU}%, MEM: ${BASELINE_MEM} KB"
    echo "Thresholds - CPU: ${THRESHOLD_CPU}%, MEM: ${THRESHOLD_MEM} KB"
    echo "---------------------------------------------------"
} > "$MONITOR_LOG"

step "Detaching monitor"
nohup "$0" --daemon > /dev/null 2>&1 &
disown
success "Monitoring loop detached."

echo ""
printf "${GREEN}${BOLD}Done!${RESET} Test is running in the background.\n"
echo ""
info "hw_test_bench PID: $BENCH_PID"
info "Duration:          $DURATION seconds ($(awk "BEGIN {printf \"%.1f\", $DURATION/86400}") days)"
info "Status file:       $STATUS_FILE"
info "Monitor log:       $MONITOR_LOG"
info "Bench log:         $BENCH_LOG"
echo ""
info "Use ./hw_test_status.sh to check progress."
info "You can safely disconnect from SSH."
echo ""
