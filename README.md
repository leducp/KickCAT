<p align="center">
    <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/kickCAT_logo_dark_mode.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/kickCAT_logo_light_mode.png">
    <img alt="KickCAT" width="300">
    </picture><br>
</p>

<p align="center">
  <strong>A lightweight, efficient EtherCAT master/slave stack for embedded and real-time systems</strong>
</p>

---

## Overview

KickCAT is a thin EtherCAT stack designed to be embedded in larger software with
efficiency in mind. It provides **both master and slave** implementations in one
codebase and runs on Linux (including RT-PREEMPT), Windows, PikeOS, and NuttX.

CoE (CANopen over EtherCAT) is fully supported on both sides. Beyond the stack
itself, KickCAT ships a complete **software ESC and network emulator**, GUIs, and
CLI tooling, so a bus can be developed, configured, and tested without physical
hardware.

For exactly what is and isn't supported, see the
[feature support matrix](docs/FEATURES.md).

## Getting started

Build the stack and tools (Linux):

```bash
source .venv/bin/activate   # provides Conan, see the building guide
./scripts/configure.sh build --with=unit_tests
./scripts/setup_build.sh build
make -C build -j
```

Then, from the repository root, run a master against an emulated slave -- no
hardware, single process:

```bash
./build/examples/master/simulated_bus/simulated_bus \
    -f examples/slave/lan9252/freedom-k64f/nuttx/freedom-k64f.xml -t Board
```

Point `-f` at any vendor ESI XML and `-t` at the device `<Type>` to emulate a
different slave. Beckhoff publish ESI files for their devices at
<https://download.beckhoff.com/download/configuration-files/io/ethercat/xml-device-description>.

## Documentation

The documentation is published at <https://leducp.github.io/KickCAT/>. It is
built with [Zensical](https://zensical.org/); preview it locally from the
repository root:

```bash
uvx zensical serve
```

- [Feature support matrix](docs/FEATURES.md)
- [Building and installing](docs/BUILDING.md)
- [Hardware guide](docs/HARDWARE.md)
- [Simulation and emulation](docs/SIMULATION.md)
- [Tools and GUIs](docs/TOOLS.md)
- [Real-time performance tuning](docs/PERFORMANCE.md)
- [Architecture overview](docs/architecture.md)
- [Release procedure](docs/RELEASE.md)
- [Contributing](docs/CONTRIBUTING.md)

### EtherCAT references

- [ETG official documentation](https://www.ethercat.org/en/downloads.html)
- [Beckhoff EtherCAT documentation](https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1257993099.html)
- [Beckhoff ESI (XML device description) files](https://download.beckhoff.com/download/configuration-files/io/ethercat/xml-device-description)
- [ESC datasheets](https://download.beckhoff.com/download/document/io/ethercat-development-products/)
- [EtherCAT Device Protocol Poster](https://www.ethercat.org/download/documents/EtherCAT_Device_Protocol_Poster.pdf)
- [ESC comparison](https://download.beckhoff.com/download/document/io/ethercat-development-products/an_esc_comparison_v2i7.pdf)

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](docs/CONTRIBUTING.md) for code-style
and PR conventions, and [docs/BUILDING.md](docs/BUILDING.md) for the development
build (unit tests and coverage).

KickCAT follows [Semantic Versioning](https://semver.org/). The release workflow
is documented in [docs/RELEASE.md](docs/RELEASE.md).

## License

[CeCILL-C](LICENSE)

## Support

- Issues: [GitHub Issues](https://github.com/leducp/KickCAT/issues)
- Discussions: [GitHub Discussions](https://github.com/leducp/KickCAT/discussions)
- Conan package: [conan-center-index](https://github.com/conan-io/conan-center-index/tree/master/recipes/kickcat)

Status: actively maintained and used in production systems. See
[Releases](https://github.com/leducp/KickCAT/releases) for the latest stable
version.
