### Quick start

#### Environment setup
- download and build NuttX project (see NuttX documentation for details)
- copy the `defconfig` file into `nuttxspace/nuttx/boards/arm/xmc4/xmc4800-relax/configs/nsh/defconfig`
- configure the NuttX project with `./tools/configure.sh -E -l xmc4800-relax:nsh` in NuttX folder.
- install `arm-none-eabi-objcopy` // TODO more details
- install `JLinkExe`              // TODO more details
- use the `export_nuttx_archive.sh` to build and deploy your app. You will need to tweak the paths in the script to match your environment.


#### Create your own application

- Create a new main and a new project in CMake. Edit `export_nuttx_archive.sh` to flash your binary.
- A minimal default EEPROM is included in the binary, it allows you to flash your own EEPROM description with the tool `KickCAT/tools/eeprom.cc`.
- If you don't know what to put in the EEPROM you can flash the `eeprom_xmc4800` file which corresponds to a simple slave.


NB: The `main_relax` is an example using the dev board xmc4800 relax.
The `main_foot` is an example used by Wandercraft to test custom boards and can be launched on the relax board as well, be careful to flash the eeprom corresponding to the flashed binary.
