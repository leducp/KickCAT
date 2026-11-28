# Testing a change

> Which test should you write for a change to the EtherCAT stack?
>
> You will learn:
>
> - how the tests are organized
> - which mock to use for each interface
> - how to test both stacks with the emulator
> - what CI checks

## The problem: a stack between two kinds of hardware

Master tests need control over network frames, while slave tests need access to
an ESC. Developers and CI must be able to run both types of tests quickly and
repeatably without EtherCAT hardware.

Some operations involve several layers. For a lost frame, the socket reports the
loss, the link handles it, and the bus receives an error callback. For a state
change, the master sends the request and the slave state machine accepts or refuses
it.

## The ways to test

The scope of a test affects what a failure tells you.

- **Test one class.** Replace its dependencies with test doubles. These tests run
  quickly and help locate failures. Their results depend on whether the doubles
  accurately represent the dependencies.
- **Test the system.** Run the parts together to check how they interact. These
  tests take longer, and a failure can be harder to locate.

Test a behavior at the lowest layer where you can observe it. Use both stacks with
the emulator when a test needs both master and slave behavior.

The hardware interfaces described in [Overview and principles](../architecture/principles.md) support both
approaches. A unit test can replace one interface with a mock. An integration test
can run the stacks with a software ESC and network.

## The unit test binary

The GoogleTest program `kickcat_unit` contains all unit tests. Test files in
`unit/src/` use the subject name with the suffix `-t.cc`. The directories
`slave/`, `mailbox/CoE/`, `mailbox/FoE/`, `CoE/`, `ESI/`, `ENI/`, and `utils/`
group related tests. Small XML parser fixtures are in `unit/`.

The commands to build it, run it and filter tests are in
[Building KickCAT](../../user-guide/building.md).

### A deterministic clock

The test program links its own clock. Each call to `now()` advances it by 1 ms, and
`sleep()` returns immediately. Timeout tests therefore run independently of host
load. `resetMockClock()` resets the clock.

### Test doubles

The mocks in `unit/mocks/` each replace one boundary.

| Mock | Replaces | Use it to test |
|---|---|---|
| `MockLink` | `AbstractLink` | The `Bus`: it queues the datagrams the bus sends and returns the answers the test prepared. |
| `MockSocket` | `AbstractSocket` | The `Link`: it checks outgoing frames and simulates replies, loss, and corruption. |
| `MockESC` | `AbstractESC` | The slave stack: the state machine, PDO handling and the mailbox, register by register. |
| `MockSPI` | `AbstractSPI` | An SPI driver for an ESC such as the LAN9252. |
| `ESMStateTest` | (a fixture) | One state of the slave state machine, with a mock ESC and its SyncManagers ready. |
| `PhysicalSocket` | a master cable | Redundancy: it routes a frame through an emulated network and returns it on the right master port, or not at all when the cable is cut. |

## Tests with the real stacks

Use both stacks to test distributed clocks, FoE transfers, redundancy, or a
simulated drive. Connect a `Bus` to emulated slaves with a `LoopbackSocket` (see
[Simulation internals](../architecture/emulation.md)). The `simulated_bus` example uses the same setup.

Each frame passes through the emulated network before the slave stack runs once. The
test uses one thread in one process, so the order is repeatable. You can follow a
frame through both stacks in a debugger.

## Integration programs

The integration programs are in `test/integration/bench/`.

- `esi_boot` reads an ESI catalog, builds an emulated slave for each device, and
  requests OP through a loopback socket. Reaching OP checks that the ESI parser,
  EEPROM image builder, dictionary, and both stacks agree on the device.
- `esi_validate` checks each device's process data mapping without requesting
  state changes.
- `hw_test_bench` runs long tests on hardware. It counts WKC errors and lost
  frames, and fails if the counts exceed a calibrated baseline. It is part of
  release qualification.

`scripts/test_install.sh` checks the installed package by building a small
application with CMake `find_package` and with pkg-config.

## What CI runs

Beyond `kickcat_unit` on each platform, CI runs:

- **Sanitizers.** A debug build runs all unit tests with AddressSanitizer (ASan),
  LeakSanitizer (LSan), and Undefined Behavior Sanitizer (UBSan). It disables the
  UBSan alignment check because the code reads packed frames in place (see [Operating systems and porting](../architecture/platform.md)).
- **Coverage.** Changes must maintain or increase code coverage (see
  [Contributing](index.md)).
- **Simulation.** C++ and Python master examples use `network_simulator` through a
  shared memory socket. A separate test checks a FoE round trip.
- **Builds and installation.** CI checks the installed package and builds the NuttX
  board firmware and documentation.

Hardware tests run before a release (see the [release procedure](release.md)).

## Choosing a test

| I changed... | Write... | Start from |
|---|---|---|
| A bus sequence (init, mapping, state requests) | a unit test with `MockLink` | `unit/src/bus-t.cc` |
| Frame handling, lost frames, the frame index window | a unit test with `MockSocket` | `unit/src/link-t.cc` |
| Redundancy or a split ring | a test with `PhysicalSocket` on an emulated ring | `unit/src/redundancy-t.cc` |
| A slave state transition | an `ESMStateTest` case | `unit/src/ESMStateSafeOP-t.cc` and the other states |
| Slave process data mapping | a unit test with `MockESC` | `unit/src/slave/PDO-t.cc` |
| A CoE or FoE message | a request or response unit test | `unit/src/mailbox/` |
| Behavior across master and slave | a loopback test with emulated slaves | `unit/src/FoE-sim-t.cc`, `unit/src/dc-t.cc` |
| The emulated ESC or network | a unit test on the emulator | `unit/src/EmulatedESC-t.cc`, `unit/src/EmulatedNetwork-t.cc` |
| A simulated device behavior | a unit test on the device | `unit/src/Ds402Motor-t.cc` |
| The ESI, SII, or ENI parser | a parser test with an XML fixture | `unit/src/ESI/`, `unit/src/ENI/` |
| A time-dependent algorithm | a unit test on the deterministic clock | `unit/src/SoftPll-t.cc`, `unit/src/Timer-t.cc` |

## Summary

| Question | Answer |
|---|---|
| Where do unit tests go? | `unit/src/`, one `-t.cc` file per subject, all in `kickcat_unit`. |
| Why are timeouts fast in tests? | The test clock advances 1 ms per call and `sleep()` does nothing. |
| How do I replace hardware? | With the mock of its interface, or with the emulator. |
| When do I use the emulator? | When the behavior crosses the master and the slave. |
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
  [Building KickCAT](../../user-guide/building.md).
- How the emulator works: [Simulation internals](../architecture/emulation.md).
- Why unaligned accesses are allowed: [Operating systems and porting](../architecture/platform.md).
- Code conventions and pull request rules: [Contributing](index.md).
