# Operating systems and porting

> What does KickCAT need from a new OS or driver?
>
> You will learn:
>
> - which OS services KickCAT uses
> - what each supported platform provides
> - how targets and unit tests measure time
> - which interfaces to implement for a new OS, network backend, or SPI driver

## The problem: one stack, many systems

The master runs on Linux and PikeOS. It also runs on Windows for tools and tests.
The slave runs on NuttX or KickOS on a microcontroller, or on Linux on a Raspberry
Pi. Each system provides different calls for clocks, raw Ethernet frames, and SPI
devices.

The EtherCAT state machine and SDO transfers work the same way on each system.
Direct OS calls in this code would require `#ifdef` blocks and introduce
platform-specific behavior.

## The ways to support several systems

One option is to require a common interface such as the Portable Operating System
Interface (POSIX). Windows and PikeOS do not provide POSIX, and a small real-time OS
may implement only part of it.

Another option is to wrap every OS service. This adds porting work for services the
stack never uses.

KickCAT provides only the OS services the stack and its tools need.

Protocol code calls `now()` and uses `AbstractSocket` and `AbstractESC` to access
the network and ESC. Each OS interface has a header and platform-specific source
files. CMake selects the implementation at configure time, so protocol code needs
no runtime platform check or `#ifdef`.

## What the layer contains

The OS headers are in `kickcat/OS/`.

| Header | What it gives |
|---|---|
| `Time.h` | `now()`, a monotonic clock in nanoseconds, `since_unix_epoch()`, `sleep()`, and helpers built on them. |
| `Timer.h` | A periodic deadline for a cyclic loop, with overrun detection. It also owns the software PLL that follows a distributed clock. |
| `SoftPll.h` | The PLL used by `Timer` to stay in phase with the reference clock. |
| `Thread.h` | A thread wrapper with a name and a scheduling priority. |
| `Mutex.h`, `ConditionVariable.h` | Locking and waiting between threads. |
| `SharedMemory.h` | A named memory segment shared between processes. |
| `Filesystem.h` | A few file operations, used by the simulator and the tools. |

Applications, tools, and the simulator can use `Thread`, `Mutex`, and
`ConditionVariable`. The stack starts no threads (see [Overview and principles](principles.md)). A
slave-only target can omit these services.

`types.h` selects types such as `os_mutex`, `os_socket`, and `os_shm` from the
target's `os_types.h`. This lets the classes use the same declarations on each
system.

## Time

Timeouts, deadlines, and watchdogs use `now()`. This clock is monotonic, so changing
the wall clock cannot move it backward or change a timeout. KickCAT uses the wall
clock, `since_unix_epoch()`, for absolute EtherCAT distributed clock time.

Times use `std::chrono` durations, usually `nanoseconds`. Each type specifies its
unit, including the units in `100ms` and `100us`.

Unit tests link a test `Time.cc`. Each `now()` call advances time by 1 ms, and
`sleep()` returns immediately. A test can exercise a 100 ms timeout without waiting
100 ms.

## Platforms

| Platform | Role | Raw frame backend | Notes |
|---|---|---|---|
| Linux | Master, simulator, tools. Slave on a Raspberry Pi. | `AF_PACKET` raw socket. Optional eXpress Data Path socket (`AF_XDP`). | Full layer. A kernel with real-time preemption (RT-PREEMPT) is recommended for real-time use. |
| Windows | Master for tools and tests | Npcap (`pcap`) | Full layer. Not real time. |
| PikeOS 5.1 | Master (ARMv8) | PikeOS shared buffer input/output (I/O) (`vm_io_sbuf`) | Time, socket and error codes. |
| NuttX | Slave | none | Shares the Unix time backend. The slave accesses the network through its ESC. |
| KickOS | Slave (experimental) | none | Platform time source. Mutex, condition variable and shared memory are placeholders that throw if used. |

The KickOS example builds outside the KickOS source tree. It finds the installed
KickOS package and builds KickCAT with the KickOS backend.

On Linux, the master selects the backend from the interface name:

- `eth0` opens a raw `AF_PACKET` socket.
- `xdp:eth0` opens an `AF_XDP` socket, which skips most of the kernel network
  stack. It is built only when the AF_XDP option is on.
- `tap:client` opens a `TapSocket` to the simulator (see
  [Simulation internals](emulation.md)).

## Endianness and packed frames

EtherCAT uses little-endian byte order. KickCAT supports little-endian hosts and
reads packed C++ structures for frames, datagram headers, and registers directly
without byte-order conversion.

Packed structures have no padding, so some fields can be unaligned. Supported
targets can read them in place, and the packed attribute tells the compiler how
to access them. For this reason, the CI build disables the alignment check in the
Undefined Behavior Sanitizer (UBSan). Keep these in-place accesses when changing
the code.

`packed.h` selects the compiler syntax. The GNU Compiler Collection (GCC) and Clang
use an attribute. Microsoft Visual C++ (MSVC) uses `#pragma pack`.

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

Start with the existing backend closest to your target. NuttX provides a slave-only
example. Unix provides a full implementation.

### A new network backend for the master

Implement `AbstractSocket` with `open()`, `setTimeout()`, `read()`, `write()`, and
`close()`. Each read or write handles one frame and returns a byte count or a
negative `errno` code. A timeout is one possible error. `Link` handles datagrams,
frame indexes, lost frames, and redundancy. Register a prefix in `createSockets()`
so applications can select the socket by name.

### A new SPI driver for a slave

A slave with an SPI ESC, such as the LAN9252, reaches the chip through
`AbstractSPI`. A driver implements `open()`, `close()`, `transfer()`, the chip
select, the baud rate and the SPI mode. The drivers for NuttX, the Raspberry Pi
(through the bcm2835 library) and KickOS are short examples. An ESC with another
interface implements `AbstractESC` directly (see [The slave stack](slave.md)).

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
- `lib/include/kickcat/types.h` and `lib/include/kickcat/OS/*/types/os_types.h`: the
  OS types.
- `lib/CMakeLists.txt`: the backend selection.
- `lib/include/kickcat/AbstractSocket.h`: the master network interface.
- `lib/master/src/helpers.cc`: `createSockets()`, which maps an interface name to a socket.
- `lib/slave/include/kickcat/AbstractSPI.h` and `lib/slave/driver/`: the SPI
  interface and drivers.
- `lib/include/kickcat/packed.h`: the packed structure macros.
- Example to read: `examples/slave/lan9252/rpi/linux/main.cc`, a slave on Linux
  that uses the Raspberry Pi SPI driver.

## Where to go next

- How to build for each platform: [Building KickCAT](../../user-guide/building.md).
- How to tune Linux for real time, and how to enable AF_XDP:
  [Real-time performance tuning](../../user-guide/realtime.md).
- How to build and flash slave firmware: [Hardware guide](../../user-guide/hardware.md).
- What `Link` does above the socket: [Transport: links, frames and sockets](transport.md).
- How the timer follows the distributed clock: [Distributed clocks](distributed-clocks.md).
- How the slave stack uses its ESC: [The slave stack](slave.md).
