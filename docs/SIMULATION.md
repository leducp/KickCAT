# Simulation and emulation

KickCAT ships with a software EtherCAT Slave Controller and a network fabric, so
a master application can be developed and tested with no physical hardware.

There are two layers:

- The **emulator** is the engine: `EmulatedESC` is a software ESC (registers,
  SyncManagers, FMMUs, EEPROM, DC clock) and `EmulatedNetwork` wires emulated
  ESCs into a topology. Both live in `lib/slave/include/kickcat/`.
- The **simulator** is how you run a master against the emulator: either
  in-process (the `simulated_bus` example) or as a separate process
  (`network_simulator`, under `simulation/`).

---

## Emulator engine

### EmulatedESC

`EmulatedESC` (`lib/slave/include/kickcat/ESC/EmulatedESC.h`) models a complete
EtherCAT Slave Controller and is driven by the real slave stack
(`Slave` + `PDO` + optional CoE mailbox). It implements:

- 16 SyncManagers, including mailbox SyncManagers (RxSM/TxSM).
- 16 FMMUs (logical-to-physical process-data mapping).
- AL (Application Layer) state machine registers and event masks.
- DL (Data Link) port descriptors, per-port link status, and error counters.
- EEPROM loaded from a raw `.bin` or compiled from an ESI XML device.
- A DC local clock with configurable drift (ppm), receive-time latching,
  system-time offset, and SYNC0.
- PDI and process-data watchdogs.
- Per-port loopback behaviour (open/closed ports, circulating frames).

A slave image is built either from a pre-made EEPROM `.bin` or, preferably,
compiled from an **ESI XML device** (the parser produces the SII image and the
CoE object dictionary).

### EmulatedNetwork

`EmulatedNetwork` (`lib/slave/include/kickcat/EmulatedNetwork.h`) routes frames
between emulated ESCs as a physical network would:

- Explicit port-to-port wiring (default is a daisy-chained line topology).
- Frame walk in physical EtherCAT port order, branching depth-first.
- Head and tail master injection points for cable redundancy, with ring-closure
  detection.
- Runtime wire break/heal (fault injection) via link-state changes.
- DC forwarding delays computed from the topology.

### Current limitations

- No interrupt emulation.
- See the matrix in [FEATURES.md](FEATURES.md) for DC and redundancy status.

---

## Run modes

### 1. In-process loopback (simplest)

The master `Bus` and the emulated slave live in the **same process**, connected
by `kickcat::LoopbackSocket` (`lib/include/kickcat/LoopbackSocket.h`). No network
interface, no `/dev/shm`, no second terminal. Each master frame is run through
the slave and the slave is ticked once. This is the recommended starting point
and the easiest to debug (single process, single thread).

Canonical example -- `examples/master/simulated_bus`:

```bash
./build/examples/master/simulated_bus/simulated_bus \
    -f examples/slave/nuttx/lan9252/freedom-k64f/freedom-k64f.xml -t Board
```

`-f` takes any vendor ESI XML and `-t` selects the device by its `<Type>`
(omit `-t` to use the first device in the file). It builds the selected device,
drives INIT -> PRE_OP -> SAFE_OP -> OPERATIONAL, and exchanges process data --
all in a short example you can read top to bottom.

Beckhoff publish ESI files for their devices at
<https://download.beckhoff.com/download/configuration-files/io/ethercat/xml-device-description>;
most vendors ship the matching ESI XML with their hardware.

`test/integration/bench/esi_boot` uses the same loopback to boot an entire ESI
catalog in parallel (a good template for batch/regression use).

### 2. Two-process over a TAP socket (network_simulator)

`network_simulator` runs the slaves in their own process and exposes them on a
shared-memory **TAP socket**; a separate master process connects to the other
end. This is closer to a real two-node setup and lets an unmodified master
example run against the simulator.

```bash
# Terminal 1 -- the simulator is the TAP server (start it first):
./build/simulation/network_simulator -i tap:server -s simulation/slave_configs/freedom-k64f.json

# Terminal 2 -- the master is the TAP client:
sudo ./build/examples/master/easycat/easycat_example -i tap:client
```

To boot from an ESI device instead, point a config's `esi` key at your vendor's
ESI XML (see the schema below).

Chain several slaves by passing multiple configs:

```bash
./build/simulation/network_simulator -i tap:server \
    -s simulation/slave_configs/freedom-k64f.json simulation/slave_configs/xmc4800.json
```

#### Named TAP segments

`tap:server` / `tap:client` use the default shared-memory name. Append `:<name>`
to run independent server/client pairs side by side:

```bash
./build/simulation/network_simulator -i tap:server:busA -s ...   # one bus
./build/examples/master/.../example   -i tap:client:busA         # its master
```

#### Different machines

If the master and the simulator run on **different machines**, use real network
interfaces or a virtual Ethernet pair instead of a TAP socket:

```bash
# Create a virtual Ethernet pair (Linux, same machine, alternative to TAP)
sudo ./simulation/create_virtual_ethernet.sh create veth
```

---

## Slave config schema (JSON)

One JSON object per slave file. Provide **exactly one** source of the slave
image -- `esi` or `eeprom`:

| Key            | Type   | Notes                                                              |
|----------------|--------|--------------------------------------------------------------------|
| `esi`          | string | Path to an ESI XML (relative to the config dir). Compiles the SII + CoE dictionary. |
| `device_type`  | string | With `esi`: pick the device by `<Type>` (e.g. `"EL1008"`).         |
| `product_code` | uint32 | With `esi`: pick the device by product code.                       |
| `revision_no`  | uint32 | With `esi`: pick the device by revision.                           |
| `eeprom`       | string | Path to a raw EEPROM `.bin` (alternative to `esi`).                |
| `coe_xml`      | string | With `eeprom`: optional CoE dictionary XML for the mailbox.        |

When several `esi` device filters match, the first match is used. With `esi`,
the CoE mailbox (if the device declares CoE) is built from the same device, so
`coe_xml` is not needed.

Examples live in `simulation/slave_configs/`.
