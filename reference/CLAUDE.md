# reference/ — read-only lookup data

Nothing here is edited by hand and nothing here is built from. It is what you consult so you
do not have to re-derive something the hard way.

```
il2cpp/classdump.txt   5.3 MB — every class, method, param count, rid, flags, slot.
                       Output of src/re/md.py + so.py. First stop for
                       "does this method exist / how many parameters does it take".
il2cpp/types.txt       456 KB — type index → name → token.
gfx_inventory.txt      Per-skin inventory of everything an LP panel draws: node path, size,
                       x position, and whether it is an image, text or hidden. Captured by
                       scripts/device/gfxsweep.sh from the mod's own gfx: log lines. This is
                       how you find out where a skin keeps its duelist caption without
                       another build.
screenshots/           Evidence. bug-duplicate-names-5p.png is the user's own screenshot of
                       the US-04 failure and is the reference for what must never ship.
```

To regenerate the il2cpp dumps, fix the hardcoded paths in `src/re/*.py` first — the inputs
are now under `apk/extracted/`.

Grep it, don't read it: `grep -n 'StartDuel' il2cpp/classdump.txt`.
