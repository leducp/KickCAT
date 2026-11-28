<p align="center">
    <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/kickCAT_logo_dark_mode.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/kickCAT_logo_light_mode.png">
    <img alt="KickCAT" width="300">
    </picture><br>
</p>

<p align="center">
  <strong>A lightweight, efficient EtherCAT master/slave stack for embedded and real-time systems</strong>
</p>

<p align="center">
  <a href="https://github.com/leducp/KickCAT/actions/workflows/ci.yml"><img src="https://github.com/leducp/KickCAT/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://leducp.github.io/KickCAT/"><img src="https://github.com/leducp/KickCAT/actions/workflows/docs.yml/badge.svg" alt="Docs"></a>
  <a href="https://pypi.org/project/kickcat/"><img src="https://img.shields.io/pypi/v/kickcat" alt="PyPI"></a>
</p>

KickCAT is an EtherCAT stack that you can embed in your own software. One codebase provides
both the master and the slave, plus an emulator for developing and testing a bus without
hardware.

**Documentation: <https://leducp.github.io/KickCAT/>**

## Quick start

Build, then run a master against an emulated slave in one process. No hardware is needed.

```bash
python3 -m venv .venv && source .venv/bin/activate && pip install 'conan>=2.10'
./scripts/setup_build.sh build
make -C build -j
./build/examples/master/simulated_bus/simulated_bus \
    -f examples/slave/lan9252/freedom-k64f/nuttx/freedom-k64f.xml -t Board
```

## Cheat sheet

Run from the repository root, with `.venv` active.

| Task | Command |
|---|---|
| Choose build options | `./scripts/configure.sh build --with=unit_tests` (`--show` lists them) |
| Fetch dependencies and configure | `./scripts/setup_build.sh build` (again after changing options) |
| Build | `make -C build -j` |
| Run the unit tests | `./build/kickcat_unit --gtest_filter='Bus*'` |
| Measure coverage | options `--with=unit_tests --with=code_coverage`, then `make -C build coverage` |
| Build with sanitizers | options `--with=asan --with=ubsan` |
| Build the GUIs | options `--with=kickui --with=eeprom_editor` |
| Run KickUI | `./build/tools/kickui/kickui` |
| Simulate a bus in a second process | `./build/simulation/network_simulator -i tap:server -s simulation/slave_configs/freedom-k64f.json` |
| Connect a master to it | `sudo ./build/examples/master/easycat/easycat_example -i tap:client` |
| Run a master on a network card | `sudo ./build/examples/master/easycat/easycat_example -i eth0` |
| Write a slave EEPROM | `sudo ./build/tools/eeprom -s 0 -c write -f eeprom.bin -i eth0` |
| Build NuttX slave firmware | `./scripts/build_slave_bin.sh freedom-k64f ~/nuttxspace/nuttx` |
| Install the Python package | `pip install kickcat`, then `./py_bindings/enable_raw_access.sh` |
| Check Python formatting | `./scripts/format_py.sh check` |
| Preview the docs | `uvx zensical@0.1.1 serve` |
| Build the docs as CI does | `uvx zensical@0.1.1 build --strict` |

## Documentation

- [Home](https://leducp.github.io/KickCAT/): what KickCAT is, and where to start
- [User guide](https://leducp.github.io/KickCAT/user-guide/features/): features, building,
  hardware, simulation, tools
- [Developer guide](https://leducp.github.io/KickCAT/developer-guide/): architecture and
  contributing
- [Reference](https://leducp.github.io/KickCAT/reference/glossary/): glossary and ETG
  specifications
- EtherCAT quick reference: the ETG
  [Device Protocol Poster](https://www.ethercat.org/download/documents/ethercat_device_protocol_poster.pdf)

## Project

Contributions are welcome: see
[Contributing](https://leducp.github.io/KickCAT/developer-guide/contributing/). KickCAT
follows [Semantic Versioning](https://semver.org/) and is used in production systems.

[Issues](https://github.com/leducp/KickCAT/issues) ·
[Discussions](https://github.com/leducp/KickCAT/discussions) ·
[Releases](https://github.com/leducp/KickCAT/releases) ·
[Conan package](https://github.com/conan-io/conan-center-index/tree/master/recipes/kickcat) ·
License: [CeCILL-C](LICENSE)
