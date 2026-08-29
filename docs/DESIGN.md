# DESIGN — the layout rulebook

Every screen the mod touches, how its elements are organised, and the rules for moving them.
**Read this before changing any position, size, scale or alignment.** Almost every visual
regression in 230 versions came from breaking one of the rules in §1.

---

## 1. The nine rules

**R1 — Never lay out in pixels.** Every number is a fraction of the rect it lives in
(`h * 0.030f`, `vis * 0.32f`, `-dh * 0.062f`). The rect is not the same on every entry
(`LifeArea` is 2003×900 cold, 1898×848 on re-entry) and it is not the same on another phone.
A pixel constant is a bug that shows up on someone else's screen.

**R2 — Measure, don't predict.** Do not compute where a game-owned element "should" be from
its anchors and pivot. Put a marker where you want it, read where the element actually is, and
shift the wrapper you own by the difference, every frame. Placing panels analytically was tried
and does not survive across skins.

**R3 — Own a wrapper; never move a game-driven transform.** Three levels:
`ModSlot` (target, ours) → `ModPanelHold` (ours, shifted each frame) → the stock panel
(left free). Same for buttons: `ModButtonRow` → `ModCell<n>` → `ModHold<n>` → the button.
Writing straight onto the panel loses — game code rewrites it every frame.

**R4 — Wait for the rect to settle.** Refuse to compute a layout until the container rect has
been unchanged for ~5 frames. An early read is a few per cent short and everything lands wrong.

**R5 — Fit to the plate, not to the rect.** A skin's rect is much bigger than the plate it
draws, and ARC-V draws a shield half again *taller* than its rect. Take
`max(rect, plate)` on each axis, then `scale = min(maxW/panelW, maxH/panelH)` — fit both axes
and take the tighter. One scale for all panels.

**R6 — The plate is off-centre inside the rect, and mirrored between the left and right
prefabs.** It cancels out across the four corners and shows only on the lone middle panel.
Measure it from the mirrored pair: `((a0-s0) - (a1-s1)) * 0.25`, where `a` is the largest
image's centre and `s` the slot centre. The reading only has to be *consistent* between the
two prefabs, not absolutely correct.

**R7 — Centre on the safe area, not on the glass.** The phone has a display cutout
(`Screen.safeArea.x = 126` of 2412 here). The game centres its whole UI on the safe area, so
the game's own dead centre is 1268, not 1205. The mod inherits that by staying on the canvas
centre — `g_shiftUnits` is deliberately **0**. What the cutout does cost is *symmetry*: the
glass runs further one way than the other, so symmetric columns must be budgeted against the
**shorter** half (`g_edgeUnits`), or the outer panel on the cutout-free side hangs off screen.
> Historical warning: an earlier version "corrected" the layout by `-63px` and put everything
> left of every other screen in the game. The real drift was measuring by pivots instead of by
> what is drawn. Do not re-introduce that correction.

**R8 — `Transform.position` is the pivot, not the centre.** Skins do not pivot their panels the
same way. Use `world_centre()` / `drawn_union()` / `GetWorldCorners`, never the raw position.

**R9 — Re-assert every frame.** Scale, caption anchors, button mirroring, the Timer's x — game
code re-drives all of them. The settle pass runs unconditionally while the screen is up.

---

## 2. Calculator settings screen (`Calculator`)

Path: `SetCalculator/Scroll View/Viewport/CalculatorSettings/Mode/Content/Toggle`
Children are the mode rows: `Multi`, `Vertical`, `Horizontal` — each with `Text`,
`RadioButton`, `Line`.

Modes: `0` = 2 screens · `1` = one screen vertical · `2` = one screen horizontal ·
**`3` = 4 screens (ours)** · **`4` = 5 screens (ours)**.

How the two extra rows are made:

1. `Instantiate(row)` then **`Transform.SetParent(parent, false)`**. The one-argument
   `SetParent` preserves the world transform and leaves the clone invisible while still taking
   up layout space — a classic hour lost.
2. **Remove the `UISystem.LocalizeText.BindingText` component** from the clone, or it rebinds
   its `TextId` on activation and re-applies the source label over yours.
3. Retarget the clone's button at its mode index.
4. Paint the radios yourself: `capture_sprites()` reads the on/off sprites off a stock row
   (ask the game to paint row 0 with `isSave = false` first), then `apply_selection()` sets
   exactly one lit.
5. Repaint **after** `FixLayout`'s coroutine — it repaints the three stock radios a frame late.

Selection persists via `files/neuronmod.mode`, and by letting the original
`OnClickCalcMode` run (its save is unconditional).

> Changing the **Calculator Design** takes effect on the *next* duel entry, not the current
> one. When sweeping skins, enter and leave a duel once before screenshotting.

---

## 3. Duel screen (`StartDuel`) — the main event

Sibling order under `Duel`: `Timer`, `Quit` (the X, top-left), `Info` (help, top-right),
`LifeArea`, `Reset`, `Undo`, `Menu` (`ShowHistory` / `Results` / `PlayUtility` / `SetBGM` /
`SearchCardByCamera`), then the dialogs (`ConfirmReset`, `LifeLog`, `Utility`, `Bgm`,
`Download`, `DataExpantion`).

### 3.1 The panel grid

Panels are cloned from the two stock ones, **alternating `i % 2`** so each column keeps the
prefab mirroring the skin expects, and named `Life0<n>`.

Slots, in canvas units, with `vis` = visible canvas width, `h` = LifeArea height:

```
budget per panel      maxW = vis * (np == 5 ? 0.30 : 0.32)     maxH = h * 0.50
scale                 min(maxW/panelW, maxH/panelH)            one scale for all
column offset  cx     halfUnits − sw/2 − vis*0.035,  clamped to w/2 − sw/2 − w*0.035
row offset     cy     (panelDrawnHeight/2) + h*0.030,  clamped to h/2 − sh2*0.52 − h*0.010 − topMargin
top margin            h * 0.012                                (clears the status bar)
```

Positions (`sh` = `g_shiftUnits`, currently 0):

| players | slot 0 | slot 1 | slot 2 | slot 3 | slot 4 |
|---|---|---|---|---|---|
| 4 | (−cx, +cy) | (+cx, +cy) | (−cx, −cy) | (+cx, −cy) | — |
| 5 | (−cx, +cy) | (+cx, +cy) | (−cx, −cy) | (+cx, −cy) | (0, 0) middle |
| 3 | (−cx, +cy) | (+cx, +cy) | (0, −cy) bottom centre | — | — |

Three duelists reuse the four-screen grid with **the bottom row reduced to one panel**, moved
to the centre: two across the top, the third below and between them. All counts share one code
path — `np` and the `tx[]`/`ty[]` tables are the only difference.

**The third panel goes on the bottom row, not at the centre of the screen.** Parking it at
`y = 0` left the whole block hanging from the top with an empty band underneath — the block was
centred on nothing. On the bottom row it is symmetric about the centre line and keeps the row
spacing every other count already uses.

Why each clamp exists, so you do not "simplify" one away:
- the `vis*0.035` inset — on 5D's the Quit cross sits in the top-left corner and was landing
  on duelist 1's name;
- the `cyMax` clamp — a skin can draw past its plate (the ARC-V shield's tail is about a tenth
  of its height) and the whole grid is already pushed down by `topMargin`, so the bottom row is
  where the overhang shows;
- the row gap of `panelDrawnHeight*0.5 + h*0.030` — anything tighter and GX's rows touch.

### 3.2 Duelist captions

**Diff the two stock panels rather than guessing node names.** The two panels are the same
prefab showing different captions, so whatever differs between them *is* the caption
(`dump_panel_diff` / `diff_caption`). Skins split two ways:

- **TMP text** — just `set_text` it on the clones.
- **Artwork** (5D's, ARC-V, VRAINS) — `Duelist/TextPlayer` with sprites like
  `lp_5ds_bg_1p` / `_2p`, and the game ships **no art for players 3–5**. Hide that Image and
  write into the *blank* TMP sibling `Duelist/PlayerName` — the custom-name field the skin
  leaves empty and switched off, so `SetActive(true)` it.
  - **Pick the empty sibling.** The other one says "LP".
  - **Do not copy the artwork node's rect onto it.** The name field is already positioned
    correctly; copying scrambles it.
  - **Correct its x, never its y.** VRAINS anchors its name box off the right of the plate, so
    the field's world x is matched to the stock caption's. Matching the *y* as well is what
    struck every name through: Standard rules its caption off with `Duelist/ImageLine`
    (7.78 x 0.04), Simple with one 6.71 x 0.03 and GX with `Duelist/line` 4.00 x 0.02, and the
    stock caption sits on that hairline. Move the name onto it and the line runs through the
    glyphs. The field is already at the right height in its own skin.
- The localiser fills captions a frame or two late, so **re-run the diff each frame during the
  settle burst**, not once at build time. `panel_ready()` gates it; `caption_is_the_score()`
  stops it landing on the life total.

### 3.3 The button row

Four buttons in one row across the bottom: **Log · Reset · Undo · Tools**
(`Menu/ShowHistory`, `Reset`, `Undo`, `Menu/PlayUtility`). `SetBGM`, `SearchCardByCamera` and
`Info` are switched off to free the space.

- A real `UnityEngine.UI.HorizontalLayoutGroup` on `ModButtonRow` does the spacing.
- **Side margin comes from `sizeDelta.x = rowW − dw`, not from `m_Padding`** — `RectOffset` is
  a native wrapper and its fields are not plain ints. `rowW = halfUnits * 0.78` is the single
  knob that tightens the gaps.
- **Configure UI components by writing their serialized fields**
  (`m_ChildForceExpandWidth`, `m_ChildAlignment`, `m_Spacing`, …). IL2CPP strips the property
  setters the game itself never calls.
- **Normalise all four buttons; don't copy geometry between them.** Reset and Undo are one
  prefab with Undo mirrored (`localScale.y` negative), which is why their triangles point
  opposite ways and their captions sit at different heights. Uniform scale 1, flip only
  `ImageBtn` / `Image_eff` on Reset+Undo, pin every `text_img` to anchor (0.5, 0) pos (0, 10).
  Copying Reset's whole root rect from Undo parks it exactly on top of Undo and it vanishes.
- **Align on `Button.m_TargetGraphic`'s transform**, not the button rect — Reset's caption sits
  above its icon, so the rect centres differ even when the rects line up.
- Sibling index: slot the row **right after `Menu`** — above the panels, below every dialog.
  Appended last, it sat on top of the Log and Tools popups.
- `Quit` is an *earlier* sibling than `LifeArea`, so duelist 1's full-rect `btn` child (whose
  touch area covers its transparent corners) swallowed taps meant for the X. Move `Quit` to
  just after `LifeArea`, and lift it by `-dh * 0.062` to clear the top row.
- The `Timer` is a game element but inherits the same canvas — apply the same `g_shiftUnits`
  correction to its `anchoredPosition.x` each frame.
- Reset and Undo carry only a `SoundPlay` component; their up/down bob is game code in
  `Update`, so it is cancelled by running the settle pass every frame, not by disabling a
  component.

---

## 4. Keypad (the panel you get after tapping a duelist)

`StartDuel.SelectedPlayerIndex` is at `self + 44`. The model is correct for all five players;
only the *readout* is stale, because the header is wired to the game's cache of two:

- **name** → `DuelistNameIf` (`self + 256`) → `m_Placeholder` — write
  `g_pname[idx]`, or `"Duelist N"`.
- **life** → `EquationText` (`self + 248`) — rewrite only the **leading integer** of the
  equation line, leaving the rest of the expression alone.
- Rewrite for **every** index, not just 3–5: the first two reach the keypad through another
  player's branch, so their header is as much ours to write.

The count animation is aimed at the tapped panel by `aim_animation()`; `scrub_panels()` puts
back anything it scribbled on the others.

---

## 5. Log dialog (`LifeLog`) and the saved-log screen

Same table drawn in two places, so both go through `table_rows()`.

- Column count = player count (2 → 4 or 5).
- **`col_geom(c, n, rowW)` reproduces the stock 2-column layout exactly** when `n = 2`
  (308 wide in a 620 row, centred at ±156) and then widens it. Never special-case n=2.
  ```
  w   = (rowW − gap*(n−1)) / n          gap = 4
  cx  = −rowW/2 + w/2 + c*(w + gap)
  sdx = w − rowW                        (sizeDelta.x for the cell)
  ```
- Headings sit at `y = −110`, height 60, anchored top-centre, x offset by the Scroll View's own
  `anchoredPosition.x`.
- **Font size: measure for 5 frames, then pin.** Auto-size fits each heading independently, so
  a short name came out twice the size of a long one. Let the fit run, take the **smallest**
  size any heading settled on, then force every heading to it. Two earlier attempts failed by
  reading the fitted size on the same frame it was requested, and by reading it off the
  heading's own transform (several skins keep no text there — walk the subtree,
  `tmp_tree_min_fs`).
- Cells are cloned from `Duelist01` and named `ModCol2..4`; `cell_col()` maps a node name back
  to its column. Move a cell into its column and **leave everything else about it alone**.
- An undone entry is marked with a back-arrow icon drawn **inside** the text cell — preserve it
  when rewriting the text (`col_fill`'s `mark`).
- Log Archives rows: the game leaves empty boxes where the duel time belongs, and fills a name
  line with its own two duelists. `fix_row_labels()` rewrites them and `row_add_time()` stamps
  the minute from the archive. The footer's four unrelated tabs are trimmed — and it puts them
  back on `OnEnable`, `Default`, `Adapt` and `Start`, all four of which are hooked.

---

## 6. Checklist before you call a layout change done

1. All eight designs: Standard, Simple, Duel Monsters, GX, 5D's, ZEXAL, ARC-V, VRAINS.
2. Both counts (4 and 5), and 3 if it is enabled.
3. Cold entry **and** a second entry without restarting (different rect, and the teardown path).
4. Nothing overlaps the Quit X, the Timer, or the bottom button row.
5. Nothing is clipped at the cutout edge.
6. Names still unique and correctly matched to their panel — `userstories/US-04`.
