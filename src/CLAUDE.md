# src/ — everything we author

**Only current, shipping code lives here.** Superseded snapshots go to `archive/`, compiled
output goes to `build/`, and the game's own decoded code is in `apk/decoded/` — never in `src/`.

```
native/   mod.c        the mod itself (one translation unit, ~5200 lines)
java/     Toaster.java the toast bridge, compiled once into classes4.dex
tools/    mkapk.py     apk surgery: raw-copy every entry, replace the ones you name
          addassets.py restore asset bundles from a donor apk
re/       md.py        parser for the non-standard global-metadata.dat (v39)
          so.py        method rid → native address, via the codegen module
```

## Rules for all of it

1. **No new dependencies.** Everything runs in Termux on the phone: clang, python3, adb.
   No build system, no package manager, no vendored libraries.
2. **A comment explains *why*, and usually names the bug it prevents.** The comments in
   `mod.c` are the real documentation of how the game behaves — they are how the next agent
   avoids re-walking the dead ends in `docs/HISTORY.md`. If you fix something subtle, write
   the paragraph. Do not delete a comment because the code "reads fine now".
3. **Do not reformat, re-order or "tidy" code you are not changing.** A diff has to be
   reviewable against a game that can only be tested by hand.
4. **Nothing here is dead code you may remove on sight.** The `dump_*` and `probe_*` helpers
   are guarded by `static int once` and are the intended way to learn a new skin's structure.
   They are tools, not leftovers.

## `native/mod.c` house style

- `static` on everything; no headers, no second .c file.
- Log with `nlog()`. Tag is `NEURONMOD`. Log every miss — a `NULL` node that goes unreported
  is how the mod once did nothing at all on three skins without anyone noticing.
- Forward-declare at the top of the section that needs it, not in a header block.
- Section banners `/* ---------- name ---------- */` are the navigation:
  `grep -n '^/\* -\{3,\}' native/mod.c`
- Guard every hook body with the mode check (`if (g_selMode < 3) { orig(...); return; }`) so
  the stock 2-player modes are never touched.
- **Whether `orig_*` runs first or last is behaviour, not style.** See `docs/ARCHITECTURE.md` §2.
- Never lay out in pixels — `docs/DESIGN.md` R1.

## `tools/` and `re/`

Single-purpose scripts, stdlib only, docstring at the top saying what and why. `md.py` and
`so.py` still hardcode the pre-reorganisation paths — see the root `CLAUDE.md`.
