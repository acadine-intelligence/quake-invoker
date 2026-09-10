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
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / "build/Release"


def load_tga(path):
    """Return (width, height, pixels) for a 24-bit type 2 or type 10 TGA."""
    data = path.read_bytes()
    idlen, _cmap, imgtype = data[0], data[1], data[2]
    width, height, bpp, _desc = struct.unpack_from("<HHBB", data, 12)
    assert imgtype in (2, 10) and bpp == 24, "unsupported TGA"
    pos = 18 + idlen
    total = width * height
    pixels = bytearray(total * 3)
    if imgtype == 2:
        pixels[:] = data[pos:pos + total * 3]
    else:
        i = 0
        while i < total:
            n = data[pos]
            pos += 1
            if n & 0x80:
                b, g, r = data[pos:pos + 3]
                pos += 3
                for _ in range(n & 0x7F):
                    pixels[i * 3:i * 3 + 3] = bytes((b, g, r))
                    i += 1
            else:
                for _ in range(n & 0x7F):
                    b, g, r = data[pos:pos + 3]
                    pos += 3
                    pixels[i * 3:i * 3 + 3] = bytes((b, g, r))
                    i += 1
    return width, height, pixels


def assert_menu_fits(path):
    """The manual page draws light text on the dark menu backdrop. Fail when
    any text pixel reaches the outer 8 pixels of the frame, which is what a
    clipped or over-wide line looks like."""
    width, height, pixels = load_tga(path)
    xmin, xmax, hits = width, -1, 0
    for y in range(height):
        row = (height - 1 - y) * width * 3
        for x in range(width):
            o = row + x * 3
            if pixels[o] > 60 or pixels[o + 1] > 60 or pixels[o + 2] > 60:
                hits += 1
                xmin = min(xmin, x)
                xmax = max(xmax, x)
    assert hits > 500, f"Menu page looks blank: {path}"
    assert xmin >= 8 and xmax <= width - 8, (
        f"Menu text reaches the frame edge (x {xmin}..{xmax} of {width}): {path}")


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
    args += ["+devmap", "oa_dm3", "+set", "activeAction", "exec visual_smoke.cfg"]
    with (run / "engine.log").open("w") as log:
        result = subprocess.run(args, cwd=RELEASE, stdout=log,
                                stderr=subprocess.STDOUT, timeout=100)
    if result.returncode:
        raise SystemExit(f"Game exited {result.returncode}. Inspect {run / 'engine.log'}")
    console = (mod / "visual-console.txt").read_text(errors="replace")
    for expected in ("orbs: Q W E", "invoked Rocket Launcher (WQW)",
                     "invoked Lightning Gun (WWQ)", "Ice Wall: not castable yet",
                     "invoked Ghost Walk (QQW)", "cast Ghost Walk (-25 mana)",
                     "invoked Sunstrike (EEE)", "cast Sunstrike (-45 mana)",
                     "sunstrike impact", "Invoker manual opened"):
        assert expected in console, f"Missing {expected!r} in {mod}"
    for error in ("unknown cmd orb", "unknown cmd invoke", "unknown cmd invswap",
                  "unknown cmd invcast", "VM_Abort", "ERROR:", "Unknown command",
                  "May not switch teams"):
        assert error not in console, f"Unexpected {error!r} in {mod}"
    for combo, shot in (("Rocket Launcher (WQW)", "rocket_cast"),
                        ("Lightning Gun (WWQ)", "lightning_cast"),
                        ("Ghost Walk (QQW)", "ghost_equipped"),
                        ("Sunstrike (EEE)", "sun_g1")):
        assert console.index("invoked " + combo) < console.index(
            f"Wrote screenshots/{shot}.tga"), "Screenshot preceded server confirmation"
    assert console.index("sunstrike impact") < console.index(
        "Wrote screenshots/sunstrike_after.tga"), "Strike impact missing before final shot"
    assert console.index("Invoker manual opened") < console.index(
        "Wrote screenshots/invoker_manual.tga"), "Manual opened after its screenshot"
    assert_menu_fits(mod / "screenshots/invoker_manual.tga")
    names = ("empty", "colors", "rocket_cast", "swapped_left", "rocket_ready", "lightning_cast",
             "dual_before", "left_fired", "right_firing", "both_after", "spell_attempt",
             "ghost_equipped", "ghost_cast",
             "sun_g1", "sun_g2", "sun_g3", "sun_g4", "sun_g5", "sun_g6",
             "sun_g7", "sun_g8", "sun_g9", "sun_g10", "sun_g11", "sun_g12",
             "sunstrike_after",
             "effects_off", "spectator", "respawn", "ingame_menu", "invoker_manual",
             "back_to_game")
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
