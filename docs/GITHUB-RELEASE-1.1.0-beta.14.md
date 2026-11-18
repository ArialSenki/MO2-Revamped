MO2 Revamped 1.1.0 beta 14 is the **newest version**, still a beta. It is
beta 13 plus **native installation of Wabbajack modlists**, which until now
was only in the separate experimental builds. The experimental build is
retired: everything it had is in this beta.

## Beta status: what works and in which games

This is a **beta**. It works for daily use in the games below, but expect bugs.
Back up your saves and your mods folder first, and please report what fails.

**Tested by hand with beta 13, with the real game and mods**

- **Skyrim Special Edition**: SKSE64 2.3.1 started from MO2.
- **Nolvus Awakening 6.0.21** (3,597 mods), as a linked instance. The game
  started from MO2 with SKSE and its 200 plugins loaded.
- **Cyberpunk 2077** 2.5: the Welcome to Night City collection (364 mods)
  installed. The game started with RED4ext and its plugins loaded.
- **Baldur's Gate 3** (DX11): started from MO2 with a 278-mod list.

**Tested with earlier betas, not played again with this one:** Elden Ring and
Elden Ring Nightreign, Fallout 4, Fallout 3 and Fallout: New Vegas.

**Wabbajack lists tested** with the experimental builds (installed from download to the new instance
opening in MO2 Revamped, on Windows 11; none played in game yet):

| List | Game | Size | Result |
|---|---|---|---|
| Baldur's Gate 3 Enhanced Edition | Baldur's Gate 3 | 282 archives, 1.7 GB | Installed with no failures (732 files); opens with the author's profile, load order, separators, colours and programs. Not played. |
| Viva New Vegas | Fallout: New Vegas | 146 archives, 3 GB | Installed with no failures; opens with the author's profile. Its guide's manual steps are still needed, as with Wabbajack. Not played. |
| The Midnight Ride | Fallout 4 | 86 archives, 5.8 GB | Installed with no failures (5,444 files); a 2.26 GB download paused and continued gave the same file. Not played. |
| Waters of Life | Fallout 3 | 148 archives, 4.5 GB | Installed with no failures (10,345 files), including a Google Drive file taken from the browser's Downloads folder. Not played. |
| Mini Wildlander | Skyrim Special Edition | small | Installed with no failures (216 files), with its rebuilt BSA; the Stock Game note shows when its copy of the game isn't filled yet. Not played. |

**Not in this beta:**
- MergedPatch (zEdit merges) in Wabbajack lists, and the MO2 plugins a list
  brings (they are set aside and named in the summary);
- installing or updating Nolvus itself (use the Nolvus Dashboard);
- the Microsoft Store / Game Pass editions.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.14.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data (beta 13 or the experimental 1.1.0-dev.3).
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Wabbajack lists

Tools > Wabbajack lists opens a `.wabbajack` file, or browses Wabbajack's own
gallery, and installs the list into an instance of its own, without
Wabbajack:

- shows what the list needs and which archives are already downloaded;
- downloads its archives (Nexus, Wabbajack's CDN, direct links; Google Drive
  and other files from the browser's Downloads folder), with pause and
  resume, and checks every file by its hash;
- installs it into a new instance with the author's profile, load order,
  categories, notes, colours and programs, fixed for this PC; Stock Game only
  when it really holds the game;
- a summary of what was done and what the guide still leaves by hand.

Back up your saves and your downloads folder, install a list into a new
folder, and please report what fails, with the list's name and, if possible,
`.mo2r-install\summary.txt` from the install folder.

### New since the experimental dev.3

- **Manual steps of a list:** Tools > Wabbajack lists > Manual steps of this
  list (and a button on the notice a new list instance shows) names the folders
  a list leaves for its guide ("__Files Requiring Manual Install", "__Vanilla
  BSAs Patcher"...) and what each needs. It works for instances installed
  before, too.
  - Game-file folders: a button copies their files into the game folder as
    they are laid out (for example `nvse_loader.exe`,
    `Data\NVSE\nvse_config.ini`). Files they replace are backed up first to
    `.mo2r-install\game-backup\<time>`; a failure on the way puts back what was
    already replaced. Identical files are skipped.
  - Patchers run only from their own button, after a question that says MO2
    doesn't check what they do.
  - Other folders (DXVK versions, Steam overlay variants, GECK) are named with
    a button to open them; the guide says which one to use.
- **The list's notes in the instance:** an instance made from a list keeps the
  list's name, guide, the game version it was made for and the folders its
  guide has you copy by hand. The first time it opens, a notice names those
  folders and compares the game version.
- **Game version before Run:** when the game isn't the version the list was
  made for (Fallout 3 updated to 1.7.0.4 while the list's FOSE wants 1.7.0.3),
  MO2 asks whether to start anyway, once per session.
- **A screen size the screen can't show:** before a Gamebryo/Creation game
  starts, a prefs INI asking for a window larger than the screen, or a full
  screen mode the display doesn't list, gets the screen's size. Viva New Vegas
  stopped with "Failed to initialize renderer" after asking for 2560x1440 on a
  1920x1080 screen.
- **Stock Game:** when the list brings the game's exe into its own copy of the
  game (Wildlander) and that copy still isn't the game at the end, the summary
  says the install didn't finish and should be run again, instead of telling
  you to fill it by hand.

## From beta 13

### Nolvus

- **Find Nolvus:** Instances finds your Nolvus install, in `C:\Nolvus` or a
  folder you pick.
- **Open in MO2 Revamped:** makes a linked instance. It uses Nolvus' mods and
  game but its own copy of the profiles, so the Nolvus Dashboard keeps updating
  the list.
- **Transfer to MO2 Revamped:** turns a Nolvus install into a normal instance.
  You can move it (seconds on the same drive), copy it (sizes and free space
  are shown first) or keep it in place. Every step can be undone.
- **Warnings before running:** plain warnings for Nolvus requirements, which
  never block.
- **Downloads:** the Nolvus archives show as a read-only group.

### Faster with very large lists

Measured with Nolvus Awakening (3,597 mods, ~3,480 plugins, 546,000 files):

- **Turning all mods back on:** 24.6 s → 4.2 s.
- **Run:** the window no longer freezes while the virtual folder is prepared
  (14.5 s → about 1 s). Cancel works.
- **Data tab, first search letter:** 7.0 s → 1.0 s, with ~250 MB less memory.
- **Mod information for a mod of 64,000 files:** 0.85 s → 0.29 s.

Compared with the original Mod Organizer 2.5.2 that Nolvus installs, on the same
list (medians of three alternating runs each, warm disk):

- **Start until the list shows:** 6.1 s → 2.9 s.
- **Turning one mod on or off:** 1.2–2.1 s → 0.23–0.35 s.
- **Turning all mods back on:** 29.6 s → 8.7 s.
- **Refresh (F5):** the window is blocked for 2.6 s → 1.0 s.
- **Switching profiles:** the window is blocked for 3.6 s → 1.3 s.
- **Run:** the window is blocked for 9.1 s → 0.8 s. The whole start takes
  about 2 s longer (14 s instead of 12 s).
- **Memory after using everything:** 2.0 GB → 1.7 GB.

### Nexus

- **Request limit:** when Nexus answers "too many requests" (429), MO2 pauses
  and goes on by itself, with one notice, and no mod is marked as failed.
  Collections now make fewer requests.
- **Identification:** MO2 Revamped names itself to Nexus as MO2 Revamped.
- **Connect to Nexus** (website sign-in) is hidden until Nexus registers MO2
  Revamped's own sign-in.
  - Until then, enter your personal API key in **Settings > Nexus**.
  - A key you already saved keeps working.
- **Endorse:**
  - explains each refusal in one sentence;
  - is hidden for the mod's author;
  - never sends twice.
- **Adult collections** are hidden in the collection browser unless you ask
  for them.

### Also in beta 13

- **Collections:**
  - a Wabbajack-style window that stays open with live progress;
  - one notice to start and one to end, none per mod;
  - a free-space check before starting.
- **Notices:** they never take the focus or cover a dialog's buttons. The
  Notifications window explains its checks for your game.
- **Downloads:** grouped by origin or into your own categories, and large
  folders open about four times faster.
- **Baldur's Gate 3:** LSLib updates, and JSON to YAML works. Reparsing pak
  metadata is eight times faster.
- **Interface:** the floating log, windows that open off screen, the plugin
  counter and the plugin Name column were fixed. The mouse wheel no longer
  switches tabs.
- **Help:** a help guide, and a bug report that hides your personal data.

## Known issues

- Dragging downloads between groups with the mouse may not drop. Use
  **Move to category** in the menu instead.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.14/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, Nolvus, FromSoftware or
Bandai Namco Entertainment.
