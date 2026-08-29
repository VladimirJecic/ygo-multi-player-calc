# apk/

```
dist/       shipped, signed builds. Currently neuron-mod-v230.apk.
decoded/    the apktool decode of the game (AndroidManifest, smali, assets, lib, resources).
extracted/  the raw apk payload the RE tools read: assets/, dex/, lib/.
```

## Rules

1. **`dist/` must always keep the newest build.** `scripts/build.sh` uses the previous apk as
   its base — the patched `classes.dex`, the added `classes4.dex` and the restored asset
   bundles all ride along inside it. Deleting the newest apk breaks the pipeline.
   Older builds may be deleted; v228 and v229 already were, to save 214 MB.
2. **`decoded/` is the only copy of the game's original smali and assets in this checkout.**
   The pristine `neuron-single.apk` it was decoded from is gone. Do not delete it, and do not
   edit it — the only smali change the mod needs (`System.loadLibrary("neuronmod")` in
   `MessagingUnityPlayerActivity`) is already baked into the shipped apk.
3. `extracted/` exists so `src/re/md.py` and `so.py` have `global-metadata.dat` and
   `libil2cpp.so` to read. Same: read-only.
4. Nothing in here is source. Do not build from `decoded/` — see `docs/BUILD.md` for why the
   apktool round trip is not the normal loop.
