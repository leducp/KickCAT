# Building KickCAT

KickCAT uses Conan (for dependencies) and CMake, wrapped by the scripts in
`scripts/`. Prefer the wrappers over invoking `cmake` / `conan` directly.

## Prerequisites

- **Linux**: gcc, cmake, conan
- **Python bindings**: uv or pip
- **Hardware access**: a network interface with raw-socket capability

## Linux (recommended)

```bash
# 1. Configure build options (optional -- defaults are sensible)
./scripts/configure.sh build --with=unit_tests

# 2. Set up the build environment (installs Conan deps + runs CMake)
./scripts/setup_build.sh build

# 3. Build
cd build && make -j

# 4. Grant network capabilities (required for raw-socket access)
sudo setcap 'cap_net_raw,cap_net_admin=+ep' ./tools/your_binary
```

`configure.sh` stores the selected options in `.buildconfig`, which
`setup_build.sh` consumes. Available options include `unit_tests`,
`code_coverage`, `esi_parser`, `simulation`, `tools`, `master_examples`,
`slave_examples`, `eeprom_editor`, and `kickui`.

### Rebuild after source changes

From an existing `build` directory, no need to re-run `configure.sh` /
`setup_build.sh` unless CMakeLists or Conan deps changed:

```bash
cd build && make -j
```

### GUI tools (KickUI, EEPROM editor)

These are off by default and need ImGui + GLFW:

```bash
./scripts/configure.sh build --with=kickui --with=eeprom_editor
./scripts/setup_build.sh build
cd build && make -j
```

See [TOOLS.md](TOOLS.md) for what each tool does.

## Python bindings

```bash
# Install with uv (recommended)
uv pip install .

# Or for development (faster rebuilds)
uv pip install --no-build-isolation -Cbuild-dir=/tmp/build -v .
```

The wheel build enables the EEPROM-editor GUI (`BUILD_EEPROM_EDITOR=ON` in
`pyproject.toml`), which needs `imgui` and `glfw`. The cibuildwheel images
provide them; on a bare environment that lacks them, either install them (via
Conan) or disable the GUI for the build:

```bash
uv pip install --config-setting=cmake.define.BUILD_EEPROM_EDITOR=OFF .
```

The published package is available on PyPI:

```bash
pip install kickcat
```

The Python interpreter needs raw-socket capabilities to drive a bus:

```bash
./py_bindings/enable_raw_access.sh
```

### Multi-wheel (CI)

This project uses cibuildwheel to generate wheels for all supported
configurations. To run it locally:

```bash
uvx cibuildwheel
```

## Manual build (without the wrapper scripts)

```bash
# 1. Create the build directory
mkdir -p build

# 2. Install dependencies with Conan
python3 -m venv kickcat_venv
source kickcat_venv/bin/activate
pip install conan

conan install conan/conanfile.py -of=build/ \
  -pr:h conan/your_profile_host.txt \
  -pr:b conan/your_profile_target.txt \
  --build=missing -s build_type=Release

# Or create the Conan package directly
conan create conan/all --build=missing --version 2.5 -pr build/profile.txt

# 3. Configure and build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

## Windows

Windows is **not** suitable for real-time use, but it is useful for tools and
testing.

Requirements:

- Conan for Windows (tested with 2.9.1)
- gcc for Windows (tested with w64devkit 2.0.0)
- npcap (driver 1.80 + SDK 1.70)

Follow the manual build instructions above, using the appropriate Windows paths
and the `conan/profile_windows_x86_64.txt` profile.

## PikeOS

Tested on PikeOS 5.1 for the native personality (p4ext).

Provide a CMake cross-toolchain file that defines the `PIKEOS` variable. Example
process/thread configurations are in `examples/PikeOS/p4ext_config.c`.

## Slave firmware

Building and flashing NuttX slave firmware (XMC4800, Arduino Due, Freedom K64F)
is covered in [HARDWARE.md](HARDWARE.md).

## Testing

### Unit tests

```bash
./scripts/configure.sh build --with=unit_tests
./scripts/setup_build.sh build
cd build && make -j
make test
```

The test binary is `build/kickcat_unit`. Run a subset with
`./kickcat_unit --gtest_filter='<Pattern>*'`.

### Code coverage

```bash
uv pip install gcovr

./scripts/configure.sh build --with=unit_tests --with=code_coverage
./scripts/setup_build.sh build
cd build && make -j
make coverage
```
