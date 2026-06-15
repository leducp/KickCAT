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
| Multiple PDO / more than 2 SyncManagers  | Supported | Partial (up to 2 SyncManagers) |

## Mailbox protocols

| Protocol | Master     | Slave      | Notes |
|----------|------------|------------|-------|
| CoE (CANopen over EtherCAT) | Supported | Supported | See CoE breakdown below. |
| FoE (File over EtherCAT)    | Planned    | Planned    | Protocol header only; no mailbox handlers yet. |
| EoE (Ethernet over EtherCAT)| Planned    | Planned    | Protocol header only; no mailbox handlers yet. |
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
| EEPROM write (slave provisioning)| Partial   | Not applicable |

See [TOOLS.md](TOOLS.md) for the `eeprom` CLI and the EEPROM editor GUI.

## Platforms

| Platform           | Role   | Status                                       |
|--------------------|--------|----------------------------------------------|
| Linux x86-64       | Master | Supported (RT-PREEMPT recommended for real-time) |
| Windows            | Master | Supported for tools and testing only (not real-time) |
| PikeOS 5.1 (ARMv8) | Master | Supported                                    |
| NuttX RTOS         | Slave  | Supported                                    |
| Arduino Due        | Slave  | Supported via NuttX                          |
| Infineon XMC4800   | Slave  | Supported via NuttX, CTT-validated           |
| NXP Freedom K64F   | Slave  | Supported via NuttX                          |

## Known limitations

- Little-endian hosts only.
- Windows is not suitable for real-time operation; use it for tooling and tests.
- Slave PDO support is limited to 2 SyncManagers (multi-PDO is in progress).
- Slave-side Distributed Clocks are not implemented.
- The simulator/emulator does not emulate interrupts; see
  [SIMULATION.md](SIMULATION.md).
