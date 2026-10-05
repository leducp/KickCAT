# ENI support

KickCAT can load an EtherCAT Network Information (ENI) XML file and execute its
startup commands to configure a master bus. This support is **experimental** and
has been tested only with the in-process simulator.

Enable `eni_parser` to load XML files. The `eni_sequencer` example also needs
`esi_parser` and `master_examples` (all enabled by default). See
[BUILDING.md](BUILDING.md) for prerequisites:

```bash
source .venv/bin/activate
./scripts/configure.sh build --with=eni_parser --with=esi_parser --with=master_examples
./scripts/setup_build.sh build
cd build && make -j
cd ..
./build/examples/master/eni_sequencer/eni_sequencer \
    -n unit/kickcat_eni_test_drives.xml \
    -e simulation/slave_configs/ecat402-drive.xml
```

`eni_sequencer` creates a simulated bus from the ESI file. To inspect an ENI
without starting a bus, run `./build/examples/master/load_eni/load_eni -f FILE`.

## Using the sequencer

`ENI::loadFile()` returns a configuration for `ENI::Sequencer`. Keep that
configuration and the link alive for as long as the sequencer uses them. The
sequencer runs the ENI startup commands; do not also call `Bus::init()` or
`Bus::createMapping()`.

```cpp
ENI::Sequencer sequencer(bus, link, config);
sequencer.requestState(State::SAFE_OP);

std::vector<uint8_t> iomap(sequencer.processImageSize());
sequencer.mapProcessImage(iomap.data(), iomap.size());
bus.processDataReadWrite(error);

sequencer.requestState(State::OPERATIONAL, [&]()
{
    bus.processDataReadWrite(error);
});
```

The process image becomes available at SAFE_OP. Outputs must be exchanged
before requesting OP; the callback keeps them cycling while the transition is
checked. See [`eni_sequencer.cc`](../examples/master/eni_sequencer/eni_sequencer.cc)
for a complete example. `cycleTime()` and `shiftTime()` expose the common DC
timing, when the ENI specifies it, for `Bus::enableDC()`.

## Current limits

- The detected slave count and identities must match the ENI and the devices'
  SII data. Mailbox and process data layouts are checked against the registers
  programmed by the ENI.
- SoE, EoE, FoE, AoE and VoE startup commands are not executed. Bootstrap,
  HotConnect and `PreviousPort` checks are not supported.
- The regular `Bus` exchange builds its own frames from the process data
  mapping read back from the slaves: ENI `<Cyclic>` frames only serve to check
  that mapping, and their `CycleTime` is the application's to use.
  `<ProcessImage>` variables are parsed but not used.
- Mailboxes are polled slave by slave: `MailboxStates` and the status FMMUs it
  describes are not used.
- Each startup command uses its own round trip; `Requires=frame/cycle` is not
  implemented. A failed command throws, without automatic recovery.
- `Validate` only supports the default `EQ` comparison: `NONE` skips the check
  and the other comparison types are refused when loading the file.
