MO2 Revamped 1.1.0 beta 16 is the **newest version**, still a beta. It is
beta 15 plus **our own game plugins** for Cyberpunk 2077, Baldur's Gate 3,
The Witcher 3, Stardew Valley and Valheim, a **faster start** that loads only
the instance's game, much faster mod toggles and problem checks with **large
lists** like Nolvus, help pages and first steps, Settings reorganised with
animation levels, rewritten installer dialogs and previews, collection and
Wabbajack fixes, separate saves for The Convergence, Wine and Proton fixes,
and security and stability fixes.

## Beta status: what works and in which games

This is a **beta**. It works for daily use in the games below, but expect bugs.
Back up your saves and your mods folder first, and please report what fails.

**Tested by hand with beta 16 builds**

- **Elden Ring and Elden Ring Nightreign** (offline, without EAC): loose files
  load in both, and BootBoost works in Nightreign. Seamless Co-op 2.0.3 and
  Reforged 2.3.9.1 reach the world and save in the profile. The Convergence
  3.0.4 reaches the world and saves in the profile as `ER0000.cnv`, or as
  `ER0000.cnvco2` with Seamless Co-op. Steam saves are hidden while you play
  and come back when the game closes.
- **Skyrim Special Edition**: the Wabbajack list A Dragonborn's Fate and the
  collection Essential Mods for Skyrim, from the gallery to the game's main
  menu with SKSE.
- **Fallout 3**: the collection Fallout 3 - Essentials, installed and started
  to the main menu with FOSE.
- **Fallout 4**: the collection Module 01 - Basics & Tools installed into its
  own new instance (37/37 mods, F4SE as a Root program).
- **Cyberpunk 2077** and **Baldur's Gate 3**: the new plugins were checked
  against real mods from Nexus and with the real games while they were written.
  The NCR Core collection (426 mods) was used to find and fix the install that
  stopped halfway and the wrong game version (see below).
- **Nolvus Awakening 6.0.21**: MO2 starts and works with it; the speed figures
  below were measured on it. The game itself was not started with beta 16.

**Not tested with the real game yet:** The Witcher 3, Stardew Valley and
Valheim (their plugins pass their own tests, and Stardew's and Valheim's mod
checks were run on real Nexus mods), and anything on **Linux, Steam Deck,
Wine or Proton** (those fixes come from reports and a code review).

**Not in this beta:**
- MergedPatch (zEdit merges) in Wabbajack lists, and the MO2 plugins a list
  brings;
- installing or updating Nolvus itself (use the Nolvus Dashboard);
- the Microsoft Store / Game Pass editions.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.16.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data (beta 15 or earlier).
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Our own game plugins

Written from scratch for MO2 Revamped. They keep the old plugins' names and
settings, so existing instances open as before, and an update replaces the old
plugin.

- **Cyberpunk 2077:** a Content column and filter, an archive conflicts tool,
  Codeware and Root Builder checks, and the mods whose scripts redscript can't
  compile are named. The game's version is its patch number (2.5), as on
  Nexus, so collections and Wabbajack lists made for it no longer say it is
  the wrong version.
- **Baldur's Gate 3:** a load order window, split paks, mods inside a wrapper
  folder, Native Mod Loader mods, damaged paks named, and a failure making the
  load order no longer stops the game. Root/bin DLLs are found in the mods'
  folders and in Overwrite, and a DLL copied into the game folder isn't loaded
  twice.
- **The Witcher 3:** DLC, menu files and DLL/ASI mods install without Root
  Builder; Script Merger as a program; a warning when two mods change the same
  script.
- **Stardew Valley** and **Valheim:** mod checks that fix common layouts, and
  BepInEx mods read from their DLL.

The plugins' texts are in English, Spanish, German, French, Italian and
Brazilian Portuguese. The Skyrim SE and Fallout 4 plugins warn about a script
extender built for another game version and a missing Address Library, without
the false alarms of the first try.

## A faster start

- MO2 loads only the instance's game plugin and the tools, installers and
  checks for that game. Other games' plugins wait as light stand-ins until a
  DLL is really needed.
- The Python game plugins (Cyberpunk, BG3, Witcher 3, Stardew, Valheim and the
  other basic games) no longer search Steam, GOG, Epic and the other stores at
  every start: only the instance's game is looked for.
- The FOMOD C# and OMOD installers start .NET only when one of them installs a
  mod.
- Python plugins ship compiled and load their heavy parts when they are used.

## Large mod lists

Measured on Nolvus Awakening (about 3,700 mods and 3,500 plugins):

- enabling or disabling a mod: about 0.2–0.35 s instead of about 1 s, and
  disabling one no longer rebuilds every row of the plugin list;
- the plugin list is rebuilt once instead of twice, in about 0.15 s;
- saving plugins.txt and loadorder.txt: about 25 ms instead of about 120 ms;
- creating the mods at start: about a third faster.

The problem checks are cheaper too. The Cyberpunk, Stardew and Valheim checks
read a mod again only when it changed. On NCR Core's 426 mods, the Cyberpunk
check after a toggle takes 21–39 ms instead of 105–138 ms.

Plugins with no fixed place now get the same order at every start (before, the
same list could come out in different orders).

## Collections and Wabbajack

- **An install no longer stops halfway in big collections.** NCR Core stopped
  for good after 70 to 90 mods, with the window still drawn, when a problem
  check and a mod refresh waited for each other.
- Collections say when the install waits for a file only you can get, and
  select it with its link.
- A file no installer knows goes in as its archive lays it out.
- Script extenders a collection puts in a mod's Root folder show as programs.
- Mods done by hand are counted apart in the summary.
- Wabbajack names a game file a list is missing, and warns when a list's
  script extender is for another game version.
- A collection's or a Wabbajack list's new instance takes this instance's
  theme, and its categories: the list's own, or else this instance's. No
  first-start questions stop the install halfway.
- A C# script FOMOD in a collection (UIO, for example) no longer asks to
  confirm its name first.
- Open the selected list or collection straight from the galleries;
  **Ctrl+F** finds and **F5** reloads in both galleries.
- The Wabbajack download folder, the game the gallery showed last and the
  collection install target belong to each instance, so one instance no
  longer opens with another's choices.
- A collection link that arrives while the collection window asks something
  no longer closes that window. A Wabbajack install folder whose longest path
  would pass 259 characters is refused, with the longest folder that fits.
- Many downloads from one server no longer end paused with "the server stopped
  sending data" while they only waited for a free connection.

## Elden Ring and Nightreign

- **The Convergence keeps its own saves:** `ER0000.cnv`, as with its own
  launcher, or `ER0000.cnvco2` with Seamless Co-op. Before, under MO2 it played
  and saved over `ER0000.sl2`, the save of the game without mods, and with
  Seamless it shared `ER0000.co2` with Seamless played alone. With The
  Convergence active, Seamless loads once, from a copy of its folder in the
  instance with your own settings; the mod itself isn't changed. The save list,
  save recovery and the save tool know both names.
  **If you played The Convergence from MO2 before:** that character is in
  `ER0000.sl2` (or `ER0000.co2` with Seamless). Copy it as `ER0000.cnv` (or
  `ER0000.cnvco2`) in the same folder to go on with it.
- **Bridge alpha.75:** MO2's save name stays when a mod DLL changes it (The
  Convergence's ErdTools did). Also new since beta 15: native initializers run under
  the virtual file system and match the DLL's real path, also for paths over
  259 characters; a DLL whose load failed after starting a thread is loaded
  once more; a thread whose DLL was unloaded no longer crashes the game; a
  second way to the loose-file resolver for game updates.
- **me3_compat 0.5.4:** a mod with its own regulation.bin (The Convergence)
  can save again; before, the game said "Failed to save game. Save data is
  corrupted". Reforged closes cleanly again, also with NativeTweaks.
- The Elden Ring tools load only in Elden Ring instances.

## Fallout 3 and New Vegas

- MO2 asks before starting when the script extender in a mod's Root folder is
  not for the game's version. For FOSE on Steam's Fallout 3 1.7.0.4 it says to
  run the Fallout Anniversary Patcher first (before, the game reached its menu
  without any FOSE plugin and without a word).
- NVHR works on Steam's New Vegas: the folder its DLL loads from is copied with
  it before the game starts.
- A read-only Fallout.ini no longer makes the profile's copy read-only, so MO2
  stops asking about it at every start.

## Settings and appearance

- **Appearance** (formerly Theme) holds everything about how MO2 looks: the
  theme, a Display card (interface size, grey text for OLED screens and
  Qt Quick), the animations and the toolbar and panels. **General** keeps the
  everyday options in order of use, and categories moved to **Mod list**.
- **Animations** has four levels: Off, Saver (30 Hz), Standard (60 Hz, as
  before) and Monitor (your screen's rate, up to 240 Hz). Each frame comes at
  its own time, so 144 Hz is really 144.
- **OLED Black:** the main button has a dim accent fill and outline, so it
  stands out; the notification cards match the other cards; and the
  first-start theme question offers OLED Black too.
- **F1** in Settings opens its help. "Ask before switching
  instances" is the new, shorter name of that option.
- The drop hint frames the whole list from just outside it and goes away the
  moment the drag leaves.
- "Last saved" in the top bar no longer counts Steam's sync file or script
  extender logs as a save.

## Help and first steps

- A **Guide** with first steps, a page per game (including The Witcher 3,
  Stardew Valley and Valheim) and Settings explained.
- A new, empty instance offers the first steps.
- Settings > Plugins says which plugins are MO2 Revamped's own.
- A [user guide](https://github.com/ArialSenki/MO2-Revamped/tree/v1.1.0-beta.16/docs/user-guide)
  in English and Spanish in the repository.

## Installers and previews

- The questions, messages and choices of C# FOMOD and OMOD scripts show as
  MO2's own dialogs. In a C# script's choice list, clicking an option's name
  ticks it.
- An archive with both a FOMOD XML and a C# script always gets the same
  installer.
- A mod's own text and broken XML no longer break the FOMOD and OMOD
  installers; a cancelled OMOD script leaves no empty mod.
- The BAIN wizard's descriptions and Ready page line up at every scale.
- The FNIS checker's questions after a warning or error say what happened.
- **Previews:** pictures fit the window with their size shown, are no longer
  blurred by display scaling, and GIFs play. Text files are never read as web
  pages. The BSA preview says what the archive holds, has its filter above the
  files, and says so when the file isn't an archive.
- A question asked while another is open no longer freezes MO2.

## Qt Quick (optional, off by default)

Settings > Appearance > **Use the new interface (Qt Quick) where available**:
the Instances window, the first-start theme question and the main window's
summary tiles are drawn with Qt Quick. They work the same as before. It applies
after a restart and draws in software, so it works on every graphics card.

## Wine and Proton

- The setup installs Microsoft's Visual C++ runtime under Wine and Proton and
  puts a copy beside ModOrganizer.exe there, keeping your own DLLs.
- No black or transparent window (Mica is off under Wine), blank web pages
  fixed, nxm links accepted, and the startup log is on by default.

## Security

Security reviews of the changes since beta 15 found nothing critical or
high. Fixed:

- An unfinished Nolvus transfer record is never trusted as it is: undoing
  only touches the instance and the Nolvus folders it really finds.
- Game programs from a mod's Root folder are added only when that mod is
  enabled.
- FOMOD pictures can't load files from another computer or outside the mod,
  and a link to a network share dropped on MO2 no longer starts a download.
- Gallery images never load from this computer or the local network, also
  after a redirect, and more forms of local addresses are refused for direct
  downloads.
- A WinRAR older than 7.13 is still offered for archives 7-Zip can't open, but
  the question names its path risk and the answer isn't remembered.
- Size limits for downloads without a size, Wabbajack lists, collections and
  INI tweaks; collection paths refuse 8.3 short names.
- A custom browser gets the address encoded, so it can't be split into extra
  arguments. LOOT's pipe has a new name each run and refuses remote clients.
- Bug reports also hide Nexus API keys, Steam user ids and more tokens.
- The installer limits a new install folder right after creating it, and only
  folders it created or found empty; other users of the PC can read and run
  MO2 but not change it. When it runs as administrator, its logs stay in
  Program Files.
- Damaged Skyrim SE and Fallout 4 saves, and huge or malformed BG3, .NET,
  Stardew and Cyberpunk files, can no longer crash or freeze MO2.

Still as in Mod Organizer 2: C# FOMOD scripts run without asking. Install
only mods you trust.

## Stability

- Games no longer crash or hang on exit because usvfs freed its memory under
  running threads, and each instance and MO2 build has its own usvfs memory.
- Quitting while a collection installs no longer freezes MO2, and a single
  install waits for a running collection.
- Installing several selected downloads finds each one by its file, so a
  paused download of the same name is no longer taken instead.
- MO2 asks only once at a time whether to register for nxm links, and the
  question closes by itself after two minutes, so no hidden handler stays
  running.
- MO2 also listens on the link pipe name from before beta 15, so upstream MO2
  and older MO2 Revamped builds reach it again.
- Fallout 3, New Vegas and Oblivion: a plugin rewritten with the same size and
  date is read again.
- Opening an instance no longer changes the global language.

## From beta 15

Everything else from beta 15 is unchanged; see the
[beta 15 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.15).

## Known issues

- Dragging downloads between groups with the mouse may not drop. Use
  **Move to category** in the menu instead.
- Under Linux, Steam Deck, Wine and Proton, beta 16 has not been run yet;
  please report what fails there.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.16/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, Nolvus, CD PROJEKT RED,
Larian Studios, FromSoftware or Bandai Namco Entertainment.
