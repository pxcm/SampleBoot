#!/usr/bin/env python3
"""SampleBoot - optional helper (Python 3, no extra packages).

Installs or updates the boot selector on an SD card and adds OSes straight
from their release zips:

  python3 add_mod.py /Volumes/MPC                      install / update the selector
  python3 add_mod.py /Volumes/MPC Melodee-MPC-Sample-v0.2.0.zip SampQOL-OS-v0.2.0.zip
  python3 add_mod.py /Volumes/MPC some-os.zip --name "Some OS 1.0"

Doing it by hand works just as well: copy the release's "autorun" folder to
the card, then copy each OS's "autorun" folder into autorun/mods/<name>.
"""
import argparse, datetime, os, re, shutil, sys, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RELEASE_AUTORUN = HERE.parent / "autorun"
MARKS = ("# SAMPLEBOOT", "# MPC_BOOT_SELECTOR")   # this launcher, and the one of the test builds
# files in autorun/sampleboot that belong to the user: never overwritten on update
KEEP = {"sampleboot.conf", "last", "last-boot.log", "sampleboot.txt", "sampleboot.png", "sampleboot.jpg", "sampleboot.jpeg",
        "sampleboot.bmp", "sampleboot.raw"}


def install(card):
    ar = card / "autorun"
    if not (RELEASE_AUTORUN / "autorun.sh").exists():
        sys.exit(f"release autorun folder not found next to this script: {RELEASE_AUTORUN}")
    if (ar / "autorun.sh").exists() and not any(m in (ar / "autorun.sh").read_text(errors="ignore") for m in MARKS):
        bak = card / ("autorun-before-sampleboot-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
        ar.rename(bak)
        print(f"! another autorun was on the card: moved to {bak.name}")
        print("  (if it is an OS, move it into autorun/mods/<name>)")
    fresh = not (ar / "autorun.sh").exists()
    (ar / "sampleboot").mkdir(parents=True, exist_ok=True)
    (ar / "mods").mkdir(exist_ok=True)
    for name in ("autorun.sh", "autorun.json", "autorun.check"):
        shutil.copyfile(RELEASE_AUTORUN / name, ar / name)
    for f in (RELEASE_AUTORUN / "sampleboot").iterdir():
        dest = ar / "sampleboot" / f.name
        if f.name in KEEP and dest.exists():
            continue
        if f.name == "firmware.txt" and dest.exists():       # merge the firmware list
            lines = dict(l.split(None, 1) for l in dest.read_text().splitlines() if len(l.split()) == 2)
            lines.update(l.split(None, 1) for l in f.read_text().splitlines() if len(l.split()) == 2)
            dest.write_text("".join(f"{h} {v}\n" for h, v in lines.items()))
            continue
        shutil.copyfile(f, dest)
    rm = RELEASE_AUTORUN / "mods" / "README.txt"
    if rm.exists():
        shutil.copyfile(rm, ar / "mods" / "README.txt")
    tpl = RELEASE_AUTORUN / "mods" / "_template"
    if tpl.is_dir() and not (ar / "mods" / "_template").exists():
        shutil.copytree(tpl, ar / "mods" / "_template")
    print(("installed" if fresh else "updated") + f" SampleBoot in {ar}")


def autorun_root(names):
    """The OS's autorun folder in a zip: the shortest path holding autorun.json
    and autorun.check (the firmware's own pair; the script can have any name)."""
    for n in sorted(names, key=len):
        if n.endswith("autorun.json") and n[: -len("autorun.json")] + "autorun.check" in names:
            return n[: -len("autorun.json")]
    return None


def has_script(folder):
    """As SampleBoot decides: a script named in autorun.json, else autorun.sh."""
    j = folder / "autorun.json"
    if j.exists():
        for m in re.finditer(r'"path"\s*:\s*"([^"]+)"', j.read_text(errors="ignore")):
            n = m.group(1)
            if not n.startswith("/") and ".." not in n and (folder / n).is_file():
                return True
    return (folder / "autorun.sh").is_file()


def guess_name(files, src, top=""):
    for n in files:
        if n.endswith("/VERSION") and n.count("/") == 1:      # sample-base apps such as Melodee
            ver = files[n]().decode(errors="ignore").split()[0].lstrip("vV")
            return f"{n.split('/')[0].capitalize()} {ver}"
    m = re.search(r"v?(\d+\.\d+\.\d+)", src.name)
    if "mpcqol.so" in files:
        return f"SampQOL {m.group(1)}" if m else "SampQOL"
    if top:                                                   # the package's own folder, e.g. MPC-Simple-1.0-Beta-2
        return re.sub(r"[-_]+", " ", top).strip()
    return re.sub(r"\.zip$", "", src.name, flags=re.I)


def add(card, src, name):
    src = Path(src).expanduser()
    if src.is_file() and src.suffix.lower() == ".zip":
        z = zipfile.ZipFile(src)
        names = set(z.namelist())
        pre = autorun_root(names)
        if pre is None:
            sys.exit(f"{src}: no autorun folder (autorun.json and autorun.check) in it")
        files = {n[len(pre):]: (lambda n=n: z.read(n)) for n in names if n.startswith(pre) and not n.endswith("/")}
        # Folders the package puts next to its autorun folder go to the top
        # level of the card, as its own instructions say (MPC Simple: MPC-Simple/).
        parent = pre.rstrip("/").rpartition("/")[0]
        parent = parent + "/" if parent else ""
        extra = {n[len(parent):]: (lambda n=n: z.read(n)) for n in names
                 if n.startswith(parent) and not n.startswith(pre) and not n.endswith("/")
                 and "/" in n[len(parent):]}
        top = pre.split("/")[0] if pre.count("/") > 1 else ""
    elif src.is_dir():
        root = src if (src / "autorun.json").exists() or (src / "autorun.sh").exists() else src / "autorun"
        if not has_script(root):
            sys.exit(f"{src}: no OS autorun folder found (autorun.json with its script, or autorun.sh)")
        files = {str(p.relative_to(root)).replace(os.sep, "/"): (lambda p=p: p.read_bytes())
                 for p in root.rglob("*") if p.is_file()}
        extra = {str(p.relative_to(root.parent)).replace(os.sep, "/"): (lambda p=p: p.read_bytes())
                 for d in root.parent.iterdir() if d.is_dir() and d != root and root.name == "autorun"
                 for p in d.rglob("*") if p.is_file()}
        top = ""
    else:
        sys.exit(f"{src}: expected a zip file or a folder")
    files = {r: f for r, f in files.items() if not r.split("/")[-1].startswith("._")}
    extra = {r: f for r, f in extra.items() if not r.split("/")[-1].startswith("._")}
    name = (name or guess_name(files, src, top)).strip().replace("/", "-")
    if not name or name[0] in "._":
        sys.exit(f"bad folder name: {name!r}")
    dest = card / "autorun" / "mods" / name
    if dest.exists():
        shutil.rmtree(dest)
    for r, get in files.items():
        p = dest / r
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(get())
    print(f"+ autorun/mods/{name}")
    for folder in sorted({r.split("/")[0] for r in extra}):
        for r, get in extra.items():
            if r.split("/")[0] == folder:
                p = card / r
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(get())
        print(f"+ {folder}/  (at the top level of the card, as the package asks)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("card", help="root of the SD card, e.g. /Volumes/MPC")
    ap.add_argument("sources", nargs="*", help="OS release zips or autorun folders")
    ap.add_argument("--name", help="folder/menu name (only with a single source)")
    a = ap.parse_args()
    card = Path(a.card).expanduser()
    if not card.is_dir():
        sys.exit(f"{card} does not exist")
    if a.name and len(a.sources) != 1:
        sys.exit("--name needs exactly one source")
    install(card)
    for s in a.sources:
        add(card, s, a.name)
    mods = sorted(p.name for p in (card / "autorun" / "mods").iterdir()
                  if p.is_dir() and has_script(p) and p.name[0] not in "._")
    print("\nMenu at the next power-on:\n  AKAI firmware")
    for m in mods:
        print(f"  {m}")


if __name__ == "__main__":
    main()
