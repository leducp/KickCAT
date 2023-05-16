# Arduino CMake Toolchain

**Arduino CMake toolchain** is a CMake toolchain for cross-compiling CMake based projects for all Arduino compatible boards (AVR, ESP32 etc.). Of course, this means all the benefits of CMake for Arduino compilation, like using your favourite IDE, configuration checks (e.g. `try_compile`, `CheckTypeSize`), etc. This also brings the Arduino compilation to professional users, who are limited by the Arduino IDE compilation.

## Note by technyon

This is a fork of 

https://github.com/a9183756-gh/Arduino-CMake-Toolchain

The original projects seems to be not maintained anymore. As far as I have tested, it
only works with the Arduino ESP32 core up to version 1.6. I've merged a few open
pull requests from the original project to get compatibility with Core 2.0.3
(latest at the time of writing).<br>
The included fixes are:

- https://github.com/a9183756-gh/Arduino-CMake-Toolchain/pull/53
- https://github.com/a9183756-gh/Arduino-CMake-Toolchain/pull/57
- Generate build_opts.h file in cmake binary dear to pass cpp compiler check 

## Project Roots

[Arduino-CMake-NG](https://github.com/arduino-cmake/Arduino-CMake-NG) is a great project, which could have prevented me from writing yet another Arduino CMake toolchain. However, as claimed by the project, Arduino-CMake-NG could not be easily utilized/modified for other Arduino compatible boards other than AVR, like ESP32, due to the fact that it does not fully work the way Arduino IDE works and has lot of AVR specific stuff. An other important limitation is related to portability. Arduino-CMake-NG provides Arduino specific CMake interface, requiring CMake scripts to be written/modified specifically for Arduino, rather than just passing `-D CMAKE_TOOLCHAIN_FILE=/path/to/Arduino-toolchain.cmake` to a generic CMake project.

My initial expectation was to contribute to Arduino-CMake-NG to fix the above limitations, but had to redo a lot of core logic making it very incompatible (including the usage). Also, the project Arduino-CMake-NG seems to be no longer maintained. I would like to acknowledge the authors who contributed directly/indirectly to Arduino-CMake-NG, and thus indirectly contributed to this project.

## Features

- [x] CMake Arduino toolchain (passed to CMake using `-D CMAKE_TOOLCHAIN_FILE=/path/to/Arduino-toolchain.cmake)`
    - [x] Support for all Arduino compatible platforms (such as **AVR**, **ESP32** etc.)
    - [x] Generic CMake scripting interface without requiring Arduino specific functions
    - [x] Arduino IDE compatible build (e.g. use of build rules and flags in board.local.txt, pre/postbuild hooks etc.)
    - [x] Selection of board and board-specific menu options as in Arduino IDE tools menu (See `ARDUINO_BOARD_OPTIONS_FILE`)
- [x] Generate Arduino HEX binaries and upload to Arduino boards (See `target_enable_arduino_upload`)
    - [x] Upload using serial port
    - [x] Remote provisioning through network
    - [x] Upload using programmer
    - [x] Burn bootloader
- [x] Support linking with Arduino libraries (see `target_link_arduino_libraries`)
    - [x] Arduino *native* libraries (e.g. Ethernet, Wire)
    - [x] User installed 3rd Party Arduino libraries (e.g. IRremote)
    - [x] Project specific Arduino libraries (those present in `<CMAKE_SOURCE_DIR>/libraries`)
- [x] Support for automatic dependency resolution (Arduino IDE like, but unprofessional)
- [ ] Serial port monitoring
- [ ] Support for `.ino` and '.pde' sketch files (Arduino IDE like, but unprofessional)
- [ ] Board and Libraries Management without requiring installation of Arduino IDE

## Usage

The provided toolchain file (Arduino-toolchain.cmake) is passed to cmake as folows

```sh
cmake -D CMAKE_TOOLCHAIN_FILE=/path/to/Arduino-toolchain.cmake <CMAKE_SOURCE_DIR>
```
Note: As this is cross compilation, use any cross compilation compatible generator, like makefile generators (e.g. `-G "NMake Makefiles"` or `-G "MinGW Makefiles"` on Windows command prompt or `-G "Unix Makefiles"` on UNIX compatible prompts etc.).

The above command generates a file **BoardOptions.cmake** in the build directory, that enumerates all the installed Arduino boards (installed through Arduino IDE or any other board manager) and their menu options. Select the Arduino board and any non-default options for the board from the BoardOptions.cmake (Or from cmake-gui), and then reinvoke the same command above.

If you already have a customized BoardOptions.cmake file for the Arduino Board, you can use that instead, without waiting for the generation of BoardOptions.cmake, as given below.

```sh
cmake -D CMAKE_TOOLCHAIN_FILE=/path/to/Arduino-toolchain.cmake -D ARDUINO_BOARD_OPTIONS_FILE=/path/to/any/BoardOptions.cmake <CMAKE_SOURCE_DIR>
```

Note:
1. After the cmake generation is successful, changing the menu options in BoardOptions.cmake may work, but changing the board itself may not be allowed by CMake because the compiler, ABI, features determination and any cache dependencies may not be retriggered again.
1. CMake does not support build for multiple architectures in the same build tree. If a project requires to build applications for more than one type of Arduino boards, refer to CMake documentation for multiple architecture build.
1. When this toolchain is used, executables (added with `add_executable`) have the entry points setup()/loop() and not main(). Need to include "Arduino.h" for these entry points.
1. If your source files are compiled for both Arduino and other platforms like linux, then the CMake flag `ARDUINO` and the compiler flag `ARDUINO` can be used for script/code portability. Other Arduino board/architecture specific standard flags can also be used.

### Linking with Arduino code/libraries (`target_link_arduino_libraries`)

`<CMAKE_SOURCE_DIR>/CMakeLists.txt` and any other dependent CMake scripts of the project contain the standard CMake scripting using `add_library`, `add_executable` etc. without Arduino specific changes. Refer to CMake documentation for the same. However when the project source code depends on the Arduino code or libraries (i.e. if the corresponding header files are included), then appropriate linking is required, as expected. This is done using `target_link_arduino_libraries` as explained below.

If Arduino.h is included in your source files, then the target must be linked against the 'core' Arduino library as follows.

```cmake
add_library(my_library my_library.c) # my_library.c includes Arduino.h
target_link_arduino_libraries(my_library PRIVATE core)
```

If any other native or 3rd party libraries are used, then those libraries must be linked similarly as follows.

```cmake
add_executable(my_app my_app.c) # my_app.c includes Wire.h, Arduino.h
target_link_arduino_libraries(my_app PRIVATE Wire core)
```

Like Arduino IDE, if the required Arduino libraries are to be automatically identified and linked, then it can be done as follows.

```cmake
add_executable(my_app my_app.c) # my_app.c includes Wire.h, Arduino.h
# Link Wire and core automatically (PUBLIC linking in this example)
target_link_arduino_libraries(my_app AUTO_PUBLIC)
```

Note:
1. *Wire* and *core* in the above examples are not CMake targets. They are just Arduino library names (case-sensitive).
1. It is required only to specify the direct dependencies. Any deeper dependencies are automatically identified and linked. For example, if *SD.h* is included, it is sufficient to link with *SD*, even if *SD* depends on other Arduino libraries, like *SPI*.

These examples illustrates simple usage, but powerful enough for most use cases. However more advanced control and customization of Arduino libraries should be possible. Please refer to the [Examples](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/tree/master/Examples) folder, as well as the API documentation of `target_link_arduino_libraries` (Currently documented as comments in [BoardBuildTargets.cmake](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/blob/master/Arduino/System/BoardBuildTargets.cmake)).

### Uploading to the target board (`target_enable_arduino_upload`)

If support for generating HEX binary and uploading it to the board is required, then a call to `target_enable_arduino_upload` is required for each executable target, as shown below.

```cmake
add_executable(my_executable my_executable.c)
target_link_arduino_libraries(my_executable PRIVATE core) # Assuming my_executable.c includes Arduino.h
target_enable_arduino_upload(my_executable) # This adds a target upload-my_executable
```

Upload the executable (from the above example) to the board on COM3 serial port as follows

```sh
<make-command> upload-my_executable SERIAL_PORT=COM3
```

Upload the executable to the board through remote provisioning as follows

```sh
<make-command> upload-my_executable NETWORK_PORT=<IP>[:<port>]
```

For using a programmer, select the programmer in board options or the CMake GUI, and then execute the following

```sh
<make-command> program-my_executable CONFIRM=1
```

Using the programmer, bootloader can be flashed as below

```sh
<make-command> burn-bootloader CONFIRM=1
```

## Serial port monitoring

Currently there is no support available for this within this toolchain. However any external serial port monitor can be used (e.g. Putty). External serial monitor may need to be closed before upload and reopened after upload, because both use the same serial port.

## Known issues

Many of the issues in the master branch have been fixed in release-1.1-dev branch. Although not tested to be fully stable, release-1.1-dev is stable enough to try out and report any futher issues before it gets merged into master.

Below are the list of known issues in the master branch.

**1. Uploaded application does not work on some boards**

Caused by build linking issue that does not link some object files related to platform variant sources contained in the core library. Affects any Arduino platform that has variant source files in addition to the variant header files.

Resolution: Please try with release-1.1-dev branch or otherwise, temporary fixes are available in the branches [fix/variant_link_alt1](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/tree/fix/variant_link_alt1) and [fix/variant_link_alt2](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/tree/fix/variant_link_alt2).

**Compromises when using the fix/variant_link_alt1 fix**: (1) CMake version must be above 3.13, (2) Application needs to link with core directly, like in [Examples/01_hello_world](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/tree/master/Examples/01_hello_world), and not like in [Examples/03_portable_app](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/tree/master/Examples/03_portable_app) which links transitively.

**Compromises when using the fix/variant_link_alt2 fix**: Need to retrigger cmake and do rebuild, after the first successful build, if transitive linking of core is used in the project. May get "source some_file.o not found error" in CMake during the first invocation of CMake that can be ignored.

**2. Build/link issue on some 3rd party platforms**

Resolution: Please try with release-1.1-dev branch.

**3. Some libraries are not detected by *target_link_arduino_libraries***

Currently, *target_link_arduino_libraries* takes only include names (i.e. the name of the header file without extension). If the include name does not match with the library name (as mentioned in *library.properties* of the library), the detection of the library fails \(Refer issue [#19](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/issues/19)\).

Workaround: Rename the library folder to the include name and use include name in *target_link_arduino_libraries*.

Resolution: Please try with release-1.1-dev branch.

## How it works

This toolchain follows the build process described in [Arduino Build Process](https://arduino.github.io/arduino-cli/sketch-build-process/), and processes the JSON, platform.txt and boards.txt files correponding to the Arduino platform as specified in the documentation [Arduino IDE 1.5 3rd party Hardware specification](https://arduino.github.io/arduino-cli/platform-specification/).

## License

MIT © 2020 [Arduino-CMake-Toolchain](https://github.com/a9183756-gh/Arduino-CMake-Toolchain/blob/master/LICENSE.md)
