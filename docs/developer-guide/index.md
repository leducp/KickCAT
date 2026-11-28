# The KickCAT developer guide

This guide explains how KickCAT works. Each chapter describes a problem, the options, and
the reason for the design. The headers provide the full API.

The guide assumes you can read C++ and have built a CMake project. The
[EtherCAT primer](../get-started/ethercat-primer.md) introduces EtherCAT for beginners. The architecture chapters explain
the KickCAT code.

Start with [How to read this guide](how-to-read.md).

## Chapters

| Chapter | What you learn |
|---|---|
| [How to read this guide](how-to-read.md) | The audience, example board, and suggested reading order. |
| [EtherCAT primer](../get-started/ethercat-primer.md) | The EtherCAT concepts the guide relies on, and where KickCAT handles each one. |
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
