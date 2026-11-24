# Roadmap

What's planned for SampleBoot. The idea: do more with SampleBoot and make it
nicer to use, without weighing it down. Plans can change: ideas and feedback
are welcome in the [issues](https://github.com/pxcm/SampleBoot/issues).

## 1.1.0 (planned)

### Finding your system

- **List view.** A button switches the menu between the full-screen carousel
  and a vertical list, which shows several systems at once. SampleBoot
  remembers your choice.
- **Sort order.** A–Z or Z–A. The stock firmware always stays first.
- **Filter by name.** Type a few letters with the pads, like the keypad of an
  old phone, and the menu shows only the systems whose name contains them
  (for example `sam` for every SampQOL version).
- **Favourites.** An empty star on each system, filled once you add it to your
  favourites, and a button to show only your favourites.
- **Hide systems.** Take a system out of the menu without renaming its folder.

### Settings and the card from the MPC

- **Options on the MPC.** A menu on the MPC itself to see and change
  SampleBoot's settings (countdown, beeps, splash, view, sort order…),
  saved to `sampleboot.conf`.
- **SD card over USB.** Show the card to a computer over the USB cable, as the
  custom OSes do, to add, change or remove systems and files without taking
  the card out. The menu reloads when you're done.
- **Release folders as they are.** If a system's folder has no `autorun.sh`
  inside, SampleBoot also looks one level down, in
  `mods/<name>/autorun/`. You can drop in the folder from a release zip
  without moving its files around.

### Look

- **New splash animation.** The logo appears in bands with a glowing scan
  line, as on [sampleboot.com](https://sampleboot.com). New settings:
  `SPLASH_STYLE` (`bands` or `lines`, the 1.0 animation) and `SPLASH_BEAM`
  (the colour of the scan line). The theme editor and the website show the
  same animation.

## Being evaluated

- **Switch system without restarting.** Go back to the menu without switching
  the MPC off, for example by taking the card out and putting it back in.
  Each OS would need to be stopped cleanly, without losing unsaved work, so
  this needs input from the OS authors first.
- **Browser interface over USB.** A page on the computer, reached over the
  USB cable at a fixed address, to manage SampleBoot: install a system from
  its zip, rename or hide entries, edit the theme on the real screen.
  Feasibility and need still to be checked.
- **Stock firmware 1.4.** SampleBoot is tested on firmware 1.3.0.12. Akai's
  official update is 1.4.0.8, and it changes how cards start programs:
  - the signed `autorun/` folder that 1.3.0.12 starts by itself is no longer
    started;
  - with Developer Mode on (Settings, Beta Enhancements), the stock app
    finds an `autorun.sh` at the top of the card and asks each time whether
    to run it. Turning Developer Mode on is recorded permanently on the MPC
    (the `dev-mode-used` flag).

  SampleBoot can start the same way: a top-level `autorun.sh` that starts
  SampleBoot, plus 1.4.0.8 in the firmware list. The stock app would start
  first and ask, then the menu would show. Most systems check for firmware
  1.3.0.12 and won't start on 1.4.0.8 until their authors update them, so
  this waits until SampQOL and Melodee support it. **Until then, stay on
  1.3.0.12 if you use SampleBoot.**

## Proposed to OS authors

- **One folder for the mods' own files: `MPC-Mods/`.** At the card root,
  next to the stock `MPC-Sample/`, each system would keep its files in its own
  subfolder (`MPC-Mods/SampQOL/`, `MPC-Mods/MPC-Simple/`,
  `MPC-Mods/Tracker/`…) instead of creating folders anywhere on the card. The
  card root stays clean, and everyone knows where each system keeps its
  things. It only works if the authors agree on it: once they do, it goes
  into the [notes for OS authors](docs/OS-AUTHORS.md).

## Done

- **Compatibility list** in the README and on the website: systems tested by
  the author and systems users report working.
- **1.0.1:** SampQOL OS 0.3.0's 16 USB outputs work when it is started from
  SampleBoot.
- **1.0.0:** first public release. See the [changelog](CHANGELOG.md).
