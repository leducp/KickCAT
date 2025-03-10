### Quick start

#### Environment setup
- build your KickCAT project (see KickCAT documentation for details) 
- use the `export_nuttx_archive.sh` to build and deploy your app.

Note: this setup was tested on Ubuntu version 22.04 using the Nuttx version 12.6.0 and gcc (arm-gmu-toolchaine) version 13.2.1


#### Create your own application

- Create a new main and a new project in CMake. Edit `export_nuttx_archive.sh` to flash your binary.
- A minimal default EEPROM is included in the binary, it allows you to flash your own EEPROM description with the tool `KickCAT/tools/eeprom.cc`.
- If you don't know what to put in the EEPROM you can flash the `eeprom_xmc4800` file which corresponds to a simple slave.


NB: The `main_relax` is an example using the dev board xmc4800 relax.
The `main_foot` is an example used by Wandercraft to test custom boards and can be launched on the relax board as well, be careful to flash the eeprom corresponding to the flashed binary.