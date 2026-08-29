# ARCHITECTURE

How the mod gets into the game, what it hooks, and how it is put together.
Read `DESIGN.md` next if you are going to move anything on screen.

---

## 1. The shape of the thing

The game is Unity 2021-era, compiled with IL2CPP: all the C# lives as native arm64 code inside
`libil2cpp.so`, with the type system in `assets/bin/Data/Managed/Metadata/global-metadata.dat`.
There is no managed assembly to patch and no Mono to attach to.

The mod is therefore a **native library injected into the game's own process** that talks to
the IL2CPP runtime through its exported C API and rewrites `MethodInfo` records at runtime.
Nothing about the game's own code is patched on disk except one line of smali.

```
 apk
  ├─ classes.dex          ← one <clinit> injected: System.loadLibrary("neuronmod")
  │                          in com/google/firebase/MessagingUnityPlayerActivity
  ├─ classes4.dex         ← added: neuron.mod.Toaster (on-screen toast from native code)
  └─ lib/arm64-v8a/
     ├─ libil2cpp.so      ← the game, untouched
     └─ libneuronmod.so   ← added: the mod
```

Boot order: Android loads the activity → the injected `<clinit>` loads `libneuronmod.so` →
`JNI_OnLoad` starts a watcher thread → the thread waits for `libil2cpp.so` to appear **plus
about three seconds** → `unity_init()` resolves every symbol and class it needs → hooks are
installed.

> The three-second wait is not superstition. Touching the runtime while Unity is still
> initialising kills the process with no tombstone at all.

## 2. Hooking

`il2cpp_class_get_method_from_name()` returns a `MethodInfo*`. The mod overwrites two fields
in it:

```
MethodInfo  +0   methodPointer          ← patched
            +8   virtualMethodPointer   ← patched
            +16  invoker
            +24  name
            +32  klass
```

Patching the codegen `methodPointers` array instead **does not work** — IL2CPP has already
cached the pointer into `MethodInfo` by the time the mod runs.

What this reaches:

- ✅ MonoBehaviour messages (`OnEnable`, `Update`, `OnDisable`)
- ✅ `UnityEvent` callbacks (button `onClick` handlers)
- ✅ anything the runtime invokes indirectly
- ❌ **direct C# → C# calls**, which the compiler turned into a plain `bl`. Those need an
  inline hook or a caller-side patch. Several things in this mod exist only because a call
  could not be intercepted (see `watch_rename`, §5).

Every hook follows the same shape:

```c
static void (*orig_X)(void *, void *);
static void my_X(void *self, void *mi) {
    if (g_selMode < 3) { orig_X(self, mi); return; }   /* stock modes: hands off */
    ...
    orig_X(self, mi);        /* order matters, see below */
    ...
}
```

**Whether you call the original first or last is load-bearing**, not style. Two examples that
cost days:

- `Calculator.OnClickCalcMode(n, isSave)` must run the original with `n = 3` or `4` — its
  save is unconditional, so letting it through is exactly what makes the extra modes persist.
  It paints no radio for an unknown `n` and does not crash.
- `StartDuel.OnDisable` reads `g_CalcMode`, so the mod's mask must stay in place until after
  the original has run, or the calculator canvas is left on top of the main menu.

## 3. The hooked surface

| Class / method | Why |
|---|---|
| `Calculator.OnEnable` | inject the "4 screens" / "5 screens" rows into the settings list |
| `Calculator.OnClickCalcMode` | accept modes 3 and 4, persist them |
| `Calculator.FixLayout` (coroutine `MoveNext`) | it repaints the stock radios a frame later — repaint ours after it |
| `StartDuel.OnEnable` | full teardown, then rebuild the multi-player screen |
| `StartDuel.Update` | the per-frame settle pass (§4) |
| `StartDuel.OnDisable` | hand the screen back to the game exactly as found |
| `StartDuel.ClickLife(idx)` | route a panel tap to the right player; aim the count animation |
| `Duel.CreatePlayers` | the stock one ignores its `playerNum` and always makes 2 |
| `DoReset` / `Undo` | UnityEvent handlers — extend to N players |
| `LogList.OnEnable`, `DisplayLogList` | the Log Archives list: names, timestamps |
| `Footer.OnEnable` / `Default` / `Adapt` / `Start` | it puts its tabs back, repeatedly |

## 4. The central technique: **settle, don't set**

The duel screen animates the LP panels' and buttons' own transforms **every frame from game
code** — not a Tween, not an Animator, not a layout component, so probing for those finds
nothing. Anything the mod writes onto a game-owned transform is overwritten before it is drawn.

The whole mod is built around this. Three levels, and only the middle one is ours to move:

```
ModSlot<n>      ← ours. The target position. Never moves after the layout is computed.
  └ ModPanelHold  ← ours. Shifted each frame by (slot world pos − panel world pos).
      └ Life0<n>    ← the game's. Left completely free; game code keeps driving it.
```

Each frame: measure where the panel actually ended up, move the holder by the error. It
converges in about three frames and then idles. The button row uses the identical pattern
(`ModButtonRow` → `ModCell<n>` → `ModHold<n>` → the stock button).

Corollaries you will trip over:

- **Read a rect only once it has held still.** `LifeArea` is 2003×900 on a cold entry and
  1898×848 on a re-entry, and it animates in. `build_four_player_layout` refuses to run until
  the rect has been unchanged for 5 frames.
- **Re-assert every frame.** Button scale, caption anchors, the Timer's x — all re-driven by
  game code, all re-applied by `settle_buttons()` / `settle_panels()` on every `Update`.

## 5. State the mod owns

The game holds exactly two of everything. The mod keeps its own five and writes them back:

| What | Where | Persisted to |
|---|---|---|
| selected calc mode (3 = four screens, 4 = five) | `g_selMode` | `files/neuronmod.mode` |
| five duelist names | `g_pname[5][40]` | `files/neuronmod.names` |
| per-duel life table for the Log | `g_logStart[5]`, `g_logLife[128][5]` | `files/neuronmod.logdb` (last 40 duels) |

All three files live in `/data/user/0/jp.konami.YugiohOcgSupports/files/`.

Names deserve their own note, because it is the acceptance criterion that keeps regressing
(`userstories/US-04-jedinstvena-imena-4-5.md`). The game has `StartDuel.DuelistName1` /
`DuelistName2` as **statics**, with nothing indexed by player, so a name typed for duelist 4
was written into duelist 2's slot. And `OnDuelistnameSubmit` is a direct call that the hook
never sees. So `watch_rename()` polls the `TMP_InputField` instead, and:

- reloads the field with the selected duelist's own name **every time the selection changes**
  (without this, the field keeps the last text and hands it to whoever you open next — that
  is literally the "several Aleksas" bug in `reference/screenshots/`),
- commits only when the text differs from what the mod put there,
- saves and restores the game's own two statics around any rename meant for duelist 3–5.

## 6. Failure modes worth knowing before you debug

- **Blank duel screen on the *second* entry.** `StartDuel.SetCalcObject()` resolves its pieces
  **by path**, so any mod object left in the tree when `OnEnable` runs makes it throw before it
  activates a canvas. Hence the full teardown at the top of the `OnEnable` hook. The symptom
  that pinpoints it: the hook's own log line after `orig(...)` never prints.
- **`Object.Destroy` is deferred to end of frame.** Reparent to `null` first, *then* destroy,
  or the doomed object is still found by name in the same frame.
- **Stripped methods.** IL2CPP drops anything the game itself never calls —
  `GameObject.GetComponents(Type)` does not exist in this build. Resolve overloads by
  signature and expect misses; `probe_components()` exists because of this.
- **A missing node is silent.** `find_life_area` originally walked a fixed child index, which
  was correct on Standard and Simple and `NULL` on Duel Monsters — so the mod did nothing at
  all on those skins and nothing said so. Search by name with `find_deep()` and **log the miss**.

## 7. Diagnostics

Everything goes to logcat under the tag `NEURONMOD`:

```
adb logcat -c && adb logcat -s NEURONMOD
```

The main buffer must be raised — the default 256 KiB evicts the mod's output within a minute:

```
adb logcat -G 16M
```

Two channels drive the mod from outside while it is running: a TCP listener on
`127.0.0.1:24243` and an instruction file at
`/storage/emulated/0/Android/data/jp.konami.YugiohOcgSupports/files/neuronmod.say`, whose
contents are shown as an on-screen toast. That is how a test tells the user what to tap.

The one-shot dump helpers (`dump_tree`, `dump_rect`, `dump_graphics`, `dump_panel_diff`,
`dump_button`, `probe_components`) are all guarded by `static int once` and are the intended
way to learn a new skin's structure. `reference/gfx_inventory.txt` is a saved run of them.

## 8. Module layout of the source

```
src/native/mod.c    the whole mod, ~5200 lines, one translation unit
src/java/           neuron.mod.Toaster — native code cannot raise a Toast on its own
src/tools/          mkapk.py, addassets.py — apk surgery, see BUILD.md
src/re/             md.py, so.py — the custom global-metadata parser, see IL2CPP.md
```

`mod.c` is one file on purpose: it is loaded into a foreign process, has no build system
beyond a single clang invocation, and every hook needs the same globals. It is kept navigable
by banner comments — `grep -n '^/\* -\{3,\}' src/native/mod.c` lists the sections:

```
module base · il2cpp api · hook · duelist names · the button row
the Log dialog · laying the rows out in five columns · the saved-log screen
```

House style, follow it: `static` everything, no headers, `nlog()` for logging, and a comment
above anything non-obvious that says **why**, in prose, usually naming the bug it prevents.
The comments in `mod.c` are the real documentation of the game's behaviour — when you fix
something subtle, write the paragraph.
