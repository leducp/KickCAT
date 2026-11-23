# Overview and principles

> What is KickCAT for, and what rules does every part of it follow?
>
> You will learn:
>
> - what KickCAT sets out to be
> - the principles every later chapter applies, and why
> - which chapters develop each one

## What KickCAT is

KickCAT is an EtherCAT stack for both sides of the bus. The master stack runs
as a process on a host operating system (OS), such as Linux or PikeOS. The slave
stack runs on a microcontroller, next to an EtherCAT Slave Controller (ESC).
KickCAT also ships a software ESC and a software network, so a master can run
against slaves that exist only in memory.

KickCAT is a library, not a framework. It is meant to be embedded in a larger
program: a robot controller, a test bench, a firmware. That program already has
its own main loop, its own threads and its own memory plan. KickCAT must fit
into them, and not the reverse.

The principles below follow from that aim.

## The application owns the loop and the thread

**Why.** A real-time program decides exactly what runs, when, and on which
core. A library that starts its own thread takes that decision away: it adds a
thread with its own priority and its own timing, and the program must now share
the CPU with it.

**What KickCAT chose.** The stack starts no thread. Nothing happens on the bus
until the application calls the stack. The application decides when frames
leave, when replies are processed and how long it waits. On the master, it calls
the `Bus` in its cycle. On the slave, it calls `routine()` in its main loop.

The price is that the application must call the stack often enough. A mailbox
reply does not arrive by itself: someone must send the frame that fetches it.
Helpers exist for the common cases, such as a function that keeps the process
data moving while it waits for a state change.

**Where it is developed.** [Chapter 3](master.md) shows the master's cyclic loop.
[Chapter 6](slave.md) shows the slave's main loop.

## The caller owns the memory

**Why.** Embedded and real-time programs control their memory. Some forbid
allocation once the cycle starts. Some place buffers in a specific RAM region.
A stack that allocates its own buffers makes these choices for them.

**What KickCAT chose.** The data that the application reads and writes every
cycle lives in memory the application gives. On the master, the process image is
a buffer the application passes to `createMapping()`. On the slave, the input
and output buffers and the object dictionary belong to the application, and the
stack only keeps pointers to them. The application must keep this memory alive
as long as the stack uses it.

**Where it is developed.** [Chapter 3](master.md) shows the layout of the
master's process image. [Chapter 6](slave.md) shows how the slave binds its
buffers to the object dictionary.

## An asynchronous core, with blocking helpers on top

**Why.** A cyclic program cannot stop and wait for one slave to answer. It must
send its frame, do its work, and collect the answers later. A simple tool, on
the other hand, wants to call "read this value" and get the value back.

**What KickCAT chose.** The core of the master is asynchronous. The application
adds datagrams, each with a callback for the answer and a callback for errors,
then sends them and later processes what comes back. Mailbox messages work the
same way: the application queues a message and checks its status later.

Blocking functions, such as reading one CoE value, are built on top of this
core. They are convenient during setup and in tools. In a real-time cycle, use
the asynchronous calls.

**Where it is developed.** [Chapter 4](transport.md) follows one datagram and
its callbacks. [Chapter 5](mailbox-implementation.md) shows a mailbox message from
queue to answer.

## Swappable hardware boundaries

**Why.** The stack must run on many network cards, many slave controllers and
many operating systems. It must also run with no hardware at all, in tests and
in the simulator.

**What KickCAT chose.** Each place where the stack touches hardware is a small
abstract interface. The master sends frames through an `AbstractSocket`. The
slave reads and writes its controller through an `AbstractESC`, and a controller
on a serial bus uses an `AbstractSPI`. FoE (File over EtherCAT) reads and writes
files through an `FoE::AbstractStorage`. Everything above these interfaces is
the same on every target.

This choice is what makes the simulator honest. The emulator replaces the
controller and the wire, and nothing else. The master talks to the real slave
stack, the same code that runs in firmware.

**Where it is developed.** [Chapter 4](transport.md) lists the sockets,
[Chapter 6](slave.md) the controllers, [Chapter 8](simulation.md) the emulator,
and [Chapter 10](os-and-porting.md) the OS layer.

## One protocol codebase for both sides

**Why.** The master and the slave must agree on every byte. If each side has its
own definition of a frame or a mailbox header, the two can drift apart, and a bug
on one side hides a bug on the other.

**What KickCAT chose.** The protocol definitions are written once and used by
both stacks: the frame and datagram headers, the EEPROM layout, the CoE and FoE
messages and the object dictionary. The master builds requests and the slave
builds responses, but they share the types that describe the wire.

**Where it is developed.** [Chapter 5](mailbox-implementation.md) puts the
request and response sides next to each other. [Chapter 9](from-xml-to-bytes.md)
shows one device description feeding both sides.

## Check the configuration once, then trust it

**Why.** Checks cost time and code size, and a microcontroller has little of
both. A check that runs on every access is also a check that can fail in the
middle of the cycle, where there is no good way to recover.

**What KickCAT chose.** Configuration is checked when it is built, not when it
is used. The object dictionary generator refuses an invalid dictionary. The
master checks that the process image fits in the buffer when it creates the
mapping. After that, the stack trusts the configuration and does not check it
again at run time.

**Where it is developed.** [Chapter 6](slave.md) and
[Chapter 9](from-xml-to-bytes.md) for the object dictionary,
[Chapter 3](master.md) for the process image.

## Errors: exceptions on the master, status codes on the slave

**Why.** The two sides run in different worlds. The master runs on a host OS,
where exceptions are cheap to use and hard to ignore. The slave runs on a
microcontroller, and EtherCAT already defines how a slave reports a problem: it
refuses a state change and gives an Application Layer (AL) status code.

**What KickCAT chose.** The master reports errors with exceptions, and with
error callbacks for datagrams in the cyclic path. The slave reports errors to
the master the EtherCAT way, with AL status codes.

**Where it is developed.** [Chapter 3](master.md) and [Chapter 4](transport.md)
for the master, [Chapter 6](slave.md) for the slave.

## A low barrier to entry

**Why.** A stack that is hard to start with does not get used. EtherCAT already
has a lot to learn, and the stack should not add to it.

**What KickCAT chose.** A working master is a short `main`: create a socket, a
link and a bus, call `init()`, map the process image, and loop. A working slave
is the same size. The simulator lets you run both with no hardware. The
`simulated_bus` example, walked in [Chapter 2](../architecture.md), is the whole
of it.

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

- Where each part lives, and one example read from top to bottom:
  [Chapter 2](../architecture.md).
- The master, from discovery to OP: [Chapter 3](master.md).
