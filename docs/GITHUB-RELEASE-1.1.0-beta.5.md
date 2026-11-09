MO2 Revamped 1.1.0 beta 5 is a **pre-release**: mods' sounds and a faster start and exit
reach Nightreign, separators get their colors back, and a long review of the whole
program fixed a hundred or so smaller problems. 1.0.2 stays the stable release.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.5.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Highlights

- **Nightreign**: custom sounds in a mod's `sd` folder play, the intro logos are
  skipped, BootBoost keeps the archive headers ready after the first start, and the
  game closes in about 3 seconds. The logo skip and BootBoost are switches in the
  profile's startup options, on by default.
- **Reforged's save is safer**: if MO2 can't give ELDEN RING Reforged its own save
  file, it now closes the game before it reads any save and tells you why, so it
  can't write over the save of your other mods.
- **Seamless Co-op** starts from a mods folder with any name.
- **Mod list**: separators are a colored band as tall as a mod, and new ones get a
  random color or the one you choose. The conflict colors when you select a mod are
  back.
- **Each game shows what applies to it**: Elden Ring and Nightreign no longer list
  options and tools for other games' file formats.
- **Icons and windows**: every icon was checked, the game's file types have their own,
  and every window got a pass for alignment and spacing in both themes. Tab now shows
  where you are.
- **Saves**: export keeps every route apart and includes the set-aside saves, and a
  failed transfer puts the old save back.
- **Setup**: an interrupted install or restore can be finished or undone, a restore
  checks its backup first, and Copy data brings shared saves and launch settings.
- **Fixed**: arranging a large archive by hand no longer freezes MO2, mods with a
  loose file beside their game folders are no longer marked as invalid, a DLL
  missing from disk no longer blocks the launch, New category no longer renames
  another one, a failed download retry no longer damages the partial file, and
  refreshes and downloads no longer crash in several rare cases.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.5/CHANGELOG.md).

## Known issues

- With the Nightreign mod Damage Competition, about 1 start in 15 closes in the first
  second, inside that mod's DLL. Start it again. Earlier betas did it too.
- With ELDEN RING Reforged, MO2 sets aside 12 GB of memory for the game, as me3
  does. On a PC with little RAM or a small page file, other programs may run short
  while the game runs.
- Rarely, the game can stall at start-up while MO2 installs its file hooks. Close
  it and start it again.
- With Steam Cloud on, a mod profile hides the Steam save while the game runs.
  It came back before Steam looked in every test, by a second or two.
- If you elevate the setup with a different administrator account, shortcuts go to that
  account's desktop and Start menu. Installing through a folder link installs but can't
  be uninstalled from that path.
- Other games were not played for this beta.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, FromSoftware or Bandai
Namco Entertainment.
