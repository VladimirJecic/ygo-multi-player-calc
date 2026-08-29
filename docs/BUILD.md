# BUILD — toolchain, compile, sign, install

Everything runs on the phone itself, in Termux. There is no desktop in this loop.

---

## 1. Toolchain

| Tool | Used for | Where it comes from |
|---|---|---|
| `aarch64-linux-android-clang` | compiling `mod.c` → `libneuronmod.so` | Termux `clang` |
| `python3` | `mkapk.py`, `addassets.py`, `md.py`, `so.py` | Termux `python` |
| `apksigner` | signing the rebuilt apk | Termux `apksigner` (needs `openjdk-17`) |
| `adb` | install, logcat, `input tap`, `screencap` | Termux `android-tools`, over wireless debugging |
| `apktool` 3.0.3 | **one-off** decode/rebuild for the smali patch | Termux `apktool` |
| `javac` + `d8` | **one-off** build of `neuron.mod.Toaster` → `classes4.dex` | `openjdk-17`, `android-tools` |

### Getting onto the device: Shizuku, not adb

**Use `scripts/device/shell.sh`.** It runs a command as uid 2000 (`shell`) through
Shizuku's `rish`, which is a local binder call into a service the user has already
authorised. It needs no pairing, no port, and survives reboots of this session.

adb over wireless debugging does not work unattended: Android hands out a **fresh random
port every time wireless debugging is toggled**, and the pairing code only ever appears on
the phone's screen. An agent in Termux cannot read either, so it can only stop and ask —
which is what stalled two rounds of this work.

`rish` and its dex ship inside the Shizuku apk; no download is needed:

```sh
A=$(pm path moe.shizuku.privileged.api | cut -d: -f2)
unzip -o -j "$A" assets/rish assets/rish_shizuku.dex -d ~
sed -i 's/"PKG"/"com.termux"/' ~/rish        # RISH_APPLICATION_ID
chmod +x ~/rish && chmod 400 ~/rish_shizuku.dex   # Android 14+ refuses a writable dex
```

Shizuku itself has to be running — its own app starts it. The shell it gives you is in the
`log`, `adb`, `input` and `sdcard_rw` groups, which covers logcat, `pm install`, `input tap`
and `screencap`.

| Want | Command |
|---|---|
| any shell command | `scripts/device/shell.sh 'cmd'` |
| the mod's log | `scripts/device/logcat.sh` |
| install a build | `scripts/device/install.sh apk/dist/neuron-mod-vNNN.apk` |

**`pm install` cannot read an apk on `/storage/emulated/0`** — that path is fuse and
system_server is denied read on it (`Unable to open file`, with an avc denial in the log).
`install.sh` stages the apk under `/data/local/tmp` first, which is why it exists.

Raise the logcat buffer once per session or the mod's output is evicted within a minute:

```sh
scripts/device/shell.sh 'logcat -G 16M'
```

---

## 2. The normal loop — one version, about 30 seconds

```sh
scripts/build.sh <prev> <new>        # e.g. scripts/build.sh 230 231
```

What it does:

1. `aarch64-linux-android-clang -shared -fPIC -O2 -o libneuronmod.so mod.c -llog`
   — bails immediately if the `.so` is empty, so a compile error can never be signed and shipped.
2. `mkapk.py neuron-mod-v<prev>.apk neuron-mod-unsigned.apk lib/arm64-v8a/libneuronmod.so=mod/libneuronmod.so`
3. `apksigner sign --ks keys/$KS_FILE --ks-pass pass:$KS_PASS --key-pass pass:$KEY_PASS --ks-key-alias $KS_ALIAS`
   — credentials sourced from the untracked `keys/keystore.env`
4. `adb install -r neuron-mod-v<new>.apk`

**The base is always the previous apk**, not a pristine one. The patched `classes.dex`, the
added `classes4.dex` and the restored asset bundles all ride along inside it. This is why
`apk/dist/` must always keep the newest build, and why versions must not be skipped.

It resolves the project root from its own location, so it can be run from anywhere. The
unsigned intermediate is written to `build/` and deleted once the signed apk exists.

### Why `mkapk.py` and not apktool

`mkapk.py` raw-copies every untouched zip entry byte for byte (local header, compressed data
and data descriptor alike) and rewrites only the entries you name, then builds a fresh central
directory. **0.5 seconds** instead of apktool's ~4 minutes, and — more importantly — it cannot
mangle a resource it did not understand. A full apktool round trip is only for a change that
genuinely needs re-smaling.

---

## 3. The one-off setup, already baked into the current apk

You should not need to redo any of this, but this is what is inside the apk you are basing on.

**a. Native library.** `lib/arm64-v8a/libneuronmod.so` added.

**b. Loader.** One `<clinit>` injected into
`smali/com/google/firebase/MessagingUnityPlayerActivity.smali`:

```smali
.method static constructor <clinit>()V
    .locals 1
    :try_start_0
    const-string v0, "neuronmod"
    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0
    ...
```

Wrapped in a `try/catch` so a missing library can never stop the game from starting.
This is the only on-disk change to the game's own code.

**c. Toast bridge.** `src/java/neuron/mod/Toaster.java` → `javac` → `d8` → `build/classes4.dex`,
added to the apk as `classes4.dex`. Native code cannot raise a `Toast` on its own; `Toaster`
fetches the `Application` via `ActivityThread.currentApplication()` reflection and posts to the
main looper, swallowing every throwable so a toast can never break the game.

**d. Restored asset bundles.** The modded apk had been built from a merged base+split that had
lost **977** Unity asset bundles, which is why ARC-V and VRAINS drew their LP panels as blank
white boxes. `src/tools/addassets.py ours.apk donor.apk out.apk` copies every entry of `ours`
byte for byte and then appends only the entries the donor has and it does not. Nothing that
already worked is touched.

---

## 4. Signing

```
keystore     keys/neuron.jks          (untracked — this phone only)
credentials  keys/keystore.env        (untracked — sourced by scripts/build.sh)
DN           CN=Neuron Local Build, O=Personal, C=RS
```

**Losing this file means the user must uninstall the app to install any future build, which
wipes their duel history. It has already happened once.** Do not regenerate it, do not
overwrite it, and never sign with a fresh key "just to test".

---

## 5. Verifying a build

```sh
unzip -l apk/dist/neuron-mod-v231.apk | grep -E 'classes|neuronmod'
```

Expect `classes.dex`, `classes2.dex`, `classes3.dex`, `classes4.dex` and
`lib/arm64-v8a/libneuronmod.so`.

Then, on device:

```sh
adb logcat -c && adb logcat -s NEURONMOD &
# start the game, tap through to a duel
```

`=== neuronmod loaded, pid=… ===` within a second or two, then `libil2cpp base = 0x…`,
then the mod's own layout lines (`4P: LifeArea is …`, `buttons: one … row …`).
If the load line never appears, the `<clinit>` did not run. If it appears and nothing else
does, the runtime was touched too early or a symbol is missing (`MISSING <name>` in the log).

---

## 6. On-device test automation

In `scripts/device/` — all of it drives the real game through `adb`.

| Script | What it does |
|---|---|
| `nav.sh [mode-y] [design-y]` | cold-start the game and land in the calculator, **checking each step** (screen lit? landscape yet?) instead of guessing at sleeps — off charger this phone boots the game three times slower than on the bench |
| `design.sh <checkbox-y> <out.png>` | switch Calculator Design and come back into a duel, then screenshot |
| `gtap.sh <x> <y>` | tap, but **refuse unless the game is the resumed activity** — a stray tap on the user's home screen opens whatever happens to be there |
| `logtest.sh` / `logtest2.sh` | drive three life changes from three different duelists, then open the Log |
| `gfxsweep.sh <design…>` | walk designs and collect the `gfx:` dump lines into `reference/gfx_inventory.txt` |
| `keepawake.sh` / `keepawake-stop.sh` | wake lock + a `KEYCODE_WAKEUP` every 10 min so `screencap` does not return a black panel overnight. **Turn it off when the session ends.** |

| `shot.sh <out.png>` | `adb exec-out screencap -p` — **not** `adb shell screencap`, which mangles the binary stream on some builds |
| `run_design.sh <1..8> <out.png>` | design index → `design.sh`. **Refuses to run until its `DESIGN_Y` table is filled in** — the original coordinates were lost and guessing taps on the user's phone is not acceptable |

**Always use `gtap.sh`, never bare `adb shell input tap`.** This is the user's own phone.
