# ygo-calc — Yu-Gi-Oh! Neuron 5-player calculator mod

Runtime mod of `jp.konami.YugiohOcgSupports` 4.12.0 (Unity, IL2CPP, arm64) that turns the
stock 2-player life-point calculator into a 3/4/5-player one. Current build: **v232**,
the first stable one of this run — duelist names are correct on designs 1-5, still open on 6-8.

This file is the entry point. Read it, then read the doc that matches what you are about to do.

| You are about to… | Read first |
|---|---|
| change how the mod hooks the game | `docs/ARCHITECTURE.md` |
| move, resize or align anything on screen | `docs/DESIGN.md` — **mandatory**, it is the layout rulebook |
| build, sign, install a version | `docs/BUILD.md` |
| look up a C# class, method or field | `docs/IL2CPP.md` + `reference/il2cpp/` |
| understand why something is the way it is | `docs/HISTORY.md` |
| know what "done" means | `userstories/` — the five acceptance criteria |

## Folder map

```
CLAUDE.md            you are here
DSH.md               the project brief as the user dictated it (goal + acceptance criteria)
docs/                the guides: ARCHITECTURE, DESIGN, BUILD, IL2CPP, HISTORY
userstories/         what the user asked for, one file per requirement, with acceptance tests
src/                 everything we author  (native/ java/ tools/ re/)  — current code ONLY
scripts/             build driver + on-device test automation
build/               compiled outputs (libneuronmod.so, classes4.dex)
apk/                 dist/ = shipped apks, decoded/ = apktool tree, extracted/ = raw apk payload
reference/           read-only lookup data: class dump, per-skin graphics inventory, screenshots
keys/                the signing keystore — see the warning below
archive/             superseded source snapshots, kept for reference, never built
```

Each of those folders has its own `CLAUDE.md` with the rules that apply inside it. The nearest
one wins.

## Hard rules

1. **Never lose `keys/neuron.jks`.** Every installed build is signed with it. A different key
   means Android refuses the upgrade and the user has to uninstall, wiping their duel history.
   Credentials live in the untracked `keys/keystore.env` — see `keys/CLAUDE.md`.
2. **Never lay out in pixels.** Every position and size is a fraction of the canvas rect it
   sits in. The phone has a display cutout and the rects are not the same on every entry;
   pixel constants are the single most common way this mod has broken. See `docs/DESIGN.md`.
3. **A change is not done until it has been checked on all eight Calculator Designs.**
   Standard, Simple, Duel Monsters, GX, 5D's, ZEXAL, ARC-V, VRAINS. Each is a separate prefab
   with different proportions and a different node layout. Something that looks right on
   Standard is routinely broken on ARC-V or VRAINS.
4. **The phone is the test harness.** There is no unit test and no emulator. Reach it with
   `scripts/device/shell.sh` (Shizuku, not adb — `docs/BUILD.md` says why). Never ship a fix
   you have not seen run: three unseen builds in a row produced a regression on 2026-08-29.
5. **One version = one build number, and a build that did not work does not get its own.**
   `scripts/build.sh <prev> <new>` takes the previous apk as its base and swaps in the new
   `.so`. Do not skip numbers. Rebuilding the same number in place is correct when the last
   build of it was broken and never accepted — the user asked for it that way.
6. **Never guess at the game's internals.** Look them up in `reference/il2cpp/classdump.txt`.
   Methods the game never calls are stripped from the binary and simply do not exist.

## Before every commit — a cleaning pass

Do this on your own diff **before** you commit, every time. The next agent to open this file
has to orient fast and must not be led into assuming something that is not true.

1. **Three sentences, often one.** That is the ceiling for a comment. Delete any that narrates
   what the code plainly says or retells how you found the bug; keep the ones that record a
   trap, a measured number, or why the obvious alternative is wrong. The debugging story
   belongs in the commit message and in `userstories/`, not above the function.
2. **Say it once.** If a mechanism is written out over the declaration and again over the
   function, keep the copy next to the mechanism and cut the other.
3. **Name for what a thing is, not how it works.** A helper is named for what it returns.
   A global that shadows another (`g_capPath` / `g_capName`) should read as a pair.
4. **Fix the docs you just made wrong, in the same commit.** `docs/DESIGN.md` and
   `userstories/` are read as fact; a stale line there costs more than no line at all.
5. **Leave the structure better than you found it.** If you had to hunt for something,
   that is the thing to move or name properly before you commit.

## Tests

There is no test suite here and one is not wanted for its own sake — the phone is the test
(`docs/BUILD.md`). Add a check only where it can catch something that has actually gone wrong:
`scripts/build.sh` bails on an empty `.so` because a failed compile once got signed and
shipped. A check that cannot fail is build time spent for nothing, on every version.

## Working language

Code comments and docs are in English. The user writes in Serbian; `DSH.md` and `userstories/`
keep his own wording. Answer him in Serbian.

## Paths

Nothing in this repository hardcodes an absolute path. Scripts resolve their own location
(`DIR=$(cd "$(dirname "$0")" && pwd)`), and `src/re/md.py` / `so.py` derive the project root
from `__file__`, with `NEURON_METADATA` / `NEURON_LIBIL2CPP` as overrides. Keep it that way —
absolute paths are what broke every script when the project moved out of `~/ygo`.

One thing is still missing: `scripts/device/run_design.sh` refuses to run until its `DESIGN_Y`
table is filled in from a screenshot. See `scripts/CLAUDE.md`.
