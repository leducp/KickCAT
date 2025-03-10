#!/bin/bash

#set -x 
set -e 

usage() {
    echo "Usage: ./export_nuttx_archive.sh [foot|relax]"
    exit 1
}

if [ "$#" -ne 1 ]; then
    echo "Error:  An argument is required."
    usage
fi

project="$1"


if [[ "$project" != "foot" && "$project" != "relax" ]]; then
    echo "Error: Invalid argument '$project'. Arguments are 'foot' and 'relax'."
    usage
fi

echo "Selected project: $project"

# Step 0. Go up 5 levels and get paths

bin=xmc4800_${project}
nuttx_version=nuttx-export-12.6.0

cd ../../../../..

KickCAT_src="$(pwd)/KickCAT"
mkdir -p "${KickCAT_src}/build_for_xmc"
build="${KickCAT_src}/build_for_xmc"
cd "${KickCAT_src}/.."

# Step 1. Create the nuttxspace folder where you want to work
mkdir -p nuttxspace
cd nuttxspace
nuttxspace_path="$(pwd)"
nuttx_src=${nuttxspace_path}/nuttx

# Step 1.1: Extract NuttX version 12.6.0 into a folder named "nuttx"
tar -xzf ${KickCAT_src}/examples/slave/nuttx/xmc4800/build_nuttx/nuttx-nuttx-12.6.0.tar.gz
mv nuttx-nuttx-12.6.0 nuttx

# Step 1.2: Extract NuttX-apps version 12.6.0 into a folder named "apps"
tar -xzf ${KickCAT_src}/examples/slave/nuttx/xmc4800/build_nuttx/nuttx-apps-nuttx-12.6.0.tar.gz
mv nuttx-apps-nuttx-12.6.0 apps

# Step 1.3: Build NuttX as described in the documentation

## 1.3.1: Ensure the arm-gnu-toolchain v13.2 is in your PATH and verify its version
tar -xf ../KickCAT/examples/slave/nuttx/xmc4800/build_nuttx/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz
mv arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi arm-gnu-toolchain
gnu_path="$(pwd)"
export PATH=$PATH:${gnu_path}/arm-gnu-toolchain/bin
arm-none-eabi-gcc --version

## 1.3.2: Copy defconfig from the example board to the NuttX configuration directory
cp ../KickCAT/examples/slave/nuttx/xmc4800/boards/relax/defconfig nuttx/boards/arm/xmc4/xmc4800-relax/configs/nsh/
## NOTE

## 1.3.3: Configure the NuttX project (run this inside the nuttx folder)
cd nuttx
./tools/configure.sh -E -l xmc4800-relax:nsh
## NOTE

## 1.3.4: Build NuttX with make export (building with 'make' may show an error about an "undefined reference to 'main'")
make export -j8

# The export process generates a compressed file (e.g., nuttx-export.tar.gz) in the nuttx folder.
# Move it one level up to nuttxspace and decompress it, renaming the decompressed folder to "nuttx-export".
mv nuttx-export-0.0.0.tar.gz ../
cd ..
tar -xzf nuttx-export-0.0.0.tar.gz #xzvf
mv nuttx-export-0.0.0 nuttx-export
rm -rf nuttx-export-0.0.0.tar.gz

# 2. Create the build_xmc folder in KickCAT
cd ../KickCAT/build_for_xmc

# 2.1 build and deploy the project
cmake -B ${build} -S ${KickCAT_src} -DCMAKE_TOOLCHAIN_FILE=${nuttxspace_path}/nuttx-export/scripts/toolchain.cmake
make ${bin}
arm-none-eabi-objcopy -O binary ${build}/examples/slave/nuttx/xmc4800/${bin} ${build}/${bin}.bin

# 3 flash the board

# 3.1 Create flashBin.jlink file
test -f flashBin.jlink && rm flashBin.jlink
echo "loadfile ${build}/${bin}.bin 0x0C000000" >>flashBin.jlink
echo "reset" >>flashBin.jlink
echo "quit" >>flashBin.jlink

# 3.2 Flash the XMC4800 board using jlink
JLinkExe -device XMC4800-2048 -nogui 1 -if swd -speed 4000kHz -commandfile flashBin.jlink
ls -lh
rm flashBin.jlink