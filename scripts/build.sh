#!/data/data/com.termux/files/usr/bin/bash
# Build + sign + install one version.  Base is the previous apk, with only the
# native mod swapped in - everything else (patched dex, classes4) rides along.
# usage: build.sh <prev-version> <new-version>
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREV=$1; NEW=$2

# Keystore credentials live outside git - see keys/CLAUDE.md.
# shellcheck source=/dev/null
[ -f "$ROOT/keys/keystore.env" ] || {
  echo "missing $ROOT/keys/keystore.env - see keys/CLAUDE.md"; exit 1; }
. "$ROOT/keys/keystore.env"
rm -f "$ROOT/build/libneuronmod.so"
aarch64-linux-android-clang -shared -fPIC -O2 \
  -o "$ROOT/build/libneuronmod.so" "$ROOT/src/native/mod.c" -llog
test -s "$ROOT/build/libneuronmod.so" || { echo "COMPILE FAILED"; exit 1; }
python3 "$ROOT/src/tools/mkapk.py" \
  "$ROOT/apk/dist/neuron-mod-v$PREV.apk" "$ROOT/build/neuron-mod-unsigned.apk" \
  lib/arm64-v8a/libneuronmod.so="$ROOT/build/libneuronmod.so"
apksigner sign --ks "$ROOT/keys/$KS_FILE" \
  --ks-pass "pass:$KS_PASS" --key-pass "pass:$KEY_PASS" \
  --ks-key-alias "$KS_ALIAS" --out "$ROOT/apk/dist/neuron-mod-v$NEW.apk" \
  "$ROOT/build/neuron-mod-unsigned.apk"
rm -f "$ROOT/build/neuron-mod-unsigned.apk"
adb install -r "$ROOT/apk/dist/neuron-mod-v$NEW.apk"
echo "INSTALLED v$NEW"
