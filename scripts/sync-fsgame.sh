#!/bin/sh
# Refresh the fs_game ("invoker") data directories that the desktop app and
# the browser page load from.
#
# Why this exists: the build emits fresh QVMs into Release/baseq3/vm, but the
# desktop app with `+set fs_game invoker` and the browser page both resolve
# vm/*.qvm and the mod cfgs from the fs_game directory (build/Release/invoker,
# build-em/Release/invoker). Those directories are manual copies that the
# build does not refresh, so they go stale silently after gameplay changes.
# Run this after every build that touches code/game, code/cgame, code/ui or
# configs/.
set -e
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

for dest in "$root/build/Release/invoker" "$root/build-em/Release/invoker"; do
    [ -d "$dest" ] || continue
    mkdir -p "$dest/vm"
    cp "$root"/build/Release/baseq3/vm/qagame.qvm "$dest/vm/qagame.qvm"
    cp "$root"/build/Release/baseq3/vm/cgame.qvm "$dest/vm/cgame.qvm"
    cp "$root"/build/Release/baseq3/vm/ui.qvm "$dest/vm/ui.qvm"
    cp "$root"/configs/invoker.cfg "$dest/invoker.cfg"
    cp "$root"/configs/invoker-demo.cfg "$dest/invoker-demo.cfg"
    echo "synced $dest"
done

echo "done: fs_game dirs now carry the current QVMs and cfgs"
