# Testing a change

> Which test proves that a change works, and where does it go?
>
> You will learn:
>
> - how KickCAT tests are organized, from one class to a full bus
> - which test double replaces which hardware boundary
> - how the emulator lets a test run the master and the slave together
> - what the Continuous Integration (CI) checks beyond the unit tests

## The problem: a stack between two kinds of hardware

KickCAT sits between a network card and an EtherCAT Slave Controller (ESC). A
test cannot rely on either one. It must run on any developer machine and in CI,
with no EtherCAT port. It must also be fast and give the same result every time,
or nobody will trust it.

Yet the behaviours that matter often cross several layers. A lost frame starts
in the socket, is handled by the link, and must end as an error callback in the
bus. A state change starts in the master, crosses the wire, and is decided by
the slave state machine.

## The ways to test

Two styles pull in opposite directions.

- **Isolate one class**, and replace its neighbours with test doubles. The test is
  small and fast, and a failure points to one place. But the doubles encode an
  assumption about the neighbours, and the assumption can be wrong.
- **Run the whole system.** The test proves the parts work together. But it is
  slower, and a failure can come from anywhere.

**KickCAT tests a behaviour at the lowest layer that shows it, and uses the real
stacks through the emulator when a behaviour crosses the master and the slave.**

The hardware boundaries described in [Chapter 1](principles.md) make both styles
cheap. Each boundary is an interface, so a test can replace it with a mock. The
emulator implements the same interfaces, so a test can also replace the
hardware with a software ESC and a software network, and keep all the real code.

## The unit test binary

All unit tests build into one GoogleTest program, `kickcat_unit`. A source file
is named after what it tests, with a `-t.cc` suffix, under `unit/src/`.
Subdirectories group an area: `slave/`, `mailbox/CoE/`, `mailbox/FoE/`, `CoE/`,
`ESI/`, `ENI/` and `utils/`. Small XML fixtures for the parsers sit in `unit/`.

The commands to build it, run it and filter tests are in
[Building KickCAT](../BUILDING.md).

### A deterministic clock

The unit test program links its own time source in place of the real one. Its
`now()` moves forward by 1 ms on each call, and `sleep()` returns at once. So a
test that waits for a timeout runs in a few calls and never depends on the load
of the machine. A test can reset this clock with `resetMockClock()`.

### Test doubles

The mocks in `unit/mocks/` each replace one boundary.

| Mock | Replaces | Use it to test |
|---|---|---|
| `MockLink` | `AbstractLink` | The `Bus`: it queues the datagrams the bus sends and returns the answers the test prepared. |
| `MockSocket` | `AbstractSocket` | The `Link`: it checks each frame written and forges the frame read back, including lost and corrupted frames. |
| `MockESC` | `AbstractESC` | The slave stack: the state machine, the PDO and the mailbox, register by register. |
| `MockSPI` | `AbstractSPI` | An SPI ESC driver such as the LAN9252. |
| `ESMStateTest` | (a fixture) | One state of the slave state machine, with a mock ESC and its SyncManagers ready. |
| `PhysicalSocket` | a master cable | Redundancy: it routes a frame through an emulated network and returns it on the right master port, or not at all when the cable is cut. |

## Tests with the real stacks

Some behaviours only make sense with a master and a slave together:
distributed clocks, FoE file transfers, redundancy, a simulated drive. For these,
a test builds emulated slaves and a real `Bus`, and connects them with a
`LoopbackSocket` (see [Chapter 8](simulation.md)). It is the same assembly as the
`simulated_bus` example, inside a test.

Such a test runs in one thread and in one process. Each master frame passes
through the emulated network, then the slave stack runs once. So the test is
still repeatable, and a debugger can follow a frame from the master to the slave
and back.

## Integration programs

The programs in `test/integration/bench/` go beyond one test case.

- `esi_boot` takes an ESI catalog, builds an emulated slave for each device, and
  drives each one to OP through a loopback. Reaching OP is the proof that the ESI
  parser, the EEPROM builder, the dictionary and both stacks agree on the device.
- `esi_validate` is a faster, static check. It runs the slave mapping resolution
  on each device, without changing states.
- `hw_test_bench` runs on real hardware for a long time. It counts working
  counter errors and lost frames, and fails when they exceed a calibrated
  baseline. It is part of the hardware qualification of a release.

`scripts/test_install.sh` checks the installed package: it builds a small
consumer with CMake `find_package` and with pkg-config.

## What CI runs

Beyond `kickcat_unit` on each platform, CI runs:

- **Sanitizers.** A Debug build with ASan, LSan and UBSan (the address, leak and
  undefined behaviour sanitizers) runs all unit tests. The UBSan alignment check
  is off on purpose: packed frames are read in place (see
  [Chapter 10](os-and-porting.md)).
- **Coverage.** A change must not lower the code coverage (see
  [Contributing](../CONTRIBUTING.md)).
- **Simulation tests.** Master examples, in C++ and in Python, run against
  `network_simulator` through the shared memory socket. A separate case runs an
  FoE round trip.
- **Install test**, slave firmware builds for the NuttX boards, and the
  documentation build.

Real hardware is not in CI. It is tested before a release (see the
[release procedure](../RELEASE.md)).

## Choosing a test

| I changed... | Write... | Start from |
|---|---|---|
| A bus sequence (init, mapping, state requests) | a unit test with `MockLink` | `unit/src/bus-t.cc` |
| Frame handling, lost frames, the frame index window | a unit test with `MockSocket` | `unit/src/link-t.cc` |
| Redundancy or a split ring | a test with `PhysicalSocket` on an emulated ring | `unit/src/redundancy-t.cc` |
| A slave state transition | an `ESMStateTest` case | `unit/src/ESMStateSafeOP-t.cc` and the other states |
| Slave process data mapping | a unit test with `MockESC` | `unit/src/slave/PDO-t.cc` |
| A mailbox message (CoE, FoE) | a request or response unit test | `unit/src/mailbox/` |
| Behaviour across master and slave | a loopback test with emulated slaves | `unit/src/FoE-sim-t.cc`, `unit/src/dc-t.cc` |
| The emulated ESC or network | a unit test on the emulator | `unit/src/EmulatedESC-t.cc`, `unit/src/EmulatedNetwork-t.cc` |
| A simulated device behaviour | a unit test on the device | `unit/src/Ds402Motor-t.cc` |
| The ESI, SII or ENI parsers | a parser test with an XML fixture | `unit/src/ESI/`, `unit/src/ENI/` |
| A time-dependent algorithm | a unit test on the deterministic clock | `unit/src/SoftPll-t.cc`, `unit/src/Timer-t.cc` |

## Summary

| Question | Answer |
|---|---|
| Where do unit tests go? | `unit/src/`, one `-t.cc` file per subject, all in `kickcat_unit`. |
| Why are timeouts fast in tests? | The test clock advances 1 ms per call and `sleep()` does nothing. |
| How do I replace hardware? | With the mock of its interface, or with the emulator. |
| When do I use the emulator? | When the behaviour crosses the master and the slave. |
| What runs on real hardware? | `hw_test_bench`, before a release. |

## In the code

- `unit/src/`: the unit tests.
- `unit/mocks/`: `Link.h`, `Sockets.h`, `ESC.h`, `SPI.h`, `ESMStateTest.h`,
  `EmulatedNetworkHelpers.h`, `Time.h`.
- `lib/src/OS/Test/Time.cc`: the deterministic test clock.
- `test/integration/bench/`: `esi_boot.cc`, `esi_validate.cc`, `hw_test_bench.cc`.
- `simulation/ci/`: the scripts of the CI simulation tests.
- `scripts/test_install.sh`: the install test.
- Example to read: `unit/src/FoE-sim-t.cc` runs a real master against an emulated
  slave in one test.

## Where to go next

- How to build and run the tests, and how to measure coverage:
  [Building KickCAT](../BUILDING.md).
- How the emulator works: Chapter 8, [Simulation internals](simulation.md).
- Why unaligned accesses are allowed: Chapter 10,
  [Operating systems and porting](os-and-porting.md).
- Code conventions and pull request rules: [Contributing](../CONTRIBUTING.md).
