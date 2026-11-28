# Installation

!!! abstract "TL;DR"
    - To use KickCAT in a C++ project: add the Conan package `kickcat`.
    - To script a master in Python: `pip install kickcat`.
    - To run the examples, tools and simulator, or to use changes newer than the latest release:
      build from source.

## Build from source

You need a C++17 compiler, CMake, Make, Python, and Conan 2.10 or newer. On Linux:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install 'conan>=2.10'
./scripts/setup_build.sh build
make -C build -j
```

This builds the library, the master examples, the tools and the simulator. Next, run
[your first bus](first-bus.md). For build options (unit tests, GUIs, AF_XDP) and other
platforms, see [Building](../user-guide/building.md).

## Use KickCAT in your C++ project

### With Conan

KickCAT is available on [Conan Center](https://github.com/conan-io/conan-center-index/tree/master/recipes/kickcat)
as `kickcat`. Add it to your project's `conanfile.txt`:

```ini
[requires]
kickcat/v2.5

[generators]
CMakeDeps
CMakeToolchain
```

Link your target against it:

```cmake
find_package(kickcat CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE kickcat::kickcat)
```

Then install the dependencies and build:

```bash
conan profile detect            # once, if you have no default profile yet
conan install . --output-folder=build --build=missing
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The ESI parser is off by default in the package. To enable it, add `kickcat/*:with_esi_parser=True`
under `[options]`.

!!! note
    Conan Center carries stable releases only; the recipe lists the available versions.
    Features added since the latest release, such as FoE, require a source build.

### From a source build

[Build from source](#build-from-source), then install the build into a prefix:

```bash
cmake --install build --prefix "$HOME/.local/kickcat"
```

The installed package is named `KickCAT`, unlike the Conan package:

```cmake
find_package(KickCAT CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE KickCAT::kickcat)
```

Configure your project with the prefix and the KickCAT build directory:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local/kickcat;/path/to/KickCAT/build"
```

!!! note
    The KickCAT build directory contains the Conan files for `tinyxml2`, a dependency of
    `KickCAT::kickcat` when built with the ESI or ENI parser. Both parsers are enabled by
    default on Linux and Windows.

For builds that do not use CMake, get the flags from pkg-config by setting
`PKG_CONFIG_PATH` to `$HOME/.local/kickcat/lib/pkgconfig`:
`pkg-config --cflags --libs --static kickcat`.

## Use Python

```bash
pip install kickcat
```

The Python package covers the master: bus startup, process data and SDO. The FoE bindings
are newer than the latest release; to use them, build the package from a checkout (see below).
To reach a network card, grant raw-socket capabilities to your Python interpreter:

```bash
sudo setcap 'cap_net_raw,cap_net_admin=+ep' "$(readlink -f "$(which python)")"
```

`py_bindings/enable_raw_access.sh` runs the same command. A bus comes up as in C++:

```python
--8<-- "py_bindings/examples/freedom-k64f.py:bring-up"
```

More examples are in `py_bindings/examples/`.

To build the package from a KickCAT checkout, run `uv pip install .` or `pip install .`. The
build includes the EEPROM editor GUI. If its dependencies are not available, disable it:

```bash
uv pip install --config-setting=cmake.define.BUILD_EEPROM_EDITOR=OFF .
```
