#!/usr/bin/env bash

cd $PINTOSHOME/src/threads
make clean && make
cd build || exit
killall qemu-system-i386


echo "Enter Command: "
read program
echo "Running `$program`: DISPLAY=window pintos --qemu --gdb -- run $program"


nohup bash -c "DISPLAY=window pintos --qemu --gdb -- run $program > pintos.log" &
echo "Done!"