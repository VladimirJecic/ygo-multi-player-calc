#!/data/data/com.termux/files/usr/bin/bash
# Cold-start into the 5-screen calculator, make three life changes from three
# different duelists, then open the Log.
DIR=$(cd "$(dirname "$0")" && pwd)
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
PKG=jp.konami.YugiohOcgSupports
"$DIR/shell.sh" logcat -c
"$DIR/shell.sh" am force-stop $PKG
"$DIR/shell.sh" monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 14
"$DIR/shell.sh" input tap 542 1530; sleep 7
"$DIR/shell.sh" input tap 420 930;  sleep 10
tap() { for t in "$@"; do "$DIR/shell.sh" input tap $t; sleep 1; done; }
# Duelist 3: -1500
"$DIR/shell.sh" input tap 500 840; sleep 3; tap "340 814" "804 814" "1266 628" "1266 1000" "2194 914"; sleep 2; "$DIR/shell.sh" input tap 340 204; sleep 3
# Duelist 5 (middle): -800
"$DIR/shell.sh" input tap 1268 540; sleep 3; tap "340 814" "1266 442" "1266 1000" "2194 914"; sleep 2; "$DIR/shell.sh" input tap 340 204; sleep 3
# Duelist 2: +200
"$DIR/shell.sh" input tap 2000 300; sleep 3; tap "340 628" "1266 814" "1266 1000" "2194 914"; sleep 2; "$DIR/shell.sh" input tap 340 204; sleep 3
"$DIR/shell.sh" input tap 962 950; sleep 4
"$DIR/shot.sh" "${1:-/data/data/com.termux/files/usr/tmp/log.png}"
