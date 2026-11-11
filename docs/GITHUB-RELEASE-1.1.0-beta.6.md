MO2 Revamped 1.1.0 beta 6 is a **pre-release**: MO2 moves to Qt 6.12, LOOT sorts
with its current rules again, and the other games get a long round of fixes. 1.0.2
stays the stable release.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.6.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Highlights

- **Qt 6.12**: MO2 runs on Qt 6.12, the current long-term support release, with
  Qt WebEngine 6.140 (Chromium 140), instead of Mod Organizer 2.5.2's Qt 6.7, which
  no longer gets updates. The design does not change.
- **LOOT** sorts with its current rules again: Mod Organizer 2.5.2's LOOT read a
  masterlist branch LOOT stopped updating in May 2025. LOOT is now libloot 0.29.7.
- **Archives 7-Zip can't open**: if WinRAR is installed, MO2 offers to extract them
  with it, and every installer works as usual. Nothing of WinRAR comes with MO2.
- **Installer**: it installs the Microsoft Visual C++ runtime MO2 needs when it is
  missing or too old, from Microsoft's own installer, after checking its signature.
- **Baldur's Gate 3** is supported, with MO2's official game plugin and its tools.
- **Each game lists only what it can use**, and the Plugins and Archives tabs match
  the rest of the design.
- **BSA and BA2 archives** in folders with Japanese, Korean, Chinese or other
  characters outside the system code page open, preview and extract.
- **Fixes from later Mod Organizer 2 versions**: crashes with early nxm links,
  UTF-16 text files, hidden-file conflicts, and fixes for many games without their
  own plugin.
- **Mod list, log and window**: calmer separators, the main window fits 1920 x 1080
  at 150 % and 175 %, the log keeps its height, and the Data tab follows mods being
  switched on or off.
- **Built from source**: uibase, the plugins, USVFS, 7-Zip (now 26.06, with its
  security fixes) and PyQt6 are built here from their sources instead of being
  copied from the 2.5.2 release.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.6/CHANGELOG.md).

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
- No game was played with this beta: MO2 was opened and set up for each game, but
  the move to Qt 6.12 was not tried in a game session.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, FromSoftware or Bandai
Namco Entertainment.
