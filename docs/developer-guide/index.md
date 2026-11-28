# The KickCAT developer guide

This guide explains how EtherCAT works
and how KickCAT implements it. Each chapter describes a problem, the options, and
the reason for the design. The headers give the full API.

The guide assumes you can read C++ and have built a CMake project. The EtherCAT background
chapters introduce EtherCAT for beginners. The architecture chapters explain the KickCAT code.

Start with [How to read this guide](how-to-read.md).

## Chapters

| Chapter | What you learn |
|---|---|
| [How to read this guide](how-to-read.md) | The audience, example board, and suggested reading order. |
| [What is EtherCAT?](ethercat/what-is-ethercat.md) | What a fieldbus does, what a master and a slave are, and why EtherCAT is fast. |
| [Frames and datagrams](ethercat/frames-and-datagrams.md) | Frame contents, slave addressing, and the working counter. |
| [Inside a slave controller](ethercat/inside-a-slave.md) | The ESC, registers, memory, SyncManagers, and address mapping. |
| [The EtherCAT state machine](ethercat/state-machine.md) | The five slave states and what each allows. |
| [Process data](ethercat/process-data.md) | How inputs and outputs are exchanged every cycle, and how PDO mappings define their layout. |
| [Mailbox protocols](ethercat/mailboxes.md) | CoE for configuration and emergency messages, and FoE for files. |
| [Topology, redundancy and clocks](ethercat/topology-and-clocks.md) | How a frame travels through ports, how a ring works with a broken cable, and how clocks stay synchronized. |
| [Device and network descriptions](ethercat/device-descriptions.md) | Who uses ESI, the SII, and ENI. |
| [Overview and principles](architecture/principles.md) | What KickCAT is for, and the principles every later chapter applies. |
| [A map of the code](architecture/code-map.md) | Source directories and namespaces. |
| [Master discovery and startup](architecture/master.md) | Startup through OP, the process image, and the cyclic loop. |
| [Transport: links, frames and sockets](architecture/transport.md) | How a datagram leaves the master and comes back, and how redundancy works. |
| [Mailboxes in KickCAT](architecture/mailboxes.md) | Requests on the master, responses on the slave, and how to add a protocol. |
| [The slave stack](architecture/slave.md) | The state machine, the ESC abstraction, process data, and how to port to a new board. |
| [Distributed clocks](architecture/distributed-clocks.md) | How the master sets up the clocks and keeps its own cycle synchronized with them. |
| [Simulation internals](architecture/emulation.md) | How the emulator runs the real slave stack against a software ESC and network. |
| [From device files to bytes](architecture/device-descriptions.md) | How XML files produce memory images, object dictionaries, and network configurations. |
| [Operating systems and porting](architecture/platform.md) | The operating system layer, the supported platforms, and what a new one must provide. |
| [Testing a change](contributing/testing.md) | Which kind of test fits which kind of change. |
