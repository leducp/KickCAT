Currently the direct arduino support is deactivated.

To compile it you need to

- uncomment the CMakeLists
- Dowloand in the arduino folder the project https://github.com/a9183756-gh/Arduino-CMake-Toolchain
- follow this readme.


### How to build and upload on an arduino due with an easyCAT shield

The arduino due uses an arm processor (most of the arduino boards are based on avr processors) so most of the cmake arduino projects don't handle it.


Working project :
https://github.com/a9183756-gh/Arduino-CMake-Toolchain

**The useful readme is the one in the Examples folder of the project**

Requirements:

- Download the legacy Arduino IDE 1.8 https://www.arduino.cc/en/software en suivant https://docs.arduino.cc/software/ide-v1/tutorials/Linux

- In the ide in tab tools/board_manager install the due board support (sam boards)

- In the CMakeLists of the arduino-cmake-toolchain, the build of subdirectories 02_arduino_lib and 05_auto_link have been removed because they use the EEPROM lib which was not defaultly installed and unused for ethercat slave stack.

- Edit in `$HOME/.arduino15/packages/arduino/hardware/sam/1.6.12/platforms.txt` 
`tools.bossac.upload.pattern="{path}/{cmd}" {upload.verbose} --port={serial.port.file} -U {upload.native_usb} -e -w -b "{build.path}/{build.project_name}.bin" -R`
Remove `upload.verify` field in the command. Bossac is the tool used to upload to the due board.
So that the line is `tools.bossac.upload.pattern="{path}/{cmd}" {upload.verbose} --port={serial.port.file} -U {upload.native_usb} -e -w -b "{build.path}/{build.project_name}.bin" -R`

- Follow the arduino-cmake-toolchain Examples folder readme for cmake configuration

- In ccmake choose the board `Arduino Due (Programming Port) [sam.arduino_due_x_dbg]` and makes sure the usb micro is plugged in the port on the right of the board.



To upload the binary:

- press the erase button for a few seconds
- press reset (otherwise Bossac don't detect the board on the serial port)
- `make SERIAL_PORT_FILE=ttyACM1 upload-hello_world` **Do not put /dev before ttyXXX**

Enjoy your cmake compilation and upload \o/
