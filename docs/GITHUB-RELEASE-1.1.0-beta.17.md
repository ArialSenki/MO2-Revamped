MO2 Revamped 1.1.0 beta 17 is the **newest version**, still a beta. It is
**beta 16 with two fixes**, nothing else: changing the theme in Settings
reaches every part again, and mods tagged with the game's display name no
longer show the "different game" indicator.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.17.exe`: run it and choose **Update MO2
  Revamped** to keep an existing installation and its data (beta 16 or
  earlier), or **Install MO2 Revamped** for a new one.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Fixes

- **Changing the theme** in Settings reaches every part again. With the new
  interface (Qt Quick) on, the main window's summary tiles kept the colors of
  the theme MO2 started with, and so did the Instances window and the theme
  question opened afterwards: Qt Quick read the colors from a copy that was
  never updated. The toolbar and executables' icons are also redone once the
  new theme is fully applied, not before (edits to a theme's file included).
- **Mod list:** a mod whose meta.ini names the game by its display name
  (`gameName=ELDEN RING` instead of `eldenring`) no longer shows the yellow
  "different game" indicator, and neither does a mod with no game name.

## From beta 16

Everything else is the same as in beta 16, including what was tested and the
known issues; see the
[beta 16 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.16).

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.17/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, Nolvus, CD PROJEKT RED,
Larian Studios, FromSoftware or Bandai Namco Entertainment.
