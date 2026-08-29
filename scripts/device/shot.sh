#!/data/data/com.termux/files/usr/bin/bash
# Screenshot the phone into a local png.
# usage: shot.sh <out.png>
#
# screencap writes to a file on shared storage and we read it from there, rather
# than piping the image back: the binary stream does not survive the shell
# transport intact and you get a png no decoder will open.
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
OUT=${1:?usage: shot.sh <out.png>}
TMP=/storage/emulated/0/Download/.shot-$$.png

"$DIR/shell.sh" "screencap -p $TMP" >/dev/null 2>&1
if [ -s "$TMP" ]; then
  mv -f "$TMP" "$OUT"
  echo "$OUT"
else
  rm -f "$TMP"
  echo "shot: screencap produced nothing - is the screen on?" >&2
  exit 1
fi
