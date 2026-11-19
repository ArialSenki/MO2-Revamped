# Changelog

## 1.1.0-beta.15 (pre-release) — November 19, 2026

Beta 14 plus installing several mods at once, archives dropped from Explorer,
a Nexus Collections gallery laid out like the Wabbajack one, smoother corners
and a new **OLED Black** theme.

### Install several mods at once

- **Downloads:** select several (Ctrl or Shift) and choose **Install N
  selected**, press Enter, or double-click one of them. They install one after
  another, top to bottom.
- **Install a mod...** lets you pick several archives.
- Each installer (quick, manual, FOMOD, BAIN) opens for its own mod. Cancel one
  and MO2 asks whether to go on with the next or stop. At the end one notice
  sums up installed, skipped and failed mods, and the virtual file tree is
  rebuilt once.
- A queue can't start while a collection is installing.

### Drag archives from Explorer

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

### Nexus Collections gallery

- Laid out like the Wabbajack gallery: search as you type, a game picker with
  the number of collections of each game, sort order and adult content.
- **All games** shows a section per game, the instance's game first, with
  **See all N collections of ...** for each.
- Every game MO2 Revamped supports that has collections on Nexus (43 today).
  Collections of another game can be viewed and opened on Nexus, not installed
  into this instance.
- Thumbnails in both galleries sit centred in one box, so names line up.

### Window sizes

- The Collections and Wabbajack windows no longer open maximized: the first
  time they take 80% of the screen, centred on MO2. They remember their size,
  place and maximized state, fitted to the current screen.
- The floating Log always undocks at its default size and place. Maximize and
  restore brings it back to its own size, and it no longer shrinks a little
  every session.

### Smoother corners

- Menus, drop-down lists, autocompletion and tooltips: one clean Windows 11
  rounded corner with a single border in the theme's colour, instead of two
  curves and two borders.
- The floating Log has no black corners.
- The status bar has no line across the window under the cards.

### OLED Black theme

Settings > Theme > **OLED Black**, applied live:

- true black (#000000) window, title bar, toolbar, gaps, status bar and
  dialogs; near-black cards, lists, menus and Log so they still stand apart;
- off-white text, a calmer accent and dimmer status colours;
- for burn-in: the Run button and default buttons are outlines on a dark tint,
  the active tab line, progress bars and switches are dimmed;
- Mica is off in this theme because it greys the black.

It is built on top of Dark, so fixes to Dark reach it too.

### Fixes

- **nxm links:** MO2 no longer freezes for five seconds before "Download
  started".

## 1.1.0-beta.14 (pre-release) — November 18, 2026

Beta 13 plus **native installation of Wabbajack modlists**, which until now
was only in the separate experimental builds (1.1.0-dev.1 to dev.3). The
experimental file is retired: everything it had is in this beta.

### Wabbajack lists

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

The [1.1.0-dev.1](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-dev.1)
to [dev.3](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-dev.3)
entries below have the details. Tested lists (installed with no failures,
then the new instance opened; none played in game yet): Baldur's Gate 3
Enhanced Edition, Viva New Vegas, The Midnight Ride (Fallout 4), Waters of
Life (Fallout 3) and Mini Wildlander (Skyrim SE). Not done yet: MergedPatch
(zEdit merges) and the MO2 plugins a list brings.

### New since dev.3

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

### Merged with beta 13

- Downloads keep beta 13's settings (never from the cache; a server that stops sending
  counts as a failure) and the Wabbajack line's retry over HTTP/1.1 for replies that end
  short.
- Closing MO2 during a Wabbajack install asks first; closing it during a
  collection install still waits, as in beta 13.
- A Wabbajack list that started MO2 is kept when the instance has to be picked
  again, as an nxm link is.

## 1.1.0-beta.13 (pre-release) — November 18, 2026

A full audit of every window and option, two rounds of fixes from hands-on
testing, Nolvus support and much faster handling of very large lists.

### Nolvus

- Nolvus installs are found in Instances (default `C:\Nolvus`, or a folder you
  pick), with their version, variant and mod count.
- **Open in MO2 Revamped** makes a linked instance: it uses Nolvus' mods and
  STOCK GAME but keeps its own copy of the profiles, so Nolvus' MO2 folder and
  profiles stay untouched and the Nolvus Dashboard still updates the list.
  *Copy the Nolvus profiles again* refreshes the copy after an update.
- **Transfer to MO2 Revamped** turns a Nolvus install into a normal instance:
  move (seconds on the same drive), copy (shows sizes and free space first) or
  keep in place. Every step is logged and can be undone; a failure rolls back.
- Before a program runs, plain warnings for Nolvus requirements (STOCK GAME,
  the downgraded game and SKSE, the chosen ENB's files, profile settings). They
  never block.
- The Nolvus archives show in Downloads as their own read-only group, without
  copying anything.
- No first-start questions in a Nolvus instance; the plugins a profile names
  but no mod has are one log line, not hundreds.

### Very large lists (tested with Nolvus: 3,597 mods, ~3,480 plugins, 546,000 files)

- Turning all mods back on: 24.6 s → 4.2 s; 100 selected mods on: 0.94 → 0.43 s.
- Run: the virtual folder is built off the interface thread, so the window
  stays responsive (frozen 14.5 s → about 1 s) and Cancel works.
- Data tab: the first search letter 7.0 → 1.0 s, and ~250 MB less memory.
- Mod information of a 64,000-file mod: 0.85 → 0.29 s. Refresh, profile
  switches and sorting long lists are faster too.
- Plugin headers are read in parallel and cached; installing many small mods
  is about five times faster.

### Nexus

- **Request limit (429):** MO2 pauses its Nexus requests and goes on by itself
  when Nexus allows, with one calm notice; no mod is marked as failed.
  Collections no longer check each installed mod for updates, so they make
  fewer requests.
- MO2 Revamped now names itself to Nexus as MO2 Revamped (based on Mod
  Organizer 2.5.2), as Nexus' API policy asks.
- **Connect to Nexus** (website sign-in) is hidden until Nexus registers MO2
  Revamped's own sign-in; it used Mod Organizer 2's. Enter your personal API key
  in Settings > Nexus. A key you already saved keeps working.
- The API key never follows a redirect to another address.
- The collection browser hides adult collections unless Adult content is ticked.
- **Endorse** endorses the MO2 Revamped page in Modding Tools, is hidden for the
  mod's author, says thanks once if you've already endorsed, never sends twice,
  and explains each refusal in one sentence. Withdrawing is in the menu.

### Collections

- A Wabbajack-style collection window and browser, opened maximized.
- One notice when a collection starts or goes on and one when it ends, none per
  mod; errors go to the collection's list.
- The window stays open with live progress; Run or a profile switch asks first
  whether to pause it.
- Checks free space before starting and offers to resume an unfinished install.

### Notices

- Notices never take the focus or cover a dialog's buttons, and clicks pass
  outside the card.
- The Notifications window (the bell) explains its checks with an example for
  the instance's game; the "files in Overwrite" text is right for Cyberpunk 2077,
  Baldur's Gate 3 and other games.
- No question about the wiki when creating an instance.

### Downloads

- Grouped by origin (each collection or list, Nexus, other sites) or into your
  own categories, with Move to category in the menu. Large folders open in
  2.8 s instead of 11 s.
- The columns no longer shift when the scrollbar appears.
- False "Unknown error (0)" fixed; a stalled download is retried after 60 s;
  nxm links that arrive while MO2 starts are no longer lost.

### Game support

- **Baldur's Gate 3:** LSLib updates in place; Convert JSON to YAML works without
  PyYAML and no longer stops at 98%; Reparse Pak Metadata 10.3 → 1.3 s.
- **Skyrim SE:** ShaderCache, KiLoader (ENB Extender) and SunHelm mods no longer
  show "No valid game data".
- **Cyberpunk 2077:** mods wrapped in one extra folder are fixed on install.

### Windows and interface

- The log panel floats and docks cleanly, with column titles over its list.
- A window restored partly off screen is moved back inside it.
- The plugin counter shows the real total (it was cut to its last digits), and
  the plugin Name column keeps its room in a narrow window.
- The mouse wheel no longer switches tabs or changes an unfocused choice.
- A thin loading bar on the status bar's line; Profiles no longer reloads on
  close without changes; About windows updated; the Endorse button is visible.
- A help guide for MO2 Revamped's own parts, and a bug report that removes your
  user name, PC name, emails and keys and shows what it will share first.

### Fixes

- Crashes fixed: closing with the floating log still resizing, the Data tab
  while it builds, and Finish in "Where is the game now?".
## 1.1.0-dev.3 (experimental) — November 16, 2026

**Experimental.** This build is 1.1.0 beta 12 plus **native installation of
Wabbajack modlists**, which is still being tested. Beta 12 is the recommended
version; use this build only to try Wabbajack lists. Since dev.2 it has the
beta 12 changes and these fixes and features for Wabbajack lists.

### Wabbajack lists

- **Browse Wabbajack's gallery:** Tools > Wabbajack lists > Browse Wabbajack
  lists… shows every list from Wabbajack's own gallery, as Browse collections
  does for Nexus. It has search, the managed game first, and unofficial and
  adult lists behind their own boxes, as in Wabbajack. Sizes and dates can be
  sorted, and each list has its readme and links. **Open the list** downloads
  the `.wabbajack` file from Wabbajack's servers, checks it and opens it; a
  file already downloaded is checked and used again. Lists down for
  maintenance or with failing archives are marked.
- **The author's mod list as the author sees it:** the list's categories, the
  Notes column (where authors write "Optional" and similar) and the mods'
  colours now show in the new instance.
- **Baldur's Gate 3 lists start with their mods in the load order.** Baldur's
  Gate 3 Enhanced Edition started with only the base game: the list author's
  cache entries and some `meta.ini` files weren't read. A `&` in a mod's name
  no longer breaks `modsettings.lsx`.
- **The list's programs work on this PC:**
  - programs that point at a folder (Larian Launcher), or at a path on the
    author's machine (the author's own xEdit), are left out, and the summary
    names them;
  - the list's "Explore Virtual Folder" used an Explorer++ that is never
    installed, so MO2 Revamped's own is used;
  - a working folder on the author's machine is dropped, and `<game>\Data` is
    changed to the game's real data folder only when that folder doesn't
    exist (Baldur's Gate 3 keeps it);
  - a program the guide has you copy later, inside the install or the game
    folder, is kept.
- **Stock Game:** the list's own copy of the game becomes the instance's game
  folder only when it really holds the game. Otherwise the real game folder is
  used, and the summary says what to do once the guide has it filled.
- **The right profile:** when the list names none of its profiles, the instance
  starts with the list's first profile instead of an empty "Default".
- **Root folders:** mods with a `Root` folder no longer show as having no valid
  game data. MO2 Revamped puts their programs and DLLs in the game folder when
  the game starts (see beta 12), and the summary says so.
- **Windows that fit the screen:** the list window and the gallery open at a
  size that fits the screen they are on, and refit when moved to another
  screen. The very large size earlier versions saved is shrunk once.
- **Paused lists:** the list to reopen is kept per instance, so a paused list
  no longer opens in another instance. A list paused in dev.2 has to be opened
  again by hand once.
- **The install folder follows the instance name** until you pick a folder by
  hand. Install paths follow the same rules as collections: no names Windows
  refuses.
- **Downloads:**
  - a file another program keeps open stops as failed, with that reason,
    instead of being checked again forever;
  - a server that ignores where a paused download stopped starts it over, unless
    what it sends clearly isn't the file;
  - files from the browser are checked where they are, before being copied;
  - downloads that redirect to an address on this PC or its local network, or
    from https to http, are refused;
  - the space part-downloads already use counts in the free-space check.
- **Install checks:**
  - whole files are checked by their hash while they are copied;
  - a list that gives a file a broken hash fails that file instead of skipping
    the check;
  - parts of a list MO2 Revamped can't install yet are named in the summary;
  - the summary lists every failure (the log keeps the first 200).
- **Faster with big lists:** the window refreshes only the rows that changed,
  and the free-space and game checks aren't repeated on every change.
- **A list opened while the window has a question up** waits for the answer
  instead of closing the window under it.
- **Lists tested with this build** (installed in full with no failed files,
  then the new instance opened in MO2 Revamped; none was played in game):
  - Baldur's Gate 3 Enhanced Edition: 732 files, with its load order and
    programs;
  - Mini Wildlander (Skyrim SE): 216 files, with its rebuilt BSA and Stock Game;
  - The Midnight Ride (Fallout 4): a 2.26 GB download paused and continued,
    identical to the original;
  - Viva New Vegas: opened and closed cleanly.

## 1.1.0-dev.2 (experimental) — November 15, 2026

Fixes found by testing more Wabbajack lists after 1.1.0-dev.1. Everything in
dev.1 is still here, and beta 11 is still the recommended version.

- **Update notice:** 1.1.0-dev.1 showed "MO2 Revamped 1.1.0-beta.11 is
  available", because a "dev" version counts as older than a beta. Following it
  would go back to the beta and lose Wabbajack lists. An experimental build now
  offers a beta or a release only when it came out after the experimental build.
  Note that dev.1 itself still shows that notice; ignore it and update to dev.2.
- **Files from the browser with Internet Download Manager:** IDM and similar
  download managers save files into folders such as `Downloads\Compressed`.
  MO2 Revamped now watches the folders right inside your Downloads folder too,
  so those files are picked up.
- **The game's edition, without asking:** the edition is also read from the
  game folder's name. For example, "Fallout 3 goty" is Game Of The Year, so the
  new instance no longer asks for it on its first start.
- **Download errors in your language:** network errors such as "the server
  stopped answering" or "the file isn't on the server any more" are no longer
  shown in English. Qt's original text goes to the log.
- **Tested with two more lists:**
  - The Midnight Ride (Fallout 4): 86 archives, 5.8 GB, 5,444 files installed
    in 46 seconds, no failures.
  - Waters of Life (Fallout 3): 148 archives, 4.5 GB, including one from Google
    Drive taken from the Downloads folder; 10,345 files installed in 91 seconds,
    no failures.

  Both opened with the author's profile, mods and plugins.

## 1.1.0-dev.1 (experimental) — November 15, 2026

An **experimental** build, published apart from the beta. It has everything in
1.1.0-beta.12 (below; not released on its own), the collection fixes made since,
and a new feature still being tested: **installing Wabbajack modlists natively**.
Keep using beta 11 if you want the safer version. The update notice offers this
build only to people who choose the Development channel in Settings.

### Wabbajack modlists (new, experimental)

MO2 Revamped can read a `.wabbajack` file, download what it needs and install
it into an instance of its own, without Wabbajack or the original Mod Organizer
the list brings.

- **Open a list:** Tools > Wabbajack lists > Open a Wabbajack list…, or open a
  `.wabbajack` file with MO2 Revamped. Nexus collections and Wabbajack lists
  now have their own submenus in Tools.
- **Before downloading:** the window shows the list's image, author, version,
  game and links. It also shows the download and install sizes, where each
  archive comes from, the game files it takes from your game folder, and the
  free space on the drive.
- **Downloads:**
  - **Nexus:** Premium accounts download everything by themselves. Free
    accounts open each file's page and press "Slow download"; the list takes
    the link.
  - **Direct links and Wabbajack's own servers:** downloaded in parts, and each
    part is checked.
  - **Pause and continue:** a download picks up where it stopped. Files already
    in the downloads folder are recognised by their hash, and the game's own
    files are taken from the game folder.
- **Files only a browser can get** (Google Drive, Mega, MediaFire, ModDB,
  LoversLab and similar):
  - Double-click one to open its page.
  - MO2 Revamped watches your browser's Downloads folder and takes each file
    when it arrives, by its hash.
  - A file saved elsewhere goes in with a right click.
- **Install:** pick a name and a folder. The install:
  - extracts the archives, including BSA/BA2 and archives inside archives;
  - applies the list's patches and writes the list's own files;
  - rebuilds its BSA/BA2 archives and transforms textures (size, mipmaps,
    compression);
  - fixes the paths the list points at and sets your screen size in the game's
    INI files.

  Every file is checked by its hash.
- **The new instance keeps the author's setup:** the author's profiles, mod
  order, separators with their colours, INI files, programs and Stock Game
  folder. You are offered a restart into it.
- **What is left out:** the list's own Mod Organizer files aren't installed,
  because MO2 Revamped takes their place. MO2 plugins the list adds are set
  aside and named in the summary.
- **Summary:** `.mo2r-install\summary.txt` in the install folder lists what was
  done and names the folders the author leaves for you to copy by hand.
- **It stays out of your way while you play:**
  - MO2's notices go to the log while a list runs.
  - One sound plays when the downloads end.
  - Hashing runs at background priority, the install at a lower priority, and
    texture compression uses at most half of the processor.
- **Sounds the author kept uncompressed** in BSA/BA2 archives stay
  uncompressed. The games don't play them compressed.
- **Not done yet:**
  - MergedPatch (zEdit merges).
  - Installing the MO2 plugins a list brings.
  - Very large lists haven't been tried yet.
- **Tested with:**
  - Baldur's Gate 3 Enhanced Edition (282 archives, 1.7 GB) and Viva New Vegas
    (146 archives, 3 GB) installed with no failures, then opened with the
    author's profile, separators and colours.
  - A small Skyrim Special Edition sample that rebuilds a BSA.
  - Viva New Vegas still needs its guide's manual steps, as with Wabbajack.

### Downloads

- **Downloads that ended empty or short now continue.** Some downloads used to
  end with "Unknown error (0)", or ended early with the file taken as done.
  They now continue from where they stopped. Automatic retries use a connection
  of their own (HTTP/1.1), and the log records the details.
- **A download waiting for its retry** shows "Retrying…" in amber instead of a
  red "Error".

### Nexus collections (since beta 12)

- **Separators:** collection and category separators are made for every
  install path and reused when you continue or update. Empty old ones go to the
  Recycle Bin.
- **Quiet downloads:** no sound for each file, and one sound at the end.
- **Retries:**
  - Failed downloads are retried 3 times (after 5, 20 and 60 seconds), with a
    fresh link for Premium and the file's page again for free accounts.
  - Retry failed tries the rest.
- **Order:** category grouping keeps the author's order. Mods whose rules cross
  categories go to "Patches and overrides" at the end.
- **Large collections are much faster:** grouping 3,000 mods by category takes
  56 ms instead of 8.6 s. The window and the downloads list update without
  freezing.

## 1.1.0-beta.12 (pre-release) — November 16, 2026

### Nexus collections, complete

- Binary patches from the collection are applied after a mod installs, only to
  the file they were made for (checked by its checksum and size).
- Exact copies of the author's install: files outside the collection's list are
  hidden (it can be undone) and differences are listed.
- File overrides and INI tweaks from the collection. INI tweaks go only into the
  game's own INI files, in the profile's own copies; MO2 asks before changing
  the game's INI files in Documents.
- Update a collection to a new revision: only what changed is downloaded; mods
  it no longer has go to the Recycle Bin after asking.
- Browse collections: Nexus or Tools > Browse collections lists the game's
  collections, with search and sorting.
- Each mod goes under a separator for its category, in the collection's own
  order; only mods whose order rules cross categories and share files go to
  "Patches and overrides" at the end. Separators are made the same way for a
  new instance, a new profile or the current one, without duplicates.
- Failed downloads are retried on their own (three times) and there's a Retry
  failed button; no sound for each file while a collection installs.
- Much faster with large lists (thousands of mods): ordering, grouping and the
  download checks no longer freeze the window.

### Linux and Steam Deck

- MO2 no longer crashes at start under Wine or Proton (PortProton, Steam Deck)
  when Qt has no screen yet. If it still fails for you, please send the crash
  dump MO2 writes (ModOrganizer-*.dmp).

- Safety fixes from an audit: patches can't write outside their memory or use
  it all, curator text can't load files from your PC or the network, nothing is
  applied to your own mods after a Merge, and the Nexus button opens Nexus in
  one click again.

### Root folders without Root Builder

- A mod's `Root` folder (Root Builder's layout, used by many Wabbajack lists and
  mods) goes to the game folder through MO2's virtual folder, with no plugin.
  This covers Bethesda games, Morrowind, Baldur's Gate 3 and other games whose
  data folder is the game folder, `Data` or `Data Files`. With the Root Builder
  plugin turned on, it stays in charge and MO2 leaves Root folders to it.
- Files the virtual folder can't give a game are handled at launch, keeping the
  game folder as it is wherever that's reliable:
  - a script extender's loader in a Root folder (`skse64_loader.exe`,
    `nvse_loader.exe`): MO2 starts the game with the extender's DLL for the
    game's version loaded first;
  - DLLs the game exe imports (ENB or ReShade's `d3d11.dll`, DXVK's `d3d9.dll`,
    NVHR's `d3dx9_38.dll`): loaded from the mod's own file;
  - where that isn't reliable (an exe wrapped by Steam's DRM, an extender
    whose loader has to start it, an ENB preset in another mod, a mod that
    replaces the game exe such as FNV4GB), the files are copied into the game
    folder for that run. Anything they replace is backed up, and everything is
    put back when the game closes. If MO2 closes first, it's put back on the
    next start. The log says how each file was handled.
- A program found only in a mod's Root folder runs from there, with the game
  folder as its working folder.
- Files a program creates in the game folder (logs, caches) stay there, as
  without Root folders; Sort and shortcuts leave no empty `Overwrite\Root`.
- A mod with a `Root` folder no longer shows as having no valid game data
  (Bethesda games).
- Cyberpunk 2077 keeps loading CET and RED4ext installed at a mod's top level
  when Root Builder is on.
- The profile is saved before the launch is planned, so a mod
  turned on just before Run counts.
- `nvse_loader.exe -editor` (GECK) and `-altexe` start what they
  name, not the game.
- Tools in their own folder (LOOT, xEdit) aren't planned at all.
- A copy that fails stops the start, and what was already copied
  is put back. The exception is a game folder that can't be written to: then
  the game starts without those files, and MO2 says so once per session.
- Several instances sharing one game folder: a file another
  instance copied there is never backed up as the game's. The program isn't
  started, and MO2 names the instance that has to restore it.
- A file changed by something else while copies were out is kept
  for you, with its backup moved to a `conflicts` folder and logged.
- Unchanged mod files aren't read again on every start (hashes
  kept by path, size and time).
- When a loader exits right after starting the game, MO2 waits
  up to 15 seconds for the game before putting the copies back.

### Skyrim Special Edition, Fallout 4, New Vegas and Fallout 3

- **Fallout 3:**
  - The Fallout Launcher program is found again: Steam's Game Of The Year
    edition has `Fallout3Launcher.exe`.
  - The Steam app ID is 22370 for a Game Of The Year install, even when the
    Regular edition was picked.
  - FOSE co-saves (`.fose`) move and are deleted along with their save.
- **Skyrim SE:** saves with a newer header version still show their screenshot
  and details.
- **Fallout 3 and New Vegas** are also found from GOG and from any Steam
  library, before the game has run once. The new-instance wizard finds Steam's
  Fallout 3 GOTY.
- **Skyrim SE and Fallout 4:** with "show mods installed outside MO" off, the
  DLC and Creation Club plugins show in the mod list again.
- **Mod data:** Skyrim SE and Fallout 4 accept `Seq` and `LODSettings`, and
  Fallout 4 accepts `Vis` (previs).
- **Collections:** mods Vortex puts in the game folder (script extenders, ENB)
  are installed into the mod's `Root` folder, with the collection's patches,
  file lists and overrides following them.

### Saves

- The Saves tab and the transfer window show each save's time in your local
  time; they showed UTC.
- Skyrim SE, Skyrim VR and Enderal SE no longer take 6 hours off the time, which
  was right in one time zone only. Oblivion and Nehrim, which store local time,
  show it correctly.

### Plugin list

- Moving plugins with Ctrl+Up/Down saves `plugins.txt` and `loadorder.txt`, as
  a drag does, so xEdit or LOOT started outside MO2 see the new order.
- A plugin past 4096 light or 256 medium plugins is marked as one the game won't
  load.
- A plugin whose file differs only in case is no longer dropped on a refresh.
- Switching profiles writes a pending plugin change to the old profile at once,
  and a save waiting for a refresh is never written into another profile.

### Baldur's Gate 3

- Mods laid out for other MO2 plugins (as Wabbajack lists install them):
  - top-level `Public`, `Generated`, `Localization`, `ScriptExtender`, `Video`
    and `Mods/<folder>` go to the game's `Data`;
  - `SE_CONFIG` goes to Script Extender's folder;
  - paks in `PAK_FILES` go with the others.
  These mods count as valid.
- Loose folders of mods without a `Data` folder were dropped without a notice.
  This affected loading screens, `Generated` assets and similar mods.
- Profile saves are mapped on a first start, when the game's saves folder
  doesn't exist yet.
- Script Extender and native-mod DLLs (also in `Root\bin`) are loaded into the
  game only, not into the Larian Launcher. `modsettings.lsx` is also written
  when the Larian Launcher is started from MO2.
- A native mod's `.toml` goes to `bin` with its DLL.
- `meta.ini`:
  - a pak with spaces in its name is no longer read again on every start;
  - a list author's load-order entry is found;
  - names with `&` no longer break `modsettings.lsx`;
  - values with commas or quotes are kept as written.
- Paks built from a mod's working folder are rebuilt after their files change,
  also when files are deleted or renamed. Working folders with "bin", "Data" or
  "Mods" in their name are packed.
- Two paks with one name give the mapping and the load order the same file.
- A pak entry another version of the plugin wrote no longer counts as an
  override: lists such as Baldur's Gate 3 Enhanced Edition started with only the
  base game in `modsettings.lsx`. `meta.ini` keeps its keys' case, so a mod is no
  longer left out over a `gamename`/`gameName` pair.
- The game's own DLLs are recognised whatever the case of their names.
- BG3 Mod Manager: a notice with the download link when the .NET Desktop
  Runtime it needs is missing.
- Quicker start: each mod's folder is walked once, there's no fixed wait after
  the load order is written, and the mapping window repaints less often.
- After a program, only the empty folders MO2 made are removed; your own empty
  folders in Overwrite and AppData stay.
- A cancelled start leaves nothing for a later one.
- A `meta.ini` with odd bytes or keys before its first section is read
  instead of leaving the mod out.
- The .NET check uses the version BG3 Mod Manager (and Divine) asks for.
- Mods whose paks are already known are read at once.

### Nexus collections

- In an exact copy of the author's install, a file the game's installer moved
  (`DWrite.dll` into `bin`, `Public` into `Data\Public`) is no longer hidden as
  extra.
- Closing the collection window while it checks files waits until the check
  ends.
- Notices held back while a collection installs are counted and shown once it
  ends. The ones you must see, such as Settings' restart notice, still show
  during the install.
- Esc pauses a running collection, as closing the window does.
- Patches and the files' checksums are applied and checked off
  the window's thread, so the window keeps painting.
- The final ordering checks the collection is still running in
  its profile, and pauses instead of applying anything otherwise.
- Fewer disk reads for file overrides in large collections.
- A collection's patches are only taken from that mod's own
  folder in the collection's archive, and a folder you allowed to be replaced is
  replaced only for the mod being installed.

### Performance with large lists

- Conflicts are worked out faster: each other mod is looked up once, not once
  per shared file.
- Plugins are matched to their archives with a binary search, and the master
  order fix updates the list once.
- A refresh converts the active archives once, not again for every mod.
- Nexus update checks and tracked mods no longer slow down with thousands of
  mods, and removing a collection's dropped mods reads each profile once.
- The log has start timings (virtual folder, profile and plugins, Root copies).

### Settings

- **Interface size** (80 to 125%) in Settings > General, applied at the next
  start. Programs started from MO2 (LOOT, Qt tools) are no longer scaled with
  it.
- **Smooth text in grey** for OLED screens, without ClearType's coloured edges.
- The collection window opens at a size that fits the screen it's on, and fits
  another screen when moved there.

### Other fixes

- A failed download that can't be resumed shows as paused, with its reason,
  instead of "Retrying…" forever.
- A program started with another profile reads that profile without repairing
  or migrating it.

## 1.1.0-beta.11 (pre-release) — November 14, 2026

- The Tools menu no longer opens empty in games whose tools all belong to other
  games (Cyberpunk 2077, Baldur's Gate 3): it says so, and it now also has
  Download a Nexus collection.

## 1.1.0-beta.10 (pre-release) — November 14, 2026

### Nexus collections

MO2 Revamped can now download and install Nexus collections (modlists), built
from scratch for MO2.

- Open a collection with "Download with mod manager" on its Nexus page, or paste
  its address in Nexus > Download a Nexus collection.
- Install it into a new instance (the default, apart from your other mods, as
  Wabbajack and Nolvus lists are), a new profile or the current profile.
- Premium: everything downloads and installs after Start, with no more clicks.
  Free accounts: MO2 opens each file's page on Nexus and you press "Slow
  download" there, as Nexus requires; the next page opens on its own.
- The mods install with the collection's FOMOD choices, in its order, under a
  separator, with their categories and a separator per category when the order
  allows it; Bethesda games get the plugin order. A summary lists what needs a
  look.
- Progress is saved: close MO2 and continue later.
- Tested with Night City FPV Collection (10 mods) and Welcome to Night City
  (364 mods) on Cyberpunk 2077 2.5: CET, RED4ext and its plugins loaded, and the
  game folder stayed unchanged. Not yet supported: binary patches, exact copies
  of the author's install and INI tweaks.

### Other changes

- Notices stack tighter, at most four, and stay above the taskbar.

## 1.1.0-beta.9 (pre-release) — November 12, 2026

### Which games were tested

Tested by hand with the real games, on Windows 11:

| Game | What was tested | Result |
|---|---|---|
| Skyrim Special Edition, Fallout 4, Fallout 3, New Vegas | Script extenders and the borderless window (in beta 8) | Work |
| Cyberpunk 2077 (2.5) | Cyber Engine Tweaks, RED4ext, ArchiveXL and TweakXL, installed and started from MO2 | Work: CET starts, RED4ext loads ArchiveXL and TweakXL, and nothing is copied into the game folder |
| Baldur's Gate 3 | BG3 Script Extender and a .pak mod (Community Library) | Work: the Script Extender loads and the .pak goes into the load order |
| Elden Ring, Nightreign | The changes below come from a code audit; they start in MO2 with a test instance | Not played yet with this version |

Oblivion, the original Skyrim, Morrowind and Starfield start in MO2 with a
test instance but weren't played with mods. Tale of Two Wastelands, Enderal
and the VR versions weren't tried.

### Elden Ring and Nightreign

- **Closing the game:** two of the game's threads could close it at the same
  moment, and MO2's file layer was undone twice at once. That is the most likely
  cause of the rare crash as the game closes. Now only the first one goes on.
- **Starting the game:** MO2's hooks wait until the game's own start-up code has
  run, so they no longer collide with ELDEN RING Reforged's Arxan removal. The
  "start minimized" option can no longer freeze the game at start, and an
  internal error in the me3 compatibility layer no longer closes the game.
- **Saves:** with a mod profile's saves hidden, MO2 checks for the game closing
  every half second in every case (it was every 5 seconds when MO2 wasn't
  waiting for the game), so the Steam save is back sooner. Save isolation checks
  again that the game is closed after you confirm each action. A restore that
  was cut short is finished only if every file is still as it left it, and a
  new one doesn't start until that is settled. The new instance wizard sets an
  old `.bak` aside instead of pairing it with the new save.
- **Seamless Co-op:** the build for one game is never loaded into the other,
  and installing it in the wrong game says so.
- **Native DLL Profile:** it shows DLLs added to a mod after it was installed,
  and only those the game would load.
- Nightreign writes its own bridge logs (`NightreignMO2Bridge.log`). To help find
  the cause of the Damage Competition start crash, the bridge log now lists the
  mods' DLLs as they load and unload.
- Smaller fixes: names Windows can't use are refused in "Arrange by hand", a
  manual Reforged merge removes the old online release, texts name the real tools
  and buttons, and old BootBoost cache files are removed after 60 days.

### Cyberpunk 2077

- Cyber Engine Tweaks and RED4ext's plugins (ArchiveXL, TweakXL and the
  others) now work: MO2 loads CET's `version.dll` and RED4ext's `winmm.dll`
  as the game starts. Before, Windows' own `version.dll` was used and RED4ext
  started too late to load its plugins. Their files are no longer moved to a
  `root` folder, which only the Root Builder plugin used; mods installed with
  that folder need to be installed again.
- Saves with accented or non-Latin names show correctly, the folder for saves
  follows a Saved Games folder you moved, and the REDmod load order setting
  applies without restarting MO2.

### Baldur's Gate 3

- Mods whose paks couldn't be read in time are named in a message instead of
  silently left out of the load order; a pak that can't be read no longer
  holds up the others. The load order follows the current profile, and a mod
  with YAML settings no longer breaks every other mod's mapping.

### Setup

- Uninstalling removes the install folder. Before, the folder stayed after an
  uninstall because the setup's own window kept it in use.
- Uninstalling removes only what is MO2's: its files and what it writes while
  running. Files of your own in that folder, and instance data in folders you
  chose, stay and are listed. With your instances in AppData, they are kept
  unless you choose to remove them, and they can't be removed while another
  installation uses them.
- Shortcuts, the Start menu folder and the Nexus link registration that point
  to the removed folder are removed too, and nothing that belongs to another
  installation or program.
- If a file is in use, the uninstall stops with the program still in place and
  can be run again.
- New options: close the setup when finished, open Nexus links with this MO2,
  close MO2 when updating. A summary before starting, Open log and Open folder
  buttons when something fails, warnings for Program Files and for older
  versions, and silent switches for scripted installs (see the README).
- Each installation has its own name in Windows' Installed apps.

### Games and instances

- An instance whose game folder moved or was uninstalled asks for that game's
  new folder, and can no longer be switched to another game by mistake. The
  edition (Steam, GOG, Epic) is asked again when the folder changes.
- A game that was never started no longer makes MO2 write INI files, saves and
  a Data folder into its own folder.
- Plugins with names like `Ñandú.esp` no longer turn themselves off (all
  Bethesda games).
- Found in more places: Fallout 4 from GOG and from a Steam library MO2
  didn't know, Enderal from Steam; Nehrim is no longer mistaken for Oblivion.
  Fallout 4 VR's script extender is recognised, and Starfield's Shattered
  Space files count as part of the game.
- Profiles: "Last used" is when you chose the profile; a profile's missing
  game settings are restored with a clear notice; its INI files go to the
  Recycle Bin when you turn that option off. Renaming a profile updates the
  desktop and Start menu shortcuts that open it, and the Profiles window no
  longer rewrites the other profiles. Duplicate instance says what is copied and
  what stays shared. Starfield is also found in Steam libraries.
- Deleting an instance never offers the game folder or your own folders.

### Downloads

- A full drive, an expired link or a dropped connection pauses the download
  instead of removing it; retries continue where the download stopped. A new
  link for a paused download resumes it. A failed or paused download says why
  in its tooltip.
- "Download started" shows only once the download is in the list; answering No
  to "Download again?" stops it.
- Nexus links register every name Nexus uses for the game.
- Several fixes against downloads saved under the wrong name, cut short, or
  losing their Nexus information.

### Mod list

- A mod named like "Weapons backup" is no longer turned into a backup.
- Create backup and dropping a folder on the list copy with a progress window and
  Cancel, and leave nothing half copied.
- "Create mod" from Overwrite with Replace keeps the old version until the new one
  has its files, with its Nexus ID, version and category.
- A mod can't be named "data" or "overwrite"; one that already is gets a message
  instead of mixing its files with the game's.
- Removing several mods at once is one step.
- Removing a mod asks with No as the default and says it goes to the Recycle
  Bin; every removal that says so really uses the Recycle Bin.
- Conflicts with the game's own files are counted right, and names Windows
  can't use for a folder are refused.
- Installing suggests the mod's whole name: "red4ext-1.31.1" is red4ext, not
  "red".

### Running programs and plugins

- An empty entry in the list of programs that don't get the VFS no longer
  turns the VFS off for every program.
- MO2 no longer stays locked after the game exits when Steam keeps running.
- Arguments with spaces given on the command line reach the program whole, and
  restarting MO2 as administrator keeps them. Batch files get quoted arguments.
- A game started by its loader is waited for even when the loader is in the list
  of programs that don't get the VFS.
- Programs you removed from Edit programs no longer come back.
- LOOT only changes the load order when it finishes; the plugin list warns past
  the game's plugin limit.
- Settings > Plugins: Cancel undoes turning plugins on or off.
- Messages from Mod Organizer 2 in these areas are rewritten and translated.

## 1.1.0-beta.8 (pre-release) — November 12, 2026

### Which games were tested

What's new in this beta (script extenders, borderless window) was tested by
hand with the real games, on Windows 11:

| Game | Script extender | Borderless window |
|---|---|---|
| Skyrim Special Edition | SKSE64: works | works |
| Fallout 4 | F4SE: works | works |
| Fallout 3 | FOSE: works | works |
| New Vegas | xNVSE: works | works; at exit the screen still goes black for about a third of a second |

Elden Ring and Nightreign don't use either and work as in beta 7.

Not tested yet:

- **Cyberpunk 2077** and **Baldur's Gate 3**: not tried with this beta.
- **Oblivion, Skyrim (the original), Enderal, Tale of Two Wastelands,
  Starfield and the VR versions**: their script extenders and the borderless
  window should work the same way, but nobody has tried them with the real
  games.
- **Windows display scaling above 100%** with the borderless window in the
  older games (new in this beta, see below).
- **The setup on Linux, Steam Deck, Proton or Wine**: the fix below comes
  from a user's report and was checked on Windows only.

Known issue: in Fallout 3, New Vegas, Oblivion and Skyrim, the borderless
window is hidden when the game stops answering on a black screen, which is
what they do while they close. If a game does that while playing (a long
load after a fade to black, say), the desktop can show until it answers
again.

### Script extenders

- Installing the archive of the game's script extender (SKSE64, SKSE, F4SE,
  NVSE, FOSE, OBSE, SFSE and their VR versions) asks where its files go. Its
  loader and DLLs have to be next to the game's exe, where a mod can't put them:
  - **Install it in the game folder** copies them there and installs the rest
    (Data\Scripts) as a mod, which keeps the list of the copied files.
  - **I'll copy the files myself** installs the rest as a mod and lists which
    files go into the game folder, with a button that opens it.
  - **Install it as a regular mod** works as before, for tools such as Root
    Builder.
- Updating the extender the same way sends the files of the older version (its
  DLL for the old game version) to the Recycle Bin.
- Removing that mod offers to send the files it copied into the game folder to
  the Recycle Bin too, but only those still as it copied them: a newer version
  copied there by hand or from another instance stays.
- The window warns when the extender is built for another version of the game,
  and so does Run before the game starts through the extender's loader: after a
  game update, the old extender doesn't start the game.
- Once per instance, when the extender is installed and Run starts something
  else, MO2 offers to start the game through the extender.

### Borderless window

- The Run button's menu has **Borderless window**, per instance, for Skyrim,
  Skyrim Special Edition, Enderal, Fallout 3, New Vegas, TTW, Fallout 4 and
  Oblivion. The game starts in a window without a border that covers the
  screen, at its resolution. Skyrim Special Edition, Enderal SE and Fallout 4
  do it themselves (MO2 sets bBorderless in their Prefs INI); the older games
  only have a framed window: a small DLL of MO2's, loaded into the game only
  with the option on, takes the frame off as the game creates its window, so
  no framed window shows first, and hides the window when the game stops
  drawing to close, instead of a black screen while it shuts down.
- It applies when the game starts, also through its script extender, not for
  its launcher or editor. Turned off, the INIs MO2 changed get their display
  settings back at the next start.
- The older games start with Windows' high-DPI compatibility setting, so
  display scaling above 100% doesn't stretch their window a second time.

### Setup

- On Linux (Steam Deck, Proton, Wine), the setup stopped right after copying
  the files with "The method or operation is not implemented" and undid the
  installation: Wine doesn't support the way it created the Start menu and
  desktop shortcuts. It now makes them through the shell's own shortcut object,
  which Wine supports, and a shortcut that still can't be made is only a warning
  at the end: MO2 stays installed.
- Under Wine, **Open MO2 when finished** starts MO2 directly. On Windows it
  still goes through the desktop so MO2 doesn't run as administrator.

### Windows and dialogs

- Every dialog opens centred over the window in front of it, on that
  window's monitor, and stays above it: before, some opened behind the main
  window or the instance wizard (the Nexus category import when creating an
  instance, for one), on another monitor, or moved after appearing. A dialog
  that opens while MO2 is in the background flashes its taskbar button.
- Windows' own windows that MO2 opens (moving files to the Recycle Bin,
  administrator prompts) open over MO2 too.
- Starting MO2 while it is already open brings the open one to the front,
  restored and with its dialog on top, instead of a message. Only while the
  other one is still starting is there a notice.
- The remaining windows from Mod Organizer 2 are rewritten in MO2 Revamped's
  design: the "remember my choice" questions and the task dialogs plugins
  use, BSA Extractor's questions and progress, and the file attribute check
  before a program starts. C# FOMOD scripts' own windows open centred over
  MO2.
- Plain-language texts for the Form 43 and script extender plugin checks,
  the games' settings (Settings > Plugins) and the sTestFile and Starfield
  ESP notices, translated into Spanish, German, French, Italian and
  Portuguese.

### Other fixes

- BSA Extractor keeps the archive when some of its files couldn't be
  extracted (a full disk): before, it could send it to the Recycle Bin.
- Script extender mods are no longer flagged as having no valid game data.

## 1.1.0-beta.7 (pre-release) — November 11, 2026

### FNIS

- The FNIS tools are rewritten in MO2 Revamped's design. One window, **Set up
  FNIS**, asks for everything at once instead of a chain of messages:
  GenerateFNISforUsers.exe, and the mods FNIS's files and logs go to, which it
  can create. Each choice says what is wrong with it, and the window opens again
  from **Tools > FNIS > Change FNIS settings**. Settings from Mod Organizer 2's
  FNIS tool carry over.
- **Choose FNIS patches** has a search, counts the patches that are on, and keeps
  the hidden ones as they were.
- After FNIS runs, its output mod is on again. Mod Organizer 2's tool left it off
  whenever MO2's FNIS check asked something at the end of the run.
- MO2 no longer keeps waiting after a program it ran from a plugin finishes, when
  another plugin asks something at that moment (MO2's FNIS check does): the wait
  went on until some later refresh.

### DDS preview

- The DDS preview has a new bar in MO2 Revamped's design: one line with the
  texture's format, size, kind and mip levels, the channels as a row of choices
  (RGBA, RGB, R, G, B, A) and the background behind transparent pixels
  (checkerboard, black, white or any color), with the chosen ones marked.
- The checkerboard follows the theme instead of staying white on the dark one.
- A damaged, cut-short or unsupported DDS file shows what is wrong with it
  instead of an empty preview. Before, a texture with less data than its header
  said went to the graphics card anyway, which could take MO2 down.

### Configurator

- The Configurator, which edits the game's INI settings, is rewritten in MO2
  Revamped's design, laid out like MO2's Settings: the sections on the left, a
  search across every setting, a switch for the advanced ones, and a row per
  setting with what it does, its default and a button to restore it. The
  descriptions are clearer and translated.
- Saving changes only the lines of the settings that changed. Before, MO2
  rewrote the whole INI: comments were lost, and an INI with a key written twice
  could not be opened at all.
- The Configurator now edits the copy of a setting the game reads: the first
  one when a key appears twice, and Fallout4Custom.ini's when it has the
  setting, since that file wins.
- Unsaved changes are asked about once when the window closes, and Enter in a
  field no longer saves.

### BAIN wizard

- The BAIN wizard installer is rewritten in MO2 Revamped's design. Each question
  shows its options beside the chosen one's picture and description; long
  questions and option names wrap instead of being cut.
- The requirements page shows each version the package asks for, the one MO2
  found, and whether it is met, missing or something MO2 can't check.
- The last page shows what will be installed: the sub-packages, which plugins
  start enabled and which stay in the optional folder, the INI tweaks and the
  script's notes. Install stays off while the mod name is empty.
- A plugin the script renames is now installed enabled. Mod Organizer 2's wizard
  put it in the optional folder.
- Back goes through every step, also when the script asks the same question
  again in a loop.
- The script's notes and questions are shown as written, not read as Markdown or
  HTML.

### Quick Install

- Quick Install, for archives whose files are already laid out as the game
  expects, is a Windows 11 dialog like MO2 Revamped's others: what it will do,
  the mod's name, and Install, Install manually and Cancel side by side. Install
  stays off while the name is empty, and the guessed name always shows (Mod
  Organizer 2 left the field empty when the guess wasn't in its list).

### Other installers

- The manual installer, the BAIN package installer (numbered parts without a
  wizard) and the bundle installer (an archive holding several archives) are
  Windows 11 dialogs like the others, built the same way.
- **Manual installation**: Use as `<data>`, Reset `<data>`, New folder and
  Open are buttons as well as in the right-click menu, so the keyboard reaches
  them (New folder is also Ctrl+Shift+N). Whether the layout looks right shows
  in a colored bar instead of red and green borders. The question before
  installing a layout that doesn't look right starts on Go back, and a new
  folder's name is checked before it is created.
- **BAIN package**: the package.txt shows beside the parts instead of in another
  window, with buttons to choose all or none and a count; Install stays off
  when no part is chosen (Mod Organizer 2 installed an empty mod).
- **Bundled archives**: the archives are in a list; a double click installs one.
- The BAIN wizard's questions, and these lists, follow the selection that
  assistive tools make, not only the mouse's.

### FOMOD

- The FOMOD installer is rewritten in MO2 Revamped's design: the step's name
  and the package's author, version and website at the top, each group as a
  card with what it asks for ("Choose one.", "Choose one or more."…), and the
  option's picture and description beside them. The keyboard's focus shows an
  option's description too, not only the mouse; Tab goes through the options
  first.
- When a group needs a choice, Next stays as it is, disabled, and the reason
  shows beside the buttons (Mod Organizer 2 renamed it "Disabled" and explained
  only in a tooltip). Install waits for a mod name.
- The pictures open full screen over a dark backdrop, with the previous and next
  ones, a strip of all of them, and the arrow keys and Escape. Pictures are
  scaled smoothly.
- The list of files a FOMOD names but the archive lacks shows as written; paths
  with `*` or `_` were read as Markdown.
- FOMOD C#: the windows before and after the script are content dialogs. After
  the script, **Save as INI Tweaks** is now the default instead of Apply, which
  rewrites the profile's INI files.
- OMOD: the name, readme, option and picture windows are content dialogs; the
  option picker shows the focused option's picture and description too.
- The INI editor's viewer and its Find dialog are rebuilt in the same design.
  Find next's F3 no longer replaces Find's Ctrl+F, and Find with no file open no
  longer closes MO2.

### BSA Packer

- BSA Packer's window is rewritten in MO2 Revamped's design: it says what packing
  does, chooses the first mod and archive name by itself, and lists only mods it
  can pack. Separators, the game's own DLC and Creation Club content, backups and
  Overwrite are left out; when nothing is left, it says why.
- A mod without a plugin says so before packing, as the game loads an archive
  only with a plugin of the same name.
- When no archive is created (nothing to pack, or packing was cancelled), BSA
  Packer says so and leaves the mod as it was. Mod Organizer 2 still made the
  dummy plugin and the .override file, and could hide the loose files with
  nothing to replace them. Afterwards, it lists the archives it made.
- Its questions are translated, and the name it asks for after **Another
  name…** is a window like the others, over MO2's, with Pack off until there is
  a name.
- With no plugin in the mod, the note says what BSA Packer's dummy plugin
  setting will do, or where to turn it on.

### Save details

- The card over a save in the Saves tab is rebuilt for every Bethesda game:
  the screenshot with rounded corners at the screen's scale, the character,
  "Level 31, Whiterun" and the save's number and date, then the plugins the
  save needs that aren't active, by kind (ESP and ESM, medium, light), seven per
  kind and how many more. With nothing missing it says so. Morrowind's card
  shows health and the game's day.

### Fixes from the final review

- The Elden Ring-only edition ships the shared helpers the BAIN wizard needs; it
  failed to load there.
- A FOMOD group with no options no longer closes MO2 (also in Mod Organizer 2).
- Links in a FOMOD's descriptions and its website open only web pages; any text
  went to the shell before, a program's path included.
- The FOMOD buttons read the same as the other installers' in every language
  (Italian showed "Cancella", which means delete).
- The Configurator writes what you type in the Windows code page the games read
  (UTF-8 showed as "JosÃ©"), says so when a character doesn't fit instead of
  failing silently, and opens INIs with numbers too large to read.
- The BAIN wizard shows any script error on its error page (a division by zero
  left Next doing nothing), and says why Install is off without a name.
- In FNIS's windows, Enter no longer starts FNIS or saves and closes.
- The manual installer refuses folder names Windows keeps (CON, NUL…).
- FOMOD pictures are read once per installer instead of on every hover; a
  different picture at the same temporary path still shows as itself.
- The FNIS check's question before starting the game names the program, and its
  message after a failed run has a title.

## 1.1.0-beta.6 (pre-release) — November 11, 2026

### Qt 6.12

- MO2 runs on Qt 6.12, the current long-term support release, with Qt WebEngine
  6.140 (Chromium 140) for the web views, instead of Mod Organizer 2.5.2's Qt 6.7,
  which no longer gets updates. The organizer, its plugins and PyQt6 (now 6.12) are
  built against it; the design does not change.
- The Python plugins' PyQt6 offers the modules for the Qt libraries MO2 ships;
  the 13 for libraries it never shipped (Bluetooth, Multimedia, PDF and others),
  which could not be imported, are gone.
- The main window keeps its Windows 11 backdrop while LOOT sorts and after LOOT's
  report or a Nexus description opens: Qt 6.12 had moved it onto Direct3D, which
  turned the command bar, the status bar and the window's edges white.

### Archives 7-Zip can't open

- When 7-Zip, which MO2 uses, can't open a mod archive and WinRAR is installed,
  MO2 offers to extract it with your WinRAR and installs it from there, with every
  installer (FOMOD, BAIN and the rest) working as usual. Nothing of WinRAR comes
  with MO2; the answer can be remembered.

### Installer

- The installer makes sure the Microsoft Visual C++ runtime MO2 needs is installed:
  x64, and x86 for 32-bit games such as Fallout 3, New Vegas and Oblivion. When it
  is missing or too old, it downloads Microsoft's own installer, checks that
  Microsoft signed it, and installs it. If that fails, MO2 is still installed and
  the result shows Microsoft's download links.

### Other games

- Baldur's Gate 3 is supported, with MO2's official game plugin and its tools. The
  plugin downloads LSLib on the first launch and tells you if the .NET 8 Runtime it
  needs is missing.
- Each game lists only what it can use: Cyberpunk no longer shows the INI tools, the
  BAIN and OMOD installers stay in the games with plugin files, and Back-date BSA
  archives is only for Skyrim.
- The Plugins and Archives tabs and their tools match the rest of the design, with
  the search on top and number columns that keep just the room they need.
- The Plugins list's columns no longer grow a little at every start until the names
  disappear. Its column widths are measured again once, including widths you set
  yourself.
- New Vegas starts from a profile made before its launcher ever ran. An empty prefs
  file in a profile is now filled from the game's own, instead of sending the game
  to its launcher.
- Lines about Elden Ring no longer appear in the log of other games.
- BSA Extractor, BSA Packer and the BSA preview only load for games with BSA or BA2
  archives, and the INI Editor for games with INI files; before, they were only
  hidden from the menus. The FNIS check also covers Skyrim Special Edition and VR,
  and games without plugin files no longer search every mod for missing masters.

### Fixes

- Plugins: the right-click menu acts on the plugins it was opened on, even if the
  list refreshes while it is open; on empty space it offers only the actions for
  all plugins. Enabling or disabling several plugins updates their checkboxes,
  load order and warnings at once, and a search made only of separators shows
  every plugin.
- Archives: Extract works on the "Base game" group and keeps working if the list
  refreshes while the folder is chosen; the warning icon goes to the Archives tab
  in games without plugins.
- Settings no longer fails to save when a folder it didn't change can't be
  created (an offline drive, for example). It refuses relative folders and folders
  inside one another, and says Nexus link registration worked only when it did.
- The New instance wizard refuses names Windows reserves (CON, NUL, COM1...), and
  its last page no longer cuts long captions.
- Ctrl+N and Ctrl+I work again; the Nexus menu shows the game's name ("Visit
  Skyrim Special Edition on Nexus"), and "No backups" says what is missing.
- Refresh in the mod list menus saves a change still waiting to be written
  instead of reading the old list back.
- The log keeps following new lines during long bursts; the Archives list, the
  Downloads names and moving many plugins at once are faster with large setups.
- The Plugins search follows "Use regular expressions", as the other lists do.
- The Configurator also edits Tale of Two Wastelands, Skyrim VR and Fallout 4 VR,
  and shows each game's own settings, which it skipped before. The script
  extender check also reads Fallout 4 VR's log.
- The Run menu says whether each entry adds or removes the program or shortcut,
  and a program with "&" in its name shows it as written.
- Settings says that turning a plugin on or off takes effect at once, and when a
  new crash dump type applies; the Developer page no longer promises features
  in testing that aren't there.
- The command bar keeps its own tooltips after a language change, and the
  information plugin no longer adds a second, hidden "About MO2 Revamped".
- The window no longer waits for Overwrite's files and the saves folder to be
  counted: the dashboard reads them in the background.

### LOOT

- Sorting uses LOOT's current rules again. Mod Organizer 2.5.2's LOOT reads the
  masterlists' v0.21 branch, which LOOT stopped updating in May 2025, so it
  sorted with rules more than a year old. LOOT is now libloot 0.29.7 with MO2's
  current lootcli, which downloads the v0.29 masterlists and moves settings that
  still name an old branch.

### Archives with non-ASCII paths

- BSA and BA2 archives in folders whose names have characters outside the
  system code page (Japanese, Korean, Chinese, and so on) are read, previewed
  and extracted, and they can be extracted into such folders. Before, they
  usually failed to open. Names stored inside the archives are handled as
  before.

### Fixes from later Mod Organizer 2 versions

Brought in from MO2's own repositories, where they were fixed after 2.5.2:

- MO2 no longer crashes when an nxm link or another command arrives before an
  instance is chosen, or when it was started from another folder (as the nxm
  handler does).
- Text files in UTF-16 without a byte order mark open correctly, and a file
  that had a byte order mark keeps it when saved.
- Conflicts between hidden files (.mohidden) no longer count, and mods' content
  icons from more than one game feature match their contents again.
- Refreshing the mod list asks the game plugin for the load order once instead
  of once per mod.
- Skyrim SE accepts LightPlacer, PBR, Pandora and main menu folders as valid
  mod contents, Skyrim VR LightPlacer, and New Vegas Devkit. New Vegas and Tale
  of Two Wastelands use the right Steam app ID for the Polish, Czech and Russian
  edition, Fallout 4 VR uses ESL plugins when its ESL support is installed, and
  Oblivion reads saves without a screenshot.
- Nehrim's script extender and plugins.txt pointed at "Oblvion"; LOOT is found
  where its current installer puts it; Starfield copies its INI files only when
  a profile has its own INI files.
- LOOT respects "don't update the masterlist", and says so when it can't sort.
  Quick Install leaves a correct archive alone, BSA Packer names archives with
  dots correctly, and the BSA preview no longer leaks memory.
- Games without their own plugin: square brackets in MO2's folder path, broken
  Epic or EA Desktop data, and fixes for Cyberpunk 2077, Valheim, Subnautica,
  Darkest Dungeon, No Man's Sky, Blade & Sorcery, Mount & Blade II, The Witcher 3,
  Stardew Valley, NieR:Automata and Final Fantasy VII Remake.

### Interface

- The keyboard focus ring only appears after a real press of Tab, not on a window's
  first control.
- Separators are calmer: their color is a tint over the list, with square corners
  like the other rows, and their names line up with the mods' names.
- A separator's arrow sits where the mods have their switch, and a click on it opens
  or closes the separator without selecting it.
- A double click no longer ends in a rename, and a click on the arrow of a selected
  separator no longer renames it either.
- A collapsed separator keeps its color while it shows its mods' conflicts, and a
  mod's conflict color no longer looks darker around its switch.
- With the list grouped by category or Nexus ID, a group heading no longer acts as
  its first mod: keys, menu actions and removing skip it, and it no longer shows
  that mod's status, marks and colors.
- In descending priority, "Disable its mods" switches the mods shown inside the
  separator, installing into an empty separator stays there, and sending mods to a
  separator at the top no longer risks a crash.
- Dropping right below an open separator puts the mod first in it whichever column
  you drop on.
- The Mods panel's search, grouping, filter button and lists have names for screen
  readers.
- With many columns shown, the mod name keeps a readable width and the list scrolls
  sideways; Nexus IDs line up with their heading.
- A separator's information window is titled with its name, without the folder's
  "_separator" suffix.
- The main window fits a 1920 x 1080 screen at 150 % and 175 %: it can shrink to
  1009 x 568 instead of 1140 x 800. "Last saved" shortens with an ellipsis, and the
  command bar's notifications and More buttons and the grouping choice keep their
  room when the window narrows.
- The log opens again at the height you left it, even after quitting with it
  folded, and a log closed while folded comes back folded. It docks at the bottom
  or floats; floating, its fold arrow is hidden and a double-click docks it.
- The log's one-line summary keeps up with bursts of lines without slowing the
  window, and long game, instance and profile names in the status bar shorten
  instead of keeping the window wide.
- The gap above the log matches the gaps between the other cards.
- Deleting downloads in bulk names how many and which files go, says that hidden
  and filtered-out ones are included, and removes exactly those. Cancelling a download
  from its menu asks first, as the Delete key does.
- The Saves tab says when there are no saves, keeps its date column in view, waits
  for its menu to close before refreshing, and no longer opens MO2's own folder for a
  game without saves. Download rows are as tall as the Saves and Data rows, and Ctrl+F
  reaches the Data search.
- Screen readers announce Notifications and Update in the status bar as buttons, and
  the Saves, Data and Downloads lists by name.
- The Data tab follows mods being switched on or off, so origins and conflict marks
  stay right without pressing Refresh, and a file stops showing as conflicted once
  the other copy is gone. "Show archive files" keeps the state you left it in.
- Download progress repaints its row a few times a second instead of on every network
  event, and the download list's columns keep their widths instead of being squeezed
  and stretched again on every resize.

### Built from source

- uibase, the game, installer, preview and tool plugins, the Python plugin host,
  archive, LOOT CLI and helper are now built from their Mod Organizer 2.5.2
  sources in this repository, and the USVFS virtual file system from its 0.5.7.2
  source, the version 2.5.2 shipped, instead of being copied from the 2.5.2
  release. They load the
  same way: each one is checked against the 2.5.2 binaries for the same exports,
  dependencies and plugin interfaces.
- The splash, the icons and the Python plugins come from this repository too; a
  release could still carry the 1.0.1 splash and the old `MO2-Revamped.ico`.
- The MO2 translations are compiled from sources in this repository, and the BAIN
  wizard interpreter is kept here with its plugin; both are the 2.5.2 versions,
  unchanged.
- `7z.dll` and PyQt6, which Mod Organizer 2 compiled itself, are now compiled
  here from their official sources, with the same options as 2.5.2.
- 7-Zip is updated from 24.05 to 26.06, which fixes known vulnerabilities in
  unpacking archives, among them one in Zstandard decoding (CVE-2024-11477).

## 1.1.0-beta.5 (pre-release) — November 9, 2026

### Nightreign

- Custom sounds in a mod's `sd` folder play. Nightreign has two functions that open
  sound files, where Elden Ring has one, and the bridge now hooks both.
- BootBoost works in Nightreign too, and the switch is available in its startup
  options.
- The intro logos are skipped, with me3's method for this game. It is a switch in
  the startup options, on by default.
- The game closes in about 3 seconds.

### Saves

- If MO2 can't give ELDEN RING Reforged its own save file, the bridge closes the game
  before it reads any save, and MO2 explains why. Without this it could write to
  ER0000.sl2, the save of your other mods. Choosing **Start anyway** in the warning
  before the launch still starts the game.
- Export keeps every route apart and includes the saves set aside while a mod profile
  runs.
- A failed transfer into the Steam route puts the old save back, also across drives.
- The backups window offers Reforged's online backups, a route chosen by hand drops a
  waiting wizard choice, and deleting a profile's saves checks what was left.

### Mod list

- Separator colors show again, as one rounded band as tall as a mod's row. A separator
  keeps its color under the cursor and when selected: an outline shows the state. The
  conflict colors shown when you select a mod came back with them.
- New separators get a random color that your other separators don't use. Settings,
  Mod list, **Color of new separators** can ask each time, reuse the last color or
  use none.
- Mods keep their position when the list has separators.
- The rename field is centered on the row and stays clear of the switch.
- The blue edge on the selected row is gone.
- The categories menu is more compact and fits on the screen in one column.
- Double-clicking a separator collapses it from any column, not only the first.
- Group headings are no longer read as the first mod, an older version on Nexus shows
  in amber, and categories put back from the menu restore each selected mod.

### Mods and launching

- Seamless Co-op starts from a mods folder with any name, and a linked mods folder
  loads each DLL once.
- Arranging a large archive by hand no longer freezes MO2.
- Mods with a loose file beside their game folders are no longer marked "No valid
  game data".
- A winning DLL missing from disk no longer blocks the launch.
- Without any active mod, Nightreign still gets BootBoost.
- Faster exit: the check the game makes at shutdown is only stepped over while the
  game is closing.
- BootBoost's cache has a checksum and is written safely; the old cache is replaced
  at the next start.

### Setup

- An interrupted install or restore can be finished or undone, and no update goes over
  one that did not finish.
- A restore checks its backup first, and an elevated run stays in its folder.
- Copy data brings the shared saves, save backups and Elden Ring launch settings.

### Reliability

- Refresh: no stuck refresh flag, a second refresh after changes made during one, and
  a failed refresh no longer rebuilds or saves the plugin list.
- Downloads: a retry after an error no longer appends the whole file to the partial
  one, and several crashes when a download ends, fails or is removed are fixed.
- Removing a mod checks that its folder is gone, and the installer never extracts
  outside the mods folder.
- Nexus: a cancelled or refused check no longer blocks the next one.

### Interface

- New category no longer renames another one.
- Selected rows show in the lists that had no highlight: plugins, downloads, Saves,
  Archives, filters, Conflicts, Overwrite and Settings. Row colors stay under the
  cursor, such as a conflicted origin or a download's status.
- Selecting a mod tints its plugins in the plugin list again.
- Double-clicking a plugin opens its mod from any column.
- A right-click on empty space no longer opens the menu of what stayed selected in
  Saves, Conflicts and Overwrite.
- Only rows that do something when clicked react to the cursor.
- Tab reaches the log panel and status bar buttons, and a ring shows the button,
  switch, tab or list it reached. A click shows none.
- Every icon was checked against what it stands for. Endorse, Browse, desktop shortcut,
  libraries and backups each have one icon everywhere, and the game's file types have
  their own in the Data tab and the archive installer: sound, video, containers. A
  `.bak` only looks like a save when it is a copy of one, and Reforged's `.err` does.
- Settings, Manage Categories, Log and every window got a pass for alignment, spacing,
  backgrounds and text in both themes.
- A mod without categories no longer shows an empty tooltip.
- Changing the language while MO2 is open updates the toolbar, the dashboard and the
  Data column titles.
- Enter no longer confirms destructive questions by accident, a '%1' or '%2' in a name
  or path stays as written, and the folded log follows new lines.
- Nightreign instances name the right game in German and French.

### Each game shows what applies to it

- Settings, Plugins lists only what means something for the game: other games' support
  plugins and the BSA and INI tools are hidden where they do nothing. Hiding a row
  doesn't change the plugin.
- The Managed filter, the Contents icons, the BSA Packer, the INI Editor and the
  Configurator appear according to what the game has, not its name, and the texts of
  Workarounds, Maintenance, Saves and invalid mods no longer talk about what the game
  doesn't use.
- About and the bug report show the Elden Ring tools only in their games, Nightreign
  has its own names there, and it no longer offers Reforged's online switch.

### Performance

- The mod list caches its scroll bar marks and marker colors and repaints only the
  switch that moves.
- The dashboard counts in slices and stops counting Overwrite at 1000 files.
- Save isolation looks at new dialogs once, instead of on every Qt event.

## 1.1.0-beta.4 (pre-release) — November 6, 2026

### Added

- **Update notice**: when a newer MO2 Revamped release is out, MO2 opens a window
  laid out like Profiles and Instances. On the left, the releases newer than yours,
  newest first, and yours below them. On the right, the selected release as a
  changelog: its summary, then each section (Highlights, Known issues...) with its
  points, read from the release notes on GitHub. **Open on GitHub**, **Open on
  Nexus**, **Skip this version** and **Remind me later**. Updating stays in your
  hands, with the setup; nothing is downloaded or installed by itself. A skipped
  version no longer opens the window, but the status bar and the Update command
  still show it. The window waits until no other question is open, so it does not
  appear while the game runs. MO2 asks GitHub at most once a day; the answer is
  shared by all instances.
- This is the first version that tells you about the next one. Earlier versions
  can't, so update to this one by hand.
- Settings, General, Updates: **Tell me when a new MO2 Revamped version is out**
  turns the check and the window off. **Versions to hear about** picks the
  channel: stable releases, stable and beta, or also development builds. Until
  you choose, it follows the version you use, so a beta hears of the next beta and
  early test builds reach only those who chose them. The GitHub tag gives the
  channel: `v1.2.0` is stable, `-beta.N` and `-rc.N` are betas, `-alpha.N`,
  `-dev.N`, `-test.N` and `-preview.N` are development builds.

### Faster start and exit

- Elden Ring closes in about 4 seconds instead of up to 20. One of the game's own
  threads sleeps 16.7 seconds at a time, and the game waited for it on exit; the
  bridge now wakes it as soon as the game starts waiting. Every exit also ended
  in a crash report from a check the game makes at shutdown, which cost a few
  more seconds; the bridge steps over that one check, so the game closes cleanly.
- With Convergence, the game closes cleanly too. Some of its DLLs crashed while
  the game was closing (ErdTools.dll and erdyes.dll, and one game thread after
  them), and Windows then spent seconds on a crash report. Once the game is
  closing, the bridge now ends the process at once when that happens, with a
  line in its log. While the game runs, crashes are handled as before.
- Skipping the intro logo screens is now on by default; a profile can turn it
  off in its startup options.
- Elden Ring reaches its title screen about 2 seconds sooner with me3's
  BootBoost. The game decrypts its archive headers on every start; after the
  first start they are kept, ready, in `%LOCALAPPDATA%\MO2 Revamped\BootBoost`,
  and a game update makes new ones. For this the me3 compatibility DLL now loads
  at every Elden Ring start; without ELDEN RING Reforged it does only that, and
  leaves Arxan and the game's memory as they are. It is a switch in the profile's
  startup options, **Faster game start (BootBoost)**, on by default.
- The bridge's log shows the time of each line since the game started, with the
  moments its window appears and hides and when the process exits.

### Fixed

- Mods' sound files load: custom music and sound banks in a mod's `sd` folder
  (Wwise `.bnk` and `.wem`) play, Convergence's music for example.
- ELDEN RING Reforged keeps its own sound: MO2 now applies me3's memory patch
  (mimalloc), and the compatibility DLL loads first, so Arxan is switched off at
  the game's entry point as intended.
- Native DLLs load in the order the Native DLL Profile shows; they loaded in
  reverse.
- Executables, Force load libraries now lists libraries in the order they load,
  for every game, and libraries you add load after the game plugin's own (before,
  usvfs loaded the list from the bottom up). If you added several for one program
  and their order matters, check it there.
- Elden Ring and Nightreign: the bridge, the me3 compatibility DLL and the mods'
  DLLs show as the game plugin's own in Force load libraries. They can be switched
  off there but no longer edited or removed (a removed one came back by itself,
  and an edited one loaded twice).
- Elden Ring: a sound file in Overwrite now wins over the same file in a mod, as
  everything else in Overwrite does.
- Elden Ring: an error while MO2 prepares a launch no longer leaves the Steam
  saves hidden; the launch stops with a message and the saves come back at once.
  MO2 also warns when the game ran without its bridge (Force load libraries off),
  instead of leaving the mods out without a word.
- Elden Ring: "could not restore the saves" no longer appears when the saves came
  back and only their emptied recovery folder was still in use; the "still open
  as MO2 closes" notice no longer appears when you cancel closing MO2.
- Seamless Co-op starts from a mods folder reached through a link (junction).
- The log panel's summary empties with **Clear all**, shows a log line with `<`
  as written, and the log stays where you scrolled to instead of jumping to the
  end on every new line.
- Plugins: right-clicking a plugin in a sorted or filtered list with nothing
  selected acts on that plugin, not on another one.
- Mod information, Nexus page: the version uses the same checks and colors as the
  mod list (accent for an update, caution for an older version on Nexus).
- The update check compared GitHub releases with the version of the Mod Organizer
  2 it is based on (2.5.2), so it never found a newer MO2 Revamped. The build now
  carries its own version, and the release builder checks that the source and
  ModOrganizer.exe both match.
- Endorsing MO2 Revamped went to an old, removed Nexus page and sent the MO2 base
  version; it now goes to the current page with the MO2 Revamped version.
- New instance: one Enter moves one step; it moved two, and from the folders page
  it could create the instance at once.
- Profiles no longer opens again and again after a profile could not be created.
- Settings, Nexus, Clear cache moves the cache folder's contents to the Recycle
  Bin; it deleted them for good, whatever folder Paths pointed at.
- Downloads: the right-click commands act on the download you clicked even if
  another one finishes while the menu is open, and Delete on a download under way
  asks before cancelling it. A paused download shows in amber instead of red.
- The window title and About show the MO2 Revamped version (About also the MO2
  base), and so do the Revamped About's header, components and diagnostic report.
- Endorsing another mod no longer marks MO2 Revamped as endorsed after a failed
  attempt.
- A Nexus key that was revoked or changed fails at once, saying where to connect
  again, instead of retrying and asking to try the same key again. A pasted key
  loses any space or line break inside it, and the rate-limit wait no longer comes
  out negative late in the evening.
- Renaming a profile or an instance: spaces around an instance name are dropped,
  the same name is no longer an error, and the active profile is recognised
  whatever the case of its name.
- Translations: the Overwrite folder keeps its name in the wizard, older wording in
  Spanish and German is corrected, and many texts no longer used are gone.
- The installed `licenses` folder now has every licence the third-party notices
  name, and the notices themselves as they are in this release; the release
  builder stops if one is missing.

### Known issues

- With ELDEN RING Reforged, MO2 now sets aside 12 GB of memory for the game, as
  me3 does. On a PC with little RAM or a small page file, other programs may run
  short while the game runs.
- Rarely, the game can stall at start-up while MO2 installs its file hooks. Close
  it and start it again.

## 1.1.0-beta.3 (pre-release) — November 5, 2026

### Faster

- The main window appears about a second sooner: the splash no longer waits a
  full second for itself, and the web engine starts only when a page needs it.
- Mod information stays smooth: Nexus Info keeps the mod's facts and commands, and
  its description opens in a window of its own with the **Description** button. A
  window holding a web page is drawn through the GPU until it closes, which showed
  in how the whole window responded on fast screens. The description window follows
  the mod you move to, and has Refresh and Open in Browser.
- Mod information, tab changes, program lists and the Reforged sync wait less.

### Removed

- Leftovers from the old Mod Organizer 2 that nothing used: its old themes and
  build files, unused icons and code, and stale translations. Links to the old
  project now appear only in the "..." menu, which adds one to its GitHub.
- The Classic Refined themes, and the code that kept the original look beside
  Fluent. A stored classic theme becomes Light or Dark. Fluent is the only look,
  so changing between Light and Dark no longer needs a restart.
- Settings migrations for versions before MO2 2.4. Their leftover keys stay in the
  file, unused.

### Changed

- Small windows share the Fluent look of the rest: questions, warnings, errors, text and number prompts, "remember my choice", enable or disable all, new empty mod and separator, backups, endorsements and the setup's own questions now have a title, the text, and the buttons side by side in a band. Choices with a description are cards. Long texts wrap instead of being cut.
- When the game closes within seconds of starting, MO2 now says so and suggests checking the Steam account and the mods, instead of showing nothing.

### Fixed

- MO2 asks about Steam and blacklisted programs before plugins prepare a launch. Elden Ring and Nightreign don't reach that question, but a game that does could have its saves brought back early while it waited.
- Enter on a focused Cancel in the new dialogs cancels; it used to press the action.
- Closing the "Plugin error" window skips the plugin for this session instead of blacklisting it.
- "Use the global Steam save?" no longer accepts on Enter.
- Setup: the root of a drive is refused as a target, since it could not be uninstalled later. An uninstall that keeps an edited file no longer leaves an entry in Windows pointing at a missing uninstaller. The uninstall question no longer promises to restore the old version after an update.
- A new installation starts in the language picked in its setup, unless MO2
  Revamped already has a language saved. It used to follow Windows.
- Mod information's Advanced conflicts list gives its width to the file names,
  which were cut down to their last few letters.
- The installer wizard's updated look and the Form 43 checker's translated name
  now ship; they were only in the baseline plugin folder, which no build reads.

## 1.1.0-beta.2 (pre-release) — November 4, 2026

### ELDEN RING Reforged

- The archive installer recognizes ELDEN RING Reforged and installs it without
  the layout choice: its game files at the game root and its natives in
  `DLLs`. Its launcher, me3 and setup tool are left out.
- Reforged runs from MO2, offline, with your other mods. MO2 hands
  `reforged.dll` the attach config it expects from me3. A new MO2 DLL,
  `eldenring_mo2_me3_compat.dll`, does what me3 does for it: it disables Arxan
  and keeps the oversized regulation out of the save, so saving after the
  first time no longer fails.
- The bridge names the saves `ER0000.err`, as Reforged does, unless Seamless
  Co-op is active. Save Isolation, the Saves list and save transfers recognize
  `.err`, and a new instance can start from a Reforged save.
- Online mode (optional, asked at install): the installer keeps Reforged's
  release with its own launcher in `_ReforgedOnline`. Its game files and natives
  are hard links, so they aren't stored twice. Once the mod is installed MO2 adds
  the executable "ELDEN RING Reforged (online)", which starts outside the VFS with
  the profile's saves and is listed in Save Isolation > Programs. A console
  launcher started outside the VFS, like Reforged's, now opens in a visible
  window instead of waiting at a prompt nobody can see.
- Separate online saves (Save Isolation > Programs, on by default): online play
  keeps its own `ER0000.err` in the route's `reforged_online` folder, apart from
  the offline save played with other MO2 mods, so a modded character does not
  go online by mistake. The first online run starts from a copy of the offline
  save; the online save gets its own backups. Turned off, both modes share one
  save. A global Steam route shares it either way. The online launcher always
  starts outside the VFS; its old switch is gone.
- The game folders `expression`, `font` and `other` are recognized as game
  files. A mod's copy of `me3_mod_host.dll` or `libzstd.dll` is never loaded
  as a native.

### Saves

- Reforged online leaves Steam's ER0000.sl2 in place, so Steam Cloud can't upload
  the online folder's copy over your Steam save when the game closes. The saves
  also come back sooner after it closes.
- A program set to start outside MO2's virtual files warns that Steam Cloud may
  upload this profile's save.
- Steam saves set aside for a launch come back when MO2 closes, if the game
  and its launchers are closed. MO2 closed right after the game used to leave
  them aside until its next start, and other MO2 installs refused to launch
  meanwhile.
- Any MO2 Revamped install now puts back Steam saves another install set aside,
  once that launch is over: no MO2 of the other install waits for its game,
  and the game and its launchers are closed. Before, the other install had to
  be started again only to restore them.
- A launch outside the VFS never deletes or overwrites a save with no copy left. A save
  that differs from the copy taking its place, or a route save newer than the one coming
  back, stays in the recovery folder, and MO2 says where. If such a launch ended with MO2
  closed, MO2 says what went back to the route at its next start.
- A launcher left open keeps the Steam saves set aside. MO2 now names it: when the game has
  closed, when you close MO2 and when a new launch is blocked. Save copies and backups also
  wait for it when the Steam folder was empty. After switching to the other game's instance,
  the saves still come back once it closes.
- Native DLL Profile: a DLL you switched off stays off after another mod overrides it for
  a while, and saving no longer forgets it. Switching off the copy that wins keeps it from
  loading from any entry.
- Mod information: opening on another page because the last one is empty for this mod no
  longer changes the page the next mod opens on. The save extension warning's default
  button is Cancel.

### Fewer restarts

- An executable the game plugin offers while MO2 runs (a launcher that came
  with a mod just installed) is listed at once, and saved for Save
  Isolation > Programs.
- Save Isolation: switching to or from the global Steam save applies when its
  window closes. MO2 reloads the profile, as switching to another profile and
  back does, instead of restarting.
- Turning the developer mode on or off no longer restarts MO2.

- Linking or unlinking the Nexus account in Settings applies at once.

### Launching

- With several active mods that bring `regulation.bin`, MO2 says before the
  launch which one the game will use. "Don't warn again for these mods" keeps
  it quiet for that combination in that profile.
- A mod MO2 turned on to start Reforged online goes back off if the launch
  doesn't go ahead.
- When a game update has changed the code MO2 uses to load your mods' game
  files, MO2 says so before the launch instead of after you've played.

### Saves and new instances

- Save Isolation > Backups: "Export all my saves" copies the saves of Steam and
  of every route of this instance to a folder you choose.
- Making an isolated copy shows its progress and the window keeps responding.
- The saves step of the New instance wizard follows the game picked there: an
  Elden Ring instance creating a Nightreign one no longer shows Elden Ring's
  name and save types.
- The archive installer treats folders named after Nightreign as wrappers even
  beside another container, as it does for Elden Ring.

### Instances

- Switching instance from the toolbar and then cancelling the exit no longer
  changes the instance of the next start.
- A restart stays on the instance a desktop shortcut opened. Creating an instance
  from the start window keeps the prompts when closing Mod Organizer. The toolbar
  menu shows an instance without a game plugin as unavailable.
- Renaming an instance warns when others use its folders. Deleting one removes
  its desktop shortcut. Enter and a double click no longer try to open an
  instance without a game plugin.
- Duplicating refreshes the list if the copy stops partway, warns if the copy's
  profiles could not be made, and no longer doubles "Portable" in the name.

### Executables

- Titles differing only in case count as the same. Such a rename or a program
  added from a file could silently merge two programs and lose one.
- The Run button keeps its program after this window closes, also after Cancel,
  by its name rather than its position.
- Turning off "Save new files to a mod" sticks after Apply. A program follows its
  output mod when that is renamed; if the mod is gone, the program saves to
  Overwrite again and says so, instead of refusing every edit.
- Edits are saved when applied. Duplicating copies the output mod and libraries,
  without another toolbar button.
- Reforged's online program follows its mod when renamed and leaves the list when
  the mod is removed.

### Save Isolation and Native DLL Profile

- "Export all my saves" includes saves with a mod's own extension, and waits if
  Steam's saves are still set aside. Transfer and "Start from" list them too,
  Convergence's included.
- Replacing a save in Steam's folder works with the instance on another drive.
- Restore opens on the route in use with the backup clicked selected.
- Save profiles can't be created or deleted during a launch, and deleting one says
  where its saves went.
- The Native DLL Profile drops rows for DLLs a mod no longer has, which used to stop
  the launch, and leaves out the game's own libraries and root loaders, which a
  Save used to force.

### Moving between pages

- Settings and the main window's panel fade between pages without moving: icons,
  text and Browse buttons no longer jump as a page appears. In Save Isolation and
  Startup Options the cards stay in place whatever the length of a page's text.

### Alignment

- Settings pages share one set of margins, and switches and cards end at the same
  place on every page, with a scroll bar or without. The same in Save Isolation,
  Startup Options and the install window.
- Settings > Plugins heads its details with the plugin's name, its options have
  readable names (the plugin's own is in their tooltip), and true or false options
  are switches that change with one click.

- Settings > Nexus lists its options in one column, with their switches where the
  other pages have them. The translations link is gone from General. The hover of
  a row is rounded, as in Windows 11.

### Other windows

- A new mod's install that is cancelled or fails no longer leaves a folder with
  part of its files, which showed up as a mod.
- In games with plugins, the right panel opens wide enough for the plugin names
  and every tab.
- LOOT's report takes the free height, and its scroll bars follow the theme.
- Replace, when a mod is already installed: the old files stay aside until the
  new version has finished installing, then go to the Recycle Bin. If you cancel
  or the install fails, the old mod comes back as it was, also with installers
  that create the mod themselves. The window says so. If a Replace doesn't finish,
  Mod Organizer asks at the next start whether to restore or discard the old
  version, for this instance's mods only and one copy of each mod at a time.
- Renaming a mod updates the programs that save new files to it in every profile.
  A program whose chosen mod no longer exists says so at launch, instead of "not
  enabled".
- An install in which a file made by the installer can't be copied now counts as
  failed, instead of as a complete mod.
- "Move to Recycle Bin" for an instance: Windows now asks before anything too
  large for the Bin is deleted for good. If files stay behind, you get a list,
  and the instance keeps its shortcut, group and notes.
- Transfer saves: a save being overwritten is only removed once the new one has
  arrived whole, also if you choose Skip in Windows' dialog. The questions name
  "the game's save folder" and no longer run words together.
- A backup is no longer made when you install as a new mod, since that changes
  nothing in the installed one.
- Password-protected archives: Cancel stops quietly. A wrong password asks again
  when the archive's file list is protected. When only its files are protected,
  as in many RAR archives, a wrong password stops the install and says the
  password is wrong or the archive is damaged, as the archive can't tell which.
- Restore required plugins (Saves tab): the choices say "Overwrite" and "Game
  folder". A plugin in the game's folder no longer stops the whole restore, and
  plugins no installed mod has are listed afterwards.
- Replacing the category list says how many mods use a category from it. Those
  mods end up without a category instead of silently getting another one. A
  removed category leaves its mods when you press OK, so a later category with
  the same number can't land on them; category numbers can't be edited,
  and a ticked filter on a removed category no longer leaves the list empty.
- Backups of the mod list and plugin order go to the Recycle Bin when deleted.
  Restoring a backup saves the current list first.
- Archives tab: cancelling Extract stops the remaining archives too, and
  right-clicking the empty area no longer leads to a crash.
- The LOOT report follows the Light theme.
- Query info: the mod ID you type is used right away, instead of asking you to
  query again.
- The Nexus API key: spaces and line breaks around a pasted key are ignored, and
  OK with an empty field no longer disconnects your account.
- Executables: Cancel, Esc and the X ask before dropping unsaved changes.
- Lists to choose from keep OK off until something is selected. Saving a text
  output says when the file could not be written.
- Windows with a progress bar have a title and a parent, and choice windows lost
  the old arrow. Backup questions look like the other questions. Folder, file
  and requirement icons in the Manual and BAIN install windows use the new icons.
- Mod pages from plugins open in your browser. The built-in browser window and
  the old tutorials are gone. Third-party mod-page plugins that downloaded through
  the built-in browser now need the site's nxm links.

### Mod information and Overwrite

- Next, Previous, Page Up, Page Down, "Go to" and Esc now ask before dropping
  unsaved changes in a text or INI file. After "No", Close no longer asks twice.
- A text file that can't be saved says so and keeps the window open, instead of
  looking saved.
- Comments, notes and Nexus fields are saved when you press Esc, close the
  window or change mod with the keyboard. Page Up and Page Down scroll in a
  multi-line editor instead of changing mod.
- Mods reached with Next or Previous are saved when you leave them, and their
  row in the mod list updates.
- Delete in the Filetree and in Manage Overwrite moves files to the Recycle Bin,
  and the prompts say so. Deleting, renaming or hiding a file updates the other
  pages right away.
- Delete and Hide are off for a mod's own `meta.ini`, with the reason.
- The window remembers its last page on games without plugins.
- Nexus page: a custom web page no longer turns off on the next mod. Emptying the
  Mod ID removes it, and changing it forgets what the old page said. Visit,
  Refresh, Track and Endorse follow the mod's real state and are off without an
  ID. The version is red only when Nexus has a newer one, and a valid ID with no
  description no longer claims to be invalid.
- With a custom web page turned on, the mod's page button and Shift-click open it
  instead of Nexus.
- Sync to mods sends the mod's old copy to the Recycle Bin, and a failed move
  loses nothing. It no longer removes the Overwrite folder or offers the game's
  own files as a mod, and it says that only files a mod already has can be
  synced.
- Manage Overwrite no longer blocks other mods' windows, comes to the front if
  it's open, and recovers if the Overwrite folder is gone.
- Failed moves and failed emptying of Overwrite are reported. A new mod that
  received nothing is removed.
- The Images page follows the theme, and its unsupported-format message is
  readable in Light. Conflict counts drop the Archive files row on games without
  archives.

### Log and status bar

- The command bar, the cards, the log and the status bar share one edge on both sides.
- A closed log panel comes back from the "..." menu; only a right click on the command
  bar brought it back before. The command bar can no longer be hidden, since nothing
  would bring it back.

### Old look gone

- The question dialogs, such as "Restart Mod Organizer", use the Fluent look instead of
  the original's blue icon and arrows, and Continue says what happens: some changes won't
  apply until you restart.
- The profile and program lists say "Manage profiles..." and "Edit programs...", not
  "<Manage...>" and "<Edit...>". The other labels in angle brackets went too.
- An old logo left in an instance's folder no longer shows at start.
- "An instance of Mod Organizer is already running" uses the theme, and what appears before
  the main window uses the instance's theme, not always Dark.

### Saves and Downloads tabs

- Saves and Downloads say when something couldn't be removed, also when Windows was
  told to skip a file in use.
- The questions that remove several downloads appear over MO2's window, not on the main
  screen, and say that the files go to the Recycle Bin.
- In a narrow window the tabs keep their dates and sizes in view: the name gives way
  first, instead of a horizontal scroll bar.
- The Downloads search box says "Search downloads", and both tabs have their help on the
  interface on Nightreign too.

### Data tab

- "Hide" is off on the game's own files: it renamed the real file in the game's folder,
  which could leave the game unusable.
- A double click on a program asks first: it starts without your mods.
- "Add as executable" no longer replaces a program with the same name, and the new one
  is saved right away.
- The search box says "Search files", and the help on the interface names the Origin
  column, on Nightreign too.

- Long messages use the whole window instead of a narrow column beside an empty one.

### Mod list filters

- The bar under the list names the filters in use and says which ones exclude, instead
  of showing an excluded one as if it were on, or "<Multiple>".
- "Clear filters" in the filter window also clears the search, as the one under the
  list does.

- Grouped by Nexus ID, mods without an id stay apart instead of forming a group with
  Overwrite. Group titles line up with the rows, and a list with no group to open
  keeps no space for one.

### Mod list menu

- "Restore backup" no longer deletes the mod before the backup is in its place: if a
  step fails it's put back, and the old mod goes to the Recycle Bin, not away for good.
- "Clear Overwrite" says it uses the Recycle Bin, as it did, takes hidden files too, and
  says when it fails.
- Rename, Reinstall and Create backup aren't offered with several mods selected, where
  they acted on one. "Send to" has no Separator without separators, and its Priority
  starts from the mod's own.

### Mod list rows

- Status says "Removed from Nexus" when a file's author removed it, instead of "New
  version", and "Older on Nexus" when the newest there is lower than yours.
- The Status and Version tooltips say what they show, in plain words. A date in
  Version is explained as not being a version.

### Nexus file names

- MO2 read the month in the names Nexus gives its files now as the mod id, so a mod
  downloaded by hand in October could point at an unrelated page and report false
  updates. It now reads the id, version and name from them, and a number in a date
  is never an id. Mods already installed with that mistake are repaired on start.

### Mod list header

- "Check for updates" marks its filter on the buttons above the list, and says so
  when you aren't connected to Nexus.
- "Saves" in Open folder opens the folder this profile uses, not always Steam's.

### Summary cards

- Updates counts a mod whose newest version on Nexus is lower too, as the list and
  its filter do. With none it says "None found": it can't know it's up to date.
- Overwrite is amber instead of red: files there are normal after playing.

### Theme

- Changing from Dark to Light without restarting no longer leaves the title and the
  command bar dark, also when Windows itself is in dark mode.
- Run's line by its arrow is gone while Run is off.

### Main window header

- Run keeps its program when the list is rebuilt (a theme change, a program added
  from Data) and between sessions, by its name instead of its position.
- Run is off when there's nothing to start.
- "Last played" is now "Last saved": it reads the date of the save files.
- The header's help on the interface also works on Nightreign.

### Notifications and Help

- Nightreign's Help has its guide. The guide names the program's switches and
  buttons as they are.
- Bug reports list the versions this copy really has, not fixed older ones.
- A notification no longer promises an action it doesn't have.
- The "…" menu holds everything at one level, with Create bug report next to
  Report a problem, and Ctrl+H opens it. The guided tours are gone: they described
  the old window.

### Settings

- Paths: a folder typed to stay outside a new Base Directory keeps its path, which
  used to empty the mod list without warning. A Base Directory that can't be
  created isn't saved. Changing Profiles or Overwrite asks to restart. A download
  in progress keeps the download folder, in the setting too.
- Nexus: "Clear Nexus cache" asks first, and refuses a folder that holds mods or
  profiles. Registering for download links adds the game instead of replacing the
  others and says when it's done. Nightreign's download links are recognized.
  Preferred servers stay in the list.
- Workarounds: an instance without a Steam user no longer deletes the password
  another saved. The Steam card is hidden on Elden Ring and Nightreign, where it
  has no effect. Offline mode's description says what it does.
- Plugins: a double click on a switch no longer opens a list, a required plugin's
  switch shows on, and options of plugins under another plugin are saved.
- No false "Language changed" prompt on an English system. "Reset Dialog Choices"
  also brings back the category questions. The unused "Open theme folder" and
  "Show main toolbar" are gone.

### Foreground

- Mod Organizer's notices and its lock no longer take the foreground from a game
  or another program.

### Profiles

- A new, duplicated or renamed profile can't take a name already in use, in any
  case. Duplicating onto one used to report success, and its row could delete
  the original.
- A profile that can't be read no longer leaves MO2 without one when picked.
- Elden Ring and Nightreign: renaming or deleting a profile waits until the game
  is closed, and the window fits their fewer rows.
- Other games: switches changed after a rename reach the renamed profile. Deleting
  profile-specific saves uses the Recycle Bin and says if it fails. Transfer saves
  says when files fail. Options the game doesn't have stay hidden.

### New instance wizard and menus

- Menus and the lists that drop down from a field are more compact.
- Steps change without a flash: a page is set up before it shows, so the
  isolated copy's game step no longer shows the others' search box first, and
  the old page fades into the new one.
- Step 2 uses its whole page, with larger icons and a "Best for" row. Game
  icons are round, as in the instance manager.
- Nightreign gets the search through every Steam library and the choice between
  several installations, as Elden Ring had.
- Browse on the folders step updates the free space, the folders it will create
  and its warnings. Searching the game step keeps the game already chosen.
- A folder no plugin recognizes can no longer be accepted anyway: such an
  instance could not open. An isolated copy created without starting it says
  where it is. Closing the game page of an isolated copy closes it instead of
  opening the page again.
- Going back and picking another game removes the Elden Ring save options.
  A new instance of the other game says why it lists no saves.
- Nightreign no longer offers Convergence's save type, in the wizard or in Save
  Isolation, unless such a mod is active in that profile: that mod is for Elden Ring.
- The saves step's switches line up with their titles; turning a save type on
  or off no longer flickers. The review step's rows line up.

### Install window

- A new window: the mod's name, what will be installed (game files, DLLs,
  regulation.bin and what's left out, worked out by the code that installs it)
  and Install. Skipping text files and the archive's own structure move to
  Advanced options, where they update that summary instead of installing.
- The two layouts are now "Organize for MO2 (recommended)" and "Install as it
  comes in the archive". The second warns when it would leave game files where
  the game doesn't look, and both can skip text files.
- It names your active mods that also bring regulation.bin, and says when you
  already have a mod with that name.
- The window stays on its screen, so its buttons don't end up out of sight.
- Manual... opens a new window instead of MO2's generic one: start from what the
  install would do, tick what goes in (what MO2 left out is there, unticked), drag
  files where you want, move them to the top or to DLLs, rename, make folders and
  reset. It warns about files the game wouldn't read and blocks two files in one
  place. Back returns to the install window and keeps your arrangement.
- It recognizes ELDEN RING Reforged, Convergence, Seamless Co-op and FRZN Merge,
  says what it will do with each, and uses the layout each one needs.
- Reforged's online and offline versions are separate mods: a switch in this
  window installs the online one, with "Online" in its name. While that mod is
  on, the program list stays on "ELDEN RING Reforged (online)"; with it off, the
  program isn't offered. Started some other way with the mod off, MO2 asks to
  turn it on first.
- Standard layout leaves out me3 and the script that starts it when a package
  ships them, and logs folders that came in the archive.
- Fixes: Seamless Co-op lost its folder when the archive wrapped it in another;
  FRZN Merge's "Drag Files Into Here" stayed a folder beside its natives.
- The install window's description follows the layout chosen in its advanced
  options.
- The window fits its content when the advanced options open, and counts a DLL's
  settings files. Manual's buttons that need a selection wait for one.
- A Nexus download whose category has no mapping installs without a category;
  MO2 no longer stops to ask on each one.

### Elden Ring Nightreign

- Loose game files from mods now load in Nightreign (1.3.3). The native bridge
  only knew Elden Ring's game code, so it skipped them and only its log said so.
- When a game update stops MO2 from reaching the game's file loader, Elden Ring
  and Nightreign now show a notice after the game closes, if active mods have
  game files.
- The game folder keeps its real capitals when it comes from Steam, and the game
  plugin's logs name Nightreign in a Nightreign instance.

### Installer

- An elevated setup keeps its program, requests and results in a folder only
  administrators can write, below Program Files, instead of `%TEMP%`. Its log
  stays in `%TEMP%`, and is not written if that folder has become a link. If
  Windows refuses that folder, the setup says so and stops.

### Removed

- The me3 and Mod Engine 2 integrations of the developer mode, with their game
  modes (the Mode selector), their Settings > Developer switches and the
  detection of Mod Engine 2 in the new instance wizard. Instances made for a
  mode stay as ordinary instances, and their "me3 (beta)" and "Mod Engine 2
  (beta)" entries leave the executables.
- Every other trace of me3 and Mod Engine 2 as launchers: their programs and
  `launchmod_*.bat` scripts no longer get the save guard or a place in Save
  Isolation > Programs. Mods packaged for Mod Engine 2 (a `mod/` folder) still
  install, always mapped to the game folders: the installer's "Recognize Mod
  Engine 2 paths" switch is gone.
- Seamless Co-op's launchers (`ersc_launcher.exe`, `nrsc_launcher.exe`) and a
  Seamless Co-op installed in the game folder: Seamless Co-op runs from its MO2
  mod with the game's "(MO2)" program, with every other mod. The New instance wizard no
  longer offers to add its launcher.
- Elden Ring and Nightreign instances drop executables that start me3, Mod
  Engine 2 or Seamless Co-op's launchers the next time MO2 opens them.

## 1.1.0-beta.1 (pre-release) — October 27, 2026

A pre-release: some details are still under review.

### Design review (October 27)

- Every window checked in Light and Dark: the plugins' glyph buttons no
  longer vanish on Dark, tool menu entries get their own icons, and lists,
  combo boxes and fields line up in Executables, Save Isolation and Settings.
- Prompts (profile name, install as a new mod, archive password) use the
  Fluent dialog layout with their own window titles; the Elden Ring archive
  installer is laid out in cards.
- Settings: pages are titled as the side menu, Nexus cards align, paths show
  Windows separators, and a plugin's description is no longer cut off.
- Main window: the Saves list shows name and date, the side lists fit without
  a needless horizontal scroll bar, and the Overwrite notification has a
  readable title.
- Spanish texts revised throughout (Ajustes, Overwrite, plurals, one voice
  in descriptions); new strings in German, French, Italian and Portuguese.

### Seamless Co-op and launchers

- Seamless Co-op runs from its MO2 mod inside the VFS, with every other mod:
  the bridge starts its DLL the way Mod Engine 2 does once the game runs. Its
  launcher hands over to the game started from MO2; a Seamless installed in
  the game folder still starts outside the VFS with the save swap. Its `.co2`
  save stays in the profile's route.
- The archive installer keeps Seamless Co-op's `SeamlessCoop` folder whole
  (settings, language file and crash reporter beside its DLL).
- A launch with Seamless Co-op active but no session password in its settings
  file stops with a prompt that opens the file, instead of the game closing.
- Each launcher starts the way its own tool expects; nothing is bundled, and
  MO2 integrates only with what the user installed.

### Saves

- Launchers that fail inside the VFS start outside it while the profile's
  saves are swapped into the Steam folder for the run; each program can be
  switched either way in Save Isolation > Programs.
- Save Isolation follows the Settings layout (Location, Programs, Backups) and
  recognizes the save formats of each route.
- Desktop and instance shortcuts wait for MO2's first refresh, so native DLLs
  load when the game is started from one.

### Interface

- Fluent redesign of the remaining dialogs in the Executables style:
  Categories, Forced load, Mod information, Overwrite, Already installed,
  Transfer saves, Import categories, New profile, About, Startup options and
  the Native DLL profile, with new options in each (search, sorting,
  duplicate, show in folder, context menus).
- Windows 11 scroll bars, flush with the window's edge.
- The themes are Light and Dark (blue); Dark Black is retired and a stored
  Dark Black choice becomes Dark.
- Native DLL profile: one list of every native DLL of the active mods,
  Seamless Co-op included, switched on by default, with Reset.
- Fixes: opening Executables failed when a log message had an accented
  character; regenerating the themes wiped their scroll bar rules; rows
  overlapped while a list refreshed; the log no longer shows harmless
  DirectWrite font warnings.

### Developer mode (hidden)

- Seven taps on the MO2 version in About > Components turn on a developer
  mode with Settings > Developer: beta integrations for me3 and Mod Engine 2
  (MO2's mods, native DLLs and Seamless Co-op without editing their own
  profile or config), game modes that give each manager an instance of its
  own, and a way to remove each again. Off by default; none of it is needed
  to play.

### Project

- Issue forms, a security policy and a code of conduct on GitHub.

## 1.0.2 — Elden Ring Nightreign, safer saves, Fluent dialogs

Released on October 23, 2026. The setup's main task is now the complete
installation of MO2 Revamped, for every game; isolated copies for one game are
made inside MO2 from the New instance wizard, which offers them for the games
with their own support: Elden Ring and, new, ELDEN RING NIGHTREIGN. More games
will join that list as their support is added.

### Elden Ring Nightreign

- New game support (Steam app 2622380, `Game/nightreign.exe`) with the same
  tools as Elden Ring: save isolation per profile, instance or Steam route,
  named save profiles, automatic save backups and restore, the launch save
  guard, native DLL loading, the Native DLL profile, startup options and the
  archive installer. Saves live in `%APPDATA%\Nightreign\<SteamID64>\NR0000.sl2`
  (`NR0000.co2` with Seamless Co-op).
- The launch guard knows `nightreign.exe`, `nrsc_launcher.exe` and me3.
  Nightreign keeps its own recovery folder, separate from Elden Ring's.
- "Skip the intro logo screens" is off for Nightreign: the patch matches Elden
  Ring's game code only.

### Saves (Elden Ring and Nightreign)

- While the game runs from MO2 in an isolated mode, the Steam account folders
  are set aside and come back unchanged when the game closes; this now covers
  every launcher (Mod Engine 2's `launchmod_*.bat`, Seamless Co-op, me3), a
  launcher that takes long to start the game, and saves hidden by another
  MO2 Revamped install.
- Automatic backups of the save route before each launch (Settings: how many
  to keep), "Restore a backup…" in Save Isolation, and a save check in
  Notifications.
- Named save profiles import Seamless Co-op `.co2` saves; the Saves tab and
  the transfer and import lists show only the saves the game reads.
- Instance-shared saves belong to the instance folder (custom profile paths
  no longer share them between instances).
- Removing a profile sends it to the Recycle Bin; Windows asks first when it
  cannot go there.
- If the game was started from Steam while MO2 had the Steam saves set aside,
  the newer save folder is kept and named in a warning and in Notifications,
  instead of being set aside silently.
- MO2 started while the game runs waits for it to close and then puts the
  Steam saves back; two MO2 windows of one install no longer restore at the
  same time; a moved install or an older recovery record no longer blocks
  launches; a save folder linked to another drive works.
- Restoring a backup that fails halfway puts every original file back;
  backups are kept and pruned in the right order.
- Nightreign guards `start_protected_game.exe`, and launchers shared with
  other games no longer block a launch.
- Seamless Co-op, Mod Engine 2, me3 and `nrsc_launcher` launches use the
  Native DLL profile too (initializers, switched-off DLLs left out); a broken
  profile is logged and no longer blocks a launcher launch.

### Game support

- Bethesda games: plugin names are no longer cut to "..."; setting up an
  instance of a game with editions (Skyrim SE, Fallout 4) no longer reopens
  the same dialog forever.
- Nightreign tools show Nightreign names; the native profile can't list one
  DLL for two mods; Overwrite cleanup skips a locked file and continues.

### Instances

- Deleting an instance warns when other instances use its folders.
- Crash dumps are written again; an isolated copy made at first start is
  launched; portable instances get working shortcuts and forwarded commands.

### Nexus links

- The "How to handle Nexus links" window (nxmhandler, built from source as
  1.2.0.1) follows Windows light or dark mode and the accent colour: programs
  as rows with game chips and status, priority by drag or arrows, Remove
  missing, and Test a link, which shows the target and starts nothing.
- After a No at MO2 start it doesn't ask again until the registration
  changes; a link for a game without a program offers to choose one.

### Interface

- Profiles, Executables, Notifications and Manage Overwrite use the instance
  manager's card layout; context menus follow the Windows 11 order with
  "Open folder" and "Copy" submenus; short scrolls became taller windows.
- Instance manager: safer delete (never a shared data folder, a drive or the
  MO2 folder), duplicates and renames rewrite only exact paths.
- Settings, downloads and mod menus: toolbar size changes apply cleanly, OK
  no longer switches the theme or closes the log, Space pauses the selected
  download, "Add to list" is back in a mod's menu.
- Long plugin messages wrap instead of being cut; launch errors read normally.
- Help has the Mod Organizer 2 documentation, Help on the Interface and About
  Qt again; plugin-order backups are in the plugin list menu and All Mods in
  each mod's menu.
- The command bar can't be hidden any more; hovering over category submenus
  no longer changes the selected mods; dashboard tiles stay current.
- The conflict pill shows whether a mod wins, is overwritten or both, and an
  available update has its own pill; the Flags column is back; the Mods card
  and plugin list get an accent border while a filter is active.
- Install mod, Save text/CSV and the executable pickers use the Windows file
  dialog; Alt+letter toggles settings switches again.
- Short notices ("Signed in to Nexus Mods") are Fluent toasts that never take
  focus; category setup and the theme chooser use option cards with previews.
- Dropping files from another program on the mod or plugin list no longer
  crashes when no MO2 window is active.

### Installer

- "Install MO2 Revamped" (the complete installation) is the main task again,
  and "Copy data" is available; existing isolated Elden Ring installations
  are still updated, restored and uninstalled as before.
- Installations are found in the background when the window opens, so
  switching between tasks no longer pauses.
- Shows its version in the header; Enter starts the task, Esc closes.
- Uninstalling after an update removes the program instead of putting the
  previous version back; files an update drops are removed (Restore brings
  them back).
- Without an install record, uninstalling keeps user plugins and themes, and
  "delete all data" names the save folders it would delete.
- Refuses to update or uninstall while Elden Ring or Nightreign runs; a
  missing Windows uninstall entry no longer reports a failed install; MO2 is
  never started as administrator after setup.
- Installing into a folder that still holds the data an uninstall kept
  (profiles, mods, settings) reinstalls MO2 Revamped next to it, as the same
  kind of copy, without touching that data.
- Before updating or uninstalling, any program still using the installation's
  files is named and nothing is changed until it closes.
- Checks the free space on the backup drive too, and keeps only the latest
  update backup once an update finishes.
- Follows the Windows accent colour and switches between light and dark with
  Windows; dark title bar in dark mode; progress shows on the taskbar button;
  "What's new in this version" opens the release notes when it finishes.
- Uninstalling after an update without an install record removes the program;
  a failed update keeps a correct record; reinstalling before a reboot keeps
  the uninstaller; Copy data handles large folders and trailing slashes.
- Installation checks, Copy data checks, changing language and opening MO2
  after setup run in the background, so the window no longer freezes.

## 1.0.1 — Interface, installer, and package refresh

Released on October 12, 2026 as a stable release for Elden Ring. It uses one
general setup executable. For now its only new installation is the Elden Ring
Isolated edition: one portable instance for Elden Ring that keeps its data
beside the installation. The Standard multi-instance installation and the
"Copy data" task, which installs the same Complete payload, are shown as coming
soon while support for other instances is extended. Updating, restoring and
uninstalling existing installations are unchanged.

### Startup options and prompts (October 12, 2026)

- Fixed "Start minimized" and "Black background" in Startup Options applying
  only on some launches: the bridge (0.5.0-alpha.35) now installs its window
  hooks as soon as it loads instead of from a worker thread.
- New Startup option "Skip the intro logo screens" goes straight to the title
  screen (pattern from TechieW's SkipTheIntro, MIT). If the game version is not
  recognized, the logos play normally. Offline-mode notices are not patched;
  set System > Network > Launch Setting to Play Offline in the game instead.
- The "Language changed" and "Cannot start" prompts use the new Revamped
  dialog design in all three themes.
- Tool names are translated (for example, Spanish "Aislamiento de partidas",
  "Opciones de inicio", "Perfil de DLL nativas"), and help texts cite the
  translated menu entries.

### Language fixes (October 12, 2026)

- A new instance now opens in the language chosen in the instance wizard
  instead of English: when the instance has no language of its own, MO2 uses
  the global language before falling back to English.
- In other languages the Tools menu no longer lists "About MO2 Revamped" and
  "INI Editor", and Help > About finds the About dialog again.
- Translated theme names, the color-table sample, plugin-type group headers in
  Settings > Plugins, the Elden Ring overview tutorial, Startup Options counters
  ("30 seconds", "1 session"), the Native DLL Profile summary and the Save
  Isolation shared-save label.
- A new isolated installation starts MO2 in the language chosen in setup. An
  existing `EldenRingOnly.ini` is left unchanged, so updates keep the user's
  choice.
- Corrected several Spanish texts inherited from upstream MO2 translations
  (wizard step 6, "Key", conflict color labels, button names).

### Isolated installation and languages (October 11, 2026)

- The setup offers "Install isolated Elden Ring" as its default task and marks
  the Standard installation and "Copy data" as coming soon.
- When setup or the instance wizard starts MO2 afterwards, it now runs
  `ModOrganizer.exe` instead of opening the installation folder. Setup asks the
  Windows desktop shell to start it, so MO2 still runs without administrator
  rights. A new isolated copy created from the instance wizard replaces the
  running MO2, which closes first because only one MO2 can run at a time.
- Added `translations/revamped_<language>.qm` for Spanish, German, French,
  Italian and Brazilian Portuguese. It covers the roughly 900 interface strings
  that Revamped added to MO2 and every Revamped plugin dialog and menu. The
  About dialog and bug-report flow, previously fixed in Spanish, now use the
  selected language.
- The dialogs shown before an instance loads (instance manager and wizard) use
  the last chosen language. Changing the language retranslates the main window
  at once and offers to restart MO2 so every window, dock and plugin switches.

### Interface audit (October 11, 2026)

- Kept every dialog, including plugin dialogs, inside the screen's work area so
  footer buttons stay reachable on 768p displays and at 150% scaling.
- Fixed the active-mods and conflict counters clipping their last digit, invisible
  heading icons in Startup Options and Workarounds, two broken icon paths, and a
  stray divider at the top of the Help, Endorse, folder, mod-list and download
  menus.
- Fixed the custom-browser command staying disabled after loading a saved
  custom browser, and the language selector showing the system language while
  the interface was English.
- Stopped main-window theme rules from leaking into Settings and other dialogs;
  unified dialog margins, Settings heading icons and card alignment, and replaced
  legacy raster icons in Executables and Instance Manager with flat SVGs.
- Removed unreachable code: the legacy self-update download/installer path, the
  update and message-of-the-day dialogs, the unused Nexus credentials dialog, and
  281 overridden declarations per theme (no visual change).
- First start now asks before opening the game support wiki.

### Interface and installer refresh

- Added Light, Dark Blue, and Dark Black themes, and aligned the theme picker,
  startup choice, and release payload with those three names.
- Reworked the main window and its menus, file panes, status/log area, profile
  controls, Run menu, Help and Endorse menus, filters, category manager, and
  Overwrite view to use the same refined visual system.
- Made the Data, Saves, and Downloads columns adapt to the window while
  preserving manual column resizing, and corrected manual resizing for the
  mod-list name column.
- Refined the Elden Ring archive-layout installer prompt, including a more
  compact pair of layout choices.
- Replaced the installer’s old visual treatment with a Qt Widgets interface,
  clearer action icons, a redesigned language selector, and consistent views
  for installation, update, data copy, restore, and uninstall.
- Kept the installer’s single-file distribution and its embedded payload
  integrity checks, rollback data, licenses, and release checksums.

Save Isolation applies profile and instance-shared routes immediately and
refreshes the Saves view. Switching to or from the global Steam save requires
a restart; the selected route is applied when MO2 restarts.

- Moved Complete standard instances and global preferences into a dedicated
  MO2 Revamped data directory, separate from the original MO2 AppData store.
- Kept Complete Portable and Elden Ring Isolated instance selection,
  preferences, and additional instance data inside their own installation.
- Removed automatic discovery of original MO2 global instances and stopped
  the Elden Ring save plugin from scanning or writing to that directory.
- Restricted update, restore, and uninstall to installations carrying a
  Revamped marker; v1.0.0 Complete/Portable folders keep a narrow legacy
  recognition path, and Isolated still requires its Elden Ring marker.
- Kept manual data migration available for users who explicitly choose a
  source folder.
- Built the Elden Ring Isolated payload from the same Complete stage and added
  hash checks that require matching core files and shared plugin versions.
- Kept the optional Elden Ring Steam save route in the game's own save folder;
  this is separate from MO2's global data directory.
- Moved save routing and named game-save profiles to Tools, verify the mode
  and selected save profile after writing, and log each saved or opened state
  to make persistence failures diagnosable.
- Prevented unreadable or malformed save-route INI files from being treated as
  empty configuration, preserving existing settings when a read fails.
- Hardened global-save recovery to reject missing or mismatched destinations,
  malformed manifests, duplicate names, and path-like entries before moving
  any save folders.
- Corrected remaining in-app guidance that still pointed to the Profiles
  window, kept the packaged Elden Ring tutorial in sync with source, and made
  the Save Isolation version label match alpha.77.
- Kept save routing, named game save profiles, and route-aware transfers in
  Tools only; removed their duplicate controls, transfer action, and opt-out
  toggle from Profiles and Settings.
- Replaced the repeated background dialog scan with event-triggered
  integration. Each show event now refreshes only the dialog that opened
  instead of rescanning all top-level windows; pending save changes still run
  on initialization and profile-change events. The Elden Ring new-instance
  wizard retains its save choices.
- Skipped Elden Ring bridge DLL discovery when no native initializer is
  configured, stopped scanning once all requested initializers had started,
  and labeled the revised bridge alpha.34 in logs and diagnostics.
- Refined the installer and uninstaller interface with Revamped branding,
  a card-based operation chooser, clearer section hierarchy, improved spacing,
  and a more visible primary action while preserving the existing maintenance
  flows. The current general setup offers one Standard new-install action;
  users choose MO2 instance storage later in the application. One setup file
  embeds the isolated payload needed to maintain existing isolated installs.
- Expanded installer text coverage to six languages, gave action cards room
  for translated descriptions, preserved progress output when changing
  languages, and explicitly disposed of controls when switching tasks.
- Corrected release validation so the Python overlay supplies CPython runtime
  files while MO2's native Python module remains sourced from the base MO2
  runtime.
- Excluded the bundled psutil test suite from release payloads and tightened
  the Elden Ring-only game allowlist to reject nested modules for other games.
- Kept source context in diagnostics while mapping the main source tree,
  GitHub++ and build dependencies to neutral path names in the release binary.
- Restyled all seven new-instance steps and the confirmation page, plus the
  Instance Manager, tutorial, and category setup prompts.
- Set the Elden Ring Profiles dialog size before restoring older saved
  geometry, and prevented the lazy Plugins settings tab from unexpectedly
  growing its window.
- Aligned the toolbar notification controls and clarified the Instance
  Manager's folder labels.
- Fixed the Save Isolation confirmation so Enter confirms the explicitly selected
  global Steam route; Escape and **Keep current mode** still preserve the saved
  route.
- Removed Elden Ring's legacy Profiles transfer action because route-aware save
  transfers are provided from Tools.
- Rebuilt and verified the Full MO2 executable and both installer payloads from
  the same path-mapped source build; the Complete and Isolated payloads contain
  matching core files and Save Isolation plugin versions.
- Removed the stale Profiles note and pointed Elden Ring save routing to
  **Tools > Elden Ring Save Isolation**.
- Clarified Game Save Profile behavior for Global Steam saves: the profile
  selector now states that it is unavailable in this mode, and its guidance
  describes the Profile and Instance modes where named saves can be used.
- Added a Restart now / Later prompt after saving a changed Elden Ring save
  route or Game Save Profile. Restart now uses MO2's controlled restart flow;
  this candidate still needs live UI validation of the prompt.

## 1.0.0 — Initial stable release

MO2 Revamped is a community edition based on Mod Organizer 2.5.2, focused on
Elden Ring. This release consolidates the reviewed interface work, Elden Ring
support, safer installer flows, and release packaging.

### Added and improved

- Refined the main window, mod list, context menus, filters, sorting, column
  widths, hover states, alternating rows, and action icons.
- Updated Settings, Profiles, About, Notifications, Overwrite, and mod
  information windows, including file tree, conflict, category, notes, image,
  and plugin tabs.
- Added a compact filter icon and clarified category actions in the mod menu.
- Added privacy guidance and opt-in startup diagnostics for support reports.
- Added Elden Ring archive-layout installation, profile-aware native DLL
  ordering, startup options, save isolation, and the native bridge.
- Added a general installer and a separate portable, isolated Elden Ring
  installer. Both validate embedded payload files and keep rollback data for
  supported update operations. The Elden Ring-only installer refuses to
  maintain unmarked folders, and the uninstall option to erase all selected
  folder data starts unchecked.
- The release builder places the reviewed themes at the `Light.qss` and
  `dark.qss` paths MO2 loads, and omits the unused legacy `+Light.qss` and
  `+dark.qss` copies.
- Improved external-tool process-tree collection by indexing the process
  snapshot once before walking child processes. The optimization covers the
  normal Job Object path and the fallback process-handle path.
- Updated the packaged OpenSSL runtime to 3.5.9 after a source build and
  runtime compatibility checks.
- Updated the embedded CPython 3.12 runtime to 3.12.15, including the standard
  library and the CPython extension set; rebuilt `_ssl` and `_hashlib` against
  the packaged OpenSSL 3.5.9 runtime.
- Removed developer-machine directories from locally built binary debug
  metadata while retaining the code and debug identifiers; release packaging
  now rejects payloads and installers that contain local build paths.

### Compatibility notes

- The base application is Mod Organizer 2.5.2 and USVFS 0.5.7.2.
- Packaged Qt/PyQt remain 6.7.1. Python is rebuilt at 3.12.15 with the
  bundled standard-library bytecode and CPython extensions; `_ssl` and
  `_hashlib` use OpenSSL 3.5.9.
- Elden Ring component versions retain their actual alpha/build labels.
- Qt/PyQt and libloot retain their MO2-compatible versions; the release audit
  records newer versions reviewed and why a coordinated update was deferred.
- The release audit documents the checks performed and the remaining
  in-game validation boundary.
