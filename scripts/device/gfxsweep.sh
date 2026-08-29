#!/data/data/com.termux/files/usr/bin/bash
# Walk one or more Calculator Designs and collect the mod's own gfx: dump lines
# into reference/gfx_inventory.txt.
# usage: gfxsweep.sh <design 1..8> [design ...]
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
S=${TMPDIR:-/data/data/com.termux/files/usr/tmp}/gfxsweep   # screenshots: scratch, not the repo
mkdir -p "$S"
NAMES=(_ standard simple duelmonsters gx 5ds zexal arcv vrains)
OUT=$ROOT/reference/gfx_inventory.txt
: > "$OUT"
for d in "$@"; do
  n=${NAMES[$d]}
  echo "=== $n ===" >> "$OUT"
  adb logcat -c 2>/dev/null
  "$DIR/run_design.sh" $d "$S/gfx_${n}.png" >/dev/null 2>&1
  adb logcat -d -s NEURONMOD 2>/dev/null | grep -E "gfx:|fit check|-> scale" | sed 's/.*NEURONMOD: //' >> "$OUT"
done
echo "GFX DONE" >> "$OUT"
