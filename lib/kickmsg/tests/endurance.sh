#!/bin/bash
set -euo pipefail
BINARY="$1"
DURATION_SECS="${2:-3600}"
END_TIME=$(($(date +%s) + DURATION_SECS))
PASS=0
FAIL=0
RUNS=0
REORDERS=0
echo "=== kickmsg endurance test ==="
echo "Binary: $BINARY"
echo "Duration: ${DURATION_SECS}s"
echo "Start: $(date)"
echo ""
while [ "$(date +%s)" -lt "$END_TIME" ]; do
    OUTPUT=$("$BINARY" 2>&1)
    RUNS=$((RUNS + 1))
    SUMMARY=$(echo "$OUTPUT" | grep "Summary:" | tail -1)
    RUN_PASS=$(echo "$SUMMARY" | grep -oP '\d+ passed' | grep -oP '\d+')
    RUN_FAIL=$(echo "$SUMMARY" | grep -oP '\d+ failed' | grep -oP '\d+')
    RUN_REORDER=$(echo "$OUTPUT" | { grep -c "REORDER" || true; })
    PASS=$((PASS + RUN_PASS))
    FAIL=$((FAIL + RUN_FAIL))
    REORDERS=$((REORDERS + RUN_REORDER))
    ELAPSED=$(($(date +%s) - END_TIME + DURATION_SECS))
    printf "\r[%ds/%ds] runs=%d pass=%d fail=%d reorders=%d" \
           "$ELAPSED" "$DURATION_SECS" "$RUNS" "$PASS" "$FAIL" "$REORDERS"
    if [ "$RUN_FAIL" -gt 0 ]; then
        echo ""
        echo "$OUTPUT" | grep -E "REORDER|FAIL|WARN"
    fi
done
echo ""
echo ""
echo "=== FINAL RESULTS ==="
echo "Duration: ${DURATION_SECS}s"
echo "Runs: $RUNS"
echo "Scenarios passed: $PASS"
echo "Scenarios failed: $FAIL"
echo "Total reorders: $REORDERS"
echo "End: $(date)"
if [ "$FAIL" -eq 0 ]; then
    echo "VERDICT: ALL CLEAN"
else
    echo "VERDICT: FAILURES DETECTED"
    exit 1
fi
