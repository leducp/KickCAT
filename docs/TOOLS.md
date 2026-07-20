# Tools and GUIs

KickCAT ships CLI utilities (in `tools/`), two ImGui-based GUIs, and a
PySide6 application. CLI tools build with the main project; the GUIs are opt-in.

```bash
cd build && make -j
ls tools/   # built CLI tools land here
```

## KickUI (GUI)

ImGui + GLFW dashboard for inspecting, configuring, and operating an EtherCAT
bus from the master side (`tools/kickui/`). It provides:

- A network **topology view** with per-slave ports and links.
- **SDO** read/write across data types (integers, reals, strings, raw hex).
- **PDO** mapping inspection/editing and a real-time process-data view.
- A **DS402 motor bench** (setpoints, units configuration).
- An event/error log and an embedded simulator launcher.

Off by default; build it with:

```bash
./scripts/configure.sh build --with=kickui
./scripts/setup_build.sh build
cd build && make -j
./tools/kickui/kickui
```

## EEPROM editor (GUI)

ImGui + GLFW structured editor for slave SII/EEPROM images
(`tools/eeprom_editor/`), with tabs for device info, strings, SyncManagers,
FMMUs, and PDO mappings. Off by default:

```bash
./scripts/configure.sh build --with=eeprom_editor
./scripts/setup_build.sh build
cd build && make -j
./tools/eeprom_editor/kickcat_eeprom_editor
```

## eeprom (CLI)

Read or write EEPROM content from an EtherCAT Slave Controller
(`tools/eeprom.cc`):

```bash
# Write EEPROM to the slave at position 0
sudo ./tools/eeprom -s 0 -c write -f path/to/eeprom.bin -i <interface>

# Auto-detect the interface
sudo ./tools/eeprom -s 0 -c write -f path/to/eeprom.bin -i "?"

# Read EEPROM from the slave (serializes the SII image to the output file)
sudo ./tools/eeprom -s 0 -c read -f output.bin -i <interface>
```

## scan_topology (CLI)

Enumerate the slaves on the bus and display the topology, including each slave's
state and per-port DL status.

## check_network_stability (CLI, Linux)

Long-running monitor for packet loss and corruption: broadcast-reads in a loop
and reports tx/rx counts and error deltas over time.

## od_generator (CLI)

Generate Object Dictionary code from an ESI file (requires
`ENABLE_ESI_PARSER=ON`):

```bash
# Generate od_populator.cc from an ESI file
./tools/od_generator -f your_device.esi
```

The generated `od_populator.cc` defines `CoE::createOD()` (declared in
`kickcat/CoE/OD.h`). Add it to your slave application's build and call it:

```cpp
#include "kickcat/CoE/OD.h"

int main()
{
    // ...
    auto dictionary = CoE::createOD();
    // ...
}
```

You can also write `od_populator.cc` by hand by implementing `CoE::createOD()`.
See `examples/slave/xmc4800/xmc4800-relax/nuttx/od_populator.cc` and
`examples/slave/lan9252/freedom-k64f/nuttx/od_populator.cc` for references.

## ethercat_gui (Python, PySide6)

A pure-Python GUI for master bus control, shipped with the Python package.

Requirements: Python 3, PySide6, and the `kickcat` package
(`pip install -e .` from the project root).

```bash
python -m tools.ethercat_gui -i enp8s0
```
