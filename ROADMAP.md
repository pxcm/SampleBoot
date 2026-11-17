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

### Look

- **New splash animation.** The logo appears in bands with a glowing scan
  line, as on [sampleboot.com](https://sampleboot.com). New settings:
  `SPLASH_STYLE` (`bands` or `lines`, the 1.0 animation) and `SPLASH_BEAM`
  (the colour of the scan line). The theme editor and the website show the
  same animation.

### Compatibility list

- **Which systems work.** A section on the website and in the README listing
  the systems that work with SampleBoot, with their versions: the ones tested
  by the author, and the ones users report working (for example
  ODDFORM 1.2.0 beta). To report one, open an issue or write on Reddit.

## Being evaluated

- **Switch system without restarting.** Go back to the menu without switching
  the MPC off, for example by taking the card out and putting it back in.
  Each OS would need to be stopped cleanly, without losing unsaved work, so
  this needs input from the OS authors first.
- **Browser interface over USB.** A page on the computer, reached over the
  USB cable at a fixed address, to manage SampleBoot: install a system from
  its zip, rename or hide entries, edit the theme on the real screen.
  Feasibility and need still to be checked.

## Done

- **1.0.1:** SampQOL OS 0.3.0's 16 USB outputs work when it is started from
  SampleBoot.
- **1.0.0:** first public release. See the [changelog](CHANGELOG.md).
