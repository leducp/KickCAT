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

## Capabilities

### Master stack

- Full EtherCAT State Machine (INIT, PRE-OP, SAFE-OP, OP)
- Process data (PDO) read/write
- CoE: SDO read/write (expedited, normal, segmented), SDO Information, Emergency
- Bus diagnostics with error counters
- Cable redundancy
- Consecutive writes (up to 255 datagrams in flight)
- Mailbox gateway (ETG.8200)
- Distributed Clock (experimental)
- AF_XDP socket backend on Linux (opt-in, lower latency)
- Python bindings

### Slave stack

- Full EtherCAT State Machine
- Process data read/write
- CoE: Object Dictionary, SDO (including segmented and SDO Information)
- ESC support: LAN9252 (SPI), XMC4800
- EEPROM provisioning tooling
- Conformance Test Tool (CTT) validated (WDC_FOOT)

### Mailbox protocols

CoE is fully supported on master and slave. FoE is experimental on master and
slave (file transfer, including firmware update in the Bootstrap state). EoE is planned. SoE, AoE,
and VoE are not currently on the roadmap (no maintainer hardware to test
against) -- contributions are welcome. See the
[feature support matrix](FEATURES.md) for the full breakdown.

### Tooling and GUIs

- **KickUI** -- ImGui bus dashboard: topology view, SDO/PDO/FoE panels, DS402 motor bench
- **EEPROM editor** -- ImGui structured SII/EEPROM editor
- **eeprom** -- CLI to read/write slave EEPROM
- **foe** -- CLI to read/write a file on a slave over FoE
- **scan_topology** -- enumerate slaves and per-port link status
- **check_network_stability** -- monitor packet loss/corruption over time (Linux)
- **od_generator** -- generate CoE Object Dictionary code from ESI files
- **ethercat_gui** -- PySide6 bus monitoring application

See [docs/TOOLS.md](TOOLS.md) for usage.

### Simulation and emulation

KickCAT includes a software EtherCAT Slave Controller (`EmulatedESC`: registers,
SyncManagers, FMMUs, DC clock, EEPROM) and a network fabric (`EmulatedNetwork`:
topology routing, redundancy, runtime wire break/heal). Run a master against it
either in-process (the `simulated_bus` example) or as a separate process
(`network_simulator` over a shared-memory TAP socket).

See [docs/SIMULATION.md](SIMULATION.md).

## Platform support

The master runs as a host OS process; the slave runs the stack on an RTOS
against an EtherCAT Slave Controller (ESC).

**Master (host OS):**

| OS         | Architecture | Status                                       |
|------------|--------------|----------------------------------------------|
| Linux      | x86-64       | Production; RT-PREEMPT recommended for real-time |
| PikeOS 5.1 | ARMv8        | Production                                   |
| Windows    | x86-64       | Tools and testing only (not real-time)       |

**Slave (NuttX RTOS + ESC):**

| ESC      | Interface  | Status                   |
|----------|------------|--------------------------|
| LAN9252  | SPI        | Production               |
| XMC4800  | Integrated | Production; CTT validated |

Reference boards: NXP Freedom K64F (LAN9252), Arduino Due with EasyCAT shield
(LAN9252), Infineon XMC4800 Relax Kit (XMC4800). See the
[hardware guide](HARDWARE.md).

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
- [Developer guide](guide/README.md) -- how EtherCAT works and how KickCAT implements it
