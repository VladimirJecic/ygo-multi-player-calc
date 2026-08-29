#!/data/data/com.termux/files/usr/bin/bash
# usage: logtest2.sh <mode-y> <design-y> <out.png>
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
PKG=jp.konami.YugiohOcgSupports
MY=$1; DY=$2; OUT=$3
adb logcat -c
adb shell am force-stop $PKG
adb shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 14
adb shell input tap 542 1530; sleep 7
adb shell input tap 441 1665; sleep 5
adb shell input tap 975 $MY; sleep 3
adb shell input tap 975 $DY; sleep 3
adb shell input tap 75 189;  sleep 4
adb shell input tap 420 930; sleep 10
tap() { for t in "$@"; do adb shell input tap $t; sleep 1; done; }
adb shell input tap 500 840; sleep 3; tap "340 814" "804 814" "1266 628" "1266 1000" "2194 914"; sleep 2; adb shell input tap 340 204; sleep 3
adb shell input tap 962 950; sleep 4
"$DIR/shot.sh" "$OUT"
