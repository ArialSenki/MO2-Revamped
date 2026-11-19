MO2 Revamped 1.1.0 beta 15 is the **newest version**, still a beta. It is
beta 14 plus installing **several mods at once**, archives **dropped from
Explorer**, a **Nexus Collections gallery** laid out like the Wabbajack one,
smoother corners and a new **OLED Black** theme.

## Beta status: what works and in which games

This is a **beta**. It works for daily use in the games below, but expect bugs.
Back up your saves and your mods folder first, and please report what fails.

**Tested by hand with the real game and mods** (beta 13; Nolvus again with a beta 15 test build)

- **Skyrim Special Edition**: SKSE64 2.3.1 started from MO2.
- **Nolvus Awakening 6.0.21** (3,597 mods), as a linked instance. The game
  started from MO2 with SKSE and its 200 plugins loaded, and reached the main
  menu again with a beta 15 test build.
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

- `MO2-Revamped-Setup-1.1.0-beta.15.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data (beta 14 or earlier).
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Install several mods at once

- **Downloads:** select several (Ctrl or Shift) and choose **Install N
  selected**, press Enter, or double-click one of them. They install one after
  another, top to bottom.
- **Install a mod...** lets you pick several archives.
- Each installer (quick, manual, FOMOD, BAIN) opens for its own mod. Cancel one
  and MO2 asks whether to go on with the next or stop. At the end one notice
  sums up installed, skipped and failed mods, and the virtual file tree is
  rebuilt once.
- A queue can't start while a collection is installing.

## Drag archives from Explorer

- **On the mod list:** one or more archives install where you drop them
  (sorted by priority), or as the last mods anywhere else.
- **On Downloads:** MO2 asks Copy / Move and adds them to the downloads folder
  without installing.
- While you drag, a soft hint shows **Drop to install** or **Drop to add to
  Downloads**, in the theme's accent colour.
- Several downloads dragged onto the mod list now all install (before, only
  one did).
- Explorer can't drop into a program running as administrator: start MO2
  normally.

## Nexus Collections gallery

- Laid out like the Wabbajack gallery: search as you type, a game picker with
  the number of collections of each game, sort order and adult content.
- **All games** shows a section per game, the instance's game first, with
  **See all N collections of ...** for each.
- Every game MO2 Revamped supports that has collections on Nexus (43 today).
  Collections of another game can be viewed and opened on Nexus, not installed
  into this instance.
- Thumbnails in both galleries sit centred in one box, so names line up.

## Window sizes

- The Collections and Wabbajack windows no longer open maximized: the first
  time they take 80% of the screen, centred on MO2. They remember their size,
  place and maximized state, fitted to the current screen.
- The floating Log always undocks at its default size and place. Maximize and
  restore brings it back to its own size, and it no longer shrinks a little
  every session.

## Smoother corners

- Menus, drop-down lists, autocompletion and tooltips: one clean Windows 11
  rounded corner with a single border in the theme's colour, instead of two
  curves and two borders.
- The floating Log has no black corners.
- The status bar has no line across the window under the cards.

## OLED Black theme

Settings > Theme > **OLED Black**, applied live:

- true black (#000000) window, title bar, toolbar, gaps, status bar and
  dialogs; near-black cards, lists, menus and Log so they still stand apart;
- off-white text, a calmer accent and dimmer status colours;
- for burn-in: the Run button and default buttons are outlines on a dark tint,
  the active tab line, progress bars and switches are dimmed;
- Mica is off in this theme because it greys the black.

It is built on top of Dark, so fixes to Dark reach it too.

## Fixes

- **nxm links:** MO2 no longer freezes for five seconds before "Download
  started".

## From beta 14

Native Wabbajack lists and everything else from beta 14 are unchanged; see the
[beta 14 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.14).

## Known issues

- Dragging downloads between groups with the mouse may not drop. Use
  **Move to category** in the menu instead.
- OLED Black has not been checked on every dialog of every plugin yet; please
  report a grey or bright surface you find.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.15/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, Nolvus, FromSoftware or
Bandai Namco Entertainment.
