# Overview and principles

> Which design rules does KickCAT follow?
>
> You will learn:
>
> - where KickCAT runs
> - why the stack follows these rules
> - where each rule applies in the code

## What KickCAT is

KickCAT implements the master and slave sides of EtherCAT. The master runs as a
process on a host operating system (OS), such as Linux or PikeOS. The slave stack
runs on a microcontroller with an EtherCAT Slave Controller (ESC). A software ESC
and network let the master communicate with emulated slaves.

Applications such as robot controllers, test benches, and device firmware use
KickCAT as a library. Each application controls its main loop, threads, and memory.

## The application owns the loop and the thread

A real-time application decides which code runs, when it runs, and on which
processor core. A library thread would compete for processor time with its own
priority and schedule.

The stack runs only when the application calls it. The application controls when it
sends frames, processes replies, and stops waiting. The master application calls
`Bus` in its cycle. The slave application calls `routine()` in its main loop.

The application must call the stack often enough to receive data. For example, the
master must send a frame to read a mailbox reply. A helper can continue exchanging
process data while the master waits for a state change.

[Chapter 3](master.md) describes the master's cyclic loop. [Chapter 6](slave.md)
describes the slave's main loop.

## The caller owns the memory

Some real-time applications forbid memory allocation after the cycle starts. Others
need buffers in a specific area of random-access memory (RAM).

The application provides the memory for cyclic data. On the master, it passes its
process image buffer to `createMapping()`. On the slave, it provides the input and
output buffers and the object dictionary. The stack keeps pointers to them, so the
memory must remain valid while the stack uses it.

[Chapter 3](master.md) shows the layout of the
master's process image. [Chapter 6](slave.md) shows how the slave binds its
buffers to the object dictionary.

## An asynchronous core, with blocking helpers on top

A cyclic application must be able to do other work while a slave processes a
request. A setup tool can wait for a reply before continuing.

The master queues datagrams with callbacks for replies and errors. The application
sends them, then processes the replies later. It can also queue a mailbox message
and check its status later.

Blocking helpers use the same asynchronous operations. For example, a setup tool can
read one CANopen over EtherCAT (CoE) value and wait for the reply. Use asynchronous
calls in a real-time cycle.

[Chapter 4](transport.md) describes datagram callbacks. [Chapter
5](mailbox-implementation.md) describes mailbox requests and replies.

## Swappable hardware boundaries

KickCAT supports different network cards, slave controllers, and operating systems.
Tests and simulations must also run without EtherCAT hardware.

The master sends frames through an `AbstractSocket`. The slave uses `AbstractESC` to
access its controller. A controller on a Serial Peripheral Interface (SPI) bus uses
`AbstractSPI`. File over EtherCAT (FoE) uses `FoE::AbstractStorage` for file access.
Higher-level code uses the same interfaces on all targets.

The emulator replaces the controller and network. The master communicates with the
same slave stack that runs in firmware.

[Chapter 4](transport.md) lists the sockets,
[Chapter 6](slave.md) the controllers, [Chapter 8](simulation.md) the emulator,
and [Chapter 10](os-and-porting.md) the OS layer.

## One protocol codebase for both sides

The master and slave must use the same byte layout. Separate frame and mailbox
definitions can become inconsistent. Matching errors in the two definitions can also
hide a bug.

Both stacks use the same frame and datagram headers, electrically erasable
programmable read-only memory (EEPROM) layout, mailbox messages for CoE and FoE, and
object dictionary types. The master builds requests and the slave builds responses
from these types.

[Chapter 5](mailbox-implementation.md) describes requests and responses. [Chapter
9](from-xml-to-bytes.md) explains how both stacks use the same device description.

## Check the configuration once, then trust it

Repeated checks take time and increase code size on a microcontroller. A cyclic
application may have no way to recover from a configuration error during a cycle.

KickCAT checks the configuration during setup. The object dictionary generator
rejects an invalid dictionary. The master checks that the process image fits when it
creates the mapping. The stack then uses the configuration without repeating these
checks.

See [Chapter 6](slave.md) and [Chapter 9](from-xml-to-bytes.md) for the object
dictionary and [Chapter 3](master.md) for the process image.

## Errors: exceptions on the master, status codes on the slave

The master runs on a host OS where it can use exceptions. The slave runs on a
microcontroller. EtherCAT defines how a slave can refuse a state change and report
the reason with an Application Layer (AL) status code.

The master reports errors with exceptions. It uses error callbacks for cyclic
datagrams. The slave reports errors to the master with AL status codes.

See [Chapter 3](master.md) and [Chapter 4](transport.md) for master errors and
[Chapter 6](slave.md) for slave errors.

## A low barrier to entry

A master needs only a short `main`. It creates a socket, a link, and a bus, calls
`init()`, maps the process image, and starts the cyclic loop. A slave needs a
similar amount of code. The `simulated_bus` example runs both without hardware. See
[Chapter 2](../architecture.md).

## Summary

| Question | Answer |
|---|---|
| Who decides when the bus is used? | The application. The stack starts no thread. |
| Who owns the process data buffers? | The application. The stack keeps pointers. |
| How does a real-time cycle wait for a slave? | It does not. It sends, works, and processes answers later. |
| What changes between targets? | Only the code behind the socket, ESC, SPI, storage and OS interfaces. |
| When is configuration checked? | Once, when it is built. |
| How does a slave report an error? | With an AL status code, as EtherCAT defines. |

## Where to go next

- Source directories and an example program: [Chapter 2](../architecture.md).
- Master startup through the operational state (OP): [Chapter 3](master.md).
