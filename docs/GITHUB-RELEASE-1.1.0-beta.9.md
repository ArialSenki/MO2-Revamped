MO2 Revamped 1.1.0 beta 9 is the **newest version**, still a beta. Cyberpunk 2077
and Baldur's Gate 3 now work with their main mods, the setup was reworked so an
uninstall removes everything it installed, and Elden Ring and Nightreign get
fixes for closing and starting the game and for saves. It also has the fixes of
two audits of the whole program. The last stable release,
[1.0.2](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.2), is
further down the releases list.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.9.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Which games were tested

Tried by hand with the real games, on Windows 11:

| Game | What was tested | Result |
|---|---|---|
| Skyrim Special Edition, Fallout 4, Fallout 3, New Vegas | Script extenders and the borderless window (in beta 8) | Work |
| Cyberpunk 2077 (2.5) | Cyber Engine Tweaks, RED4ext, ArchiveXL and TweakXL, installed and started from MO2 | Work: CET starts, RED4ext loads ArchiveXL and TweakXL, and nothing is copied into the game folder |
| Baldur's Gate 3 | BG3 Script Extender and a .pak mod (Community Library) | Work: the Script Extender loads and the .pak goes into the load order |
| Elden Ring, Nightreign | The changes of this beta come from a code audit | Start in MO2 with a test instance; not played yet with this version |

Every other game MO2 supports (Tale of Two Wastelands, Oblivion, the original
Skyrim, Enderal, Morrowind, Starfield and the VR versions) starts in MO2 with a
test instance, but wasn't played with mods.

## Highlights

- **Cyberpunk 2077:** MO2 loads CET's `version.dll` and RED4ext's `winmm.dll` as
  the game starts, so both work through the virtual file system and the game
  folder stays clean. Mods installed earlier with a `root` folder need to be
  installed again.
- **Baldur's Gate 3:** paks that couldn't be read are named instead of silently
  left out, and the load order follows the current profile.
- **Elden Ring and Nightreign:**
  - Only the first of two threads closing the game at once goes on, which most
    likely fixes the rare crash on exit.
  - MO2's hooks wait for the game's start-up code, so they don't collide with
    Reforged's Arxan removal, and "start minimized" can't freeze the start.
  - With a mod profile's saves hidden, the Steam save comes back within half a
    second of the game closing.
  - Save isolation checks the game is closed after each confirmation, and
    finishes an interrupted restore only when every file is as it left it.
  - Seamless Co-op for one game is never loaded into the other.
- **Setup:** uninstalling removes the install folder, its shortcuts and its Nexus
  link registration, and nothing of yours. It stops with everything in place when
  a file is in use, and can be run again. New options: close when finished,
  open Nexus links with this MO2, close MO2 when updating. Silent switches for
  scripted installs are in the README.
- **Downloads:** a full drive, an expired link or a dropped connection pauses the
  download instead of deleting it, and the row says why.
- **Mod list and profiles:** backups and dropped folders copy with progress and
  Cancel; Replace from Overwrite keeps the old mod until the new one has its
  files; names like "data" are refused; renaming a profile updates its shortcuts.
- **Games and instances:** an instance whose game moved asks for its new folder;
  plugins with accented names no longer turn themselves off; Fallout 4 from GOG,
  Enderal from Steam and Starfield in any Steam library are found.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.9/CHANGELOG.md).

## Known issues

- With the Nightreign mod Damage Competition, about 1 start in 15 closes in the
  first second, inside that mod's DLL. Start it again. The bridge log now lists
  the DLLs that load and unload, to help find the cause.
- In Fallout 3, New Vegas, Oblivion and Skyrim, the borderless window is hidden
  when the game stops answering on a black screen, which is what they do while
  they close. If a game does that while playing, the desktop can show until it
  answers again.
- With ELDEN RING Reforged, MO2 sets aside 12 GB of memory for the game, as me3
  does. On a PC with little RAM or a small page file, other programs may run short
  while the game runs.
- With Steam Cloud on, a mod profile hides the Steam save while the game runs. It
  comes back within half a second of the game closing; keep a copy if your cloud
  save matters to you.
- Rarely, the game can stall at start-up while MO2 installs its file hooks. Close
  it and start it again.
- If you elevate the setup with a different administrator account, shortcuts go
  to that account's desktop and Start menu. Installing through a folder link
  installs but can't be uninstalled from that path.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, FromSoftware or Bandai
Namco Entertainment.
