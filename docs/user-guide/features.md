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
| CoE      | Supported | Supported | See CoE breakdown below. |
| FoE      | Experimental | Experimental | See FoE breakdown below. |
| EoE      | Planned    | Planned    | Protocol definitions only; no mailbox handlers yet. |
| SoE      | Not planned | Not planned | ESI parsing only. No maintainer hardware; contributions welcome. |
| AoE      | Not planned | Not planned | ESI parsing only. No maintainer hardware; contributions welcome. |
| VoE      | Not planned | Not planned | ESI parsing only. No maintainer hardware; contributions welcome. |

### CoE breakdown

| CoE feature                         | Master    | Slave     |
|-------------------------------------|-----------|-----------|
| SDO expedited upload/download       | Supported | Supported |
| SDO normal (sized) transfer         | Supported | Supported |
| SDO segmented transfer              | Supported | Supported |
| SDO Information service             | Supported | Supported |
| Emergency messages                  | Supported | Not applicable |
| Object Dictionary                   | Supported (ETG.1510 master OD, through the mailbox gateway) | Supported |
| PDO mapping / assignment            | Supported | Supported |

### FoE breakdown

| FoE feature                                   | Master       | Slave        |
|-----------------------------------------------|--------------|--------------|
| File read / write (PRE-OP, SAFE-OP, OP)       | Experimental | Experimental |
| Password                                      | Experimental | Experimental |
| Busy (slave asks the master to wait)          | Experimental | Not supported |
| Bootstrap state (firmware update)             | Experimental | Experimental |
| Simulator file backend (`foe_dir`)            | Not applicable | Supported  |

The master timeout applies to each exchange, not to the whole transfer. On the
slave, the application provides the files through `FoE::AbstractStorage`
(`mailbox::response::Mailbox::enableFoE`).

Bootstrap: `Bus::enterBootstrap` / `Bus::exitBootstrap` move one slave between
INIT and BOOT, switching its mailbox to the bootstrap layout declared in the SII.
On the slave, `slave::Slave::setBootstrapMailbox` gives the mailbox served in
BOOT (typically FoE only, with its own SyncManager layout); without one, BOOT is
refused with AL status code 0x0013.

## Distributed Clocks

| Feature                         | Master       | Slave       |
|---------------------------------|--------------|-------------|
| DC synchronization              | Experimental | Planned     |
| DC modeling in the emulator     | Supported (local clock, drift-ppm injection, SYNC0) | -- |

The ESC emulator models a DC clock with configurable drift; see
[Simulation](simulation.md). Slave-side DC is not implemented on real ESCs yet.

`Bus::enableDC` lets the reference (first DC) slave clock free-run: the cyclic drift
datagram only relays it to the other slaves (ETG-standard FRMW/ARMW relay). The master
phase-locks its own cycle loop to that clock with a soft PLL owned by `Timer`
(`Timer::sync_to`, driven each cycle by `Bus::sync`), rather than writing its
software-sampled time back into the reference — doing so would inject the master's
RT-loop jitter into the DC domain and can unlock the slaves' SYNC PLL.

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

See [Tools](tools.md) for the `eeprom` CLI and the EEPROM editor GUI.

## Platforms

The master and slave run on different layers: the master is a host OS process,
while the slave is the stack running on an RTOS against an ESC.

### Master (host operating system)

| OS         | Architecture | Status                                       |
|------------|--------------|----------------------------------------------|
| Linux      | x86-64       | Supported; RT-PREEMPT recommended for real-time |
| PikeOS 5.1 | ARMv8        | Supported                                    |
| Windows    | x86-64       | Tools and testing only (not real-time)       |

### Slave (OS + ESC hardware)

The slave stack runs on NuttX, Linux and KickOS:

| OS     | Status       | Reference board                                    |
|--------|--------------|----------------------------------------------------|
| NuttX  | Supported    | Freedom K64F (LAN9252), Arduino Due (EasyCAT, LAN9252), XMC4800 Relax Kit |
| Linux  | Supported    | Raspberry Pi (LAN9252 over SPI)                     |
| KickOS | Experimental | Freedom K64F                                       |

Supported EtherCAT Slave Controllers:

| ESC      | Interface  | Status                   |
|----------|------------|--------------------------|
| LAN9252  | SPI        | Supported                |
| XMC4800  | Integrated | Supported; CTT-validated |

Build and flashing details are in [Hardware](hardware.md).

## Known limitations

- Little-endian hosts only.
- Windows is not suitable for real-time operation; use it for tooling and tests.
- Process data is limited to one input and one output PDO SyncManager per slave
  on both master and slave; multiple PDO SyncManagers per direction are not
  supported.
- Slave-side Distributed Clocks are not implemented.
- The simulator/emulator does not emulate interrupts; see
  [Simulation](simulation.md).
