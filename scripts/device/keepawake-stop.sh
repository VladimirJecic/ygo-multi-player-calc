#!/data/data/com.termux/files/usr/bin/bash
DIR=$(cd "$(dirname "$0")" && pwd)
# Undo everything keepawake.sh turned on.  Written so it never matches itself.
for p in $(ps -Ao pid,args | grep '[k]eepawake\.sh' | awk '{print $1}'); do
  [ "$p" = "$$" ] || kill "$p" 2>/dev/null
done
termux-wake-unlock >/dev/null 2>&1
"$DIR/shell.sh" svc power stayon false >/dev/null 2>&1
echo "wake lock released, stayon off, loop stopped"
