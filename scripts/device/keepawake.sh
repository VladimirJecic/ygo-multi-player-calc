#!/data/data/com.termux/files/usr/bin/bash
# Keeps the phone from sleeping through the night while the mod work runs.
# - partial wake lock so Termux keeps executing with the screen off
# - a WAKEUP keyevent every 10 min so the display timeout never lands us in
#   a state where adb screencap only sees a black panel
LOG=${TMPDIR:-/data/data/com.termux/files/usr/tmp}/keepawake.log
while :; do
  termux-wake-lock >/dev/null 2>&1
  adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
  adb shell svc power stayon true >/dev/null 2>&1
  echo "$(date '+%H:%M:%S') awake ping" >> "$LOG"
  sleep 600
done
