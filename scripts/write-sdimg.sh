#!/bin/bash

/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh

diskutil unmountDisk /dev/disk5

sudo dd if=/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img of=/dev/rdisk5 bs=4m

sync

diskutil eject /dev/disk5

