# How to read this guide

> Where should you start?
>
> You will learn:
>
> - who the guide is for
> - which board the examples use
> - where to find other KickCAT documents
> - which chapters to read first

## Who the guide is for

This guide assumes you can read C++ and have built a CMake project. The EtherCAT
background chapters introduce EtherCAT. You can start
here without experience with industrial networks.

- [What is EtherCAT?](ethercat/what-is-ethercat.md) introduces the protocol and explains its speed.
- [Frames and datagrams](ethercat/frames-and-datagrams.md) shows what travels on the wire.
- [Inside a slave: the ESC](ethercat/inside-a-slave.md) describes the slave controller chip.
- [The EtherCAT state machine](ethercat/state-machine.md) explains slave states.
- [Process data](ethercat/process-data.md) shows how inputs and outputs move every cycle.
- [Mailboxes: CoE and FoE](ethercat/mailboxes.md) covers messages sent outside the regular exchange.
- [Topology, redundancy and clocks](ethercat/topology-and-clocks.md) covers cables, redundancy and clocks.
- [Describing a device](ethercat/device-descriptions.md) explains device description files.

[Overview and principles](architecture/principles.md) explains KickCAT's design principles. [A map of the code](architecture/code-map.md) describes the source directories. Later chapters cover
individual parts of the stack.

If you already know EtherCAT, skip the EtherCAT background and start at
[Overview and principles](architecture/principles.md).

## One example through the guide

The examples use the **Freedom K64F board**, an NXP development board with a LAN9252
EtherCAT Slave Controller. KickCAT includes the firmware and an XML file that describes the board.

The master exchanges the following data with the board.

- **3 output bytes** set the red, green, and blue levels of an LED.
- **12 input bytes** contain three accelerometer readings and three magnetometer
  readings. Each is a signed 16-bit integer.
- **A mailbox** lets the master read and write configuration objects using CoE.

You can use the `simulated_bus` example without the board. It uses the device
description to emulate the board and runs the master in the same process. [A map of the code](architecture/code-map.md) explains the example.

## How a chapter is shaped

Each chapter describes the problem, the options, and the reason for the design. The
"In the code" section lists the relevant source files.

The guide explains the main classes and the order of calls in the API. Use the headers for current function signatures.

## The other documents

| Document | Contents |
|---|---|
| This guide | KickCAT design and how the parts work together |
| [Features](../user-guide/features.md) | Supported master and slave features |
| [Building](../user-guide/building.md) | Build and test instructions |
| [Hardware](../user-guide/hardware.md) | Running KickCAT on hardware |
| [Simulation](../user-guide/simulation.md) | Running a master with emulated slaves |
| [Tools](../user-guide/tools.md) | Command-line tools and graphical user interfaces |
| [Performance](../user-guide/realtime.md) | Linux settings for real-time operation |
| The headers | Function arguments and return values |

## A reading path for a newcomer

1. Read the EtherCAT background, from [What is EtherCAT?](ethercat/what-is-ethercat.md) through [Describing a device](ethercat/device-descriptions.md).
2. Read [Overview and principles](architecture/principles.md) for the design principles.
3. Follow [Building](../user-guide/building.md), then run `simulated_bus` as shown in [A map of the code](architecture/code-map.md). Watch the slave reach OP.
4. For master development, read [The master](architecture/master.md), [Transport](architecture/transport.md), and
   [Mailboxes in KickCAT](architecture/mailboxes.md). For firmware, read [The slave stack](architecture/slave.md),
   [Mailboxes in KickCAT](architecture/mailboxes.md), and [From XML to bytes](architecture/device-descriptions.md).
5. Read the remaining chapters in any order. Read [Testing a change](contributing/testing.md) before
   changing the code.

## Where to go next

- [What is EtherCAT?](ethercat/what-is-ethercat.md), the first page of the EtherCAT background.
- The full list of chapters: [the guide's index](index.md).
