# KickCAT simulator

Run a master against emulated EtherCAT slaves with no physical hardware. Each
emulated slave is an `EmulatedESC` (the ESC registers, SyncManagers, FMMUs and
EEPROM) driven by the slave stack (`Slave` + `PDO` + optional CoE mailbox). A
slave is built either from a pre-made EEPROM `.bin` or, preferably, **compiled
from an ESI XML device** (the parser produces the SII image and the CoE object
dictionary).

There are two ways to wire a master to emulated slaves.

## 1. In-process loopback (simplest)

Master `Bus` and the emulated slave live in the **same process**, connected by
`kickcat::LoopbackSocket` (`lib/include/kickcat/LoopbackSocket.h`). No network
interface, no `/dev/shm`, no second terminal. Each master frame is run through
the slave and the slave is ticked once. This is the recommended starting point
and the easiest to debug (single process, single thread).

Canonical example — `examples/master/simulated_bus`:

```bash
./build/examples/master/simulated_bus/simulated_bus -f "Beckhoff EL1xxx.xml" -t EL1008
```

It builds the selected device, drives INIT → PRE_OP → SAFE_OP → OPERATIONAL, and
exchanges process data — all in ~150 lines you can read top to bottom.

`test/integration/bench/esi_boot` uses the same loopback to boot an entire ESI
catalog in parallel (a good template for batch/regression use).

## 2. Two-process over a TAP socket (`network_simulator`)

`network_simulator` runs the slaves in their own process and exposes them on a
shared-memory **TAP socket**; a separate master process connects to the other
end. Closer to a real two-node setup; useful for CI and for running an unmodified
master example against the simulator.

```bash
# Terminal 1 — the simulator is the TAP server (start it first):
./build/simulation/network_simulator -i tap:server -s simulation/slave_configs/freedom-k64f.json

# Terminal 2 — the master is the TAP client:
sudo ./build/examples/master/easycat/easycat_example -i tap:client
```

To boot from an ESI device instead, point a config's `esi` key at your vendor's
ESI XML (see the schema below); the in-process example above is the quickest way
to try one.

Chain several slaves by passing multiple configs, or repeat one with `-n N`:

```bash
./build/simulation/network_simulator -i tap:server \
    -s simulation/slave_configs/freedom-k64f.json simulation/slave_configs/xmc4800.json
```

### Named TAP segments

`tap:server` / `tap:client` use the default shared-memory name. Append `:<name>`
to run independent server/client pairs side by side:

```bash
./build/simulation/network_simulator -i tap:server:busA -s ...   # one bus
./build/examples/master/.../example   -i tap:client:busA          # its master
```

## Slave config schema (JSON)

One JSON object per slave file. Provide **exactly one** source of the slave
image — `esi` or `eeprom`:

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
