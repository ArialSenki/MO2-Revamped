MO2 Revamped 1.1.0 beta 2 is a **pre-release**: the whole program was reviewed
window by window, and it was played on Elden Ring and Nightreign before release.
1.0.2 stays the stable release.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.2.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Highlights

- **Your files are safer**: Replace keeps the old mod until the new one is
  installed, and brings it back if you cancel or the install fails. Deletes in
  Mod information, Overwrite, profiles, instances and backups go to the Recycle
  Bin, and failures are reported instead of passing quietly.
- **Your saves are safer**: a launcher left open no longer ends a save swap, a
  swap that stopped partway never deletes what you played, and Reforged online
  no longer lets Steam Cloud upload the wrong save over yours.
- **The rest of the old look is gone**: question dialogs, filters, lists and
  progress windows use the Fluent design, in Light and Dark.
- **Native DLL Profile**: a DLL overridden by another mod no longer stops the
  game from starting, and what you switch off stays off.
- **Clearer texts**: wording reviewed, and translations corrected, especially
  about deleting, backups and saves.
- **Removed**: the old tutorials and the integrated browser. Mod pages open in
  your own browser.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.2/CHANGELOG.md).

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
