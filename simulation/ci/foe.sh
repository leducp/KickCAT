#!/bin/bash
# FoE transfers against the simulator: write a file with the CLI, read it back with the CLI and with Python.
# Logs: simulator.log, test_output.log in the current directory.
set -eo pipefail

./build/simulation/network_simulator -i tap:server -s simulation/slave_configs/ecat402-drive-foe.json > simulator.log 2>&1 &
sim_pid=$!
trap 'kill $sim_pid || true' EXIT
sleep 2

# Several packets and a partial last one
head -c 20000 /dev/urandom > payload.bin

timeout 30s ./build/tools/foe -i tap:client -s 0 -c write -f payload.bin | tee test_output.log
cmp payload.bin simulation/slave_configs/foe_files/payload.bin

timeout 30s ./build/tools/foe -i tap:client -s 0 -c read -f read_back.bin -n payload.bin | tee -a test_output.log
cmp payload.bin read_back.bin

timeout 30s python py_bindings/examples/foe.py -i tap:client -c read -f read_back_py.bin -n payload.bin | tee -a test_output.log
cmp payload.bin read_back_py.bin

echo "Test passed!"
