#!/usr/bin/env zsh
make
cd build || exit
killall qemu-system-i386

CMD="run alarm-multiple"
nohup bash -c "DISPLAY=window ../../utils/pintos --qemu --gdb -- $CMD > pintos.log" &
echo "Done!"