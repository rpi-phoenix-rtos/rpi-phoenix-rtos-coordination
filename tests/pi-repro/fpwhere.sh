#!/bin/bash
# Where is the 17-35 ms paid: crossing the root mount, or inside each server?
mkdir -p /ramtmp/x; echo hello > /ramtmp/x/f
/bin/fileperf --stat /dev /dev/zero /ramtmp /ramtmp/x /ramtmp/x/f /tmp / /stk-assets.tar.gz /usr /usr/share/supertuxkart/data/stk_config.xml
echo "fpw: done"
