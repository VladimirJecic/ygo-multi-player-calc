#!/data/data/com.termux/files/usr/bin/bash
# design.sh <checkbox-y> <out.png> - switch Calculator Design and land back in the duel
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
Y=$1; OUT=$2
"$DIR/gtap.sh" 257 129 >/dev/null; sleep 5          # X out of the duel
"$DIR/gtap.sh" 441 1665 >/dev/null; sleep 5         # Calculator settings
"$DIR/gtap.sh" 975 "$Y" >/dev/null;  sleep 3        # the design
"$DIR/gtap.sh" 75 189 >/dev/null;   sleep 5         # back
"$DIR/gtap.sh" 420 930 >/dev/null;  sleep 14        # DUEL!
"$DIR/shot.sh" "$OUT" >/dev/null && echo "shot $OUT"
