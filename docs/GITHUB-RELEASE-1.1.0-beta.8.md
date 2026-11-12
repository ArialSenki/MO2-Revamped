MO2 Revamped 1.1.0 beta 8 is the **newest version**, still a beta. It installs
the script extenders of Skyrim and Fallout, adds a borderless window option for
those games, and rewrites the last windows left from Mod Organizer 2. It also
carries everything of beta 7, which wasn't published on its own. The last stable
release, [1.0.2](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.2),
is further down the releases list.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.8.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Which games were tested

The new features were tried by hand with the real games, on Windows 11:

| Game | Script extender | Borderless window |
|---|---|---|
| Skyrim Special Edition | SKSE64: works | works |
| Fallout 4 | F4SE: works | works |
| Fallout 3 | FOSE: works | works |
| New Vegas | xNVSE: works | works; at exit the screen still goes black for about a third of a second |

Elden Ring and Nightreign don't use either and get no changes of their own.

Not tested yet:

- **Cyberpunk 2077** and **Baldur's Gate 3**.
- **Oblivion, Skyrim (the original), Enderal, Tale of Two Wastelands, Starfield
  and the VR versions**: their script extenders and the borderless window work
  the same way, but haven't been tried with the real games.
- **Windows display scaling above 100%** with the borderless window in the older
  games.
- **The setup on Linux, Steam Deck, Proton or Wine**: the fix comes from a user's
  report and was checked on Windows only.

## Highlights

- **Script extenders** (SKSE64, SKSE, F4SE, NVSE, FOSE, OBSE, SFSE): installing
  one asks where its files go. MO2 can copy the loader and DLLs into the game
  folder, keep the rest as a mod, offer the loader for **Run**, and warn when the
  extender is for another version of the game. Removing it, or updating it the
  same way, sends the files it copied to the Recycle Bin.
- **Borderless window** in the Run button's menu, for Skyrim, Skyrim Special
  Edition, Enderal, Fallout 3, New Vegas, Tale of Two Wastelands, Fallout 4 and
  Oblivion: the game covers the screen at its resolution, without a frame. The
  older games get it from a small DLL of MO2's, loaded only with the option on.
- **Dialogs** open centred over the window in front of them, on its monitor, and
  stay above it. Before, some opened behind the main window or the instance
  wizard, or on another monitor.
- **Starting MO2 again** brings the open one to the front instead of showing a
  message.
- **The last windows from Mod Organizer 2** are rewritten in MO2 Revamped's
  design: the FNIS tools, the DDS preview, BSA Packer, BSA Extractor, the save
  details card, the "remember my choice" questions and task dialogs, and the file
  attribute check. C# FOMOD scripts' own windows open centred over MO2.
- **Plain-language texts** for the Form 43 and script extender checks, the games'
  settings and their notices, in all six languages.
- **Setup on Linux**: it no longer stops and undoes the install under Wine
  ("The method or operation is not implemented").
- **Fixes**: BSA Extractor keeps an archive whose files didn't all come out;
  script extender mods are no longer flagged as having no valid game data.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.8/CHANGELOG.md).

## Known issues

- In Fallout 3, New Vegas, Oblivion and Skyrim, the borderless window is hidden
  when the game stops answering on a black screen, which is what they do while
  they close. If a game does that while playing (a long load after a fade to
  black), the desktop can show until it answers again.
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

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, FromSoftware or Bandai
Namco Entertainment.
