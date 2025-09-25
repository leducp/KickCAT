### Setup Nuttx project:

https://nuttx.apache.org/docs/latest/quickstart/install.html


### Build and flash

- Checkout nuttx and apps repository to `nuttx-12.6.0` tag.
- Copy the defconfig file into your Nuttx installation in `nuttx/boards/arm/sam34/arduino-due/configs/nsh/defconfig`
- In nuttx folder: `./tools/configure.sh -l arduino-due:nsh`
- Download and add to your path a gcc > to 12.0. https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
- Retrieve the `bossac-1.6.1-arduino` version. One way to get it is to install the arduino IDE and retrieve the executable from its installation folder.
- See the script `export_nuttx_archive.sh` for reference as how to build and flash your program (Paths are not properly handled in the script).


Bossac troubleshooting:

In case of `No device found on ttyACM0` but you can see the device listed in `/dev/tty*`.
- make sure the usb is connected to the port closest to the arduino-due's alimentation.
- press erase on the board for a few second, release, then press reset.
- try again to upload the binary.
