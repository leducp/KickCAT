#!/bin/bash
# hw_test_status.sh
# Displays the current state of a running KickCAT hardware test.
# Works locally on the Pi or remotely from a PC (via SSH).
#
# Usage: ./hw_test_status.sh [-a <pi_addr>] [-u <user>] [-f]

set -euo pipefail

PI_ADDR=""
PI_USER="pi"
FOLLOW=false
WORK_DIR="$(cd "$(dirname "$0")" && pwd)"

source "$WORK_DIR/lib/log.sh"

while [[ "$#" -gt 0 ]]; do
    case $1 in
        -a|--addr)   PI_ADDR="$2"; shift ;;
        -u|--user)   PI_USER="$2"; shift ;;
        -f|--follow) FOLLOW=true ;;
        -h|--help)
            echo "Usage: $0 [-a <pi_addr>] [-u <user>] [-f|--follow]"
            echo ""
            echo "Options:"
            echo "  -a, --addr <ip>    Pi address (remote mode)"
            echo "  -u, --user <name>  Pi SSH user (default: pi)"
            echo "  -f, --follow       Tail the monitor log continuously"
            exit 0
            ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# --- Remote mode: SSH to Pi and run this script there ---
if [[ -n "$PI_ADDR" ]]; then
    REMOTE_FLAGS=""
    if $FOLLOW; then REMOTE_FLAGS="-f"; fi
    ssh ${FOLLOW:+-t} "${PI_USER}@${PI_ADDR}" "~/hw_test/hw_test_status.sh $REMOTE_FLAGS"
    exit $?
fi

# --- Local mode ---
STATUS_FILE="$WORK_DIR/hw_test_status.txt"
MONITOR_LOG="$WORK_DIR/hw_test_monitor.log"
MONITOR_PID_FILE="$WORK_DIR/hw_test.pid"

if [ ! -f "$STATUS_FILE" ]; then
    warn "No test status found. No test has been run in $WORK_DIR."
    exit 1
fi

source "$STATUS_FILE"

# Check if processes are alive
MONITOR_ALIVE=false
BENCH_ALIVE=false

if [ -f "$MONITOR_PID_FILE" ]; then
    MONITOR_PID=$(cat "$MONITOR_PID_FILE")
    if kill -0 "$MONITOR_PID" 2>/dev/null; then
        MONITOR_ALIVE=true
    fi
fi

if [ -n "${BENCH_PID:-}" ]; then
    if kill -0 "$BENCH_PID" 2>/dev/null; then
        BENCH_ALIVE=true
    fi
fi

# Compute times
CURRENT_TIME=$(date +%s)
if [[ "$STATE" == "COMPLETED" || "$STATE" == "CRASHED" || "$STATE" == "STOPPED" || "$STATE" == "THRESHOLD_BREACH" ]]; then
    ELAPSED=$((LAST_UPDATE - START_TIME))
    REMAINING=0
else
    ELAPSED=$((CURRENT_TIME - START_TIME))
    REMAINING=$((START_TIME + DURATION - CURRENT_TIME))
    if [ "$REMAINING" -lt 0 ]; then
        REMAINING=0
    fi
fi

format_duration() {
    local secs=$1
    local days=$((secs / 86400))
    local hours=$(( (secs % 86400) / 3600 ))
    local mins=$(( (secs % 3600) / 60 ))
    if [ "$days" -gt 0 ]; then
        printf "%dd %dh %dm" "$days" "$hours" "$mins"
    elif [ "$hours" -gt 0 ]; then
        printf "%dh %dm" "$hours" "$mins"
    else
        printf "%dm" "$mins"
    fi
}

# Determine display state and color
case "$STATE" in
    RUNNING)
        if $MONITOR_ALIVE && $BENCH_ALIVE; then
            STATE_COLOR="${GREEN}"
            STATE_DISPLAY="RUNNING"
        elif ! $BENCH_ALIVE; then
            STATE_COLOR="${RED}"
            STATE_DISPLAY="RUNNING (bench process dead!)"
        else
            STATE_COLOR="${YELLOW}"
            STATE_DISPLAY="RUNNING (monitor process missing)"
        fi
        ;;
    COMPLETED)
        STATE_COLOR="${GREEN}"
        STATE_DISPLAY="COMPLETED"
        ;;
    CRASHED)
        STATE_COLOR="${RED}"
        STATE_DISPLAY="CRASHED"
        ;;
    STOPPED)
        STATE_COLOR="${YELLOW}"
        STATE_DISPLAY="STOPPED"
        ;;
    THRESHOLD_BREACH)
        STATE_COLOR="${RED}"
        STATE_DISPLAY="THRESHOLD BREACH"
        ;;
    CALIBRATED)
        STATE_COLOR="${CYAN}"
        STATE_DISPLAY="CALIBRATED (starting monitoring...)"
        ;;
    *)
        STATE_COLOR="${YELLOW}"
        STATE_DISPLAY="$STATE"
        ;;
esac

# Progress bar
if [ "$DURATION" -gt 0 ]; then
    PCT=$((ELAPSED * 100 / DURATION))
    if [ "$PCT" -gt 100 ]; then PCT=100; fi
    BAR_WIDTH=30
    FILLED=$((PCT * BAR_WIDTH / 100))
    EMPTY=$((BAR_WIDTH - FILLED))
    PROGRESS_BAR=$(printf "%${FILLED}s" | tr ' ' '#')$(printf "%${EMPTY}s" | tr ' ' '-')
else
    PCT=0
    PROGRESS_BAR="------------------------------"
fi

# Display
echo ""
printf "  ${BOLD}KickCAT Hardware Test Status${RESET}\n"
printf "  ${BOLD}════════════════════════════════════════${RESET}\n"
printf "  State:       ${STATE_COLOR}%s${RESET}\n" "$STATE_DISPLAY"
echo "  Started:     $(date -d "@$START_TIME" "+%Y-%m-%d %H:%M:%S" 2>/dev/null || date -r "$START_TIME" "+%Y-%m-%d %H:%M:%S" 2>/dev/null || echo "epoch $START_TIME")"
echo "  Elapsed:     $(format_duration $ELAPSED) / $(format_duration $DURATION) (${PCT}%)"
echo "  Remaining:   $(format_duration $REMAINING)"
echo "  Progress:    [${PROGRESS_BAR}] ${PCT}%"
echo ""
echo "  Interface:   $INTERFACE | Slaves: $SLAVES"
echo ""
echo "  ── Resource Baselines ──"
echo "  CPU baseline:  ${BASELINE_CPU}%   (threshold: ${THRESHOLD_CPU}%)"
echo "  MEM baseline:  ${BASELINE_MEM} KB (threshold: ${THRESHOLD_MEM} KB)"
echo ""
echo "  ── Current Averages ──"
echo "  CPU average:   ${AVG_CPU}%"
echo "  MEM average:   ${AVG_MEM} KB"

if [ -n "${RESULT:-}" ]; then
    echo ""
    echo "  ── Result ──"
    printf "  ${STATE_COLOR}%s${RESET}\n" "$RESULT"
fi

echo ""

# Recent log entries
if [ -f "$MONITOR_LOG" ]; then
    LOG_LINES=$(tail -5 "$MONITOR_LOG")
    if [ -n "$LOG_LINES" ]; then
        echo "  ── Recent Log ──"
        while IFS= read -r line; do
            echo "  $line"
        done <<< "$LOG_LINES"
        echo ""
    fi
fi

# Follow mode
if $FOLLOW; then
    if [ -f "$MONITOR_LOG" ]; then
        echo "  ── Following $MONITOR_LOG (Ctrl+C to stop) ──"
        tail -f "$MONITOR_LOG"
    else
        echo "  No monitor log found."
        exit 1
    fi
fi
