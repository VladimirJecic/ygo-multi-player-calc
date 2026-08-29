# keys/

`neuron.jks` — the signing keystore for every build of this mod. **It is not in git**
(`.gitignore`), and it exists on this phone only.

Credentials live in `keys/keystore.env`, also untracked. `scripts/build.sh` sources it and
refuses to run without it. `keystore.env.example` shows the shape:

```sh
KS_FILE=neuron.jks
KS_ALIAS=neuron
KS_PASS=…
KEY_PASS=…
```

**Do not lose the keystore, overwrite it, or regenerate it.** Android refuses to upgrade an
installed app whose signature changed, so a new key means the user must uninstall — which
wipes his duel history and every name he has set. This has already happened once.

Never sign with a throwaway key "just to test". Never copy this file into shared storage
(`/storage/emulated/0/...`), where every other app on the phone can read it. Never paste the
passwords into a doc, a commit message, or a log — that is what `keystore.env` is for.

If the keystore is ever lost, there is no recovery: the user must uninstall, reinstall a
freshly signed build, and lose his data. Back it up somewhere off this phone, by hand.
