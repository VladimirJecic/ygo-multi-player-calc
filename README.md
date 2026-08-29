# ygo-multi-player-calc

A runtime mod for the Yu-Gi-Oh! Neuron life-point calculator that turns it from a
two-player calculator into a **three, four or five player** one.

Neuron ships with a fixed two-seat calculator. In a multiplayer game — Battle Royal,
a three-way, a five-man table — everyone ends up tracking their own life points on
paper or on a second phone. This mod adds the missing seats to the calculator the
game already has, keeping its artwork, its animations and its duel log intact.

---

## What it does

- **3, 4 and 5 player layouts** added next to the game's own two-player modes.
  The selection persists between launches.
- **Per-player names**, including for seats 3–5 that the game has no artwork for.
- **The duel log and archive** understand the extra players, so a five-way duel
  reads back correctly afterwards.
- **All eight Calculator Designs** are supported — Standard, Simple, Duel Monsters,
  GX, 5D's, ZEXAL, ARC-V and VRAINS. Each is a separate prefab with its own
  proportions, so each has to be checked separately.

Current build: **v230**. See `userstories/` for what is done and what still isn't.

## How it works, briefly

`libneuronmod.so` is loaded into the game at startup and hooks IL2CPP methods at
runtime by rewriting `MethodInfo` function pointers. Nothing about the game's own
logic is rewritten on disk — the only static change to the apk is a one-line
`System.loadLibrary` in an activity's static initialiser.

Layout is the hard part. The game re-animates panel and button transforms every
frame, so the mod cannot simply set a position; it owns a wrapper hierarchy and
nudges it each frame by the delta between where a thing is and where it should be.
Everything is measured as a fraction of the live canvas rect — never in pixels.

`docs/ARCHITECTURE.md` covers the hooking, `docs/DESIGN.md` the layout rules.

## Building

Everything runs on the phone itself, in Termux — there is no desktop in this loop.
You need `clang`, `python`, `apksigner` (`openjdk-17`), and `adb` over wireless
debugging.

```sh
cp keys/keystore.env.example keys/keystore.env   # fill in your keystore details
scripts/build.sh 230 231                         # compile, sign, install
```

The base of each build is the **previous** apk with only the native library swapped
in, which is why version numbers must not be skipped. Full detail in `docs/BUILD.md`.

## Layout

```
docs/          ARCHITECTURE, DESIGN, BUILD, IL2CPP, HISTORY
userstories/   what was asked for, with acceptance criteria
src/           native/ (the mod)  java/ (toast bridge)  tools/ (apk surgery)  re/ (metadata parsers)
scripts/       build driver + on-device test automation
reference/     class dump, per-skin graphics inventory, screenshots
archive/       superseded snapshots, kept for reference, never built
```

`CLAUDE.md` at the root is the entry point for an agent working on this; each folder
has its own with the rules that apply inside it.

## Not in this repository

The signing keystore, its passwords, and the game itself — the decompiled apk, its
assets and the built binaries are all gitignored. This is a personal mod of a
copyrighted app; it is not distributable, and neither is anything it is built from.
