<p align="center">
  <img src="kickCAT_logo_light_mode.png#only-light" alt="KickCAT" width="300">
  <img src="kickCAT_logo_dark_mode.png#only-dark" alt="KickCAT" width="300">
</p>

<p align="center">
  <strong>A lightweight, efficient EtherCAT master/slave stack for embedded and real-time systems</strong>
</p>

KickCAT is a thin EtherCAT stack designed to be embedded in larger software with
efficiency in mind. It provides **both master and slave** implementations in one
codebase and runs on Linux (including RT-PREEMPT), Windows, PikeOS, and NuttX.

CoE (CANopen over EtherCAT) is fully supported on both sides. Beyond the stack
itself, KickCAT ships a complete **software ESC and network emulator**, GUIs, and
CLI tooling, so a bus can be developed, configured, and tested without physical
hardware.

## Getting started

Build the stack and tools (Linux):

```bash
./scripts/configure.sh build --with=unit_tests
./scripts/setup_build.sh build
cd build && make -j
```

Then run a master against an emulated slave -- no hardware, single process:

```bash
./build/examples/master/simulated_bus/simulated_bus \
    -f examples/slave/lan9252/freedom-k64f/nuttx/freedom-k64f.xml -t Board
```

## Where to go next

- [Feature support matrix](FEATURES.md) -- exactly what is and isn't supported
- [Building and installing](BUILDING.md) -- build, install, Python bindings, Windows/PikeOS
- [Hardware guide](HARDWARE.md) -- slave firmware, flashing, end-to-end walkthrough
- [Simulation and emulation](SIMULATION.md) -- software ESC and network emulator
- [Tools and GUIs](TOOLS.md) -- KickUI, EEPROM editor, CLI utilities
- [Real-time performance tuning](PERFORMANCE.md) -- Linux RT setup
- [ENI network configuration (experimental)](ENI.md)
- [Architecture overview](architecture.md)
