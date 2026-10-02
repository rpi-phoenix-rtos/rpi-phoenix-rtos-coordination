echo B4O begin
/usr/bin/wpe-ipc-probe
/usr/bin/wpe-ipc-unixtest -v -g test_unix_socket -n fd_
/usr/bin/wpe-ipc-unixtest -v -g test_unix_socket -n message_4096
export WPE_PHOENIX_SHM_LOG=1
export PHX_TRACE_ABORT=1
/usr/bin/wpe-browser --headless --cpu-rendering --snapshot=/root/b4-1.png --timeout=600 /usr/share/wpe-browser/b4.html
echo B4O run1 rc=$?
ps
sleep 3
echo B4O run1 +3s
ps
sleep 12
echo B4O run1 +15s
ps -t
/usr/bin/wpe-browser --headless --cpu-rendering --snapshot=/root/b4-2.png --timeout=600 /usr/share/wpe-browser/b4.html &
W=$!
sleep 30
echo B4O run2 +30s
ps -t
kill $W
sleep 3
echo B4O run2 +33s
ps
echo B4O end
