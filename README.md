<p align="center">
    <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/kickCAT_logo_dark_mode.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/kickCAT_logo_light_mode.png">
    <img alt="KickCAT" width="300">
    </picture><br>
</p>

<p align="center">
  <strong>A lightweight, efficient EtherCAT master/slave stack for embedded and real-time systems</strong>
</p>

---

## Overview

KickCAT is a thin EtherCAT stack designed to be embedded in larger software with
efficiency in mind. It provides **both master and slave** implementations in one
codebase and runs on Linux (including RT-PREEMPT), Windows, PikeOS, and NuttX.

CoE (CANopen over EtherCAT) is fully supported on both sides. Beyond the stack
itself, KickCAT ships a complete **software ESC and network emulator**, GUIs, and
CLI tooling, so a bus can be developed, configured, and tested without physical
hardware.

For exactly what is and isn't supported, see the
[feature support matrix](docs/FEATURES.md).

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

CoE is fully supported on master and slave. FoE and EoE are planned. SoE, AoE,
and VoE are not currently on the roadmap (no maintainer hardware to test
against) -- contributions are welcome. See the
[feature support matrix](docs/FEATURES.md) for the full breakdown.

### Tooling and GUIs

- **KickUI** -- ImGui bus dashboard: topology view, SDO/PDO panels, DS402 motor bench
- **EEPROM editor** -- ImGui structured SII/EEPROM editor
- **eeprom** -- CLI to read/write/dump slave EEPROM
- **scan_topology** -- enumerate slaves and per-port link status
- **check_network_stability** -- monitor packet loss/corruption over time (Linux)
- **od_generator** -- generate CoE Object Dictionary code from ESI files
- **ethercat_gui** -- PySide6 bus monitoring application

See [docs/TOOLS.md](docs/TOOLS.md) for usage.

### Simulation and emulation

KickCAT includes a software EtherCAT Slave Controller (`EmulatedESC`: registers,
SyncManagers, FMMUs, DC clock, EEPROM) and a network fabric (`EmulatedNetwork`:
topology routing, redundancy, runtime wire break/heal). Run a master against it
either in-process (the `simulated_bus` example) or as a separate process
(`network_simulator` over a shared-memory TAP socket).

See [docs/SIMULATION.md](docs/SIMULATION.md).

## Getting started

Build the stack and tools (Linux):

```bash
./scripts/configure.sh build --with=unit_tests
./scripts/setup_build.sh build
cd build && make -j
```

Then run a master against an emulated slave -- no hardware, single process:

```bash
./build/examples/master/simulated_bus/simulated_bus -f "Beckhoff EL1xxx.xml" -t EL1008
```

Where to go next:

- Build, install, Python bindings, Windows/PikeOS -- [docs/BUILDING.md](docs/BUILDING.md)
- Real hardware: slave firmware, flashing, end-to-end walkthrough -- [docs/HARDWARE.md](docs/HARDWARE.md)
- Simulator and emulator -- [docs/SIMULATION.md](docs/SIMULATION.md)
- Tools and GUIs -- [docs/TOOLS.md](docs/TOOLS.md)
- Real-time performance tuning -- [docs/PERFORMANCE.md](docs/PERFORMANCE.md)

## Platform support

| Platform           | Role   | Status                                       |
|--------------------|--------|----------------------------------------------|
| Linux (x86-64)     | Master | Production; RT-PREEMPT recommended for real-time |
| Windows            | Master | Tools and testing only (not real-time)       |
| PikeOS 5.1 (ARMv8) | Master | Production                                   |
| NuttX RTOS         | Slave  | Production                                   |
| Arduino Due        | Slave  | via NuttX                                    |
| Infineon XMC4800   | Slave  | via NuttX, CTT validated                     |
| NXP Freedom K64F   | Slave  | via NuttX                                    |

## Documentation

- [Feature support matrix](docs/FEATURES.md)
- [Building and installing](docs/BUILDING.md)
- [Hardware guide](docs/HARDWARE.md)
- [Simulation and emulation](docs/SIMULATION.md)
- [Tools and GUIs](docs/TOOLS.md)
- [Real-time performance tuning](docs/PERFORMANCE.md)
- [Architecture overview](docs/architecture.md)
- [Release procedure](release/RELEASE_PROCEDURE.md)

### EtherCAT references

- [ETG official documentation](https://www.ethercat.org/en/downloads.html)
- [Beckhoff EtherCAT documentation](https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1257993099.html)
- [ESC datasheets](https://download.beckhoff.com/download/document/io/ethercat-development-products/)
- [EtherCAT Device Protocol Poster](https://www.ethercat.org/download/documents/EtherCAT_Device_Protocol_Poster.pdf)
- [ESC comparison](https://download.beckhoff.com/download/document/io/ethercat-development-products/an_esc_comparison_v2i7.pdf)

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for code-style
and PR conventions, and [docs/BUILDING.md](docs/BUILDING.md) for the development
build (unit tests and coverage).

KickCAT follows [Semantic Versioning](https://semver.org/). The release workflow
is documented in [release/RELEASE_PROCEDURE.md](release/RELEASE_PROCEDURE.md).

## License

[CeCILL-C](LICENSE)

## Support

- Issues: [GitHub Issues](https://github.com/leducp/KickCAT/issues)
- Discussions: [GitHub Discussions](https://github.com/leducp/KickCAT/discussions)
- Conan package: [conan-center-index](https://github.com/conan-io/conan-center-index/tree/master/recipes/kickcat)

Status: actively maintained and used in production systems. See
[Releases](https://github.com/leducp/KickCAT/releases) for the latest stable
version.
