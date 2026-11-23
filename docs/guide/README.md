# The KickCAT developer guide

This guide explains how EtherCAT works and how KickCAT implements it. Each
chapter starts with a problem, looks at the ways to solve it, and then gives the
choice KickCAT made and the reason for it. The exact API is in the headers. The
guide gives its shape, so that the headers make sense when you open them.

The guide is for a developer who knows C++ and has built a CMake project. It
does not assume that you know EtherCAT. Part 0 teaches the protocol one idea at a
time. The later chapters take KickCAT apart one design decision at a time.

Start with [Chapter 0.0](how-to-read.md).

## Chapters

| # | Chapter | What you learn |
|---|---|---|
| 0.0 | [How to read this guide](how-to-read.md) | Who the guide is for, the example it follows, and a reading path. |
| 0.1 | [What is EtherCAT?](what-is-ethercat.md) | What a fieldbus does, what a master and a slave are, and why EtherCAT is fast. |
| 0.2 | [Frames and datagrams](frames-and-datagrams.md) | What travels on the wire, how a slave is addressed, and what the working counter tells the master. |
| 0.3 | [Inside a slave: the ESC](inside-a-slave.md) | The EtherCAT Slave Controller, its registers and memory, SyncManagers, FMMUs and the EEPROM. |
| 0.4 | [The EtherCAT state machine](state-machine.md) | The INIT, PRE-OP, SAFE-OP, OP and BOOT states, and what each one allows. |
| 0.5 | [Process data](process-data.md) | How inputs and outputs are exchanged every cycle, and how a PDO mapping places them. |
| 0.6 | [Mailboxes: CoE and FoE](mailboxes.md) | How the master and a slave exchange messages that are not cyclic: SDOs, emergencies and files. |
| 0.7 | [Topology, redundancy and clocks](topology-and-clocks.md) | How a frame travels through ports, how a ring survives a broken cable, and how distributed clocks agree on time. |
| 0.8 | [Describing a device: ESI, SII and ENI](device-descriptions.md) | The three files that describe a slave and a network, and who reads each one. |
| 1 | [Overview and principles](principles.md) | What KickCAT is for, and the principles every later chapter applies. |
| 2 | [A map of the code](../architecture.md) | Where each part of KickCAT lives, and one example read from top to bottom. |
| 3 | [The master: from discovery to OP](master.md) | The bus lifecycle, the process image and the cyclic loop. |
| 4 | [Transport: links, frames and sockets](transport.md) | How a datagram leaves the master and comes back, and how redundancy works. |
| 5 | [Mailboxes in KickCAT](mailbox-implementation.md) | Requests on the master, responses on the slave, and how to add a protocol. |
| 6 | [The slave stack](slave.md) | The state machine, the ESC abstraction, process data, and how to port to a new board. |
| 7 | [Distributed clocks](distributed-clocks.md) | How the master sets up the clocks and keeps its own cycle in step with them. |
| 8 | [Simulation internals](simulation.md) | How the emulator runs the real slave stack against a software ESC and network. |
| 9 | [From XML to bytes](from-xml-to-bytes.md) | How ESI, SII and ENI files become EEPROM images, object dictionaries and configurations. |
| 10 | [Operating systems and porting](os-and-porting.md) | The OS layer, the supported platforms, and what a new one must provide. |
| 11 | [Testing a change](testing.md) | Which kind of test fits which kind of change. |
