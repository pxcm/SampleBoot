#!/usr/bin/env python3
"""Assembles dist/SampleBoot-<version>/ and its zip:
a ready-to-copy, signed "autorun" folder plus README and helper.
Run "make" first to build build/sampleboot-ui."""
import hashlib, json, re, shutil, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = re.search(r'#define VERSION "([^"]+)"', (ROOT / "src/sampleboot-ui.c").read_text()).group(1)
# The firmware runs autorun/autorun.sh only when autorun.check equals
# sha256(autorun.json + autorun.sh + this salt) (found by mpc-sample-plugins,
# also used by sample-base/Melodee).
SALT = b"f7bed1c9-7e90-4731-9194-c996666b4331"

STUB = """#!/bin/sh
# SAMPLEBOOT
# SampleBoot, boot selector for the Akai MPC Sample. The start-up script is
# sampleboot/sampleboot.sh, the alternative OSes are in mods/<name>/. Runs
# once per power-on (the marker in /run is gone after a reboot).
# This file is signed by autorun.check: do not edit.
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ -e /run/sampleboot.started ] && exit 0
: > /run/sampleboot.started
cp "$D/sampleboot/sampleboot.sh" /run/sampleboot.sh 2>/dev/null || exit 0
if command -v systemd-run >/dev/null 2>&1; then
    systemd-run --unit=sampleboot --no-block --property=Type=oneshot \\
        --property=RemainAfterExit=yes --property=TimeoutStartSec=infinity \\
        /bin/sh /run/sampleboot.sh "$D" > /dev/null 2>&1 && exit 0
fi
setsid /bin/sh /run/sampleboot.sh "$D" > /dev/null 2>&1 &
"""
MANIFEST = json.dumps({"value0": {"scripts": [{
    "path": "autorun.sh", "askQuestion": False, "message": "SampleBoot",
    "yesButtonText": "Start", "noButtonText": "Cancel"}], "files": []}}, indent=2) + "\n"


def main():
    binary = ROOT / "build" / "sampleboot-ui"
    if not binary.exists():
        sys.exit("build/sampleboot-ui missing: run make first")
    name = f"SampleBoot-{VERSION}"
    out = ROOT / "dist" / name
    shutil.rmtree(out, ignore_errors=True)
    ar = out / "autorun"
    (ar / "sampleboot").mkdir(parents=True)
    (out / "tools").mkdir()
    (ar / "mods").mkdir()
    (ar / "autorun.sh").write_bytes(STUB.encode())
    (ar / "autorun.json").write_bytes(MANIFEST.encode())
    (ar / "autorun.check").write_text(hashlib.sha256(MANIFEST.encode() + STUB.encode() + SALT).hexdigest())
    shutil.copyfile(binary, ar / "sampleboot" / "sampleboot-ui")
    (ar / "sampleboot" / "sampleboot-ui").chmod(0o755)
    for f in ("sampleboot.sh", "sampleboot.conf", "firmware.txt", "splash-dark.png", "splash-light.png"):
        shutil.copyfile(ROOT / "card" / f, ar / "sampleboot" / f)
    shutil.copyfile(ROOT / "card" / "sampleboot.conf", ar / "sampleboot" / "sampleboot.original.conf")
    shutil.copyfile(ROOT / "tools" / "theme-editor.html", out / "tools" / "theme-editor.html") if (ROOT / "tools" / "theme-editor.html").exists() else None
    shutil.copyfile(ROOT / "card" / "mods-README.txt", ar / "mods" / "README.txt")
    shutil.copytree(ROOT / "card" / "_template", ar / "mods" / "_template")
    shutil.copyfile(ROOT / "tools" / "add_mod.py", out / "tools" / "add_mod.py")
    shutil.copyfile(ROOT / "README.md", out / "README.md")
    shutil.copyfile(ROOT / "LICENSE", out / "LICENSE")
    shutil.copyfile(ROOT / "CHANGELOG.md", out / "CHANGELOG.md")
    shutil.copyfile(ROOT / "fonts" / "LICENSE-DejaVu.txt", out / "LICENSE-DejaVu.txt")
    z = out.parent / (name + ".zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(out.rglob("*")):
            zf.write(p, p.relative_to(out.parent))
    print(f"{out}\n{z}")


if __name__ == "__main__":
    main()
