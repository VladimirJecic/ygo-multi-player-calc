#!/data/data/com.termux/files/usr/bin/bash
# Install an apk over the existing one, keeping its data.
# usage: install.sh <path-to-apk>
#
# The apk cannot be installed straight off /storage/emulated/0: that path is
# fuse, and system_server is denied read on it - "Unable to open file", with an
# avc denial in the log.  Stage it under /data/local/tmp, which the shell user
# owns, and install from there.
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
APK=${1:?usage: install.sh <path-to-apk>}
[ -f "$APK" ] || { echo "install: no such apk: $APK"; exit 1; }

# The staging copy is made by the shell user, so the source has to be somewhere
# it can read - shared storage, not Termux's private directory.
case $APK in
  /storage/*|/sdcard/*) SRC=$APK ;;
  *) SRC=/storage/emulated/0/Download/$(basename "$APK")
     cp -f "$APK" "$SRC" ;;
esac

"$DIR/shell.sh" "cp '$SRC' /data/local/tmp/install.apk && pm install -r /data/local/tmp/install.apk; rm -f /data/local/tmp/install.apk"
