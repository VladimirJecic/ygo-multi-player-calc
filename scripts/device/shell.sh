#!/data/data/com.termux/files/usr/bin/bash
# Run a command on the device with shell (uid 2000) privileges.
#
# Prefers Shizuku over adb: wireless debugging hands out a fresh random port on
# every toggle and shows the pairing code only on the phone's screen, neither of
# which an agent in Termux can read.  rish needs no numbers at all.
#
# usage:  shell.sh 'logcat -d -s NEURONMOD'
#         echo 'id' | shell.sh
set -u
RISH=${RISH:-$HOME/rish}

if [ -x "$RISH" ]; then
  if [ $# -gt 0 ]; then printf '%s\n' "$*" | "$RISH"; else "$RISH"; fi
  exit $?
fi

# Fallback: adb, if a device happens to be connected.
if adb devices 2>/dev/null | grep -q "device$"; then
  if [ $# -gt 0 ]; then adb shell "$@"; else adb shell; fi
  exit $?
fi

cat >&2 <<'MSG'
shell.sh: no way onto the device.

  Shizuku:  ~/rish is missing.  Extract it from the installed app - the two
            files live in its apk and need no download:
              A=$(pm path moe.shizuku.privileged.api | cut -d: -f2)
              unzip -o -j "$A" assets/rish assets/rish_shizuku.dex -d ~
              sed -i 's/"PKG"/"com.termux"/' ~/rish
              chmod +x ~/rish && chmod 400 ~/rish_shizuku.dex
            Shizuku itself must be running (its app starts it).

  adb:      no device attached.  Needs the port and pairing code off the
            phone's Wireless debugging screen, by hand.
MSG
exit 1
