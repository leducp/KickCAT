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

This guide assumes you can read C++ and have built a CMake project. Chapters 0.1 to
0.8 introduce EtherCAT. You can start
here without experience with industrial networks.

- [Chapter 0.1](what-is-ethercat.md) introduces the protocol and explains its speed.
- [Chapter 0.2](frames-and-datagrams.md) shows what travels on the wire.
- [Chapter 0.3](inside-a-slave.md) describes the slave controller chip.
- [Chapter 0.4](state-machine.md) explains slave states.
- [Chapter 0.5](process-data.md) shows how inputs and outputs move every cycle.
- [Chapter 0.6](mailboxes.md) covers messages sent outside the regular exchange.
- [Chapter 0.7](topology-and-clocks.md) covers cables, redundancy and clocks.
- [Chapter 0.8](device-descriptions.md) explains device description files.

[Chapter 1](principles.md) explains KickCAT's design principles. [Chapter
2](../architecture.md) describes the source directories. Later chapters cover
individual parts of the stack.

If you already know EtherCAT, skip Part 0 and start at Chapter 1.

## One example through the guide

The examples use the **Freedom K64F board**, an NXP development board with a LAN9252
EtherCAT Slave Controller. KickCAT includes the firmware and an Extensible Markup
Language (XML) file that describes the board.

The master exchanges the following data with the board.

- **3 output bytes** set the red, green, and blue levels of a light-emitting diode (LED).
- **12 input bytes** contain three accelerometer readings and three magnetometer
  readings. Each is a signed 16-bit integer.
- **A mailbox** lets the master read and write configuration objects using CANopen
  over EtherCAT (CoE).

You can use the `simulated_bus` example without the board. It uses the device
description to emulate the board and runs the master in the same process. [Chapter
2](../architecture.md) explains the example.

## How a chapter is shaped

Each chapter describes the problem, the options, and the reason for the design. The
"In the code" section lists the relevant source files.

The guide explains the main classes and the order of calls in the application
programming interface (API). Use the headers for current function signatures.

## The other documents

| Document | Contents |
|---|---|
| This guide | KickCAT design and how the parts work together |
| [Features](../FEATURES.md) | Supported master and slave features |
| [Building](../BUILDING.md) | Build and test instructions |
| [Hardware](../HARDWARE.md) | Running KickCAT on hardware |
| [Simulation](../SIMULATION.md) | Running a master with emulated slaves |
| [Tools](../TOOLS.md) | Command-line tools and graphical user interfaces |
| [Performance](../PERFORMANCE.md) | Linux settings for real-time operation |
| The headers | Function arguments and return values |

## A reading path for a newcomer

1. Read Part 0 from [Chapter 0.1](what-is-ethercat.md) through [Chapter
   0.8](device-descriptions.md).
2. Read [Chapter 1](principles.md) for the design principles.
3. Follow [Building](../BUILDING.md), then run `simulated_bus` as shown in [Chapter
   2](../architecture.md). Watch the slave reach the operational state (OP).
4. For master development, read Chapters [3](master.md), [4](transport.md), and
   [5](mailbox-implementation.md). For firmware, read Chapters [6](slave.md),
   [5](mailbox-implementation.md), and [9](from-xml-to-bytes.md).
5. Read the remaining chapters in any order. Read [Chapter 11](testing.md) before
   changing the code.

## Where to go next

- [Chapter 0.1](what-is-ethercat.md), *What is EtherCAT?*, the first step of Part 0.
- The full list of chapters: [the guide's index](README.md).
