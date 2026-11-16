# SampleBoot – technical notes

How SampleBoot works on the MPC Sample, and how to build it. For installing
and using it, see the [README](../README.md).

## How it works

- The stock app runs `autorun/autorun.sh` only if `autorun.check` equals
  sha256(`autorun.json` + `autorun.sh` + a fixed salt). The salt was found by
  the mpc-sample-plugins project and is also used by sample-base/Melodee. The
  launcher is signed once and never changes; everything else can be edited.
- The launcher starts `sampleboot/sampleboot.sh` as its own systemd unit, once
  per power-on (a marker in `/run`). The script stops the stock app
  (`ac50.service`) and runs `sampleboot-ui` from RAM.
- `sampleboot-ui` is a small static aarch64 program. It writes 320×240 BGRX
  frames to `/dev/fb0` and reads the encoder from the raw MIDI port of the
  ALSA card `Surface` (wheel = CC 100 on channel 1, push = note 111). It also
  reads `KEY_POWER` from the `rk805 pwrkey` evdev device. It prints the chosen
  folder and exits, releasing the MIDI port. Text uses an embedded DejaVu Sans
  font, and images are decoded with stb_image.
- The countdown beeps go to the `I2S` card (AK4558), with plain ALSA ioctls,
  no alsa-lib. The codec clicks when it powers up or changes rate (see
  sample-base's "Opening and closing without a click"). So `sampleboot-ui`
  reads the codec's DAPM state (`dapm_widget`) and master clock (debugfs). A powered
  codec is joined at its own rate; a codec that is off is powered up with the
  built-in speaker held off until its outputs have settled (the jacks are
  behind the board's mute, which opens on its own 1 s after power-up).
- Speaker: on with nothing in the headphone and line-out jacks, off with
  something plugged, followed while the menu runs. The switch goes back to
  its previous state before the next app starts.
- Stopping the stock app cuts its output (scaled by its Main Volume) and the
  outputs click. So the script first lowers the codec's `Playback Volume` to 0
  in small steps (`sampleboot-ui --volume-down`, old values kept in
  `/run/sampleboot/volume`); the menu brings it back the same way once its
  own silent stream runs, and every error path restores it.
- The Main Volume position comes from the surface: the menu sends sample-base's
  position request (`F0 47 7F 5C 42 00 00 F7`) and follows CC 19.
- On opening the surface port the menu switches every light off, as
  sample-base's `led off`: pads 1–16 and the fader light black
  (`F0 47 7F 5C 65 00 04 I 0 0 0 F7`), and channel-1 CC 0 for the pad banks
  (57–64), the level meters (83, 84) and the 17 buttons that have a light.
- Splash: `splash-<SPLASH>.png` is revealed top to bottom in 0.8 s, one frame
  per change, with a 2-line "beam" (white over a dark image, black over a
  light one, 43 % opacity) at the edge; it then stays until `SPLASH_TIME`.
  Encoder or POWER events skip it and are not passed on to the menu. The
  audio opens only after the splash, so the menu's countdown and its click
  come with the menu.
- The browse arrows are open chevrons (18 px high, 2 px stroke, square tip),
  antialiased with 4×4 samples per pixel.
- A folder in `mods/` is an OS when it holds a start script: the first
  `scripts[].path` of its `autorun.json` that names a file in the folder, as
  the firmware reads it (MPC Simple: `launcher.sh`), else `autorun.sh`
  (Melodee, SampQOL OS). Absolute paths and `..` are refused.
  `sampleboot-ui --script DIR` prints it.
- The script then runs the chosen OS's own start script, unchanged, or
  restarts `ac50.service` for stock. With `IN_PLACE=1` (the default) it first
  binds the OS's folder over the card's `autorun` (`mount --bind
  autorun/mods/<name> autorun`), so the OS runs from the path it would have
  without SampleBoot. Some OSes work out other paths from their own folder:
  SampQOL 0.3.0 reads `mpcqol.conf` from `${DIR%/*}`, the card root only when
  it sits in `autorun`, and turns its 16 USB outputs on from it. The binding
  lasts until power-off or until the card is taken out. The script's log
  stays open across it, so `sampleboot/last-boot.log` keeps being written.

## Files on the card

    autorun/
      autorun.sh, autorun.json, autorun.check   signed launcher (never changes)
      sampleboot/
        sampleboot.sh             start-up script (run from /run)
        sampleboot-ui             the menu (static aarch64 program, run from /run)
        sampleboot.conf           settings and colours
        sampleboot.original.conf  the original settings
        splash-dark.png, splash-light.png   the splash screens
        firmware.txt              sha256 of the stock binary -> firmware version
        sampleboot.txt, .png      optional name and image of the stock entry
        last                      last choice
        last-boot.log             log of the last boot
      mods/<name>/                one OS each: its own autorun folder, unchanged

## Building

    sudo apt install gcc-aarch64-linux-gnu      # Debian/Ubuntu
    make                                        # build/sampleboot-ui
    make release                                # dist/SampleBoot-<version>.zip
    make editor                                 # tools/theme-editor.html (also part of make release)
    make font                                   # regenerate src/font_data.h (needs Pillow)

The theme editor's renderer (`tools/editor/editor.html`) is a line-by-line
port of the drawing code in `src/sampleboot-ui.c`, and its glyphs, test images,
splash screens, logo and defaults are put in by `tools/build_editor.py` from
`src/font_data.h`, `tools/editor/images.json`, `card/` and `docs/images/`.
When the screens change in C, change the port too: comparing `sampleboot-ui --render` with the editor's
canvases must give identical pixels.

For testing on a PC, `sampleboot-ui --render DIR --autorun PATH` writes every
screen as a PPM image. Every device path can be overridden; see `--help`.
`qemu-aarch64` runs the binary on x86.


## Repository layout

    src/            sampleboot-ui.c, font_data.h (generated), stb_image.h
    card/           start-up script, settings, firmware list, mods/_template
    tools/          make_release.py, add_mod.py, build_editor.py, gen_font.py,
                    editor/ (theme editor source and test images)
    fonts/          DejaVu Sans (for gen_font.py) and its licence
    docs/           these notes, the README images and the logo (SVG)
