# Glossary

!!! abstract "TL;DR"
    - KickCAT says master and slave. Current ETG documents say MainDevice and SubDevice.
    - Hover any acronym on this site to see what it stands for.
    - For the protocol itself, use the ETG [poster][etg-poster] (2023-10 edition) and the
      specifications listed at the end of this page.

## ETG names and KickCAT names

| ETG term | KickCAT term | In the code |
|---|---|---|
| MainDevice | master | `Bus` and the master stack in `lib/master/` |
| SubDevice | slave | `slave::Slave` and the slave stack in `lib/slave/` |
| EtherCAT SubDevice Controller | ESC | `AbstractESC`, with `Lan9252`, `XMC4800` and `EmulatedESC` |
| SubDevice Information Interface | SII | `SIIParser.h`, `ESI/SIIBuilder.h` |
| EtherCAT SubDevice Information | ESI | `kickcat::ESI` |

## KickCAT terms

| Term | What it is | In the code |
|---|---|---|
| Bus | The master's entry point: it owns the slave list, runs startup and provides the cyclic calls. | `Bus` |
| Slave, on the master | The master's record of one remote slave: addresses, SII, mailbox and process data offsets. | `kickcat::Slave` |
| Slave, on the device | The slave stack running on a device: state machine, mailbox and process data. | `kickcat::slave::Slave` |
| Slave, in a network description | One slave entry of a parsed ENI file. | `kickcat::ENI::Slave` |
| Link | Packs datagrams into frames, matches each reply to its callback, and merges redundant frames. | `AbstractLink`, `Link` |
| Socket | Sends and receives raw Ethernet frames. There is one class per transport. | `AbstractSocket`, `Socket`, `XdpSocket`, `TapSocket`, `LoopbackSocket`, `SocketNull` |
| Process image | The application's buffer, with the inputs of all slaves first, then their outputs. | `Bus::createMapping()` |
| Mailbox, on the master | The queue of mailbox requests for one remote slave. | `mailbox::request::Mailbox` |
| Mailbox, on the device | The slave's mailbox and the handlers that answer requests. | `mailbox::response::Mailbox` |
| Mailbox sequencer | Runs one mailbox step each time the cyclic loop calls it. | `MailboxSequencer` |
| Dictionary | The CoE object dictionary. The application owns it. | `CoE::Dictionary` |
| PDO, the class | On the slave, copies process data between the ESC and the application buffers. | `kickcat::PDO` |
| Drive | Master-side helper that runs one CiA 402 drive. | `CoE::CiA::DS402::Drive` |
| Gateway | Forwards mailbox requests from external tools to the slaves (ETG.8200). | `Gateway`, `MasterOD` |
| Timer | The cyclic loop's periodic deadline. Its soft PLL follows the DC reference clock. | `Timer`, `SoftPll` |
| Emulator | The software ESC and network that run the real slave stack without hardware. | `EmulatedESC`, `EmulatedNetwork` |
| Simulator | A way to run a master against the emulator: in one process, or as a separate `network_simulator` process. | `LoopbackSocket`, `simulation/network_simulator.cc`, `kickcat::sim` |
| Simulated slave | A slave built from a JSON configuration: emulated ESC, slave stack, dictionary and device behavior. | `sim::SimulatedSlave`, `sim::buildSlave()` |
| Device app | The behavior of a simulated device, such as a CiA 402 motor. | `sim::DeviceApp`, `sim::Ds402Motor` |
| Topology link | One cable between two ports in a simulator topology file. Not the transport `Link`. | `sim::Link` |

## Where to look it up

| Topic | Poster panel | Specification |
|---|---|---|
| Frames, datagrams, commands, working counter | Frame processing and examples | [ETG.1000][etg1000].4 |
| State machine, AL status codes | ESM | [ETG.1000][etg1000].6 |
| SyncManagers, FMMUs | SM and FMMU | [ETG.1000][etg1000].4 |
| Process data, PDO mapping and assignment | Process data, Terminology | [ETG.1000][etg1000].5, [ETG.1000][etg1000].6 |
| CoE, object dictionary, SDO | CoE | [ETG.1000][etg1000].5, [ETG.1000][etg1000].6, [ETG.1020][etg1020] |
| ESC internals, SII layout | SubDevice structure and ESC | [ETG.1000][etg1000].4, [ETG.2010][etg2010] |
| Distributed clocks | DC unit and synchronization | [ETG.1000][etg1000].4, [ETG.1020][etg1020] |
| CiA 402 drives | CoE | [ETG.6010][etg6010] |
| ESI files | | [ETG.2000][etg2000] |
| ENI files | | [ETG.2100][etg2100] |
| Mailbox gateway | | [ETG.8200][etg8200] |
| Master object dictionary | | [ETG.1510][etg1510] |

All ETG documents are listed on the [ETG downloads page][etg-downloads]. For questions, use
the ETG [Knowledge Base][etg-kb] and [Developers Forum][etg-forum].
