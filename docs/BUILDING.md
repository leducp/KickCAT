# Building KickCAT

The Linux development build uses the wrappers in `scripts/` to configure
Conan dependencies and CMake. You need a C++ compiler, CMake, Make, Python and
Conan 2.10 or newer. Activate the repository's Python environment before
running a script. If `.venv` does not exist yet, create it and install Conan:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install 'conan>=2.10'
```

Then build:

```bash
source .venv/bin/activate
./scripts/configure.sh build --with=unit_tests
./scripts/setup_build.sh build
cd build && make -j
```

`configure.sh` writes options to `build/.buildconfig`; `setup_build.sh` reads
them. The configuration step is optional if the defaults suit you. Run
`./scripts/configure.sh build --show` to see the current options, or
`./scripts/configure.sh build --help` for the complete list. After changing
build options, CMake files or dependencies, rerun `setup_build.sh`.

For source changes, rebuild in the existing build directory:

```bash
cd build && make -j
```

`setup_build.sh` accepts `--build-type Debug` and `--target linux-aarch64`.
The latter needs the `aarch64-linux-gnu` cross compilers.

## Tests and coverage

With `unit_tests` enabled, run the test binary from the build directory:

```bash
cd build
./kickcat_unit
./kickcat_unit --gtest_filter='<Pattern>*'
```

For coverage, install `gcovr`, enable both `unit_tests` and `code_coverage`,
then rebuild and run `make coverage` from the build directory.

## Optional GUIs

KickUI and the EEPROM editor are off by default. They require ImGui and GLFW.
Enable them with `--with=kickui` and/or `--with=eeprom_editor`, then rerun
`setup_build.sh` and build. See [TOOLS.md](TOOLS.md) for their commands.

## Python bindings

Install the Python package from the repository root with `uv pip install .`
or `pip install .`. The wheel build enables the EEPROM editor GUI by default;
if its GUI dependencies are unavailable, use:

```bash
uv pip install --config-setting=cmake.define.BUILD_EEPROM_EDITOR=OFF .
```

The package is also published as `kickcat` on PyPI. To use a physical bus on
Linux, grant raw-socket capabilities to the executable or run
`./py_bindings/enable_raw_access.sh` for the active Python interpreter. See
the [hardware guide](HARDWARE.md) for bus setup and
`py_bindings/examples/` for Python examples.

## Other platforms

The Linux wrappers generate Linux toolchains. Windows builds use the MinGW
profile in `conan/profile_windows_x86_64.txt`; see the
[CI workflow](../.github/workflows/ci.yml) for the current build setup.
Windows is intended for tools and tests, not real-time use.

PikeOS 5.1 native personality builds require a cross-toolchain file defining
`PIKEOS`. Example process and thread configurations are in
`lib/src/OS/PikeOS/p4ext_config.c`.

NuttX slave firmware build and flashing instructions are in
[HARDWARE.md](HARDWARE.md).
