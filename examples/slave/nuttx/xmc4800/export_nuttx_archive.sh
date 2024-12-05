#!/bin/bash

set -x
set -e

usage() {
    echo "Usage: ./export_nuttx_archive.sh [foot|relax]"
    exit
}

project=""

case $1 in
    -h|-\?|--help|"")
        usage
        ;;
    *)
        project=${1}
esac



nuttx_src=~/wdc_workspace/src/nuttxspace/nuttx
build=~/wdc_workspace/src/KickCAT/build_slave
src=~/wdc_workspace/src/KickCAT

bin=xmc4800_$project

nuttx_version=nuttx-export-12.5.1

rm -f ${nuttx_src}/${nuttx_version}.tar.gz
rm -rf ${build}/${nuttx_version}
rm -rf ${build}/${nuttx_version}.tar.gz

mkdir -p ${build}
make -C ${nuttx_src} export -j8
cp ${nuttx_src}/${nuttx_version}.tar.gz ${build}
tar xf ${build}/${nuttx_version}.tar.gz -C ${build}
cmake -B ${build} -S ${src}  -DCMAKE_TOOLCHAIN_FILE=${build}/${nuttx_version}/scripts/toolchain.cmake
make -C ${build} -j8

arm-none-eabi-objcopy -O binary ${build}/examples/slave/nuttx/xmc4800/${bin} ${build}/${bin}.bin


# Create flashBin.jlink file
test -f flashBin.jlink && rm flashBin.jlink
echo "loadfile ${build}/${bin}.bin 0x0C000000" >>flashBin.jlink
echo "reset" >>flashBin.jlink
echo "quit" >>flashBin.jlink

# Flash XMC4800 using JLink
JLinkExe -device XMC4800-2048 -nogui 1 -if swd -speed 4000kHz -commandfile flashBin.jlink
rm flashBin.jlink
