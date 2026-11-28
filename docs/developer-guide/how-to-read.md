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

This guide assumes you can read C++ and have built a CMake project. If you are new to
industrial networks, read the [EtherCAT primer](../get-started/ethercat-primer.md) first.

[Overview and principles](architecture/principles.md) explains KickCAT's design principles. [A map of the code](architecture/code-map.md) describes the source directories. Later chapters cover
individual parts of the stack.

If you already know EtherCAT, start at
[Overview and principles](architecture/principles.md).

## One example through the guide

The examples use the **Freedom K64F board**, an NXP development board with a LAN9252
EtherCAT Slave Controller. KickCAT includes the firmware and an XML file that describes the board.

The master exchanges the following data with the board.

- **3 output bytes** set the red, green, and blue levels of an LED.
- **12 input bytes** contain three accelerometer readings and three magnetometer
  readings. Each is a signed 16-bit integer.
- **A mailbox** lets the master read and write configuration objects using CoE.

You can run the `simulated_bus` example without a board. It uses the device
description to emulate the board and runs the master in the same process. [Your first bus](../get-started/first-bus.md) walks through the example.

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

1. Read the [EtherCAT primer](../get-started/ethercat-primer.md).
2. Read [Overview and principles](architecture/principles.md) for the design principles.
3. Follow [Building](../user-guide/building.md), then run `simulated_bus` as shown in [Your first bus](../get-started/first-bus.md). Watch the slave reach OP.
4. For master development, read [The master](architecture/master.md), [Transport](architecture/transport.md), and
   [Mailboxes in KickCAT](architecture/mailboxes.md). For firmware, read [The slave stack](architecture/slave.md),
   [Mailboxes in KickCAT](architecture/mailboxes.md), and [From XML to bytes](architecture/device-descriptions.md).
5. Read the remaining chapters in any order. Read [Testing a change](contributing/testing.md) before
   changing the code.

## Where to go next

- The [EtherCAT primer](../get-started/ethercat-primer.md), if EtherCAT is new to you.
- The full list of chapters: [the guide's index](index.md).
