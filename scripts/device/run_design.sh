#!/data/data/com.termux/files/usr/bin/bash
# Switch to Calculator Design <n> and screenshot the duel screen.
# usage: run_design.sh <design 1..8> <out.png>
#
#   1 standard  2 simple  3 duelmonsters  4 gx  5 5ds  6 zexal  7 arcv  8 vrains
#
# Thin wrapper over design.sh, which does the actual tapping.  All this adds is
# the design index -> checkbox y lookup.
#
# The y values below are NOT filled in: the originals were lost with ~/ygo and
# inventing tap coordinates on the user's own phone is how you end up opening
# whatever app happens to sit under the guess.  Fill the table once, from a real
# screenshot of the Calculator Design list:
#
#   scripts/device/shot.sh /tmp/designs.png     # with the list open
#
# then read the y of each row's checkbox and put them here.
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
N=${1:?usage: run_design.sh <design 1..8> <out.png>}
OUT=${2:?usage: run_design.sh <design 1..8> <out.png>}

DESIGN_Y=(0 0 0 0 0 0 0 0 0)      # index 0 unused; 1..8 are the designs

Y=${DESIGN_Y[$N]:-0}
if [ "$Y" -eq 0 ]; then
  echo "run_design: no y coordinate for design $N - fill DESIGN_Y in $0 first" >&2
  exit 1
fi

exec "$DIR/design.sh" "$Y" "$OUT"
