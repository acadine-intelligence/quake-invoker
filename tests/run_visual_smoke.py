#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the macOS visual smoke script in a new isolated home directory.

Build the QVMs first. This records real game screenshots and console output.
The assertions cover command flow and screenshot production. A person or
image-capable reviewer must inspect the screenshots for visual acceptance.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / "build/Release"


def main():
    executable = RELEASE / "ioquake3.app/Contents/MacOS/ioquake3"
    if not executable.is_file() or not (RELEASE / "baseoa/pak0.pk3").is_file():
        raise SystemExit("Build the macOS client and install OpenArena data first.")
    runs = ROOT / "build/visual-smoke"
    runs.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=runs))
    mod = run / "home/invoker"
    vm = mod / "vm"
    vm.mkdir(parents=True)
    hashes = {}
    for name in ("qagame", "cgame", "ui"):
        source = RELEASE / f"baseq3/vm/{name}.qvm"
        destination = vm / source.name
        shutil.copy2(source, destination)
        hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        assert hashlib.sha256(destination.read_bytes()).hexdigest() == hashes[name]
    shutil.copy2(ROOT / "tests/visual_smoke.cfg", mod / "visual_smoke.cfg")
    shutil.copy2(ROOT / "configs/invoker.cfg", mod / "invoker.cfg")
    args = [str(executable)]
    settings = {
        "fs_basepath": str(RELEASE), "fs_homepath": str(mod.parent),
        "com_basegame": "baseoa", "fs_game": "invoker", "sv_pure": "0",
        "vm_game": "2", "vm_cgame": "2", "vm_ui": "2",
        "r_fullscreen": "0", "r_mode": "6", "r_dynamiclight": "1",
        "s_volume": "0", "s_musicvolume": "0", "com_maxfps": "60",
        "g_gametype": "0", "bot_enable": "0", "net_ip": "127.0.0.1",
        "cl_allowDownload": "0", "sv_maxclients": "1",
    }
    for key, value in settings.items():
        args += ["+set", key, value]
    args += ["+map", "oa_dm3", "+set", "activeAction", "exec visual_smoke.cfg"]
    with (run / "engine.log").open("w") as log:
        result = subprocess.run(args, cwd=RELEASE, stdout=log,
                                stderr=subprocess.STDOUT, timeout=100)
    if result.returncode:
        raise SystemExit(f"Game exited {result.returncode}. Inspect {run / 'engine.log'}")
    console = (mod / "visual-console.txt").read_text(errors="replace")
    for expected in ("orbs: Q W E", "invoked Frost Rockets (QQE)",
                     "invoked Chaos Lightning (WWE)"):
        assert expected in console, f"Missing {expected!r} in {mod}"
    for error in ("unknown cmd orb", "unknown cmd invoke", "VM_Abort", "ERROR:",
                  "May not switch teams"):
        assert error not in console, f"Unexpected {error!r} in {mod}"
    for combo, shot in (("Frost Rockets (QQE)", "frost_cast"),
                        ("Chaos Lightning (WWE)", "storm_cast")):
        assert console.index("invoked " + combo) < console.index(
            f"Wrote screenshots/{shot}.tga"), "Screenshot preceded server confirmation"
    names = ("empty", "colors", "frost_cast", "frost_ready", "storm_cast",
             "effects_off", "spectator", "respawn")
    screenshots = []
    for name in names:
        image = mod / f"screenshots/{name}.tga"
        assert image.is_file() and image.stat().st_size > 18, f"Missing {image}"
        jpeg = run / f"{name}.jpg"
        subprocess.run(["sips", "-s", "format", "jpeg", "-Z", "1200",
                        str(image), "--out", str(jpeg)], check=True,
                       stdout=subprocess.DEVNULL)
        screenshots.append(str(jpeg))
    receipt = {"command_flow": "passed", "visual_review": "pending",
               "qvm_sha256": hashes, "screenshots": screenshots,
               "console": str(mod / "visual-console.txt")}
    (run / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"run": str(run), **receipt}, indent=2))


if __name__ == "__main__":
    main()
