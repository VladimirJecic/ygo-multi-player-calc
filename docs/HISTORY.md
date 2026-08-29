# HISTORY

Where the project has been, and what the state is now. Read this before concluding that an
approach is new — several of the obvious ones have already been tried and reverted.

---

## Timeline

| When | What |
|---|---|
| 2026-08-23 | Started. APK decoded, `global-metadata.dat` cracked (custom v39 format), first injection working. Versions v1–v45 were spent purely on getting the settings rows and the four panels to appear at all. |
| 2026-08-24 | 4- and 5-player calculators working end to end on all eight Calculator Designs: independent counters, keypad, Undo, Reset, and a Log dialog widened from two columns to four or five. Around v125. |
| 2026-08-25 | Per-skin fitting (plate vs rect), cutout handling, button row normalisation. v224 shipped to the user. |
| 2026-08-26 | v228–v230. A 3-player variant branched off (`archive/mod.c.3player`). |
| 2026-08-29 | v231: the struck-through captions and the off-centre 3-player grid. Built and signed, never installed — the phone had wireless debugging off. |
| 2026-08-29 | Project moved to `~/ygo-calc` and reorganised into this structure; 183 MB of stale v25–v45 logcat and two superseded apks deleted. |

## What the early versions were doing

Recovered from the on-screen instruction lines the mod toasted during testing (the logs
themselves have been deleted — see below). These are the only surviving record of v25–v45 and
show the shape of the loop: build, toast an instruction, watch the user tap, read the log.

```
v25  open a DUEL (4 screens mode)
v26  open Calculator settings again
v27  open Calculator settings, tap 4 screens, leave and come back
v28  open a DUEL (4 screens)
v29  open Calculator settings - is 4 screens lit?
v30  enter DUEL, then try closing it with X
v31  enter DUEL then press X to close
v33  enter DUEL - four panels should now be separate
v34  enter DUEL again
v35  enter DUEL once more (diagnosing wrapper creation)
v36  enter DUEL - four separate panels expected
v37  enter DUEL - panels pulled in from the edges
v38  enter DUEL - buttons moved, panels bigger
v39  enter DUEL - Tools/Log repositioned
v40  enter DUEL - Tools between Reset/Undo, Log below
v41  enter DUEL - Tools left of Undo, Log right of Undo
v42  enter DUEL - buttons tucked under Undo
v43  enter DUEL
v44  enter DUEL - Reset/Undo moved to bottom centre
v45  enter DUEL - Reset/Undo at bottom, Log/Tools at corners
```

Fourteen consecutive builds (v26–v37) on one question — whether the injected settings row
stayed lit after leaving and re-entering the screen. That is the normal cost of a change here.

---

## Dead ends — do not re-walk these

1. **Correcting the layout for the display cutout.** Everything looked like it was drifting
   right, so the whole layout was shifted `-63px`. It was not drifting: the middle panel and
   the Timer were being measured by their **pivots** instead of by what they draw, because
   `il2cpp_array_new` had not been resolved and `GetWorldCorners` was never wired up. The
   "fix" left every mod element 63px left of every other screen in the game. `g_shiftUnits` is
   0 and should stay 0. (`DESIGN.md` R7)
2. **Placing panels analytically from anchors and pivot.** Works on Standard, wrong on every
   other skin. Replaced by the measure-and-shift wrapper. (R2/R3)
3. **Patching the codegen `methodPointers` array.** IL2CPP has already cached the pointer into
   `MethodInfo`. (`IL2CPP.md` §2)
4. **Setting the panel transform from a hooked `StartDuel.Update`.** Game code rewrites it in
   the same frame, every frame. The arrangement has to live on a wrapper the mod owns. (R3)
5. **Copying Reset's geometry wholesale from Undo.** Parks Reset exactly on top of Undo and it
   disappears. Copy per child only, or better, normalise both. (`DESIGN.md` §3.3)
6. **Watching the rename input field naively.** The field keeps whatever was typed last, for
   everyone — rename duelist 2 to "Aleksa" and duelists 3, 4 and 5 all became Aleksa as soon as
   you opened them. That is the bug in `reference/screenshots/bug-duplicate-names-5p.png`.
   The field must be reloaded with the selected duelist's own name on every selection change.
7. **Reading a TMP auto-size on the frame you request the fit.** TMP has not recomputed
   anything yet. Let it run ~5 frames, then read.
8. **Reading a container rect on the first frame.** `LifeArea` animates in and is a different
   size on a cold entry than on a re-entry.
9. **Taking `LifeArea` by a fixed child index.** Correct on Standard and Simple, silently
   `NULL` on Duel Monsters — so the mod did nothing at all on those skins and said nothing.

## Bugs that were real, and what they turned out to be

| Symptom | Cause |
|---|---|
| duel screen completely blank on the **second** entry | `SetCalcObject()` resolves by path; a leftover mod object made `OnEnable` throw. Fixed by a full teardown at the top of the hook. |
| ARC-V and VRAINS panels drawn as blank white boxes | 977 Unity asset bundles lost when the apk was merged from base+split. Fixed with `src/tools/addassets.py`. |
| ARC-V panels overlapping their neighbours | fitted to the rect; ARC-V draws a shield taller than its rect. Fixed by fitting to `max(rect, plate)`. |
| GX rows touching | fixed row gap fraction. Now derived from the panel's own drawn height. |
| duelist 1's panel swallowing taps meant for the X | `Quit` is an earlier sibling than `LifeArea`. Re-siblinged. |
| mod button row on top of the Log and Tools popups | the row was appended last. Re-siblinged to just after `Menu`. |
| the calculator canvas left over the main menu on exit | `g_CalcMode` was unmasked before the original `OnDisable` ran. |
| a name typed for duelist 3–5 never surviving OK | `DuelistName1/2` are statics with no player index, and `OnDuelistnameSubmit` is a direct call the hook never sees. |
| one heading twice the size of another in the Log | per-heading auto-size. Now measured then pinned to the smallest. |

---

## Current state (v234 — stable)

Working: settings rows for 4 and 5 screens, the panel grid on all eight designs, independent
counters, per-panel keypad, Reset/Undo, the widened Log dialog, the Log Archives list with
names and timestamps, and restore-from-log.

**v232 is the first build of this run the user has accepted.** Standard (1), Simple (2),
GX (4), 5D's (5) and ARC-V (7) are correct: names in place, not struck through, no clipped
second copy, no flicker, and a rename survives saving.

v233 fixed Duel Monsters (3) and v234 ZEXAL (6), both accepted. Seven of the eight designs
now satisfy US-04; only VRAINS (8) is left, and it has not been looked at since v231.
See `userstories/US-04`.

The three defects reported against v230 on 2026-08-29 resolved as follows.

1. **Names struck through on every panel.** — *fixed in v231, unverified.* `label_panel` moved
   the mod's name field onto the stock caption's world centre, in both axes. Three skins rule
   their caption off with a hairline (`Duelist/ImageLine` 7.78 x 0.04 on Standard, 6.71 x 0.03
   on Simple, `Duelist/line` 4.00 x 0.02 on GX) and the stock caption sits on that rule, so
   matching its y parked the name on the line. The nudge exists only because VRAINS anchors its
   name box off the right of the plate — a horizontal problem — so it is now horizontal only.
2. **3-player mode is not centred**, "return it to the 224 arrangement". — *fixed in v231,
   unverified.* `ty[2]` was `-topMargin`, putting the third panel at the vertical centre while
   the other two sat a full row above it: the block hung from the top with an empty band
   underneath. It now goes on the bottom row at `-cy - topMargin`, like every other count, so
   the three panels are symmetric about the centre line. **The v224 source no longer exists** —
   every build before v230 was deleted before this repository was created — so this is a
   reconstruction of what "centred" means, not a revert.
3. **On the VRAINS design, names cannot be set at all.** — *still open.* Static reading did not
   settle it. The caption path globals are reset on every design change, so a stale path is
   ruled out; on paper VRAINS should resolve `g_capLen` to `Duelist/PlayerName` via the
   blank-name-field branch and work. Distinguishing the remaining candidates needs one logcat
   on the VRAINS design — the mod already prints everything required:

   | Line | What it would mean |
   |---|---|
   | `cap: rejected - the search landed on LifePoints` | the diff keeps hitting the score and resets every frame, so no caption is ever chosen |
   | *(no `cap: len=…` line at all)* | `panel_ready()` never goes true, so the search never runs |
   | `cap: len=… node 'PlayerName'` then no `label:` line | the caption resolves but the write is dropped |
   | `label: caption font is unusable and there is nothing to borrow` | the name is written and drawn with nothing |
   | `name: duelist N is now '…'` missing after a rename | the rename never reaches the mod, so this is not a caption problem at all |

`docs/STRATEGY-unique-names.md` holds the previous agent's written plan for the unique-names
criterion; treat it as a proposal, not as fact — it predates any of the three reports above.

---

## About the deleted logs

74 logcat captures (183 MB) plus `mod.log`, `keepawake.log` and two apktool transcripts were
deleted on 2026-08-29. Every one of them was a v25–v45 capture — **185 versions behind the
current build** — and described bugs fixed long ago. Everything durable in them was already
recorded in `IL2CPP.md`, `DESIGN.md` and the comments in `mod.c`; the version instructions
above were extracted before deleting.

New logs go to `/data/data/com.termux/files/home/.claude/jobs/*/tmp` or another scratch
directory, **never into the repository**. A log is worth keeping only if it is the evidence
for a finding that has not yet been written into a doc — and once it is written, delete it.
