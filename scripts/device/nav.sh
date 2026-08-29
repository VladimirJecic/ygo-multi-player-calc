#!/data/data/com.termux/files/usr/bin/bash
# Cold-start and land in the calculator, checking each step instead of guessing
# at sleeps - off charge this phone takes three times as long to boot the game
# as it did on the bench.
# usage: nav.sh [mode-y] [design-y]
DIR=$(cd "$(dirname "$0")" && pwd)
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
PKG=jp.konami.YugiohOcgSupports
SHOT=/data/data/com.termux/files/usr/tmp/nav.png
MY=${1:-}; DY=${2:-}

shot() { "$DIR/shot.sh" $SHOT >/dev/null 2>&1; }
# 1 = landscape (the duel screen), 0 = portrait or no shot
landscape() { shot && python3 -c "
from PIL import Image
import sys
sys.exit(0 if Image.open('$SHOT').size[0] > Image.open('$SHOT').size[1] else 1)"; }
lit() { shot && python3 -c "
from PIL import Image, ImageStat
import sys
sys.exit(0 if ImageStat.Stat(Image.open('$SHOT').convert('L')).mean[0] > 12 else 1)"; }

"$DIR/shell.sh" am force-stop $PKG
"$DIR/shell.sh" monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
for i in $(seq 1 60); do sleep 3; lit && break; done   # title screen has drawn
sleep 3
for i in $(seq 1 10); do                                # Tap to Start -> menu
  "$DIR/shell.sh" input tap 542 1530; sleep 4
  shot && python3 -c "
from PIL import Image
import sys
im = Image.open('$SHOT').convert('L').crop((300,880,760,980))
from PIL import ImageStat
sys.exit(0 if ImageStat.Stat(im).mean[0] > 40 else 1)" && break
done
if [ -n "$MY" ]; then
  "$DIR/shell.sh" input tap 441 1665; sleep 6
  "$DIR/shell.sh" input tap 975 "$MY"; sleep 3
  [ -n "$DY" ] && { "$DIR/shell.sh" input tap 975 "$DY"; sleep 3; }
  "$DIR/shell.sh" input tap 75 189;  sleep 5
fi
for i in $(seq 1 12); do                                # DUEL! -> the calculator
  landscape && break
  "$DIR/shell.sh" input tap 420 930; sleep 5
done
sleep 6
landscape || { echo "nav: never reached the duel screen"; exit 1; }
echo "nav: in the calculator"
