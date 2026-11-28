SampleBoot - mods folder
========================

Put each alternative OS here, one folder per OS. Copy the "autorun" folder
from the OS release into this folder and rename it, for example:

    autorun/mods/Melodee 0.2.0/      (Melodee's autorun folder, renamed)
    autorun/mods/SampQOL 0.2.0/      (SampQOL's autorun folder, renamed)

Every folder that holds an OS becomes an entry at the next power-on: its
autorun.json names the script to start (autorun.sh, launcher.sh, ...), as the
MPC's firmware reads it.

Releases made for firmware 1.4 often have an autorun.sh at the top, next to
their autorun folder: copy only their autorun folder here. SampleBoot's own
autorun.sh, at the top of the card, starts the menu on firmware 1.4.

Some OSes also come with a folder for the top level of the card (MPC Simple:
MPC-Simple, with its models). Copy that one to the top level, next to
autorun, as the OS's own instructions say.
If an OS can't find its files from autorun/mods/ (for example its file
browser stays inside autorun/mods), put an empty file named
"sampleboot.inplace" in its folder: SampleBoot then runs it from autorun/
itself, as if it were alone on the card. MPCTRACKER and MPC 600 S need this.

Folders starting with "." or "_" are skipped, so renaming one to
"_Melodee 0.2.0" hides it without deleting it.

Name in the menu: the folder name, or the first line of a text file
"sampleboot.txt" in the folder.

Image (optional), searched in this order inside the OS folder:
  1. sampleboot.png / sampleboot.jpg / sampleboot.bmp
     (any size, fitted to the screen;
     320x240 looks best: keep the top and bottom 46 pixels free of
     important details, the menu bars cover them)
  2. sampleboot.raw, or any .raw file of exactly 307200 bytes (320x240, 32-bit
     BGRX, the display's own format, e.g. SampQOL's qol-splash.raw)
  3. otherwise a title screen with the name is generated

The "_template" folder holds an example sampleboot.txt and sampleboot.png: copy
them into an OS folder and edit them.

The stock Akai entry works the same way, with the files in autorun/sampleboot/:
sampleboot.txt for the name, sampleboot.png (or .jpg/.bmp/.raw) for the image.
