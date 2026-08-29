#!/data/data/com.termux/files/usr/bin/bash
# Tap, but only ever into the game.  A stray tap on the user's home screen
# opens whatever happens to be there, so refuse if the game is not in front.
DIR=$(cd "$(dirname "$0")" && pwd)
set -u
FG=$("$DIR/shell.sh" dumpsys activity activities 2>/dev/null | grep -m1 "ResumedActivity" | grep -o "jp.konami.YugiohOcgSupports")
if [ "$FG" != "jp.konami.YugiohOcgSupports" ]; then
  echo "REFUSED tap $*: game is not in front"; exit 1
fi
"$DIR/shell.sh" input tap "$@"
