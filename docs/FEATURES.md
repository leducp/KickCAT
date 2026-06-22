# Feature support matrix

This document maps the EtherCAT feature set to what the KickCAT stack currently
provides, separately for the **master** and **slave** sides. It is the
authoritative source for "what works today"; the README only summarizes it.

## Legend

| Status         | Meaning                                                                        |
|----------------|--------------------------------------------------------------------------------|
| Supported      | Implemented and exercised by examples and/or unit tests.                       |
| Partial        | Usable but incomplete, or limited to a subset of the feature.                  |
| Experimental   | Implemented but not yet validated for production use.                          |
| Planned        | On the roadmap; protocol definitions or scaffolding may already exist.         |
| Not planned    | No maintainer hardware to develop or test against. Contributions are welcome.  |
| Not applicable | The feature does not apply to that side.                                        |

## State machine and process data

| Feature                                  | Master    | Slave     |
|------------------------------------------|-----------|-----------|
| EtherCAT State Machine (INIT/PRE-OP/SAFE-OP/OP) | Supported | Supported |
| Process data exchange (PDO read/write)   | Supported | Supported |
| FMMU configuration                       | Supported | Supported |
| SyncManager configuration                | Supported | Supported |
| One input + one output PDO mapping per slave | Supported | Supported |
| Multiple PDO SyncManagers (>1 input or >1 output per slave) | Not supported | Not supported |
| Mailbox status polling via dedicated FMMUs (LRD/LRW) | Supported | Not applicable |

## Mailbox protocols

| Protocol | Master     | Slave      | Notes |
|----------|------------|------------|-------|
| CoE (CANopen over EtherCAT) | Supported | Supported | See CoE breakdown below. |
| FoE (File over EtherCAT)    | Planned    | Planned    | Protocol header only; no mailbox handlers yet. |
| EoE (Ethernet over EtherCAT)| Supported | Supported | Protocol layer only; see EoE breakdown below. |
| SoE (Servo over EtherCAT)   | Not planned | Not planned | ESI parsing only. No maintainer hardware; contributions welcome. |
| AoE (ADS over EtherCAT)     | Not planned | Not planned | ESI parsing only. No maintainer hardware; contributions welcome. |
| VoE (Vendor over EtherCAT)  | Not planned | Not planned | ESI parsing only. No maintainer hardware; contributions welcome. |

### CoE breakdown

| CoE feature                         | Master    | Slave     |
|-------------------------------------|-----------|-----------|
| SDO expedited upload/download       | Supported | Supported |
| SDO normal (sized) transfer         | Supported | Supported |
| SDO segmented transfer              | Supported | Supported |
| SDO Information service             | Supported | Supported |
| Emergency messages                  | Supported | Not applicable |
| Object Dictionary                   | Not applicable | Supported |
| PDO mapping / assignment            | Supported | Supported |

### EoE breakdown

| EoE feature                         | Master    | Slave     |
|-------------------------------------|-----------|-----------|
| Frame tunneling (fragment/reassemble) | Supported | Supported |
| Interleaved frames (per port/frame number) | Supported | Supported |
| Set / Get IP Parameter              | Supported | Supported |
| Set Address (MAC) Filter            | Supported | Supported |
| Timestamp trailer                   | Discarded on receive | Discarded on receive |

EoE is implemented at the protocol layer: the stack fragments, reassembles, and
delivers each completed Ethernet frame to an application callback the instant it
is ready — it keeps no frame queue and does no buffering (the application buffers
if it needs to). Several frames may be in flight at once on a port and their
fragments may be interleaved; reassembly tracks each frame independently by
(port, frame number). Bridging the EoE endpoint to a host network interface
(TUN/TAP) is out of scope and left to the application. The slave owns its IP
stack: the parameter services are delegated to an application `EoE::SlaveConfig`.
The stack does not generate the optional send/receive timestamp, but it is
compliant on receive: a timestamp appended to the last fragment is discarded so
it does not corrupt the frame (the value is not surfaced).

## Distributed Clocks (DC)

| Feature                         | Master       | Slave       |
|---------------------------------|--------------|-------------|
| DC synchronization              | Experimental | Planned     |
| DC modeling in the emulator     | Supported (local clock, drift-ppm injection, SYNC0) | -- |

The ESC emulator models a DC clock with configurable drift; see
[SIMULATION.md](SIMULATION.md). Slave-side DC is not implemented on real ESCs yet.

## Networking and reliability

| Feature                                   | Master    | Slave     |
|-------------------------------------------|-----------|-----------|
| Cable redundancy                          | Supported | Not applicable |
| Consecutive writes (frames in flight)     | Supported | Not applicable |
| Mailbox gateway (ETG.8200)                | Supported | Not applicable |
| Bus diagnostics and error counters        | Supported | Supported |
| AF_XDP socket backend (Linux, opt-in)     | Supported | Not applicable |
| Auto-discovery of broken wires            | Planned   | Not applicable |

## EEPROM / SII

| Feature                          | Master    | Slave     |
|----------------------------------|-----------|-----------|
| SII (EEPROM) parsing             | Supported | Supported |
| EEPROM read/dump tooling         | Supported | Not applicable |
| EEPROM write (slave provisioning)| Supported | Not applicable |

See [TOOLS.md](TOOLS.md) for the `eeprom` CLI and the EEPROM editor GUI.

## Platforms

The master and slave run on different layers: the master is a host OS process,
while the slave is the stack running on an RTOS against an EtherCAT Slave
Controller (ESC).

### Master (host operating system)

| OS         | Architecture | Status                                       |
|------------|--------------|----------------------------------------------|
| Linux      | x86-64       | Supported; RT-PREEMPT recommended for real-time |
| PikeOS 5.1 | ARMv8        | Supported                                    |
| Windows    | x86-64       | Tools and testing only (not real-time)       |

### Slave (RTOS + ESC hardware)

The slave stack runs on the NuttX RTOS. Supported EtherCAT Slave Controllers:

| ESC      | Interface  | Status                   |
|----------|------------|--------------------------|
| LAN9252  | SPI        | Supported                |
| XMC4800  | Integrated | Supported; CTT-validated |

Reference boards: NXP Freedom K64F (LAN9252), Arduino Due with EasyCAT shield
(LAN9252), and the Infineon XMC4800 Relax Kit (XMC4800). Build and flashing
details are in [HARDWARE.md](HARDWARE.md).

## Known limitations

- Little-endian hosts only.
- Windows is not suitable for real-time operation; use it for tooling and tests.
- Process data is limited to one input and one output PDO SyncManager per slave
  on both master and slave; multiple PDO SyncManagers per direction are not
  supported.
- Slave-side Distributed Clocks are not implemented.
- The simulator/emulator does not emulate interrupts; see
  [SIMULATION.md](SIMULATION.md).
