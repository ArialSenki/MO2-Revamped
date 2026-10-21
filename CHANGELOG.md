# Changelog

## 1.0.2 — Elden Ring Nightreign, safer saves, Fluent dialogs

Released on October 22, 2026. The isolated edition now covers ELDEN RING
NIGHTREIGN as well as Elden Ring: each portable copy manages the game chosen
when it is first opened.

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

### Installer

- Names the edition "Elden Ring and Nightreign" in all six languages and
  shows its version in the header; Enter starts the task, Esc closes.
- Uninstalling after an update removes the program instead of putting the
  previous version back; files an update drops are removed (Restore brings
  them back).
- Without an install record, uninstalling keeps user plugins and themes, and
  "delete all data" names the save folders it would delete.
- Refuses to update or uninstall while Elden Ring or Nightreign runs; a
  missing Windows uninstall entry no longer reports a failed install; MO2 is
  never started as administrator after setup.

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
