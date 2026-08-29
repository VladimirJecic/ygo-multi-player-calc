# scripts/

`build.sh` is the whole build pipeline (see `../docs/BUILD.md`).
`device/` drives the real game on the real phone through `adb`.

## Rules

1. **This is the user's own phone, not a test device.** Never send a bare
   `adb shell input tap`. Use `device/gtap.sh`, which refuses to tap unless the game is the
   resumed activity — a stray tap on the home screen opens whatever happens to be there.
2. **Check state, don't sleep and hope.** `nav.sh` is the model: it screenshots and tests
   (is the screen lit? is it landscape yet?) at each step, because off the charger this phone
   boots the game three times slower than on the bench. New automation follows that pattern.
3. **Never `force-stop` or install while the user is mid-duel.** Ask first.
4. **`keepawake.sh` must be turned off when the session ends** — `keepawake-stop.sh`. It holds
   a wake lock and pokes the screen every 10 minutes.
5. Screenshots and logs go to a scratch directory, never into the repository.

## Paths

Every script resolves its own location and works from anywhere:

```sh
DIR=$(cd "$(dirname "$0")" && pwd)          # scripts/device
ROOT=$(cd "$DIR/../.." && pwd)              # the project root
```

Never hardcode an absolute path into a script here — that is exactly what broke all of them
when the project moved out of `~/ygo`. Screenshots and logs go to `$TMPDIR`, not the repo;
the one exception is `gfxsweep.sh`, which writes `reference/gfx_inventory.txt` because that
file *is* its product.

`shot.sh` was re-created during the move (`adb exec-out screencap -p` — `shell screencap`
mangles the binary stream on some builds).

## Unverified tap coordinates

`run_design.sh` **will refuse to run** until its `DESIGN_Y` table is filled in. The original
coordinates were lost with `~/ygo`, and guessing tap positions on the user's own phone is how
you end up opening whatever app sits under the guess. Take a screenshot of the Calculator
Design list, read the eight checkbox y values off it, and fill the table once.

The same caveat applies to the coordinates already baked into `design.sh`, `nav.sh`,
`logtest.sh` and `logtest2.sh`: they are for this phone's screen at the layout the game had in
August 2026. Verify against a screenshot before trusting a sweep.
