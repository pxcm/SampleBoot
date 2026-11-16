# Changelog

## 1.0.1

**Fixed**
- SampQOL OS 0.3.0's 16 USB audio outputs did not appear when SampQOL was
  started from SampleBoot. SampQOL keeps its settings in `mpcqol.conf` at the
  card root, but its start script reads them from the folder above its own,
  which under SampleBoot is `autorun/mods/`. SampleBoot now shows the card
  root's `mpcqol.conf` in `autorun/mods/` too (a bind mount of that one file,
  until power-off), so SampQOL reads and writes its real settings. The first
  time, a copy of the file is left in `autorun/mods/`.

**Added**
- `ROOT_FILES` setting: the settings files to show in `autorun/mods/` this
  way. Default `"mpcqol.conf"`.

**Tested** with Melodee 0.3.0 too.

## 1.0.0 – first public release

Tested on the MPC Sample with firmware 1.3.0 (1.3.0.12), with Melodee 0.1.0,
0.2.0 and 0.2.2, SampQOL OS 0.1.4.7 to 0.3.0 and MPC Simple 1.0 Beta 1 and
Beta 2, all on the same card.

**Boot menu**
- Full-screen carousel of the stock Akai firmware and every OS in
  `autorun/mods/`: turn the encoder to browse, push it to start.
- Each system shows its own image, or a title screen with its name.
- The stock entry shows the firmware version when the stock app is a known
  one (`firmware.txt`).
- Countdown: 10 seconds, 20 once you have turned the encoder; the system on
  screen starts by itself. The last choice comes first next time.
- Countdown beeps in the last 5 seconds, louder towards 0, following the Main
  Volume knob; from the speaker, or only from headphones / line out when
  something is plugged in.
- POWER in the menu asks whether to switch the MPC off.
- Splash screen at power-on, drawn line by line: `SPLASH` dark (default),
  light, off, or `splash-<name>.png` of your own; `SPLASH_TIME` seconds; the
  encoder skips it.
- The pad, bank, button and meter lights left on by the previous app (the
  stock app or an OS with a project open) are switched off.

**Systems**
- Add an OS by copying its `autorun` folder into `autorun/mods/<name>`; no
  tools needed, and no change to the signed launcher. Each OS starts with the
  script its `autorun.json` names, as the firmware does (`autorun.sh`,
  MPC Simple's `launcher.sh`, …).
- Several versions of the same OS side by side. A folder starting with `_` is
  hidden.
- Optional `sampleboot.png`/`.jpg`/`.bmp`/`.raw` image and `sampleboot.txt` name per
  system, also for the stock entry; SampQOL's splash is picked up by itself.
- `mods/_template` with an example image that marks the areas the menu covers.

**Safety**
- Nothing on the MPC is changed; without the card the MPC starts as stock.
- If an OS has not started within 60 seconds, the stock app starts instead.
  An OS counts as started when a process holds the control surface, whatever
  it is called.
- Audio: the codec volume is lowered while the stock app stops, the speaker is
  held off while the codec powers up, and every audio setting is put back
  before the next app starts.
- `autorun/sampleboot/last-boot.log` describes the last boot.

**Customisation**
- Every colour and the bar opacities in `sampleboot.conf`;
  `sampleboot.original.conf` keeps the original values.
- `tools/theme-editor.html`: an offline page that previews every screen
  exactly as the device draws it (RGB565 colours included), with colour
  pickers, ready-made themes, import of your `sampleboot.conf` and the whole new
  file to copy.
- `tools/add_mod.py`: optional helper that installs or updates SampleBoot and
  adds OSes straight from their release zips, including folders they put at
  the top level of the card (MPC Simple's `MPC-Simple`).
