#!/data/data/com.termux/files/usr/bin/bash
# Dump the mod's log and clear it, so the next read starts empty.
# usage: logcat.sh [extra grep pattern]
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
if [ $# -gt 0 ]; then
  "$DIR/shell.sh" "logcat -d -s NEURONMOD" | grep -E "$1"
else
  "$DIR/shell.sh" "logcat -d -s NEURONMOD"
fi
