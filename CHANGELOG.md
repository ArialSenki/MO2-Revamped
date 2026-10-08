# Changelog

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
