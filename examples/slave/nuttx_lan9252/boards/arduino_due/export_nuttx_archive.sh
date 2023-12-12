#!/bin/bash

set -x
set -e

nuttx_src=~/wdc_workspace/src/nuttxspace/nuttx
build=~/wdc_workspace/src/KickCAT/build_slave/
src=~/wdc_workspace/src/KickCAT/

bin=main

nuttx_version=nuttx-export-12.3.0

rm -f ${nuttx_src}/${nuttx_version}.tar.gz
rm -rf ${build}/${nuttx_version}
rm -rf ${build}/${nuttx_version}.tar.gz

mkdir -p ${build}
make -C ${nuttx_src} export -j8
cp ${nuttx_src}/${nuttx_version}.tar.gz ${build}
tar xf ${build}/${nuttx_version}.tar.gz -C ${build}
cmake -B ${build} -S ${src}  -DCMAKE_TOOLCHAIN_FILE=${build}/${nuttx_version}/scripts/toolchain.cmake
make -C ${build} -j8


arm-none-eabi-objcopy -O binary ${build}examples/slave/nuttx_arduino_due/${bin} ${build}/${bin}.bin
~/wdc_workspace/tools/bossac-1.6.1-arduino -i --port=ttyACM0 -U false -e -w -b ${build}/${bin}.bin -R
