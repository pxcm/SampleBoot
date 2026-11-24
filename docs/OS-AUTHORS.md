# Notes for OS authors

How to make a system for the MPC Sample that runs from SampleBoot exactly as
it runs on its own. These are suggestions, not rules: they come from the
issues found while testing SampQOL OS, Melodee, MPC Simple, MPCTRACKER and MPC 600 S
with SampleBoot. Questions and ideas are welcome in the
[issues](https://github.com/pxcm/SampleBoot/issues).

## How SampleBoot runs your system

Your release's `autorun` folder goes, unchanged, into
`<card>/autorun/mods/<any name>/`. At power-on SampleBoot runs the start
script named in your `autorun.json` (`scripts[].path`), as the MPC's firmware
would, from that folder. So:

- your script's folder is `<card>/autorun/mods/<name>/`, not `<card>/autorun/`;
- "the folder above your own" is `<card>/autorun/mods/`, not the card root.

## 1. Find your own folder from the script

Work out your folder from the path of your script, never from a fixed path:

```sh
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
```

## 2. Find the card root from the mounts, not from "the folder above"

The card root is the mount point that holds your folder. Read it from
`/proc/mounts` (the longest mount point that is a prefix of your folder);
`/proc/mounts` writes a space as `\040`, and card names can contain spaces:

```sh
CARD=$(awk -v d="$D/" '{ m = $2; gsub(/\\040/, " ", m) }
    m != "/" && index(d, m "/") == 1 && length(m) > n { n = length(m); c = m }
    END { print c }' /proc/mounts)
```

This works from `autorun/`, from `autorun/mods/<name>/` and from any other
place. `CARD=$(dirname "$D")` or `${D%/*}` only work when your folder is
`<card>/autorun`.

If you can't change this, ship an empty file named **`sampleboot.inplace`**
in your `autorun` folder: SampleBoot then binds your folder over
`<card>/autorun/` before starting it, so "the folder above" is the card root
again. Since SampleBoot 1.0.3 such a system gets no other mounts from
SampleBoot (no `ROOT_FILES` binds). Don't use it if you read `/proc/mounts`: the bind adds
`<card>/autorun` as a mount point.

## 3. Keep your files in known places

- Settings and data that belong to the system: inside your own folder, found
  from `$D`, or in a top-level folder named after your system
  (`<card>/MySystem/`), found from `$CARD`.
- Files at the card root found from "the folder above" (as SampQOL's
  `mpcqol.conf`) are the ones that break. SampleBoot works around
  `mpcqol.conf` itself (`ROOT_FILES`), but a new one would need a new
  workaround.

**A good example: Melodee 0.6.8.** Everything the program needs is inside its
`autorun` folder (`autorun/melodee/`), found from the script's own folder, so
it runs from `autorun/mods/<name>/` with nothing to change. Everything the
user makes or adds (projects, samples, plug-ins) goes in a single top-level
folder named after the system, `<card>/Melodee/`.

## 4. Leave the MPC as you found it

- When your app exits, however it exits, start the stock app again:
  `trap 'systemctl start ac50' EXIT`.
- If you change the USB gadget (audio channels, mass storage, webcam), put it
  back when you exit.
- Don't write to the internal storage unless the user asked for it.

## 5. Optional: look good in the menu

Put these in your `autorun` folder; the firmware ignores them:

- `sampleboot.png` (or `.jpg`, `.bmp`): 320×240, shown full screen. Keep the
  top and bottom 46 pixels free of important details: the menu bars cover
  them. `autorun/mods/_template/` in SampleBoot's release has a guide image.
- `sampleboot.txt`: the name in the menu, on the first line (for example
  `MySystem 1.2.0`). Without it, the folder name is shown.

## 6. Test it and tell us

1. Copy your `autorun` folder to `<card>/autorun/mods/MySystem/` on a card
   with SampleBoot.
2. Start it from the menu. `autorun/sampleboot/last-boot.log` shows the start
   script SampleBoot found and when your app took over the surface.
3. Check that settings, projects, the file browser and the USB features work
   as they do without SampleBoot.

Then open an [issue](https://github.com/pxcm/SampleBoot/issues) or write on
r/mpcsample with the name and version: it goes on the list of systems that
work with SampleBoot.
