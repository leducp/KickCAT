# How to read this guide

> Where should you start, and what else is there to read?
>
> You will learn:
>
> - who the guide is written for
> - the example the chapters follow
> - how the guide fits with the other KickCAT documents
> - a reading path for a newcomer

## Who the guide is for

The guide assumes a reader who can read C++ and who has built a CMake project.
It does not assume that you know EtherCAT or industrial networks. **Part 0**
(the chapters numbered 0.1 to 0.8) teaches the protocol, one idea at a time:

- [Chapter 0.1](what-is-ethercat.md) says what EtherCAT is for, and why it is fast.
- [Chapter 0.2](frames-and-datagrams.md) shows what travels on the wire.
- [Chapter 0.3](inside-a-slave.md) opens a slave and looks at its controller chip.
- [Chapter 0.4](state-machine.md) gives the states a slave goes through.
- [Chapter 0.5](process-data.md) shows how inputs and outputs move every cycle.
- [Chapter 0.6](mailboxes.md) shows the messages that are not cyclic.
- [Chapter 0.7](topology-and-clocks.md) covers cables, redundancy and clocks.
- [Chapter 0.8](device-descriptions.md) describes the files that describe a device.

[Chapter 1](principles.md) then says what KickCAT is for and the principles it
follows. [Chapter 2](../architecture.md) maps the code. The chapters after it
take KickCAT apart one design decision at a time.

If you already know EtherCAT, skip Part 0 and start at Chapter 1.

## One example through the guide

The guide follows one small device: the **Freedom K64F board**. It is an NXP
development board with a LAN9252 EtherCAT Slave Controller. Its firmware ships
with KickCAT, and so does its device description, an XML file that lists what
the board can do.

The board has:

- **3 output bytes**, written by the master: the red, green and blue levels of
  an LED.
- **12 input bytes**, read by the master: three accelerometer values and three
  magnetometer values, each a 16-bit signed integer.
- **A mailbox**, through which the master can read and write the board's
  configuration objects with CoE (CANopen over EtherCAT).

You do not need the board. The `simulated_bus` example builds an emulated copy
of it from its device description, and runs a real master against it in one
process. [Chapter 2](../architecture.md) reads that example from top to bottom.

## How a chapter is shaped

Each chapter follows the same order: the problem, the ways one could solve it,
what EtherCAT or KickCAT chose and why, and where that choice lives in the code.
A reader who only wants the code can skip to the "In the code" section at the
end of a chapter.

The guide gives the shape of the API: the main classes, the main calls and the
order of the calls. It does not list every function. For the exact signatures,
read the headers. When the guide and the code disagree, the code wins.

## The other documents

| Document | Answers |
|---|---|
| This guide | *Why* is KickCAT built this way, and how do its parts fit? |
| [Features](../FEATURES.md) | *What* works today, on the master and on the slave? |
| [Building](../BUILDING.md) | *How* do I build KickCAT and run its tests? |
| [Hardware](../HARDWARE.md) | *How* do I run KickCAT on real boards? |
| [Simulation](../SIMULATION.md) | *How* do I run a master against emulated slaves? |
| [Tools](../TOOLS.md) | *How* do I use the command-line tools and the GUIs? |
| [Performance](../PERFORMANCE.md) | *How* do I tune Linux for real-time? |
| The headers | *Exactly what* does each function take and return? |

## A reading path for a newcomer

1. **Part 0 in order**, from [Chapter 0.1](what-is-ethercat.md) to
   [Chapter 0.8](device-descriptions.md).
2. **[Chapter 1](principles.md)**, the principles.
3. **Build and run once.** Follow [Building](../BUILDING.md), then run
   `simulated_bus` as shown in [Chapter 2](../architecture.md). Seeing the slave
   reach OP makes the rest concrete.
4. **Pick your side.** A master developer reads Chapters
   [3](master.md), [4](transport.md) and [5](mailbox-implementation.md). A
   firmware developer reads Chapters [6](slave.md), [5](mailbox-implementation.md)
   and [9](from-xml-to-bytes.md).
5. **Branch out.** The other chapters can be read in any order. Before you change
   code, read [Chapter 11](testing.md).

## Where to go next

- [Chapter 0.1](what-is-ethercat.md), *What is EtherCAT?*, the first step of Part 0.
- The full list of chapters: [the guide's index](README.md).
