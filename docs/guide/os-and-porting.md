# Operating systems and porting

> What does KickCAT need from the operating system, and what must a new platform provide?
>
> You will learn:
>
> - why KickCAT has an OS layer, and how small it is kept
> - what each supported platform provides
> - how time works, on a target and in the unit tests
> - what a new OS, a new network backend or a new SPI driver must implement

## The problem: one stack, many systems

The master runs as a process on a host operating system (OS): Linux, PikeOS, or
Windows for tools and tests. The slave runs on a microcontroller with NuttX or
KickOS, or on a Raspberry Pi with Linux. These systems do not agree on how to
read a clock, how to send a raw Ethernet frame, or how to talk to an SPI
peripheral.

The protocol code must not care. A state machine or an SDO transfer is the same
on every system. If the protocol code called the OS directly, each port would add
`#ifdef` blocks to it, and each port would behave a little differently.

## The ways to support several systems

One way is to pick a portable library, such as POSIX, and require it everywhere.
This fails here: Windows and PikeOS are not POSIX, and a small RTOS (Real-Time
Operating System) may only offer part of it.

Another way is a large abstraction layer that wraps every OS service. That is a
lot of code to port, and most of it is not needed by an EtherCAT stack.

**KickCAT keeps a small OS layer with only what the stack and its tools need, and
selects one implementation per OS when the project is configured.**

The protocol code calls `now()`, writes to an `AbstractSocket` or to an
`AbstractESC`, and never names an OS. The OS layer is a set of small headers with
one source file per OS behind each of them. CMake picks the sources for the
target OS. There is no runtime switch, and no `#ifdef` in the protocol code.

## What the layer contains

The headers live in `kickcat/OS/`. Each one is small.

| Header | What it gives |
|---|---|
| `Time.h` | `now()`, a monotonic clock in nanoseconds, `since_unix_epoch()`, `sleep()`, and helpers built on them. |
| `Timer.h` | A periodic deadline for a cyclic loop, with overrun detection. It also owns the soft PLL that follows a distributed clock. |
| `SoftPll.h` | The software Phase-Locked Loop (PLL) used by `Timer` to stay in phase with the reference clock. |
| `Thread.h` | A thread wrapper with a name and a scheduling priority. |
| `Mutex.h`, `ConditionVariable.h` | Locking and waiting between threads. |
| `SharedMemory.h` | A named memory segment shared between processes. |
| `Filesystem.h` | A few file operations, used by the simulator and the tools. |

The stack itself starts no thread (see [Chapter 1](principles.md)). `Thread`,
`Mutex` and `ConditionVariable` are there for the applications, the tools and the
simulator, which do run threads. A target that runs only the slave stack does not
need them.

`types.h` selects the OS types (`os_mutex`, `os_socket`, `os_shm` and so on) from
a small `os_types.h` per OS, so the class layouts do not change their shape
between systems.

## Time

**All timeouts, deadlines and watchdogs use `now()`, a monotonic clock.** A
monotonic clock never goes backwards, even when the wall clock is set. A
timeout computed from the wall clock could end early or never end. The wall
clock, `since_unix_epoch()`, is only used where EtherCAT needs an absolute time:
the distributed clock system time.

Times are `std::chrono` durations, mostly `nanoseconds`. A duration carries its
unit in its type, so `100ms` and `100us` cannot be confused.

The unit tests replace the time source. They link their own `Time.cc` in place
of the real one. In it, `now()` moves forward by 1 ms at each call, and `sleep()`
returns at once. A test that waits for a 100 ms timeout then runs in a few calls,
with the same result every time.

## Platforms

Each platform takes only the parts it needs.

| Platform | Role | Raw frame backend | Notes |
|---|---|---|---|
| Linux | Master, simulator, tools. Slave on a Raspberry Pi. | `AF_PACKET` raw socket. Optional `AF_XDP` socket. | Full layer. RT-PREEMPT kernel recommended for real time. |
| Windows | Master for tools and tests | Npcap (`pcap`) | Full layer. Not real time. |
| PikeOS 5.1 | Master (ARMv8) | PikeOS shared buffer I/O (`vm_io_sbuf`) | Time, socket and error codes. |
| NuttX | Slave | none | Shares the Unix time backend. The slave talks to its ESC, not to a network card. |
| KickOS | Slave (experimental) | none | Real time source. Mutex, condition variable and shared memory are placeholders that throw if used. |

The KickOS build is an out-of-tree consumer: an example application finds an
installed KickOS package and builds KickCAT with the KickOS backend.

On Linux, the master picks the backend from the interface name:

- `eth0` opens a raw `AF_PACKET` socket.
- `xdp:eth0` opens an `AF_XDP` socket, which skips most of the kernel network
  stack. It is built only when the AF_XDP option is on.
- `tap:client` opens a `TapSocket` to the simulator (see
  [Chapter 8](simulation.md)).

## Endianness and packed frames

EtherCAT is little-endian, and **KickCAT supports little-endian hosts only.**
This lets the stack describe frames, datagram headers and registers as packed C++
structures, and read them in place, with no conversion.

A packed structure has no padding, so some of its fields are not aligned.
Reading them in place is an unaligned access. This is intended. It is safe on
every supported target, and the packed attribute tells the compiler to generate
the right code. For this reason the Continuous Integration (CI) sanitizer build
turns off the alignment check of UBSan (the undefined behaviour sanitizer). Do
not "fix" these accesses by copying every field.

`packed.h` hides the compiler difference: GCC and Clang use an attribute, and
MSVC uses `#pragma pack`.

## Porting

### A new operating system

A new OS needs:

1. An `os_types.h` for the OS, and a branch for it in `types.h`.
2. A `Time.cc`: `now()`, `since_unix_epoch()` and `sleep()`. This is the one part
   every target needs.
3. The other primitives the target uses: a `Timer` wait if it runs a cyclic master
   loop, `Mutex`, `ConditionVariable`, `Thread` and `SharedMemory` if its
   applications use them. A primitive that is not ready yet can start as a
   placeholder that throws on use, as the KickOS backend does, so that the library
   still links.
4. A branch in `lib/CMakeLists.txt` that lists these sources.

Start by copying the closest backend. NuttX shows how little a slave-only target
needs. Unix shows a full one.

### A new network backend for the master

Implement `AbstractSocket`: `open()` an interface, `setTimeout()`, `read()` one
frame, `write()` one frame and `close()`. A read or a write returns the number
of bytes, or a negative `errno` code, for instance when no frame arrived before
the timeout. The `Link` above it handles
datagrams, frame indexes, lost frames and redundancy, so a socket only moves
whole Ethernet frames. Then give the socket a name prefix in `createSockets()`,
so that programs can select it from the command line.

### A new SPI driver for a slave

A slave with an SPI ESC, such as the LAN9252, reaches the chip through
`AbstractSPI`. A driver implements `open()`, `close()`, `transfer()`, the chip
select, the baud rate and the SPI mode. The drivers for NuttX, the Raspberry Pi
(through the bcm2835 library) and KickOS are short examples. An ESC with another
interface implements `AbstractESC` directly (see [Chapter 6](slave.md)).

## Summary

| Question | Answer |
|---|---|
| How is the OS selected? | At configure time, by CMake. No runtime switch. |
| Which clock do timeouts use? | `now()`, monotonic, in nanoseconds. |
| Why do unit tests run fast? | Their `now()` is a counter and `sleep()` returns at once. |
| Does the stack start threads? | No. `Thread` is for applications and tools. |
| Which byte order is supported? | Little-endian only. |
| Are unaligned accesses a bug? | No. Packed frames are read in place on purpose. |
| What does a new master backend implement? | `AbstractSocket`. |
| What does a new SPI slave board implement? | `AbstractSPI`. |

## In the code

- `lib/include/kickcat/OS/`: the OS layer headers.
- `lib/src/OS/`: the sources shared by every OS, and one directory per backend
  (`Unix`, `Linux`, `Windows`, `PikeOS`, `NuttX`, `KickOS`, and `Test` for the
  unit test clock).
- `lib/include/kickcat/types.h` and `lib/include/kickcat/OS/*/types/os_types.h`: the OS types.
- `lib/CMakeLists.txt`: the backend selection.
- `lib/include/kickcat/AbstractSocket.h`: the master network interface.
- `lib/master/src/helpers.cc`: `createSockets()`, which maps an interface name to a socket.
- `lib/slave/include/kickcat/AbstractSPI.h` and `lib/slave/driver/`: the SPI interface and drivers.
- `lib/include/kickcat/packed.h`: the packed structure macros.
- Example to read: `examples/slave/lan9252/rpi/linux/main.cc`, a slave on Linux
  that uses the Raspberry Pi SPI driver.

## Where to go next

- How to build for each platform: [Building KickCAT](../BUILDING.md).
- How to tune Linux for real time, and how to enable AF_XDP:
  [Real-time performance tuning](../PERFORMANCE.md).
- How to build and flash slave firmware: [Hardware guide](../HARDWARE.md).
- What `Link` does above the socket: Chapter 4,
  [Transport: links, frames and sockets](transport.md).
- How the timer follows the distributed clock: Chapter 7,
  [Distributed clocks](distributed-clocks.md).
- How the slave stack uses its ESC: Chapter 6, [The slave stack](slave.md).
