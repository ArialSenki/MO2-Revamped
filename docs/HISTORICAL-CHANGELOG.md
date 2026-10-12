# MO2 Revamped development history

This is the consolidated record of the Revamped work reviewed during the
October 5–8, 2026 interface and release-preparation campaign. It groups the
changes by feature so it remains useful without reproducing private machine
paths, screenshots, or local backup details.

## Product and Elden Ring support

- Consolidated Mod Organizer 2 2.5.2 with USVFS 0.5.7.2 and the community
  edition's Elden Ring plugins, themes, native bridge, and setup code.
- Added Elden Ring archive-layout choices, profile-aware native DLL ordering,
  startup options, and per-instance/per-profile save isolation.
- Reviewed the Elden Ring game plugin, bridge, archive installer, save
  isolation, startup options, profile routing, and forced-load flows in MO2.
  These UI checks do not count as launching the game or validating gameplay.

## Main window and everyday workflows

- Reworked the Light and Dark Classic Refined themes and applied them across
  MO2's main window, settings, and dialogs.
- Balanced the mod-list columns, clarified the active sort direction, and
  refined selected, hovered, disabled, and alternating rows.
- Removed the stray `All Mods` entry from the mod context menu, revised the
  category actions (including Change Categories and Primary Category), and
  added an icon to the Filters control that opens its existing options window.
- Added small action icons where they help identify common actions without
  crowding the interface.
- Applied the refined theme and reviewed Downloads, Data, Saves, Logs,
  Notifications, and Overwrite. Preserved the layouts the user found clear;
  restored the prior Notification window size after a larger-size trial.
- Reviewed executable configuration, profile management, plugin settings,
  file previews, mod information tabs, conflicts, categories, notes, and
  Filetree.

## Dialogs, help, and diagnostics

- Updated About, credits and licenses, component summaries, and support
  diagnostics. The report warns that local paths and profile names may identify
  the user. The Credits and Licenses links use a compact area, and the report
  action is localized in Spanish.
- Refined standard and plugin-owned dialogs and removed stale explanatory
  wording where it no longer described the shipped package.
- Made detailed startup and first-chance memory diagnostics opt-in with
  `MO2_REVAMPED_DIAGNOSTICS=1`; the default launch avoids those extra handlers
  and diagnostic file writes.
- Reduced saved geometry for the mod Information/Filetree window so it fits
  both displays. This was a local saved-window-size adjustment; the later
  monitor scaling issue was corrected in Windows settings, not in application
  code.

## Performance and release preparation

- Changed external-tool process-tree discovery from repeatedly scanning the
  full process snapshot for each descendant to building a parent-PID index
  once and traversing it with cycle protection. Applied the same indexed
  traversal to the normal Job Object path and replaced nested process/job-ID
  comparisons with PID-set lookups.
- Audited packaged component versions and kept components whose updates
  require a wider Qt/Python or MO2 integration rebuild documented for a later
  coordinated upgrade.
- Rebuilt the embedded CPython 3.12 runtime at 3.12.15, including its standard
  library and compatible extension set, and rebuilt `_ssl` / `_hashlib`
  against OpenSSL 3.5.9.
- Rebuilt and verified the packaged OpenSSL 3.5.9 runtime from official source;
  retained this supported LTS branch after checking the newer 3.6 line.
- Tested Zig 0.17.0 against the Elden Ring bridge source, then retained the
  existing Zig 0.13.0 compiler because the newer build exports extra internal
  symbols without an explicit export policy.
- Added separate general and Elden Ring-only installer builds with manifest
  checks, per-file SHA-256 verification, and release checksums.
- Removed private build directories from CPython PDB references and stripped
  toolchain debug metadata from the Elden Ring bridge. Rebuilt GitHub++ with a
  neutral source-path mapping while preserving Qt file/line logging context,
  then scanned staged payloads and installer executables for local paths.
- Fixed release theme staging so the reviewed Light and Dark styles replace
  the filenames MO2 actually loads; omitted two unused legacy stylesheet
  copies from the packages.
- Restricted the Elden Ring-only installer to new isolated installs and
  maintenance of folders with the Elden Ring marker; full-folder deletion is
  unchecked by default.
- Built MO2 from a fresh external CMake build directory and prepared isolated
  release payloads without copying user profiles, mods, downloads, logs, or
  instance configuration.

## Release-candidate isolation correction

- Separated Complete Standard's instances and preferences from the original
  MO2 Local AppData store. Complete Portable and Elden Ring Isolated resolve
  their MO2 settings and additional instances within their own installation.
- Removed the save-isolation plugin's original MO2 AppData scan and pending
  mode file. It now discovers profiles from the active Elden Ring instance;
  the optional Steam save route remains in the game's own save directory.
- Made the migration source explicitly user-selected and left it unchanged.
- Added an installer identity marker, restricted maintenance actions to
  Revamped installations, and retained a narrow recognition path for the
  branded v1.0.0 Complete/Portable package.
- Changed the isolated release stage to derive from Complete and verify
  identical core files plus hashes for shared plugins.
- Redesigned the installer after the isolation safeguards with Revamped
  branding, clearer section cards, and a stronger primary action. Both
  installer variants compile and their embedded payload resources pass the
  local manifest/hash checks. User visual and launch review of the exact v1.0.1
  executables remains pending.

## Final pre-release audit — October 9, 2026

- Verified Save Isolation and named Game Save Profile persistence through a
  temporary-directory harness, including the three routes, independent IDs,
  imported saves, backup copying, duplicate rejection, and pending instance
  choices. The open Full process still uses its earlier plugin; the new
  candidate has not been installed or opened for a live UI test.
- Mapped source, dependency, and GitHub++ paths to neutral names in the release
  executable while retaining file/line diagnostics. The completed binary scan
  found no user-profile or source-checkout path.
- Tightened the Elden Ring-only plugin allowlist to reject nested game modules
  and removed the unused psutil test suite from release payloads. Complete and
  Isolated now share a verified core; the isolated installer excludes the
  unrelated S.T.A.L.K.E.R. stub and contains only approved Elden Ring modules.
- Prepared a local v1.0.1 candidate with 1,300 Complete files and 1,200
  Elden Ring Isolated files. Both installers pass manifest, embedded-resource,
  and checksum verification. Neither was launched, the game was not started,
  and GitHub was not updated pending user review.

## Windows display scaling note

A difference between the laptop's 125% scaling and the external monitor's
100% scaling explained the window-size changes when moving dialogs between
screens. Setting both displays to 100% returned the windows to the expected
size. This was a Windows display setting change and is not included in the
application binaries.

## Save-recovery manifest hardening — October 9, 2026

- The final source pass found that `Path("..").name` is still `".."`, so the
  previous single-component check could accept a parent-directory entry in a
  recovery manifest. Recovery now validates the manifest object, directory
  list, every path component, and duplicate names before moving any save
  folder.
- Added a temporary-directory harness for normal restoration, preservation of
  unexpected game output, recovery when a crash leaves no manifest, and
  rejection of malformed, traversal, drive-relative, and duplicate entries.
- The harness passes, and Complete and Elden Ring Isolated contain the same
  updated game plugin. The new local candidate remains unpublished pending
  user review and live UI checks.

## Save-recovery destination validation — October 9, 2026

- Recovery now also requires a non-empty string destination in the manifest
  and verifies that its normalized absolute path matches the current Elden
  Ring save folder. Missing, blank, malformed, or different destinations fail
  closed before a folder is created or moved.
- Extended the temporary recovery harness to verify missing, empty, whitespace,
  non-string, and mismatched destinations. These cases preserve the quarantined
  save and do not create or modify the global save folder.
- The Save Isolation and MO2 runtime-route harnesses pass again, and both save
  plugin files parse. Candidate r3 build and payload checks are recorded in the
  release audit; live UI persistence and visual review remain outstanding.
