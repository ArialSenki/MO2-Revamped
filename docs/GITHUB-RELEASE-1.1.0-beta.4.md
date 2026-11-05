MO2 Revamped 1.1.0 beta 4 is a **pre-release**: mods' music plays, ELDEN RING
Reforged keeps its sound, Elden Ring starts sooner and closes in seconds, and MO2
now tells you when a new version is out. 1.0.2 stays the stable release.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.4.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Highlights

- **Update notice**: when a newer MO2 Revamped is out, a window shows what changed,
  with links to GitHub and Nexus. Nothing is downloaded by itself, and Settings,
  General, Updates turns it off or picks stable or beta versions. This is the
  first version that can tell you about the next one, so update to it by hand.
- **Mods' music and sounds**: custom music and sound banks in a mod's `sd` folder
  play, Convergence's music for example.
- **ELDEN RING Reforged keeps its sound**: music and voices no longer stop on dodges
  and hits. MO2 now applies me3's memory patch, as Reforged's own launcher does.
- **Faster start**: the intro logos are skipped, and me3's BootBoost keeps the
  game's archive headers ready after the first start, so the title screen comes
  about 2 seconds sooner. Both are switches in the profile's startup options, on
  by default.
- **Faster exit**: Elden Ring closes in about 4 seconds instead of up to 20, and
  with Reforged it no longer ends in a crash report.
- **Fixed**: native DLLs load in the order the Native DLL Profile shows, a new
  instance no longer skips steps with Enter, Clear cache moves the cache to the
  Recycle Bin instead of deleting it, Downloads asks before cancelling a download
  under way, and the window title and About show the MO2 Revamped version.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.4/CHANGELOG.md).

## Known issues

- With Convergence, the game can still fault as it closes, in its own code.
  Saves are written before that happens.
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
