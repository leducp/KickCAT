#!/bin/bash
# ecat_master_bench.sh
# Benchmark suite for EtherCAT Master Stacks (Black-box testing)
#
# Usage: ./ecat_master_bench.sh [options] -- <command_to_run_master>
# Example: sudo ./tools/ecat_master_bench.sh -d 30 -i eth0 -- ./build/examples/master/easycat/easycat_example eth0

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$REPO_ROOT/scripts/lib/log.sh"

# Defaults
DURATION=10
OUTPUT_DIR="bench_results_$(date +%Y%m%d_%H%M%S)"
INTERFACE="eth0"
DO_LATENCY=true
DO_PROFILE=true
DO_MEMORY=true
USE_VALGRIND=false


# Check for root (often required for perf/cyclictest/ethercat)
if [ "$EUID" -ne 0 ]; then
  warn "This script often requires root privileges for perf, cyclictest, and raw socket access."
  warn "Please run with sudo if you encounter permission errors."
  sleep 2
fi

# Parse arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -d|--duration) DURATION="$2"; shift ;;
        -o|--output) OUTPUT_DIR="$2"; shift ;;
        -i|--interface) INTERFACE="$2"; shift ;;
        --no-latency) DO_LATENCY=false ;;
        --no-profile) DO_PROFILE=false ;;
        --no-memory) DO_MEMORY=false ;;
        --valgrind) USE_VALGRIND=true ;;
        --) shift; break ;; # End of script options, rest is command
        -h|--help)
            echo "Usage: $0 [options] -- <master_command>"
            echo "Options:"
            echo "  -d, --duration <sec>  Duration for the test run (default: 10)"
            echo "  -o, --output <dir>    Output directory (default: bench_results_DATE)"
            echo "  --no-latency          Skip latency/perf stat test"
            echo "  --no-profile          Skip perf record test"
            echo "  --no-memory           Skip memory profiling"
            echo "  --valgrind            Force use of valgrind (massif) instead of heaptrack"
            exit 0
            ;;
        *) error "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

CMD=("$@")

if [ ${#CMD[@]} -eq 0 ]; then
    error "No command provided to run the master."
    info "Usage: $0 [options] -- <command>"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
info "Results dir:    $OUTPUT_DIR"
info "Target command: ${CMD[*]}"
info "Duration:       ${DURATION}s"

# Dependency Checks
check_cmd() {
    if ! command -v "$1" &> /dev/null; then
        warn "$1 could not be found. Related tests will be skipped."
        return 1
    fi
    return 0
}

# Phase 1: Latency & CPU Stats
if [ "$DO_LATENCY" = true ]; then
    step "Phase 1: Latency (cyclictest) & CPU Stats (perf stat)"
    
    if check_cmd cyclictest; then
        info "Running cyclictest in background..."
        # Run cyclictest on all CPUs, priority 99, interval 1000us
        cyclictest --smp --priority=95 --interval=1000 --duration="${DURATION}s" \
            > "$OUTPUT_DIR/cyclictest.log" &
        CYCLIC_PID=$!
    else
        error "cyclictest not found, skipping system latency measurement."
    fi

    if check_cmd perf; then
        info "Running Master with perf stat..."
        # We use 'timeout' to enforce the duration on the master command if it doesn't self-terminate
        perf stat -o "$OUTPUT_DIR/perf_stat.txt" -d -d -d \
            timeout --signal=SIGINT "${DURATION}s" "${CMD[@]}" &> /dev/null || true
    else
        info "Running Master (no perf)..."
        timeout --signal=SIGINT "${DURATION}s" "${CMD[@]}" &> /dev/null || true
    fi

    # Wait for cyclictest if it's still running
    if [ -n "$CYCLIC_PID" ]; then
        wait "$CYCLIC_PID" || true
    fi
    
    success "Phase 1 Complete."
fi

# Phase 2: Hotspots (Perf Record)
if [ "$DO_PROFILE" = true ] && check_cmd perf; then
    step "Phase 2: Hotspot Profiling (perf record)"
    info "Recording for $DURATION seconds..."
    
    # Record call graphs
    perf record -g -o "$OUTPUT_DIR/perf.data" -- \
        timeout --signal=SIGINT "${DURATION}s" "${CMD[@]}" &> /dev/null || true
        
    if [ -f "$OUTPUT_DIR/perf.data" ]; then
        info "Generating report..."
        perf report -i "$OUTPUT_DIR/perf.data" --stdio > "$OUTPUT_DIR/perf_report.txt"
        success "Profile saved to $OUTPUT_DIR/perf.data"
        info "Tip: Use 'perf report -i $OUTPUT_DIR/perf.data' to view interactively"
        info "Tip: Use 'flamegraph.pl' (if installed) to generate visualization."
    else
        error "perf.data not generated."
    fi
fi

# Phase 3: Memory Profiling
if [ "$DO_MEMORY" = true ]; then
    step "Phase 3: Memory Profiling"
    
    # Prefer heaptrack, fallback to valgrind
    if [ "$USE_VALGRIND" = false ] && command -v heaptrack &> /dev/null; then
        info "Using heaptrack..."
        heaptrack -o "$OUTPUT_DIR/heaptrack" -- \
            timeout --signal=SIGINT "${DURATION}s" "${CMD[@]}" &> /dev/null || true
        success "Heaptrack data saved to prefix $OUTPUT_DIR/heaptrack"
        
    elif check_cmd valgrind; then
        info "Using valgrind (massif)..."
        warn "Valgrind significantly slows down execution. Real-time behavior will be distorted."
        
        valgrind --tool=massif --massif-out-file="$OUTPUT_DIR/massif.out" \
            timeout --signal=SIGINT "${DURATION}s" "${CMD[@]}" &> /dev/null || true
            
        success "Massif output saved to $OUTPUT_DIR/massif.out"
        info "View with: ms_print $OUTPUT_DIR/massif.out"
    else
        error "Neither heaptrack nor valgrind found. Skipping memory profile."
    fi
fi

echo ""
printf "${GREEN}${BOLD}Done!${RESET} Benchmark complete.\n"
echo ""
info "Results stored in: $OUTPUT_DIR"
if [ -f "$OUTPUT_DIR/cyclictest.log" ]; then
    info "Max Latency (cyclictest):"
    grep "Max Latencies" "$OUTPUT_DIR/cyclictest.log" || tail -n 5 "$OUTPUT_DIR/cyclictest.log"
fi
