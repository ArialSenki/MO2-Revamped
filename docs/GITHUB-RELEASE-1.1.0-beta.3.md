MO2 Revamped 1.1.0 beta 3 is a **pre-release**: it starts faster and drops what
was left of the old Mod Organizer 2 that this program no longer uses. 1.0.2 stays
the stable release.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.3.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Highlights

- **Faster**: the main window appears about a second sooner. The first Mod
  information of each session takes a moment longer, because the web engine now
  starts only when a page needs it. Mod information, tab changes, program lists
  and the Reforged sync wait less.
- **Less left over from the old program**: its old themes and build files, unused
  icons and code, and stale translations are gone. Links to the old project now
  appear only in the "..." menu.
- **Fixed**: a new installation starts in the language picked in its setup, Mod
  information's Advanced conflicts list shows the file names, and the installer
  wizard's updated look and the Form 43 checker's translated name now ship.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.3/CHANGELOG.md).

## Known issues

- With Convergence, the game can fault as it closes, inside `ErdTools.dll` or
  in the game's own code. With Reforged online, the game and `me3.exe` can too.
  Saves were written correctly in every case seen, and none of these came from
  MO2's code.
- Seamless Co-op doesn't start from a mods folder that isn't named `mods`, or
  that is a link to another folder.
- With Steam Cloud on, a mod profile hides the Steam save while the game runs.
  It came back before Steam looked in every test, by a second or two.
- Other games were not played for this beta. Skyrim was only checked on a test
  folder without the game, and Fallout not at all.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, FromSoftware or Bandai
Namco Entertainment.
