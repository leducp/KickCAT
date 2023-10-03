# A simple C++ EtherCAT master stack.

Kick-start your slaves!

Thin EtherCAT stack designed to be embedded in a more complex software and with efficiency in mind.

### Current state:
 - Can go to OP state
 - Can read and write PI
 - interface redundancy is supported
 - CoE: read and write SDO - blocking and async call
 - CoE: Emergency message
 - Bus diagnostic: can reset and get errors counters
 - hook to configure non compliant slaves
 - consecutives writes to reduce latency - up to 255 datagrams in flight
 - build for Linux and PikeOS

**NOTE** The current implementation is designed for little endian host only!

### TODO:
 - CoE: segmented transfer - partial implementation
 - CoE: diagnosis message - 0x10F3
 - Bus diagnostic: auto discover broken wire (on top of error counters)
 - More profiles: FoE, EoE, AoE, SoE
 - Distributed clock
 - AF_XDP Linux socket to improve performance
 - Addressing groups

### Operatings systems:
## Linux
To improve latency on Linux, you have to
 - use Linux RT (PREMPT_RT patches),
 - set a real time scheduler for the program (i.e. with chrt)
 - disable NIC IRQ coalescing (with ethtool)
 - disable RT throttling
 - isolate ethercat task and network IRQ on a dedicated core
 - change network IRQ priority

## PikeOS
 - Tested on PikeOS 5.1 for native personnality (p4ext)
 - You have to provide the CMake cross-toolchain file which shall define PIKEOS variable (or adapt the main CMakelists.txt to your needs)
 - examples are provided with a working but non-optimal process/thread configuration (examples/PikeOS/p4ext_config.c): feel free to adapt it to your needs

## Build:
KickCAT project is handled through CMake. To build the project, call CMake to configure it and then the build tool (default on Linux is make):
  1. Configure the project (more information on https://cmake.org/cmake/help/latest/)
  ```
  mkdir -p build
  cd build
  cmake ..
  ```
  2. Build the project
  ```
  make
  ```

### Build unit tests (optional)
In order to build unit tests, you have to enable the option BUILD_UNIT_TESTS (default to ON) and to provide GTest package through CMake find_package mechanism. You also need gcovr to enable coverage report generation (COVERAGE option).
Note: you can easily provide GTest via conan package manager:
  1. Install conan and setup PATH variable (more information on https://docs.conan.io/en/latest/installation.html)
  ```
  pip install conan
  pip install gcovr
  export PATH="$PATH:$HOME/.local/bin"
  ```
  2. Install GTest in your build folder:
  ```
  mkdir -p build
  cd build
  conan install ../conan/conanfile.txt -of=./ -pr ../conan/profile_linux_x86_64.txt --build=missing -s build_type=Debug
  ```
  Beware `-s build_type` must be consistent with `CMAKE_BUILD_TYPE` otherwise gtest will not be found.

  3. Configure the project (can be done on an already configured project)
  ```
  cmake .. -DCMAKE_BUILD_TYPE=Release
  ```

## Simulator
To start a network simulator, you can either create a virtual ethernet pair (on Linux you can use the helper script 'create_virtual_ethernet.sh') or use a real network interface by using two computer or two interfaces on the same computer.
Note: the simulator has to be started first


## Slave stack
The slave counterpart of the stack is currently under development. For now it is in a POC state with a running example on the arduino due board with the easycat shield.
Follow the readme in the `slave_stack/examples/arduino` folder for the building and uploading information.

## Release procedure

KickCAT versions follow the rules of semantic versioning https://semver.org/

On major version update, a process of testing starts. The version is in alpha phase until API stabilization. Then we 
switch to release candidate (-rcx). To leave a release candidate state, it is required that:

- the software is tested intensivly (at least 5 continuous days - 24*5 hours) without detecting a bug (realtime loss, crash, memory leak...).
- the code coverage is sufficient (80% line and 50% branch) for master/slave stack (tools and simulation can be less).

For each tag, Conan center has to be updated (https://github.com/conan-io/conan-center-index/tree/master/recipes/kickcat).

When a version leaves the release canditate state a github release shall be generated on the corresponding tag.

## EtherCAT doc
https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1257993099.html&id=3196541253205318339
https://www.ethercat.org/download/documents/EtherCAT_Device_Protocol_Poster.pdf

protocol:
https://download.beckhoff.com/download/document/io/ethercat-development-products/ethercat_esc_datasheet_sec1_technology_2i2.pdf

registers:
https://download.beckhoff.com/download/Document/io/ethercat-development-products/ethercat_esc_datasheet_sec2_registers_3i0.pdf

eeprom :
https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1358008331.html&id=5054579963582410224

various:
https://sir.upc.edu/wikis/roblab/index.php/Development/Ethercat

diag:
https://www.automation.com/en-us/articles/2014-2/diagnostics-with-ethercat-part-4
https://infosys.beckhoff.com/english.php?content=../content/1033/ethercatsystem/1072509067.html&id=
https://knowledge.ni.com/KnowledgeArticleDetails?id=kA00Z000000kHwESAU

## Update Conan Recipe on conan center
Kickcat is available on **conan-io**. Whenever there is a new tag:
1. Create a new PR on https://github.com/conan-io/conan-center-index 
2. Follow PR: https://github.com/conan-io/conan-center-index/pull/19482 to add new versions to the recipe