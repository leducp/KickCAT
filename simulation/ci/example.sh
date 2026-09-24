#!/bin/bash
# Run a master example against the simulator and check that it reaches OPERATIONAL.
# Usage: simulation/ci/example.sh <example binary or .py> <slave config>...
# Logs: simulator.log, test_output.log in the current directory.

example=$1
shift

./build/simulation/network_simulator -i tap:server -s "$@" > simulator.log 2>&1 &
sim_pid=$!
sleep 2

if [[ "$example" == *.py ]]; then
    timeout 15s python "$example" -i tap:client > test_output.log 2>&1 || true
else
    timeout 15s "$example" -i tap:client > test_output.log 2>&1 || true
fi

kill "$sim_pid" || true

echo "--- Summary of Test Output (last 20 lines) ---"
tail -n 20 test_output.log
echo "----------------------------------------------"

if grep -qi "operational" test_output.log; then
    echo "SUCCESS: Reached OPERATIONAL state."
else
    echo "FAILURE: Did NOT reach OPERATIONAL state."
    exit 1
fi

# The example shall have run for a while, not exited right away
if [ "$(wc -l < test_output.log)" -gt 15 ]; then
    echo "SUCCESS: Runtime output detected."
else
    echo "FAILURE: Output too short, the example likely did not run."
    exit 1
fi

echo "Test passed!"
