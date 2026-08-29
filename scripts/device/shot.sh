#!/data/data/com.termux/files/usr/bin/bash
# Screenshot the phone into a local png.
# usage: shot.sh <out.png>
#
# exec-out, not `shell screencap`: the shell transport mangles the binary stream
# on some builds and you get a png that no decoder will open.
set -u
OUT=${1:?usage: shot.sh <out.png>}
adb exec-out screencap -p > "$OUT" 2>/dev/null
test -s "$OUT" || { echo "shot: screencap produced nothing (screen off? adb down?)"; exit 1; }
echo "$OUT"
