#!/bin/bash

set -x
set -e

nuttx_src=~/wdc_workspace/src/nuttxspace/nuttx
build=~/wdc_workspace/src/KickCAT/build_slave/arduino
src=~/wdc_workspace/src/KickCAT/slave_stack/examples/nuttx_arduino_due

bin=main

rm -f ${nuttx_src}/nuttx-export-12.2.1.tar.gz
rm -rf ${build}/nuttx-export-12.2.1
rm -rf ${build}/nuttx-export-12.2.1.tar.gz

mkdir -p ${build}
make -C ${nuttx_src} export -j8
cp ${nuttx_src}/nuttx-export-12.2.1.tar.gz ${build}
tar xf ${build}/nuttx-export-12.2.1.tar.gz -C ${build}
cmake -B ${build} -S ${src}  -DCMAKE_TOOLCHAIN_FILE=${build}/nuttx-export-12.2.1/scripts/toolchain.cmake
make -C ${build} -j8


arm-none-eabi-objcopy -O binary ${build}/${bin} ${build}/${bin}.bin
~/wdc_workspace/tools/bossac-1.6.1-arduino -i --port=ttyACM0 -U false -e -w -b ${build}/${bin}.bin -R
