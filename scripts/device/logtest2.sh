#!/data/data/com.termux/files/usr/bin/bash
# usage: logtest2.sh <mode-y> <design-y> <out.png>
DIR=$(cd "$(dirname "$0")" && pwd)
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
PKG=jp.konami.YugiohOcgSupports
MY=$1; DY=$2; OUT=$3
"$DIR/shell.sh" logcat -c
"$DIR/shell.sh" am force-stop $PKG
"$DIR/shell.sh" monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 14
"$DIR/shell.sh" input tap 542 1530; sleep 7
"$DIR/shell.sh" input tap 441 1665; sleep 5
"$DIR/shell.sh" input tap 975 $MY; sleep 3
"$DIR/shell.sh" input tap 975 $DY; sleep 3
"$DIR/shell.sh" input tap 75 189;  sleep 4
"$DIR/shell.sh" input tap 420 930; sleep 10
tap() { for t in "$@"; do "$DIR/shell.sh" input tap $t; sleep 1; done; }
"$DIR/shell.sh" input tap 500 840; sleep 3; tap "340 814" "804 814" "1266 628" "1266 1000" "2194 914"; sleep 2; "$DIR/shell.sh" input tap 340 204; sleep 3
"$DIR/shell.sh" input tap 962 950; sleep 4
"$DIR/shot.sh" "$OUT"
