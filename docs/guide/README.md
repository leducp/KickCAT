# The KickCAT developer guide

This guide explains how EtherCAT works
and how KickCAT implements it. Each chapter describes a problem, the options, and
the reason for the design. The headers give the full application programming
interface (API).

The guide assumes you can read C++ and have built a CMake project. Part 0 introduces
EtherCAT for beginners. Later chapters explain the KickCAT code.

Start with [Chapter 0.0](how-to-read.md).

## Chapters

| # | Chapter | What you learn |
|---|---|---|
| 0.0 | [How to read this guide](how-to-read.md) | The audience, example board, and suggested reading order. |
| 0.1 | [What is EtherCAT?](what-is-ethercat.md) | What a fieldbus does, what a master and a slave are, and why EtherCAT is fast. |
| 0.2 | [Frames and datagrams](frames-and-datagrams.md) | Frame contents, slave addressing, and the working counter. |
| 0.3 | [Inside a slave controller](inside-a-slave.md) | The EtherCAT Slave Controller (ESC), registers, memory, SyncManagers, and address mapping. |
| 0.4 | [The EtherCAT state machine](state-machine.md) | The five slave states and what each allows. |
| 0.5 | [Process data](process-data.md) | How inputs and outputs are exchanged every cycle, and how process data object (PDO) mappings define their layout. |
| 0.6 | [Mailbox protocols](mailboxes.md) | CANopen over EtherCAT (CoE) for configuration and emergency messages, and File over EtherCAT (FoE) for files. |
| 0.7 | [Topology, redundancy and clocks](topology-and-clocks.md) | How a frame travels through ports, how a ring works with a broken cable, and how clocks stay synchronized. |
| 0.8 | [Device and network descriptions](device-descriptions.md) | Who uses EtherCAT Slave Information (ESI), the Slave Information Interface (SII), and EtherCAT Network Information (ENI). |
| 1 | [Overview and principles](principles.md) | What KickCAT is for, and the principles every later chapter applies. |
| 2 | [A map of the code](../architecture.md) | Source directories and an example program. |
| 3 | [Master discovery and startup](master.md) | Startup through the operational state (OP), the process image, and the cyclic loop. |
| 4 | [Transport: links, frames and sockets](transport.md) | How a datagram leaves the master and comes back, and how redundancy works. |
| 5 | [Mailboxes in KickCAT](mailbox-implementation.md) | Requests on the master, responses on the slave, and how to add a protocol. |
| 6 | [The slave stack](slave.md) | The state machine, the ESC abstraction, process data, and how to port to a new board. |
| 7 | [Distributed clocks](distributed-clocks.md) | How the master sets up the clocks and keeps its own cycle synchronized with them. |
| 8 | [Simulation internals](simulation.md) | How the emulator runs the real slave stack against a software ESC and network. |
| 9 | [From device files to bytes](from-xml-to-bytes.md) | How XML files produce memory images, object dictionaries, and network configurations. |
| 10 | [Operating systems and porting](os-and-porting.md) | The operating system layer, the supported platforms, and what a new one must provide. |
| 11 | [Testing a change](testing.md) | Which kind of test fits which kind of change. |
