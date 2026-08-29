# src/native/

`mod.c` — the whole mod. Read `../CLAUDE.md` for the house style, then:

- **`../../docs/ARCHITECTURE.md`** before changing how anything hooks or when it runs.
- **`../../docs/DESIGN.md`** before changing any position, size, scale or alignment.
  It is a rulebook, not background reading; R1–R9 are each there because of a specific
  regression.
- **`../../reference/il2cpp/classdump.txt`** before assuming a game method exists.

Compile check without shipping:

```sh
aarch64-linux-android-clang -shared -fPIC -O2 -o /dev/null mod.c -llog
```

A build that produces an empty `.so` must never be signed — `scripts/build.sh` already bails
on that, do not remove the check.
