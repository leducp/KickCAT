#!/bin/bash

touch flashBin.jlink
echo "loadfile $1 0x00000000" > flashBin.jlink
echo "reset" >> flashBin.jlink
echo "quit"  >> flashBin.jlink

JLinkExe -device MK64FN1M0xxx12 -if swd -speed 4000 -autoconnect 1 -commandfile flashBin.jlink
