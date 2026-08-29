# IL2CPP — reverse-engineering notes

Everything here cost real effort to find. Check it before you re-derive anything.

---

## 1. The metadata is non-standard

`assets/bin/Data/Managed/Metadata/global-metadata.dat` is **unencrypted but not the stock
format**: version **39**, and the header is 31 `(offset, size, count)` **triples** where the
usual layout has `(offset, size)` pairs. Stock Il2CppDumper cannot read it, and every public
tool that tries will give you nonsense offsets.

Use the custom parsers instead:

- **`src/re/md.py`** — parses `global-metadata.dat`: types, methods, fields, string literals.
- **`src/re/so.py`** — resolves a method rid to a native address through the `Assembly-CSharp`
  codegen module.

```
codegen module   0x2ef35c8
methodPointers   0x30a6ce8      22198 entries
```

Both scripts hardcode the old `~/ygo/work/...` paths. The files now live at:

```
apk/extracted/assets/bin/Data/Managed/Metadata/global-metadata.dat
apk/extracted/lib/arm64-v8a/libil2cpp.so
```

Their output is already saved, so you usually do not need to run them at all:

```
reference/il2cpp/classdump.txt   5.3 MB — every class, its methods, param counts, rid, flags, slot
reference/il2cpp/types.txt       456 KB — type index → name → token
```

`grep -n 'class-or-method-name' reference/il2cpp/classdump.txt` is the first move for any
"does this method exist / how many parameters" question.

---

## 2. Hooking: patch `MethodInfo`, not the pointer table

```
MethodInfo  +0   methodPointer          ← patch this
            +8   virtualMethodPointer   ← and this
            +16  invoker
            +24  name
            +32  klass
```

Patching the codegen `methodPointers` array is **not enough** — IL2CPP has already cached the
pointer into `MethodInfo`. Details and the reachability limits in `ARCHITECTURE.md` §2.

**Direct C# → C# calls are compiled to a plain `bl` and bypass the hook entirely.** If a hook
"never fires", check whether its only caller is C# code before you doubt the patch.

---

## 3. Stripping

IL2CPP drops anything the game itself never calls. Confirmed missing in this build:

- `GameObject.GetComponents(Type)` — hence `probe_components()`, which asks for each Component
  subclass by name instead.
- Property **setters** on `UnityEngine.UI` components the game never sets from code. Write the
  serialized field directly (`m_ChildForceExpandWidth`, `m_Spacing`, `m_ChildAlignment`, …).

Resolve overloads by signature (`meth_sig()`, matching the first parameter's type name) and
expect misses. `RectOffset` is a native wrapper — its fields are not plain ints, so `m_Padding`
cannot be written the obvious way.

---

## 4. The classes that matter

**`Duel`** — already N-player. `Players` list, `GetLife/AddLife/SubLife(playerIndex, …)`,
`VerifyPlayerIndex` checks the real list `Count`. Only `CreatePlayers(playerNum, life1, life2)`
is hardcoded to two — and it **ignores its `playerNum` argument**. The Duel model is not a
field of `StartDuel`; it hangs off the log archive (`get_duel_model()`).

**`StartDuel`** — the LP screen.
```
+44    SelectedPlayerIndex
+144   (fallback calculator GameObject)
+216   CurrentCalculator
+248   EquationText
+256   DuelistNameIf          (TMP_InputField for renaming)
       PlayerNum              field
static DuelistName1 / DuelistName2, SetDuelistName1/2, IsSetDuelistName1/2
```
Nothing in the name API is indexed by player — that is the whole reason the mod keeps its own
five names. `SetCalcObject()` resolves its pieces **by path**, so a stray mod object in the
tree makes `OnEnable` throw.

**`Calculator`** — the settings screen. `OnClickCalcMode(number, isSave)` is a plain
`cmp`/`beq` chain: 0/1/2 paint their radios, anything else paints nothing and does not crash;
the save (`str w20,[x9]`) is unconditional. `isSave = false` skips it — handy for making the
game paint row 0 so the on/off radio sprites can be read off it. `FixLayout` is a coroutine
that repaints the stock radios a frame later.

**`LifeLog`** — only snapshots `LifeOfPlayer1` / `LifeOfPlayer2`, so the mod keeps its own
table. `LifeHistory{PlayerIndex, ExchangeValue, ValueBeforeCalculation}` *is* per-player.

**`g_CalcMode`** — a static the mod masks to 0 for the whole duel screen, because
`StartDuel.OnDisable` keys off it too.

---

## 5. Runtime facts

- Wait **~3 seconds after `libil2cpp.so` appears** before calling into the runtime. Touching it
  during Unity's init kills the process with no tombstone.
- `il2cpp_array_new` must be resolved before `GetWorldCorners` can be used — when it was not,
  everything was measured by pivot instead and the whole layout was "corrected" in the wrong
  direction for several versions.
- `Camera.main.WorldToScreenPoint` disagrees with what is actually on screen on this device.
  Do not use it to detect the cutout offset; read `Screen.safeArea` and `Screen.width`.
- `Object.Destroy` is deferred to end of frame. Reparent to `null` first, then destroy.
- Field lookup walks up the class hierarchy (`fld()` / `fld_obj()`), because several of the
  fields the mod reads are declared on a base class.
