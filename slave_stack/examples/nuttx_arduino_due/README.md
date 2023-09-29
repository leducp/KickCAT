### Setup Nuttx project:

https://nuttx.apache.org/docs/latest/quickstart/install.html


### Build and flash

- Copy the defconfig file into your Nuttx installation in `nuttx/boards/arm/sam34/arduino-due/configs/nsh/defconfig`
- Download and add to your path a gcc > to 12.0. https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
- Retrieve the `bossac-1.6.1-arduino` version. One way to get it is to install the arduino IDE and retrieve the executable from its installation folder.
- See the script `export_nuttx_archive.sh` for reference as how to build and flash your program (Paths are not properly handled in the script).
