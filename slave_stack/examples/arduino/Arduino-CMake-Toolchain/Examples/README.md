Examples code is compiled as follows.

```sh
cd /path/to/Arduino-CMake-Toolchain
mkdir Examples_build # Any folder name
cd Examples_build
cmake -D CMAKE_TOOLCHAIN_FILE=../Arduino-toolchain.cmake ../Examples
```

If there are errors related to CMake version, update CMake or download/build a local version of CMake. CMake above 3.7.0 are supported.

If there are errors (related to not finding Arduino IDE installation), please install the Arduino IDE and follow the instructions to guide the toolchain with the installation path.

As we have not yet chosen an Arduino board yet, the above *cmake* command exits with error, prompting to select a board from the generated BoardOptions.txt in *Examples_build* folder. Edit the file to choose the board and then reinvoke the command.

```sh
cmake -D CMAKE_TOOLCHAIN_FILE=../Arduino-toolchain.cmake ../Examples
# Depending on the CMake generator used, invoke the make or open appropriate ID menu
# Or just use cmake to start the build as given below
cmake --build .
```

After the build is successful, the hello world application can be uploaded to the Arduino board as follows

```sh
# Assuming /dev/ttyUSB0 to be the serial port corresponding to the board
cmake --build . --target upload-hello_world -- SERIAL_PORT=/dev/ttyUSB0
# Note: May need 'sudo' to access the serial port?
# Instead of cmake, you can use the make command as below
# <make> upload-hello_world SERIAL_PORT=/dev/ttyUSB0
```
