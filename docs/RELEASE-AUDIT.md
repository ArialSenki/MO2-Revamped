# Release audit

## Pre-release — October 27, 2026 (v1.1.0-beta.1)

- Installer: `MO2-Revamped-Setup-1.1.0-beta.1.exe`, 378 MB, SHA-256
  `475058056B6EFEDE779EECAD98E68514A36C3103F1556D048F906538BDC226B0`.
- Built from commit 574bc8f (design/fluent-v4 after the October 27 design
  review) with ModOrganizer.exe compiled from that tree (Release) and the
  1.0.2 nxmhandler.exe (its source is unchanged).
- Complete payload: 1,376 files; Elden Ring Isolated payload: 1,276 files.
  The retired Dark Black theme is left out of both. No personal paths found
  in the payload's text files.
- Each window was checked in Light and Dark on the development build; the
  installer itself was not run (install, update, uninstall) for this
  pre-release. Not Authenticode signed.

## Published release — October 23, 2026 (v1.0.2)

- Installer: `MO2-Revamped-Setup-1.0.2.exe`, 377,820,160 bytes, SHA-256
  `4F5E1F599095117A81871F391CEC2E7E7680DB8F18FBA6398A408ACBAFA30B25`.
- Built from commit 19df444 (design/fluent-v4 with the October 22–23 audits)
  with ModOrganizer.exe and nxmhandler.exe 1.2.0.1 compiled from that tree.
- The main task is the complete installation for every game; isolated Elden
  Ring installations from 1.0.1 are still updated, restored and uninstalled.
- Complete payload: 1,426 files; Elden Ring Isolated payload: 1,326 files.
  Elden Ring game plugin 0.5.0-alpha.75, native bridge 0.5.0-alpha.35.
- Tested with administrator rights: a fresh install with the earlier 1.0.2
  candidate (1c031d4), an update with this build (new ModOrganizer.exe and the
  Revamped nxmhandler in place, backup made), and an uninstall that removed
  the folder and its Windows uninstall entry. Dark title bar, taskbar
  progress, the full-size window icon and "What's new in this version"
  (opens the releases page without administrator rights) checked in a real
  install. Not Authenticode signed.

## Earlier release — October 12, 2026 (v1.0.1, rc12h)

- Installer: `MO2-Revamped-Setup-1.0.1.exe`, 683,894,272 bytes, SHA-256
  `8AE75917426AF3CAA563A0D7B8AA149907CE3E950204B731BA5306DB4BB9CF48`.
- The only new installation it offers is the Elden Ring Isolated edition
  (portable, data beside the installation). Standard and "Copy data" are shown
  as coming soon; update, restore and uninstall are unchanged.
- Complete payload: 2,651 files; Elden Ring Isolated payload: 2,551 files.
- Includes bridge 0.5.0-alpha.35 (window hooks installed on load, optional
  intro-logo skip), the new Revamped prompts, translated tool names, and the
  setup language written to a new `EldenRingOnly.ini`.
- An earlier rc12e build of this candidate passed install, update and uninstall
  with administrator rights; rc12h changes only MO2, the bridge, plugins and
  translations. Not Authenticode signed.

## Earlier local candidate — October 11, 2026 (v1.0.1)

- Installer: `MO2-Revamped-Setup-1.0.1.exe`, 376,334,848 bytes, SHA-256
  `3B7207F3AAB28D3F65FB9CB75516253A9B616991B4397C7CEC577CCC551034BF`.
- Complete payload: 1,321 files / 376,811,824 bytes; ZIP SHA-256
  `B86A9289A1946CA9BF51F919CCB63DDA4E13E82D9158A2E0BC3E8BF0C68CB3D6`.
- Elden Ring Isolated payload: 1,221 files / 367,653,796 bytes; ZIP SHA-256
  `3A93CF292F2AC1B8124B783A1A38DB37638CEDAA041945069D76AE94DBC4464F`.
- The release builder completed against the October 11 source tree. Its checks
  confirmed manifest hashes, Complete/Isolated core parity, embedded installer
  resources, expected plugin files, and absence of local profile/repository
  paths. Manual archive inspection found all three themes and the current
  Elden Ring archive installer in both payloads, with no profile, mod,
  download, overwrite, or log data at the package root.
- The payload contains Light, Dark Blue, and Dark Black styles under
  `stylesheets/Light.qss`, `stylesheets/dark.qss`, and
  `stylesheets/dark-black.qss`. The Elden Ring archive installer in the
  package matches its maintained source.
- The installer is not Authenticode-signed. No files have been uploaded to
  Nexus or GitHub.

This is a build/package audit, not a claim that every installer operation was
tested on a clean second machine. A clean fresh install, in-place update, data
copy, restore, and uninstall pass remains outstanding. A full Elden Ring launch
and gameplay validation were not part of this packaging run. Publish as a beta
and retain normal MO2 data backups when exercising maintenance flows.

## Previous candidate checkpoint — October 9, 2026 (r10b)

> **Previous candidate — October 9, 2026 (r10b, one general setup):** `MO2-Revamped-Setup-1.0.1.exe`, 314,009,088 bytes, SHA-256 `3A3E65FB89E9DF54408AEDF669C7CD4884EDF02098BD1DBB0973C27A461A003E`. The fresh-install screen offers one Standard setup action named **New installation**; it has no Full/Portable/Isolated edition selector. The user reviewed and approved the screen. The single setup embeds the Complete and legacy Isolated payloads; the Isolated payload remains for maintaining existing isolated installations. An isolated operation-matrix test passed 10 groups using temporary targets and a non-elevated QA manifest; the actual elevated interactive install was not run. Full is closed and its executable, Save Isolation plugin, Elden Ring game plugin, and Light/Dark stylesheets match the maintained source. Their verified hashes are `12732D1DCBFA31DFE38CA4FD7BBA3A03CD35EE37519ECC02C20E01D38AC9E7A0`, `A1B3BFD3D6B3F34855C2514109A2511FA912E992B0A4936299D4CF269C5E0938`, `FB2AB0AA9F4CE7643826182D90AC77B9B9E152F312737B9FAFA52B67FA0C7B63`, `7C83096ECCDEE745CFC8270ADCBA134DE509DDCEF3FF208F01E8AFDAD70B236F`, and `C98D598658055A2E2CCD6A03552E09CB837BE3A925585E99DB701360C55ACF90`. The user confirmed profile and instance-shared save routes apply immediately and refresh Saves; switching to Steam requires restart and works after restart. Backup manifests verified; no files were published to GitHub.

> **Previous checkpoint — October 9, 2026 (r9t):** Complete/Portable contains 1,301 files (351,560,721 bytes) and Elden Ring Isolated contains 1,201 (342,402,693 bytes). Both stages and Full use the rebuilt MO2 executable (SHA-256 `550AE56A1861B201C2BC26423A65E414A4484B4A1981F7061CF20CE50D70FA69`) and Save Isolation alpha.77 (SHA-256 `A3DEB400FF535810CEC6B472B20672275BAEB37E3D09160E358EB9791DC4841D`). At that checkpoint, the user confirmed that the Steam save was recognized and **Restart now** returned to the same `Default` profile. The **Later** path and an Elden Ring launch were still unverified then. Installer SHA-256: Complete `55de5c058c69b3723970d397feedc9939252c827b231cc1e3a8808059e36b04c`; Isolated `5795dfb82b9f33d70f070408a6b703ff6ab7772e2e4bb4fedc73543d9bac25ec`. GitHub was not updated.

> **Previous checkpoint — October 9, 2026 (local candidate r9m):** Complete contains 1,301 files / 351,556,120 bytes; Elden Ring Isolated contains 1,201 / 342,398,092 bytes. Both use the same path-mapped MO2 2.5.2 executable (6,237,184 bytes, SHA-256 8D7072C1BCDA47EEBA04AA91CC9411FBC5A2D2E827E15657693541E091852EEC). The Profiles subtitle now points to Tools > Elden Ring Save Isolation instead of a removed section. Save Isolation mode and named game-save-profile selection persisted in a disposable portable clone across dialog reopen and process restart; no real save or profile file was changed. Full now matches the candidate MO2 executable and Complete uninstaller; backups of both previous files are kept locally. The latest native-window visual check and a real Elden Ring launch remain pending. Installer hashes are in the adjacent SHA256SUMS.txt. The candidates are unpublished; GitHub was not updated.

Historical audit snapshot: October 9, 2026. Version claims below describe the files used
to prepare this release, not every upstream project that can be built with
another toolchain.

## Initial candidate snapshot (superseded by later checkpoints)

Save Isolation is alpha.73, and the installer includes the latest multilingual
layout and child-control corrections. The current v1.0.1 setup candidates were
rebuilt from this source and passed payload, embedded-resource, and local-path
checks. The Full installation remains on its earlier build while it is open;
the user-facing dialogs still need a restart and live review.

Current checks on October 9, 2026:

- The current MO2 source built successfully. Its executable is 6,243,328 bytes
  with SHA-256 `abf4581d3fe727b50b6a119d3640f7efcbf9df05708e40839851beba55672cb2`.
  The open Full installation still has its earlier executable, SHA-256
  `c1aed21c618a398938e6bc77a1ce4ee4c58c1afb8b487ab69d953f0ef9cb62ba`.
- Save Isolation Python syntax and a temporary-directory harness passed for
  route persistence, named save profiles, `.bak` copying, duplicate rejection,
  source-boundary checks, and first-run choice handoff. The current alpha.73
  source plugin hash is
  `61804512382eecf2ef17fec61201f0ac7a3eae3571ca4eaadc771160519ce8ed`. Its
  MO2 version label now matches alpha.73. The open Full process still has the
  earlier plugin hash
  `99719432534ba2b842d783526c67cc69053d208fc760179f12f3092f3bfeac7a`.
- The installer source compiles with the .NET Framework C# 5 compiler. Its six
  translations have no duplicate or missing keys, and measured action/option
  labels fit their allocated bounds. The current v1.0.1 candidate packages
  these installer sources; the setup executables have not been launched.
- Both QSS files pass balanced brace, quote, and comment checks. The release
  PowerShell script parses successfully. The release builder regenerated both
  setup candidates, checked the embedded resources and payload manifests, and
  found no current developer-profile or repository-root paths in the staged
  files or installers. It now overlays tutorials from source because MO2 loads
  them from disk. Live MO2 dialogs and installer flows still need user review.
- The open Full process was not replaced or restarted. Its installed Save
  Isolation plugin predates the source changes and remains loaded in memory;
  this is why its current menu does not show the updated behavior. No profile,
  save, instance setting, or user data file was changed.

## v1.0.1 candidate scope

The candidate separates MO2 Revamped's Standard instance store from the
original MO2 AppData directory, keeps Portable and Isolated data within each
installation, removes the save plugin's automatic scan of original MO2 data,
and limits installer maintenance to identified Revamped folders. Its isolated
payload is assembled from the Complete stage and checked for core and shared
plugin hash parity. The general installer offers Full, Portable, and Elden Ring
Isolated; a second installer is dedicated to Isolated. The installer chooser
uses selectable operation cards, and the new-instance wizard, Instance
Manager, startup prompts, Profiles dialog, and Settings sizing receive the
current Revamped styling. Both v1.0.1 setup candidates were regenerated from
the current source on October 9. Their hashes are recorded in the adjacent
`SHA256SUMS.txt`. The candidates are unpublished and still need user review and
launch checks.

## Component decisions

| Component | Packaged or build version | Audit decision |
| --- | --- | --- |
| Mod Organizer 2 | 2.5.2 | Retained; it is the current upstream release used by this source snapshot. |
| USVFS | 0.5.7.2 | Retained; it matches this MO2 integration and current upstream release. |
| OpenSSL | 3.5.9 | Updated from 3.3.0 by rebuilding the supported [3.5 LTS line](https://openssl-library.org/source/) from verified source and running load/TLS checks. Upstream 3.6.5 is newer, but its support window ends in November 2026; 3.5 LTS is supported through April 2030. Both root and `dlls` copies are staged from the same runtime. |
| Qt / PyQt | 6.7.1 | Kept with its matching runtime and native extensions. Qt 6.12.0 and PyQt 6.11.0 are newer; moving requires rebuilding Qt WebEngine, PyQt, and MO2's native extensions, then reviewing the UI again. |
| Python | 3.12.15 | Rebuilt from the official [security-only source release](https://www.python.org/downloads/release/python-31215/). Its embedded standard-library bytecode and CPython extension set are updated; `_ssl` and `_hashlib` are rebuilt against OpenSSL 3.5.9. MO2/PyQt extensions remain on the CPython 3.12 ABI. |
| libloot / LOOT CLI | 0.23.0 / 1.6.0 | Kept with the tested MO2 2.5.2 integration. Current libloot 0.29.6 needs API, binary, and sorting validation before replacement. |
| Boost | 1.85.0 | Build dependency only; not shipped as a runtime update. Boost 1.92.0 is available, but changing it requires another native rebuild and regression pass. |
| Zig | 0.13.0 | Retained for the release bridge. Zig 0.17.0 compiled the current source, but its DLL exported additional MinHook/CRT symbols compared with 0.13.0. The newer compiler is deferred until the export surface is made explicit and checked. |
| Elden Ring components | game plugin 0.5.0-alpha.75; native bridge 0.5.0-alpha.33; archive installer alpha.72; save isolation alpha.75; startup options 0.5.0.64 | Version labels are preserved as reported by the included components. Product version 1.0.1 does not imply each component has independently reached 1.0.0. |

MO2 and USVFS versions were checked against their [upstream release pages](https://github.com/ModOrganizer2/modorganizer/releases)
and [USVFS releases](https://github.com/ModOrganizer2/usvfs/releases). Qt,
Python, OpenSSL, Boost, libloot, and Zig decisions use their official
release/support information. The package intentionally avoids an untested
framework or ABI migration.

## Performance and source hygiene

- External child-process collection now indexes the process snapshot by parent
  PID once and walks only descendants. The normal Job Object path also matches
  snapshot entries through a PID set instead of comparing every process with
  every job ID. These paths now scale with the snapshot/job size plus the
  descendants visited, rather than repeated full-list scans. This is an
  algorithmic improvement; no general UI or frame-rate benchmark is claimed.
- The release builder scans staged payload files and final installer
  executables for the current developer profile and repository-root paths.
  MO2 uses MSVC source-path mapping for its source and dependency trees.
  GitHub++ keeps Qt's file/line logging context, with its source path mapped
  to a neutral prefix. The Elden Ring bridge uses Zig
  source-path mapping and strips debug metadata pulled from Zig's bundled
  unwind library. CPython's locally built DLL and extension modules retain
  their CodeView identifiers while PDB directory paths are reduced to
  filenames. The check does not rewrite unrelated upstream binaries, which
  may retain upstream build-path strings.
- Startup diagnostics are opt-in. Normal startup skips the detailed memory
  exception hooks and per-step diagnostic writes.
- The release builder excludes user data, saved instance state, logs, caches,
  backups, and build scratch from payloads. Local rollback snapshots and build
  work directories are ignored by Git.
- The release builder copies the reviewed themes to the active `Light.qss` and
  `dark.qss` filenames and omits the unused `+Light.qss` / `+dark.qss` files
  inherited from the Complete runtime. It verifies both staged theme hashes
  against the source files.
- The Elden Ring-only installer accepts fresh isolated setup, or maintenance
  of an existing folder carrying its Elden Ring isolation marker. It refuses
  unmarked folders. The uninstall option to remove all selected-folder data
  starts unchecked.
- No broad dead-code deletion was made without evidence that the code was
  unreachable; removing unrelated MO2 core paths would risk existing workflows.

## Prior candidate build and validation evidence (superseded by r9m)

- The MO2 2.5.2 candidate was rebuilt with MSVC and Qt 6.7.1. Its executable
  is 6,243,328 bytes with SHA-256
  `abf4581d3fe727b50b6a119d3640f7efcbf9df05708e40839851beba55672cb2`.
  It differs from the earlier executable still running in Full and passes the
  local developer-profile and repository-root path scan.
- The release builder produced two version 1.0.1 installers: the general
  Complete/Portable/Isolated installer and the dedicated Elden Ring Isolated
  installer. The general installer is 310,760,960 bytes with SHA-256
  `6bd8536b5d3ffbc35ca02dd7c2156bb3ad70287b99f262fd4938034deb44553e`;
  the isolated installer is 153,533,440 bytes with SHA-256
  `c0d9258f53c3a1427a8edca8b7489e5c8a4c3960e59df462777f63bd8c434f70`.
  They are not Authenticode signed. `SHA256SUMS.txt` accompanies the
  installers and records these hashes.
- The general payload contains 1,300 files. The Elden Ring isolated payload
  contains 1,200 files and excludes nested modules for other games, the bundled
  psutil test suite, the PyCfg configuration editor, user data, and build
  scratch.
- The release builder verifies each payload manifest's file count, size, and
  hashes, confirms the expected resources in each installer, and scans staged
  payloads and final installers for the local user profile and repository
  paths.
- OpenSSL 3.5.9's CLI and default provider loaded from the staged runtime. A
  TLS 1.3 connection to Nexus Mods completed with hostname and certificate
  verification. The full upstream OpenSSL test suite was not run.
- The Python 3.12.15 runtime and OpenSSL 3.5.9 extensions completed a run of
  1,267 upstream SSL, tarfile, ZIP, and urllib tests: 1,194 passed and 73 were
  skipped by the Windows test environment. The embedded `pythoncore.zip` was
  exercised without a source-tree fallback, and a verified HTTPS request to
  python.org returned HTTP 200.
- The general installer provides Complete, Portable, and Elden Ring Isolated
  options and supported maintenance actions. The separate installer provides
  the isolated Elden Ring edition and its update, restore, and uninstall
  actions.
- The isolated payload uses an explicit plugin allowlist. It omits Skyrim/FNIS
  and OMOD installation tools while keeping DDS previews and common
  FOMOD/manual/quick installers used by Elden Ring packages.
- The new installers have not been launched after these changes. Full remains
  open on its earlier build. Visual review and user launch checks remain
  pending. The game has not been launched; gameplay behavior is unverified.

## Deferred updates

Qt/PyQt remains at 6.7.1 because moving to a newer Qt line requires rebuilding
Qt WebEngine, PyQt, and MO2's native extensions, then repeating the UI review.
libloot and the Elden Ring modules also need their own API and integration
checks before independent updates. Rebuild the release if any of these
dependencies changes later.

## Follow-up candidate r6 — October 9, 2026

A final Save Isolation review found that the “save profile unavailable” marker
could remain set after choosing standard saves. The dialog now recalculates
that marker from the currently selected save-profile ID on every refresh. The
missing-profile recovery case is included in the temporary-directory
verification harness.

The harness passes route-mode persistence, independent IDs, named save
creation/import, `.bak` copying, duplicate and source-boundary rejection,
pending instance choices, and recovery from a missing named profile. All seven
maintained Revamped Python modules parse successfully. The release builder
rebuilt and verified both installers, their embedded payloads, manifests,
theme files, core parity, plugin allowlist, and local-path scan. Complete and
Portable contain 1,300 files; Isolated contains 1,200. The Save Isolation
plugin in source, Complete, and Isolated has SHA-256
`e71cb75c4f399863c5a2c93f3185f665717e2379fc2ed63990ad1fa2b37e362f`.

The new local candidates supersede r5:

- General Complete/Portable/Isolated installer: 310,760,960 bytes; SHA-256
  `c97518f71eb50bfa7d83160b584bda7a2dbe48e45200dd33a2cf5f5768756c8a`.
- Elden Ring Isolated installer: 153,533,440 bytes; SHA-256
  `bd0053b213a6f09f575b2328d234c3c53ab879bdfefe56d2da723dbecec0556`.

These candidates have not been launched. The existing Full process still has
the earlier plugin loaded, so live save-mode persistence and visual checks
remain pending. No user profile or save data was changed; GitHub was not
updated.

## Final copy and stylesheet review — candidate r7

The last review found that Elden Ring launch warnings still directed users to
the former Profiles controls. Those messages now direct users to **Tools >
Elden Ring Save Isolation** and explain when MO2 must be restarted. A search of
the maintained source confirms that these launch warnings no longer mention
the old Profiles location. I also removed the unused `eldenRingSaveModeOptionCard`
rules from both themes; that selector had no remaining widget or source-code
reference. Both QSS files pass balanced-brace, quote, and comment checks.

The r7 installers were regenerated after these changes. Their payloads contain
1,300 Complete/Portable files and 1,200 Elden Ring Isolated files. The builder
verified the payload manifests, hashes, embedded resources, isolated core
parity, plugin allowlist, theme hashes, and local-path scan. The Save Isolation
and Elden Ring game plugins in source and in both payloads match their source
hashes:

- Save Isolation: `e71cb75c4f399863c5a2c93f3185f665717e2379fc2ed63990ad1fa2b37e362f`.
- Elden Ring game plugin: `4a6a7a41937a078998e54e7808ce6e66905a80d64493390ca427561f3cfc0f85`.
- MO2 executable: `abf4581d3fe727b50b6a119d3640f7efcbf9df05708e40839851beba55672cb2`.

Candidate r7 supersedes r6:

- General Complete/Portable/Isolated installer: 310,760,960 bytes; SHA-256
  `52344b37ee4ffdfde38bf12d0e40e89a7e78169bc987d52306ba9a4f6460d4cf`.
- Elden Ring Isolated installer: 153,533,440 bytes; SHA-256
  `22e60646847c9312dc76e889a7b43dcae4341e7094db81e5a0b4e6ddd849632a`.

The installers and MO2 UI have not been opened for live verification. The
existing Full process still runs the earlier plugin, so route persistence must
be checked after closing that session and opening the candidate. User data,
profiles, and saves remain unchanged; GitHub has not been updated.

## Save-recovery manifest hardening — candidate r2 — October 9, 2026

The final source pass found that Python's `Path("..").name` is still `".."`.
The old recovery-manifest check could therefore accept a parent-directory
entry before an `os.replace` operation. Recovery now validates the manifest's
top-level object, directory list, all path components, and duplicate names
before it creates the global save folder or moves any directory. Invalid data
fails closed and leaves quarantined save folders in place for review.

The new temporary-directory harness passed normal restoration, preservation
of unexpected game output, crash recovery with no manifest, and rejection of
malformed manifests, `..`, separator and drive-relative paths, and duplicate
names before any file move. The existing Save Isolation and MO2 runtime-route
harnesses also passed. Updated game-plugin source and both Complete and
Elden Ring Isolated staging files match at SHA-256
`ca8f36b295fbb72cfcf2e35b83a923461c5c6bd862f9db239a9051e385cea0a6`.

Candidate r2 was built from the Complete runtime with the same isolated plugin
allowlist and core-parity checks. Complete/Portable has 1,300 files and
Elden Ring Isolated has 1,200. The payload manifests and embedded resources
passed the release builder; the installer hashes are recorded in
`SHA256SUMS.txt`:

- General Complete/Portable/Isolated installer: 310,761,472 bytes; SHA-256
  `ec56e7fe9884dd1f689f7280db2b9869593c61ce863f8993b4f451837eb113e4`.
- Elden Ring Isolated installer: 153,533,952 bytes; SHA-256
  `4d6f8b9265ba3ad89285b643e511cb8aa18305543be512a3904c717657d31cbd`.

The r2 candidate supersedes r1 and r7. Full remains open on r7; its loaded
plugin is unchanged. The new installer and interface have not been opened, no
Elden Ring game process or save file was touched, and the actual save-mode
click/reopen check and visual review remain pending. GitHub was not updated.

## Save-recovery destination validation — candidate r3 — October 9, 2026

Recovery now rejects manifests that omit the global save folder, provide a
blank or non-string path, or name a different folder. The destination must
normalize to the current Elden Ring global-save path before the recovery code
creates the destination or moves a quarantined directory.

The updated temporary-directory harness passes normal restoration, collision
preservation, crash recovery with no manifest, invalid folder names, malformed
manifests, and missing, empty, whitespace-only, non-string, or mismatched root
paths. Rejected input leaves the quarantined save in place and does not create
the global folder. The Save Isolation and runtime-route harnesses pass, and
both Python plugin modules parse.

Candidate r3 was built from the reviewed Complete runtime. Complete/Portable
contains 1,300 files and Elden Ring Isolated contains 1,200. The game plugin
matches between source and both payloads at SHA-256
`315586a66246d864baef005f678ada195500d15517b70faf5ad577255ba2bc47`; Save
Isolation matches at
`e71cb75c4f399863c5a2c93f3185f665717e2379fc2ed63990ad1fa2b37e362f`.

- General Complete/Portable/Isolated 1.0.1 installer: 310,761,472 bytes;
  SHA-256 `5617d2def68b56e1115219d24f781a41d95285d3d3f22ece917ff47a3229bbf9`.
- Elden Ring Isolated 1.0.1 installer: 153,533,952 bytes; SHA-256
  `03c3f6a8dbc440a6f34a08a8e13f15823ee8651a00a2a06b5b565df0783db6dc`.

The release builder validated payload counts, manifests, embedded resources,
Complete/Isolated core parity, the isolated plugin allowlist, and packaged
hashes. Release notes were checked against their source copies. Full remains
open on r7 and the instance save route remains `mode=profile`; no Elden Ring
process or save file was touched. The live click/reopen check, visual review,
and GitHub publication remain pending.

## Save tools alpha.74 y candidato corregido — 9 de octubre de 2026

- La persistencia del modo de guardado y los Game Save Profiles quedó auditada
  con tres verificadores temporales. Pasan los controles de persistencia,
  perfiles independientes, creación e importación de perfiles de partida,
  copias de respaldo, validación de rutas, selección global, guardas de
  lanzamiento y recuperación segura de partidas en cuarentena.
- Profiles ya no duplica Save Isolation, Game Save Profiles ni la transferencia
  de guardados. Tools es el punto único de gestión posterior a la creación; el
  asistente de nueva instancia conserva sus opciones iniciales de guardado. Se
  eliminó el ajuste de plugin que reactivaba la interfaz antigua. Save Isolation
  se identifica como alpha.74.
- Los seis módulos Python del paquete compilan en memoria. El análisis del
  instalador encuentra 206 textos, seis idiomas, ninguna clave duplicada,
  ninguna llamada directa sin traducción y ninguna línea de traducción mal
  formada. Los temas claro y oscuro conservan llaves, comillas y comentarios
  equilibrados.
- La primera construcción del candidato fue rechazada por la ruta de
  compilación incrustada en OpenSSL. Se cambió a la distribución saneada de
  OpenSSL 3.5.9; sus DLL coinciden en tamaño y SHA-256 con las del candidato
  anterior que pasó esa comprobación.
- La primera religa limpia omitió el manifiesto que la construcción normal de
  CMake incrusta. Ese ejecutable no abrió una ventana ni añadió entradas al
  log, por lo que se restauraron inmediatamente los tres archivos anteriores
  de Full. La configuración del perfil y el archivo de rutas de guardado
  conservaron sus SHA-256, y no se inició Elden Ring.
- El ejecutable corregido incorpora el manifiesto extraído de Full y contrastado
  con el manifiesto generado por CMake: ambos especifican `asInvoker` y la
  dependencia del ensamblado `dlls`. La extracción posterior confirmó igualdad
  del recurso incrustado.
- Full quedó restaurado con el ejecutable y plugins previos y se abrió de
  nuevo con su título principal, el log actualizado y Save Isolation alpha.73.
  Sus hashes de ejecutable, plugin de guardado y preferencias están registrados
  en el respaldo `full-save-tools-alpha74-before-20261009`.
- El candidato r3 pasó el constructor de publicación: 1,301 archivos en
  Complete/Portable y 1,201 en Elden Ring Isolated; coincidencia SHA-256 de
  `ModOrganizer.exe` y los tres plugins compartidos entre ambos payloads;
  validación de manifiestos, recursos, allowlist y rutas locales. Sus
  instaladores son Complete/Portable/Isolated 1.0.1, 310,750,208 bytes,
  SHA-256 `970da675b0ed3892d35e84aa866ffe5cff55c0d6f3daa5197ab709168bcb0db2`,
  y Elden Ring Isolated 1.0.1, 153,528,320 bytes, SHA-256
  `467f02052f13aad50f0ede4e98ea28a2816feef335e61e75d50091885bd69b5f`.
- El ejecutable r3 incorpora exactamente el manifiesto conocido de Full y del
  enlace CMake: `asInvoker` y dependencia del ensamblado `dlls`. El constructor
  no detectó rutas locales de compilación. El intento de inicio anterior usó
  el ejecutable intermedio r2 sin ese recurso, se bloqueó antes de abrir una
  ventana y se revirtió; r2 no debe usarse.
- Full permanece abierto con el ejecutable y plugins anteriores restaurados.
  Sus archivos coinciden con el respaldo y las preferencias del perfil siguen
  con los hashes registrados arriba. El ejecutable r3 todavía no se probó en
  una sesión completa: se espera a que Full esté cerrado, porque MO2 comparte
  su bloqueo entre instancias. Computer Use no expone ventanas nativas en esta
  sesión, así que falta la revisión visual de alpha.74.
- No se abrió ningún instalador ni se lanzó Elden Ring. GitHub permanece sin
  cambios a la espera de la revisión del usuario.

## Save tools alpha.75 y candidato r4 — 9 de octubre de 2026

- El filtro de eventos ahora pasa al refresco el diálogo que acaba de abrirse.
  Antes recorría todos los widgets de nivel superior tras cada evento Show;
  ese repaso completo queda únicamente para la inicialización inicial. La
  aplicación de rutas pendientes sigue asociada a inicialización y cambio de
  perfil. El análisis del árbol confirma que no hay temporizadores periódicos.
- Los tres verificadores de persistencia, rutas de runtime y cuarentena vuelven
  a pasar. También pasan compilación en memoria de los seis módulos Python,
  localización del instalador (206 claves, seis idiomas, ninguna sin traducir),
  equilibrio de los dos temas y la comprobación AST del refresco individual.
- El componente Save Isolation se identifica como alpha.75. El candidato r4
  pasó el constructor: 1,301 archivos Complete/Portable y 1,201 Isolated;
  hashes idénticos para el ejecutable y los componentes compartidos; allowlist,
  manifiestos, recursos y rutas locales validados. Sus instaladores son
  Complete/Portable/Isolated 1.0.1, 310,750,720 bytes, SHA-256
  `dabf6064b3a56f720d4ff6b5eca66f4e15a5f4b6772b8411a065414a35c26fe2`, y
  Elden Ring Isolated 1.0.1, 153,528,320 bytes, SHA-256
  `a693dbb0550d4ee1ef1c4cf69460821d102631609e57c910d0771cfebeb6bdbc`.
- r4 reemplaza a r3 para la siguiente revisión y sus hashes coinciden con
  `SHA256SUMS.txt`. Todavía no se ha copiado a Full ni probado en una sesión
  completa: Full permanece abierto con la versión anterior y sus datos siguen
  sin cambios.
- No se abrió ningún instalador, no se lanzó Elden Ring y GitHub permanece sin
  cambios.

## Latest candidate and dependency review — October 9, 2026

- Candidate r5 was rebuilt locally from the current Complete stage and current
  Revamped sources. Complete contains 1,301 files; Elden Ring Isolated contains
  1,201. Neither stage contains `.pyc`, `.pyo`, or `__pycache__` files. The
  isolated package's shared core and plugin allowlist checks passed.
- Both embedded payload archives passed their manifest and SHA-256 checks, and
  the installer contains only the expected embedded resources. No developer
  or repository paths were found in the release files.
- General Complete/Portable/Isolated installer 1.0.1:
  `dd35b71355069026c6fb8dfe614cf0e48c1a5e387f5f39e6b10c1e8c2cb210c1`.
- Elden Ring Isolated installer 1.0.1:
  `4a042ff8014f8a41c562bd4974f0053117e30271773b9d687e9f90f236632973`.
- All 259 Python modules in Complete and 185 in Isolated compile in memory.
  The installer has 206 translated strings in six languages, with no duplicate
  keys, malformed entries, or untranslated direct UI strings. Light and Dark
  QSS structure checks pass.
- Full was updated from this candidate after backing up the six changed files.
  A normal startup loaded MO2 2.5.2, USVFS 0.5.7.2, game support alpha.75,
  native bridge alpha.33, archive installer alpha.72, and Save Isolation
  alpha.75 without error or warning entries. The process was closed normally;
  save-routing and profile settings retained their previous hashes, and the
  game was not started.
- Dependency review refreshed against upstream sources on October 9: MO2 2.5.2
  and USVFS 0.5.7.2 are the latest releases. Python 3.12.15 is the latest
  security release in the embedded 3.12 ABI line; Python 3.14 is the current
  feature line and is not a compatible drop-in. OpenSSL 3.5.9 is the current
  3.5 LTS patch; the newer 3.6 line has a shorter non-LTS support horizon.
  Explorer++ 1.4.0 is current. Qt 6.12 LTS was released September 30. PyQt
  6.11.0 itself adds Qt 6.11 support, and PyQt-builder 1.20.0 added Qt 6.12
  build support on October 4; the MO2 Qt/WebEngine/PyQt/native stack has not
  been rebuilt or regressed against that pair. The remaining pinned versions
  are 7-Zip 24.05 (26.04 released October 5), libloot 0.23.0 (upstream 0.29.6),
  Boost 1.85.0 (upstream 1.92.0), and Zig 0.13.0 (stable 0.17.0 released
  October 2). Their ABI, API, archive, sorting, native-build, and bridge-export
  checks are still required before adoption. See [`UPSTREAMS.md`](UPSTREAMS.md).
- Save Isolation's route persistence, named game-save profiles, runtime
  mappings, and recovery guards passed the focused verification harnesses.
  The Qt dialog-handler harness now also passes against both staged payloads
  with MO2's Windows Qt platform. It exercises the real widgets and callbacks,
  but bypasses OS-visible manual interaction and does not replace the user's
  visual review.
- The Full window was not captured after its update. Installer UI QA
  screenshots were reviewed for all six task modes in Spanish and the initial
  screen in all six locales (English, Spanish, French, German, Italian, and
  Brazilian Portuguese). The QA harness differs from the source only by
  environment-variable controls for selecting the displayed mode and language.
  The actual release installer was not used to install, update, restore, or
  uninstall anything. No game launch or GitHub publication occurred.

## Candidate r7 and Save Isolation persistence recheck — October 9, 2026

- Rebuilt the candidate after correcting the Python overlay input. The earlier
  r6 attempt stopped before producing installers because the selected overlay
  lacked its runtime manifest; r6 is not a release candidate. Candidate r7
  passed the manifest, payload, plugin allowlist, core equality, and embedded
  resource checks with 1,301 Complete/Portable files and 1,201 Elden Ring
  Isolated files.
- The Save Isolation plugin SHA-256 is identical in maintained source, the
  updated Full install, Complete, and Isolated:
  `1A1272548E09B02E8C236E15B7329908A44CC3F448DB75FB95620C9336C05621`.
  The build now rejects a stale or mismatched plugin in either payload.
- The Qt dialog round-trip passed against both staged payloads. It changes to
  Global/Steam, closes and reopens the dialog, then verifies the selection;
  it also verifies separate named save IDs for Profile and Instance modes.
  Fresh-process checks against Complete and Isolated independently confirmed
  the Global route and a named game-save-profile ID survive process exit.
- Save-profile create/import checks passed for both payloads, including save
  data and backup copying, duplicate-name rejection, source-boundary checks,
  and Global-mode restrictions. Runtime route and launch-guard checks, plus
  save-recovery and malformed-manifest protections, passed for both payloads.
- Complete/Portable setup 1.0.1 is 310,750,720 bytes, SHA-256
  `c55dd4e010ee88faebba39dfbd1efc6e12780181ce3e8c2abc4807220256e9a3`.
  Elden Ring Isolated setup 1.0.1 is 153,528,320 bytes, SHA-256
  `dc085bb1347e91a3e24d21f47487af7b613dde54a3ffd2905da5cd549d15fd33`.
  Both match `SHA256SUMS.txt`; the release audit and upstream notes in the
  output folder match the repository copies.
- The user's active Default profile remains on `mode=profile` with its original
  SHA-256 `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65`.
  No user save route or game save was changed. GitHub remains untouched.
- The old base-tree Save Isolation copy was removed only after backing it up;
  its former SHA-256 is recorded above. The installer screenshots have now
  been reviewed; the remaining manual gate is visual review of MO2's native
  windows. No interactive installer operation or game launch was performed.
- Rechecked the nine tracked files in Full against the r7 Complete stage. Eight
  have identical SHA-256 values. The native bridge's raw file hash differs
  (`a9935244...` in Full and `55bffe34...` in r7), but a byte comparison found
  only two changed bytes at PE file offsets 128–129, the COFF build timestamp.
  All six PE sections (`.text`, `.rdata`, `.data`, `.pdata`, `.tls`, `.reloc`)
  have identical hashes; the normalized PE hash is
  `72dbd557aae24c59cc9ecfc0da4be9b5bcd490ef1fc77e89e552fce7f034256e`. No
  bridge replacement is needed.
- The `APPLIED-CANDIDATE-SHA256SUMS.txt` in the older Full backup is an
  intermediate ledger, not a manifest of the current Full install. It differs
  for four of the nine entries after subsequent rebuilds. I preserved it and
  wrote a fresh local snapshot as `work/FULL-R7-ACTUAL-SHA256SUMS.txt`.

## Candidate r8 — October 9, 2026

Candidate r8 updates the installer UI source and rebuilds both setup programs. Its Complete/Portable and Elden Ring Isolated payload archives are byte-for-byte the already verified r7 payloads; MO2 and plugin payloads were not changed for r8.

- Fixed the installer card background bleed in CompactSetupForm.cs. SectionGroupBox and NewGroup now use the same white base as their rounded card body, while the content area stays page-gray. This prevents transparent child labels from showing rectangular gray strips inside the rounded cards.
- Both setup executables compiled from the current C# source. The general Complete/Portable/Isolated installer is 310,750,720 bytes, SHA-256 99afc3847cd8f7eeeb292817235de9d6bd4679e2d74827647a426f5d1e7fabf2. The Elden Ring Isolated installer is 153,528,320 bytes, SHA-256 a4070de3fd31d43d1a4fb27ed5b446d8184d3f6c15d992fd173eed4a344d2eab. Both are unsigned. The adjacent SHA256SUMS.txt matches.
- Embedded-resource validation passed for the four expected resources in the general installer and the two expected resources in the Isolated installer. Their payload resources match the verified r7 archives and manifests.
- Repeated static checks passed: syntax for 7 maintained Python modules, XML parsing for 32 Qt UI files, QSS structure for both themes, and full hash/size manifests for 1,301 Complete/Portable files and 1,201 Isolated files.
- Save Isolation dialog and fresh-process tests passed using temporary directories: a Global/Steam route and named save-profile IDs survived closing the dialog and a new process. Named save-profile create/import, runtime routing, and recovery checks remain covered by the earlier r7 tests. These tests do not exercise a user's live MO2 click.
- The actual Default profile route file still contains mode=profile, last modified October 8 at 22:39:57. The installed Full Save Isolation plugin matches maintained source at SHA-256 1A1272548E09B02E8C236E15B7329908A44CC3F448DB75FB95620C9336C05621. No current log records a successful route change. The Global/Steam choice intentionally displays a confirmation whose default is No; declining or dismissing it restores the previously stored choice. I cannot conclude from temporary harnesses that the reported live interaction is fixed. The user should confirm the prompt with Yes, then reopen the tool and check the selection.
- The exact r8 executable was not launched for visual inspection. The source correction is compiled, but a new screenshot is still needed to confirm the gray-strip fix on the real Windows form. Native MO2 dialogs and the Elden Ring launch were not tested in this pass.
- Full is closed and unchanged. Its MO2 executable remains the verified r7 build, and the real profile route and save files were not changed. GitHub remains untouched.

The runtime review found no update that is safe to drop in without a coordinated regression: Qt/PyQt and WebEngine require a matched rebuild; 7-Zip and libloot require archive and sorting validation; Boost and Zig require native build/export checks. See UPSTREAMS.md for the current version review. This candidate remains unpublished and awaits user review.

## Candidate r9c — October 9, 2026

- The Save Isolation global-save warning now has explicit **Use global Steam save** and **Keep current mode** actions. Keeping or dismissing the prompt leaves the saved route unchanged and reports that outcome in the dialog and MO2 log. The correction is in source, Complete, Isolated, and Full at SHA-256 `B837C85F9179A3C53BAFB2BD43FBA35B60919F69C69804AED9F9A44AFC377D3F`.
- Full had still contained the previous plugin (`1A1272548E09B02E8C236E15B7329908A44CC3F448DB75FB95620C9336C05621`), explaining why the earlier behavior could remain visible there. With MO2 closed, I saved that exact plugin to `work/FULL-save-isolation-pre-r9c-20261009/backup/eldenring_mo2_save_isolation.py`, then installed and hash-verified the corrected copy. The MO2 executable was not replaced (SHA-256 `DB17CD93E3ADAF60247AD4177A5168F468DB09995940EB481EF3470B4C442A51`). The active Default profile file remains `mode=profile`, last modified October 8 at 22:39:57, SHA-256 `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65`; no game save was changed.
- Fresh-process checks passed for Profile, Instance Shared, and Global routes, including independent named save-profile IDs. The existing focused suites also passed for Save Isolation dialog callbacks, game-save-profile creation/import and backup copying, duplicate and source-boundary rejection, runtime routing, and save-recovery guards. Those tests use temporary data. A repeated Qt test in this sleeping desktop session stalled while creating QApplication and was stopped; no real click/reopen interaction was observed after updating Full.
- The release builder completed both payloads: Complete/Portable is 1,301 files / 351,551,684 unpacked bytes; Elden Ring Isolated is 1,201 files / 342,393,656 unpacked bytes. Python syntax (7 maintained modules), Qt UI XML (32 files), both QSS structures, payload hashes, embedded resources, and installer sums passed. General setup: 302,369,792 bytes, SHA-256 `9cbda5fb57ff3768550b457a602a831c05392a4b57cdf0f2ff381d278e372572`. Isolated setup: 149,376,000 bytes, SHA-256 `d5442a98c843353f3d3bd351fe311b246dd09fe5f9a6df6c51c7efa56f19eaa6`. Both are unsigned.
- The release builder's local-path scan now streams files larger than 4 MiB in 1 MiB chunks, preserving path matches that cross chunk boundaries. ASCII and UTF-16LE boundary fixtures passed; a clean fixture produced no false positive. Observed working set during the scan fell from about 1.8 GB to about 170 MB on the same stage. This is a measured run, not a benchmark.
- Section review covered the main window and Run/Data areas, Settings tabs, Profiles, Instance Manager, all seven new-instance steps and confirmation, startup prompts, theme rules, Elden Ring support and save routing, native archive installation, and the multilingual installer. Code and layout checks found no new reason to remove a working feature. The previously reported Settings and Profiles geometry warnings were not reproduced in a live MO2 window: current Settings code constrains the plugin page and scroll views, while the old Save Isolation controls have been removed from Profiles. Do not treat that source inspection as visual confirmation.
- Full remains closed after the plugin copy. No save route was forced, no game was launched, and the setup programs were not used to install, update, restore, or uninstall. GitHub was not changed. Next validation is to open Full, choose **Use global Steam save** only if intended, reopen the tool to confirm the mode, and review the native windows; then decide whether to keep this candidate.

## Candidate r9f — October 9, 2026

- Completed a second performance and correctness pass on the Elden Ring native-profile resolver. It now reads and validates each mapped mod's installed-native route index once per resolver call, then reuses that result for all DLL entries from that mod. It also builds active-mod name and priority maps once instead of repeatedly scanning the active list. A deterministic equivalence check covered 6,330 origin combinations and confirmed the new winner selection and case-insensitive mapping preserve the previous ordering behavior. This is a bounded launch-time/forced-load improvement; no idle background scan was added, and no runtime benchmark is claimed.
- Checked the reported child-process slowdown against the current loops. MO2 builds a process tree only while following an application launched from MO2; weak targets are rechecked with a 50 ms to 2 s backoff, while a running target is waited on through 50 ms `WaitForSingleObject` waits so the lock UI can respond. There is no idle process-tree scan. The native bridge scans loaded DLLs up to 40 times at 250 ms after game startup, then stops; startup UI alignment also stops after 40 checks. The USVFS log worker backs off to 200 ms when its queue is empty. One explicitly configured native initializer can remain in a readiness loop indefinitely if it keeps returning false (250 ms between calls, status logged every 5 seconds); I left its contract unchanged because false can mean legitimate delayed readiness and no failing mod/log evidence was available. No performance benchmark justifies changing those core waits.
- The updated game plugin is in maintained source and Full at SHA-256 `3BC93734C7501A315067E28FF77A5A701A1840D085CD9803C8532A8A012D37A2`. Before copying, the exact previous Full file was saved at `work/FULL-game-plugin-pre-r9d-20261009/backup/game_eldenring_mo2.py` (previous SHA-256 `315586A66246D864BAEF005F678ADA195500D15517B70FAF5AD577255BA2BC47`). Full's Save Isolation plugin remains identical to source at `B837C85F9179A3C53BAFB2BD43FBA35B60919F69C69804AED9F9A44AFC377D3F`. Full's MO2 executable was not replaced and remains `DB17CD93E3ADAF60247AD4177A5168F468DB09995940EB481EF3470B4C442A51`. Its latest startup log points the instance data to `AppData/Local/MO2 Revamped/Instances/ELDEN RING`, separate from the original MO2 data directory.
- Candidate r9f rebuilt from the Full runtime and the current source. Its Complete/Portable payload contains 1,301 files / 351,552,306 unpacked bytes; Elden Ring Isolated contains 1,201 files / 342,394,278 bytes. Compared with r9c, neither payload has missing or added files. The only changed payload files are the Elden Ring game plugin, the freshly built native bridge, and the generated uninstaller stub. Python syntax for 7 maintained modules, XML parsing for 32 Qt UI files, both QSS structures, and all payload manifest hashes passed.
- Complete/Portable installer: 310,751,232 bytes, SHA-256 `387B53E64C757B92302595F944C915780690CEE9B6DE434B830E52579340BDC5`. Elden Ring Isolated installer: 153,528,832 bytes, SHA-256 `094B231C5F42BF5D5117F7C8B8E0A26E5B8C480F0B004E70BA91AC79C9442E27`. Both match the release `SHA256SUMS.txt`; both are unsigned. The bridge sections and exports match the preceding candidate; the raw PE differences are limited to the expected build metadata.
- Save profile logic checks passed against maintained source in temporary directories: all three route values, separate Profile/Instance named-save IDs, named profile creation/import, `.bak` copying, duplicate rejection, global-mode restrictions, source-boundary validation, pending-choice storage, and recovery behavior. I found that the earlier Qt dialog harnesses replaced `QDialog.exec` without simulating the separate **Use global Steam save** confirmation; their earlier Global-route pass is not proof that this confirmation was accepted. I corrected all three work-only harnesses and executed the production persistence callback against accepted, kept, and dismissed decisions: Global is written only after explicit acceptance, while Keep/dismissal restores the stored mode. The same callback test also persisted and reread distinct Profile and Instance game-save-profile IDs. The repeatable source-level checks are `work/verify_save_isolation_confirmation_callback_20261009.py` and `work/verify_save_isolation_qsettings_interop_20261009.py`; the latter confirms that Full's Qt `QSettings` and Python `ConfigParser` read and write the same route while preserving the named-save ID. A simulated Qt dialog run against Full still hung while creating `QApplication` and was stopped, so no live click-and-reopen test was completed. The active Default profile still has `mode=profile`, unchanged timestamp October 8 at 22:39:57 and SHA-256 `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65`. The newest available MO2 log records Save Isolation alpha.75 initialization but no tool-open, route-save, or cancellation event; it therefore does not show a fresh selection attempt. If the confirmation was dismissed, the retained `profile` mode is the expected result; acceptance still needs live confirmation.
- The larger section-by-section source/layout review remains as recorded in r9c: main window and Run/Data, Settings, Profiles, Instance Manager, the complete new-instance wizard, startup prompts, themes, Elden Ring save/native support, archive installation, and the multilingual installer. No new static issue justified removing a feature. Native MO2 windows still need a real visual review; no game was launched and no user save or route was changed.
- Full remains closed. GitHub was not changed. Candidate r9f is ready for review, but the live Save Isolation interaction and native-window visual check remain user-facing validation gates.


## Candidate r9g — October 9, 2026

- Corrected the Save Isolation global-route confirmation default. When Global is selected, Enter now activates **Use global Steam save**. Escape and **Keep current mode** still cancel and retain the previous route. This fixes the ambiguous default that could make an Enter keypress appear to discard the user's selection. The updated source, Complete stage, Elden Ring Isolated stage, and Full copy all match at SHA-256 `CB733DF0F300CC50E668C8BB62F00A434E4EC91B74EA6EFD9AED61DFE7C18E2C`. Full's previous plugin is preserved at `work/save-confirmation-default-before-20261009/eldenring_mo2_save_isolation-Full.py` (old SHA-256 `B837C85F9179A3C53BAFB2BD43FBA35B60919F69C69804AED9F9A44AFC377D3F`).
- Added a defensive Elden Ring guard and null checks to the legacy Profiles transfer handler, and hide that stock action for Elden Ring because route-aware transfers are managed by the Save Isolation Tools dialog. Other games retain MO2's stock transfer action.
- Rebuilt MO2 from a clean build directory after replacing the stale GitHub++ dependency with its path-mapped library. The resulting executable is 6,232,576 bytes, SHA-256 `7B8D7266FA2A8F31709805804EAB9777E49AE4FF005C6898C95E6A16EFB24707`; a string scan found no developer user-profile path, repository checkout name, or old build-dependency path. Full now uses this executable. The previous Full executable is preserved at `work/FULL-exe-before-profiles-transfer-fix-20261009/ModOrganizer.exe` (old SHA-256 `DB17CD93E3ADAF60247AD4177A5168F468DB09995940EB481EF3470B4C442A51`); the intermediate executable is preserved separately at `work/FULL-exe-before-r9g-pathmap-20261009/ModOrganizer.exe`.
- Verified the Save Isolation production dialog and persistence callbacks with temporary data. The UI harness clicked the actual default confirmation button and confirmed that Global persisted and reopened as selected; Profile and Instance named save IDs stayed independent. Callback tests confirmed that Keep/dismissal does not change the stored route. Qt `QSettings` and Python `ConfigParser` interoperability, named save creation/import, backup copying, duplicate rejection, source-boundary validation, runtime route mapping, and save quarantine checks also passed. No real profile route or game save was modified.
- Independently checked every staged payload file against its release manifest: Complete 1,301 files / 351,551,512 bytes; Elden Ring Isolated 1,201 / 342,393,484 bytes. In both stages the MO2 executable matches the clean build and the Save Isolation plugin matches Full. Embedded resources and local-path checks passed in the release builder.
- Complete/Portable setup: 310,749,184 bytes, SHA-256 `EDE8957FFDCE18D2447E5BB97D7C17FFA11780DD528241ED4812C6885F6ABDEB`. Elden Ring Isolated setup: 153,527,808 bytes, SHA-256 `DD2C3E8480CACE6E24C13BBE2E57BB79DDFFFB0921A5C5484C666DF5AC9798B7`. Both match the adjacent `SHA256SUMS.txt` and are unsigned.
- The full native build completed successfully. Source/layout and saved screenshot review covers the main MO2 workspace and Run/Data, Settings, Profiles, Instance Manager, the seven new-instance steps and confirmation, startup/category prompts, themes, Elden Ring save/native support, archive installation, and multilingual installer. Static layout inspection is not a live visual review: the current session cannot capture MO2 native windows. No real installer maintenance action or Elden Ring launch was performed.
- The active Default profile remains on `mode=profile` with its unchanged SHA-256 `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65`; Full is closed after the checks. No user save route or game save was changed. GitHub is untouched. This candidate remains unpublished pending the user's visual review and real installation/game launch checks.

## Final r9n cross-check — October 9, 2026

- Independently recalculated each file in both staged payloads against its manifest: Complete 1,301 files / 351,556,714 bytes and Elden Ring Isolated 1,201 files / 342,398,686 bytes; all sizes and SHA-256 values match. All 1,201 Isolated paths are shared with Complete; the only differing shared file is the generated uninstaller. The native bridge binaries are identical.
- Recalculated both installer hashes against SHA256SUMS.txt. The Complete/Portable and Elden Ring Isolated installers are unchanged, unsigned, and match their recorded hashes. Release-side CHANGELOG, historical changelog, license, notices, upstream list, and audit now match the maintained source documents.
- Re-ran the Save Isolation mode-label, production-callback, and Qt/Python INI-interoperability checks. Global is written only after its explicit confirmation; Keep/dismiss restores the persisted mode; Profile and Instance named-save IDs remain independent. All checks passed using temporary data.
- The real Default profile remains in Profile mode with its original SHA-256. Full is closed. The latest available MO2 logs contain no Save Isolation open/save event after the plugin update, so the user's reported persistence issue is not yet confirmed fixed in a live Full interaction. The disposable r9i screenshot evidence and isolated checks do not replace that final click-and-reopen check.
- The wider source/layout review has no additional static defect that justifies removing an active feature. The remaining visual check needs the native MO2 window; a real Elden Ring launch and archive/install-manager regression also remain outside this static/package pass.

- Full contained 222 Python bytecode caches; two were stale against the updated Save Isolation and Elden Ring game-plugin sources. I saved both originals with SHA-256 manifests and regenerated them using the matching Python 3.12.15 runtime. All 222 caches now match their source; none are orphaned or invalid. This removes ambiguity about stale bytecode but does not prove it caused the reported save issue. Bytecode caches are excluded from the Complete/Isolated payloads, so installer binaries and manifests are unchanged.

## Post-cache verification — October 9, 2026

- The save-mode label and persistence checks passed against Full and both r9n stages; Qt/Python INI interoperability passed with temporary data. All 222 Full Python caches now match their corresponding sources.
- The prior Save Isolation cache is preserved (SHA-256 10EF4ED4D04108859A88AA2AC274B1B809D27D9063B035B349ADC970EF9C8B88); its refreshed cache is CABEBFF14D6C291F7CD100EC6B1595B6D1003C6B88272B1128B357B55C2D3A0D. The prior optimized Elden Ring game-plugin cache is preserved (F227CC2E0B9D6A04F7C36658119BF6C3B72EDB6B2DABB43BFE11B9ABE1BC4666); its refreshed cache is D163D983BDF4271624F78506E9B1C44D43B7130BA195DB66DDF6A4690AC546F2.
- Saved r9i screenshots show Global selected after acceptance and after reopening that disposable clone. They do not prove the real Full interaction after its plugin update. The real MO2 log ends before that update and contains no later Save Isolation open/save event. The real Default route remains Profile mode; no user save was touched.
- The desktop surface again reported no native applications. Full remains closed; r9n installers, manifests, and checksums are unchanged.

## Post-r9n source audit — October 9, 2026 (not packaged)

- Save Isolation now reads its INI with an explicit `open()` and `ConfigParser.read_file()`. A missing file remains valid for a new profile, while permission, encoding, and syntax errors now stop a save instead of silently treating an existing file as empty. Six regression checks passed against both the maintained source and Full plugin: preservation of unrelated sections; missing-file initialization; independent Profile/Instance save IDs; read-error preservation; malformed-INI preservation; and atomic-replace failure cleanup. The source and Full plugin hash is `565863FB05FF9565C6456EE560C920400768E0E8B2CEAE7226FACB8FC43A8B52`.
- Updated the About and diagnostic-report labels for Save Isolation from alpha.75 to alpha.76. The Elden Ring game plugin remains alpha.75. `mo2_revamped_info.py` passes a syntax check and its source/Full hashes match (`ACED24C08F8CD497604C6E8CE49E6F088A3D412620701703E7CB5CA6FFF73EC6`). The already running Full process has the old module in memory; the updated labels require a restart and have not had a new native-window visual check.
- Re-ran the production Save Isolation dialog callback round trip with version 0.5.0.76 against source and Full using temporary profiles. Global persisted and reopened selected; Profile and Instance modes retained their independent named-save IDs. A separate fresh-process check passed Global write/read and named Profile ID write/read. Named Game Save Profiles are unavailable while the test route is Global, as intended; the test switched its temporary profile back to Profile before exercising the selector.
- Process-tree inspection is performed by MO2's process runner while waiting for a launched process or during exit handling, not by an idle recurring task. Its weak-launcher discovery starts at 50 ms and backs off exponentially to 2 seconds; it waits on the selected strong process handle without repeatedly rebuilding the tree. This MO2 code was not modified.
- The Elden Ring bridge previously scanned loaded modules up to 40 times at 250 ms intervals even if no Revamped native initializer was configured. It now skips module discovery in that case and stops early after every configured initializer has started. Zig 0.13.0 built the updated DLL, SHA-256 `A79AB575F7825FD5E4E23774B145949B74C4E76D1F11518CB06447B84CD0989E`; the Full copy matches. The pre-change Full DLL remains backed up with SHA-256 `A9935244697FB90278819AA8E6DFB6FF81013E96E7B2764803C2CAD95BFEB73F`.
- The candidate preserves the prior bridge's 25 exported names/ordinals and 12 imported DLLs. PE section comparison confirms the old Full binary and the r9n bridge differ only in COFF timestamp; the new candidate changes code as intended.
- Comparing Full's pre-change DLL with the r9n Complete stage confirmed matching hashes for all six PE sections; the earlier whole-file difference was the COFF timestamp only. The new bridge candidate intentionally changes code and data sections. A static scan found no local checkout path in the candidate. Full is running from its expected path and responds; no Elden Ring process was running during deployment.
- Neither setup installer nor Complete/Isolated stage has been rebuilt after these source changes. No in-game bridge test or new native-window visual capture was possible because the desktop surface exposes no native apps. GitHub remains untouched. The source candidate needs the user's Full review before a new package is prepared.

## Full deployment checkpoint — October 9, 2026 (not packaged)

- Verified the alpha.34 bridge deployed to Full matches the built candidate at SHA-256 `A22BDF99FA91CB339BCEEEC8923D9C0DB3992FA1D24CD6307777C3BFDDE7C1C7`. The prior alpha.33 DLL is backed up at SHA-256 `A79AB575F7825FD5E4E23774B145949B74C4E76D1F11518CB06447B84CD0989E`.
- PE comparison confirms x64 architecture, the same 25 exported names/ordinals, and the same 12 imported DLLs. This establishes static interface compatibility; in-game behavior still needs a controlled Elden Ring run.
- The About/Diagnostics plugin source and Full copy match at SHA-256 `89780625D2CA94606386A14371C7F1591F2B89433E7E4F1B1C7FD7A5E1C311CA` and pass Python syntax compilation. They now identify bridge alpha.34 and Save Isolation alpha.76.
- Save Isolation source and Full copies still match at SHA-256 `565863FB05FF9565C6456EE560C920400768E0E8B2CEAE7226FACB8FC43A8B52`; the six regression cases and temporary-profile dialog/fresh-process checks are documented above.
- Full remains responsive at `C:\MO2 Revamped\Full\ModOrganizer.exe` (PID 11816). No Elden Ring or game launcher process was running. The real Default route INI and `.sl2` saves were not touched.
- Native windows could not be recaptured because the desktop surface exposes no native apps. The open process retains the previous About module in memory; the new labels require a MO2 restart. Setup programs and stages have not been rebuilt, and GitHub remains unchanged while Full review is pending.
## Independent save and path revalidation — October 9, 2026 (not packaged)

- Repeated all six INI-preservation regressions against source and Full; all 12 runs passed. Re-ran the confirmation/persistence callback check against both files with temporary profiles: Global is saved only after acceptance, Keep/dismissal restores Profile, and Profile/Instance named-save IDs stay independent.
- Located the real route file under the Revamped global instance: `%LOCALAPPDATA%\MO2 Revamped\Instances\ELDEN RING\profiles\Default\eldenring_mo2_saves.ini`. Its SHA-256 remains `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65` and its content remains `[Saves] mode=profile`; this pass did not write it.
- Reconfirmed current path logic: Global data and settings use `GenericDataLocation/MO2 Revamped`; Portable stores `PortableInstances` beside the app; Isolated stores `IsolatedInstances` and `EldenRingOnly.ini` beside the app. Active Revamped data paths do not select upstream `AppLocalDataLocation/ModOrganizer`.
- A repeat Qt dialog round trip with a temporary profile did not finish or report a result within roughly 45 seconds. I interrupted that test process and confirmed no test process or prefixed temporary directory remained. This attempt is not counted as a fresh dialog pass; the earlier recorded round trip remains separate evidence.
- Full (`C:\MO2 Revamped\Full\ModOrganizer.exe`, PID 11816) is still the only MO2 process and responds. No Elden Ring or game launcher process is present. No real save or route INI was changed.
## Dialog and fresh-process round trips — October 9, 2026 (not packaged)

- Re-ran the production dialog harness with `QT_QPA_PLATFORM=windows` and Full's `platforms` directory explicitly selected. Source and Full both passed: confirm Global, close/reopen with Global selected, return to Profile with its ID, and select Instance with its independent ID. Only temporary profiles were used.
- Repeated fresh-process verification against Full: one process wrote Global and a separate process reopened it selected; two additional processes saved and reopened a named Profile game-save ID. All four steps passed. `TemporaryDirectory` removed the test profile and test `.sl2` automatically.
- After both checks, the real route INI still has its original SHA-256 `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65`. Full's Save Isolation bytecode remains at the validated hash `D6B8D3147FFBC5F2493C95C782D5E3A18D634555F7060A9DE834B18DC2F3C7FA`.
- The first rerun without explicit Qt platform configuration did not complete. The instrumented run with the platform configured completed and passed on both targets; I cannot attribute the delay to a specific internal cause. No process or temporary folder remains. Full is responsive and Elden Ring is not running.
## Runtime version inventory and update gates — October 9, 2026 (not packaged)

- Read file versions from the installed Full runtime: MO2 2.5.2; USVFS 0.5.7.2; LOOT CLI 1.6.0/libloot 0.23.0; Qt Core 6.7.1; CPython 3.12.15; OpenSSL 3.5.9; 7-Zip 24.05; Explorer++ 1.4.0.2293. These match `docs/UPSTREAMS.md`.
- Official release pages still list MO2 2.5.2 and USVFS 0.5.7.2 as their latest releases. CPython 3.12.15 is the 3.12 security release; Python 3.14 is a separate feature series and cannot replace MO2's CPython 3.12 extensions. OpenSSL 3.5.9 is the LTS line through 2030. Although OpenSSL 3.6 has a newer release, its listed end-of-support date is November 1, 2026, so Full retains the LTS build.
- Explorer++ 1.4.0.2293 matches the latest 1.4.0 release. Python and OpenSSL updates are already present in Full and remain paired with MO2's runtime and native extensions.
- Newer components that still need compatibility validation are Qt 6.7.1 vs Qt 6.12 LTS; PyQt 6.11.0/WebEngine vs that runtime; 7-Zip 24.05 vs 26.04; LOOT CLI/libloot 1.6.0/0.23.0 vs 0.29.2/0.29.6; Boost 1.85 vs 1.92; and Zig 0.13 vs 0.17. They require a coordinated Qt/PyQt/WebEngine/native-plugin rebuild, archive regressions, API/sorting tests, a native rebuild, and bridge export validation respectively. None was replaced during the Full pass.
- Riverbank published PyQt 6.11.0 with explicit Qt 6.11 support and PyQt-builder 1.20.0 with Qt 6.12 build support. That is not a validated MO2/PyQt/WebEngine runtime build. The Qt for Python 6.12 announcement is for PySide and does not establish a compatible PyQt 6.12 release.
- Official references checked: [MO2](https://github.com/ModOrganizer2/modorganizer/releases), [USVFS](https://github.com/ModOrganizer2/usvfs/releases), [Qt 6.12](https://www.qt.io/blog/qt-6.12-released), [PyQt](https://riverbankcomputing.com/news/PyQt_v6.11.0_Released), [PyQt-builder](https://riverbankcomputing.com/news/PyQt-builder_v1.20.0_Released), [Python 3.12.15](https://www.python.org/downloads/release/python-31215/), [OpenSSL 3.5](https://openssl-library.org/source/), [7-Zip](https://www.7-zip.org/), [LOOT](https://github.com/loot/loot/releases), [libloot](https://github.com/loot/libloot/releases), [Boost](https://www.boost.org/releases/latest/), [Zig](https://ziglang.org/news/0.17.0-released/), and [Explorer++](https://github.com/derceg/explorerplusplus/releases).
- This inventory distinguishes components already current from versions retained behind compatibility gates; it does not claim the six deferred groups have been upgraded. No source runtime was changed and no Full build or installer was rebuilt for this inventory.
## Baseline overlay check — October 9, 2026

- A repository-wide search found the historical instruction to enable "Use profile-specific Save Games" from Profiles in source/mo2-python-plugins/basic_games/games/game_eldenring_mo2.py, the MO2 baseline copy.
- The release builder first copies the runtime, then overlays source/revamped/plugins/; its documentation states that this maintained Revamped copy replaces the baseline Save Isolation implementation.
- The Revamped source, Full installation, and r9n Complete and Isolated stages contain the same game_eldenring_mo2.py SHA-256: 3BC93734C7501A315067E28FF77A5A701A1840D085CD9803C8532A8A012D37A2. Both stages direct users to Tools and do not contain the obsolete Profiles instruction.
- I left the baseline file unchanged because the old wording is not in the staged candidate; preserving the upstream baseline avoids an unnecessary source modification.
- A fresh native-window review after restarting Full and an offline in-game bridge test remain outstanding. The r9n stage check does not represent a rebuild after alpha.34.

## Current Full versus release-stage parity — October 9, 2026

- Recomputed hashes for Full and both r9n stages. Full contains ModOrganizer.exe A03645F8FB023EEF18AF87ABBA8B40EF8C0C255C6542FCB4DCE065EE9964A113, Save Isolation 565863FB05FF9565C6456EE560C920400768E0E8B2CEAE7226FACB8FC43A8B52, bridge A22BDF99FA91CB339BCEEEC8923D9C0DB3992FA1D24CD6307777C3BFDDE7C1, and About/Diagnostics 89780625D2CA94606386A14371C7F1591F2B89433E7E4F1B1C7FD7A5E1C311CA.
- Complete and Isolated r9n share older files: executable 8D7072C1BCDA47EEBA04AA91CC9411FBC5A2D2E827E15657693541E091852EEC, Save Isolation DF2DABBC866C01A41F3B8CC2772B712D5C1C97AAE0CF8179594026588ABF1A65, bridge DE8F705046377702D6AF9391FEA30C90287CAA5646671F543DBD5DCE52B6DBBE, About/Diagnostics 79B15DA7412323A73D131860FD4F9DFD526C741E704DFECDB0A9E049BA744438.
- The game plugin game_eldenring_mo2.py matches across Full, maintained source, and both stages: 3BC93734C7501A315067E28FF77A5A701A1840D085CD9803C8532A8A012D37A2.
- Therefore r9n does not represent the current Full candidate and must not be treated as final. I did not alter those artifacts. After Full's visual review, rebuild Complete/Portable, Elden Ring Isolated, and both installers, then compare manifests and hashes before the user tests them.

## Save audit follow-up - October 9, 2026

- Re-ran the save-configuration regression against source: all 6 cases passed. The production Qt dialog round trip passed against both source and the plugin installed in Full: the Global route requires accepting its confirmation, the chosen mode is selected after reopening the dialog, and the Profile and Instance Game Save Profile IDs remain independent. Tests used temporary profiles and synthetic saves.
- The real Default profile INI remains `[Saves] mode=profile`, SHA-256 `4C78586A349550F22444E057B266194564BEC8259230C9D2C92891AA2EAF2C65`. No real saves were modified. The audit fixture remains under `work/save-config-read-regression-20261009/fresh-process-roundtrip` and contains a synthetic `.sl2` file.
- `C:/Users/USERNAME` in MO2 logs is intentional privacy redaction. `loglist.cpp:373-375` replaces the account name with `USERNAME`; the actual profile resides under `%LOCALAPPDATA%/MO2 Revamped/Instances/ELDEN RING`. No data path was changed.
- Corrected the stale `NativeBridgeVersion` value from alpha.33 to alpha.34 in source. The bridge DLL and About/Diagnostics were already alpha.34. The source passes AST parsing and has SHA-256 `D39101CD6DDDA1158D918158E509453539C7CEFDD07FAAC1C8BB761C76B5A2A2`. The prior file is backed up at `work/audit-backups-20261009/game_eldenring_mo2.py.before-bridge-version-correction`, SHA-256 `3BC93734C7501A315067E28FF77A5A701A1840D085CD9803C8532A8A012D37A2`.
- I did not copy the plugin into Full because `ModOrganizer.exe` remains open (PID 11816). Full still has the old label on disk; it will not appear in a fresh log until the file is safely deployed and MO2 restarted. Installers were not rebuilt and nothing was published to GitHub.
- Still pending: deploy the correction after Full closes, confirm alpha.34 in a fresh startup, and validate the bridge offline in Elden Ring. A read-only screenshot confirmed the main window loads; the attempt to open Settings did not produce a dialog and no setting was changed.


## Controlled alpha.34 deployment and baseline-copy audit - October 9, 2026

- Confirmed no ModOrganizer.exe process was running before deployment. Backed up the installed plugin and optimized bytecode under the audit worktree. The prior plugin backup has SHA-256 3BC93734C7501A315067E28FF77A5A701A1840D085CD9803C8532A8A012D37A2; the prior bytecode backup has SHA-256 D163D983BDF4271624F78506E9B1C44D43B7130BA195DB66DDF6A4690AC546F2.
- Installed the updated source file into Full, SHA-256 D39101CD6DDDA1158D918158E509453539C7CEFDD07FAAC1C8BB761C76B5A2A2. Explicitly launched C:\MO2 Revamped\Full\ModOrganizer.exe; process 8160 and its window title confirm the Full path.
- On startup Python regenerated game_eldenring_mo2.cpython-312.opt-2.pyc. Its timestamp is newer than the source and its new hash is 826EB0FD7F43849F415D1A17B273D9038F443A4796C1370C904A2EF0DB1B0D89. The compiled cache contains 0.5.0-alpha.34 and no longer contains the stale alpha.33 label. Full remains open.
- The available desktop surface did not expose native windows for inspection, so this does not count as visually reading the in-app log or reviewing Settings live. Elden Ring was not launched; no save modes or game saves were changed.
- Also audited the existing deletion of source/mo2-python-plugins/eldenring_mo2_save_isolation.py. It is not a release-builder input: Build-Release.ps1 overlays source/revamped/plugins/ onto the runtime and compares the maintained plugin hash; the Python runtime overlay receives plugins/plugin_python from the runtime. The deleted path is not referenced as a packaging source. The Revamped plugin is present in Full and the builder requires it in both Complete and Isolated.
- Packages were not rebuilt and nothing was published to GitHub during this pass.


## Isolated plugin-filter review - October 9, 2026

- Parsed the archived r9n eldenring-payload-manifest.json: 1,201 total files and 346 plugin entries. Under plugins/basic_games/games it contains only __init__.py, eldenring_mo2_bridge.dll, and game_eldenring_mo2.py. No other-game modules or matches for the builder's excluded paths appear.
- Remaining plugin groups are shared MO2 support explicitly allowed by the build filter: installers, diagnostics, Python, and the installer wizard. This validates the filter behavior in r9n only; r9n predates the latest changes and must be rebuilt before it can represent a final package.

## Save Isolation restart prompt and current candidate — October 9, 2026

- The user manually confirmed that Save Isolation recognized the Steam save. The other routes behaved as configured; where no save exists for a route, Elden Ring starts with a new-save state. The user's Default profile currently selects `instance_shared`, SHA-256 `EFEED4C33D60B6CF42ADC819E32A19BDA1E8DF14F8940FEFDD068560344A16FC`.
- Added a Restart now / Later prompt after a verified route or named game-save-profile change. Restart now closes the tool dialog and queues MO2's existing `ExitModOrganizer(Exit::Restart)` flow through a Qt slot; if MO2 refuses the restart, it tells the user the route remains saved and must be restarted manually. No duplicate process is launched.
- Built the MO2 Release executable successfully. Its SHA-256 is `550AE56A1861B201C2BC26423A65E414A4484B4A1981F7061CF20CE50D70FA69`; a static binary check finds the registered restart slot. The plugin passes Python AST parsing and identifies as alpha.77.
- Rebuilt local candidate r9r: Complete/Portable contains 1,301 files and Elden Ring Isolated contains 1,201. The setup installers are `MO2-Revamped-Setup-1.0.1.exe` (SHA-256 `9B6C4A2AC3E227EEF0BCEA0A0AF5D82396A05466888FEA09925E192112EA90A17`) and `MO2-Revamped-Elden-Ring-Isolated-Setup-1.0.1.exe` (SHA-256 `8E8301704659866F6AA5F4A5350A184E815EAA6C41BAD7531DB5F712434C10CF`).
- With all MO2 processes closed, deployed the candidate executable and alpha.77 Save Isolation plugin to `C:\MO2 Revamped\Full`. Previous executable, plugin, bytecode, and route INI are backed up under `work/audit-backups-20261009/Full-before-alpha77-restart-prompt-20261009`. Full matches the candidate executable and plugin. The route INI was unchanged during deployment and held `instance_shared`. On the user's next launch, `mo_interface.log` confirmed alpha.77 initialized. The old `.pyc` still contains alpha.76 and no restart slot, so the current process loaded the updated source; both release stages exclude this stale bytecode.
- At this checkpoint, the restart prompt had not yet been exercised in the live UI. That status was superseded by the follow-up below.

## Restart now live check and isolated payload — October 9, 2026

- The user confirmed that the last action in the Full session was choosing **Restart now**. The Default profile route file now stores `mode = profile`, with empty named-save IDs; its SHA-256 is `58347E40BF0123456069B7812EB290746F1A630600284C958320CA89807D1193`.
- The user confirms no error appeared during the interaction, and clarifies that `Default` was the active profile.
- The log sequence shows MO2 started again from `C:\MO2 Revamped\Full` as PID 20220, using `%LOCALAPPDATA%\MO2 Revamped\Instances\ELDEN RING`. The later Tools log says it opened for `Default` and read the stored mode `profile`. A preceding USVFS session used PID 23776, corroborating the process replacement. This confirms that **Restart now** returned to the same instance/profile and loaded the saved route.
- The relevant MO2 log has no warning/error/restart-failure entries; both adjacent USVFS logs show normal initialization and attachment. Full is now closed. The audit did not modify the route INI or any save file. The **Later** button branch and an Elden Ring launch remain unverified.
- Parsed the exact r9s isolated payload manifest: 1,201 files, 342,402,693 bytes, and 346 plugin files. `plugins\\basic_games\\games` contains only `__init__.py`, `eldenring_mo2_bridge.dll`, and `game_eldenring_mo2.py`; no `.pyc`, `.pyo`, or `__pycache__` entries are packaged. The two r9s installer hashes match `SHA256SUMS.txt`.
- No files were uploaded to GitHub. Local r9t installers were rebuilt after the live check: Complete/Portable contains 1,301 files (351,560,721 bytes) and Elden Ring Isolated contains 1,201 files (342,402,693 bytes). Both installer checksums match `SHA256SUMS.txt`: Complete `55de5c058c69b3723970d397feedc9939252c827b231cc1e3a8808059e36b04c`; Isolated `5795dfb82b9f33d70f070408a6b703ff6ab7772e2e4bb4fedc73543d9bac25ec`.

## Save Isolation candidate revalidation — October 9, 2026

- SHA-256 matched for the alpha.77 plugin across maintained source, Full, and both r9s Complete and Isolated stages: `A3DEB400FF535810CEC6B472B20672275BAEB37E3D09160E358EB9791DC4841D`. The Elden Ring game plugin also matches across source and both stages: `D39101CD6DDDA1158D918158E509453539C7CEFDD07FAAC1C8BB761C76B5A2A2`.
- Ran the current callback harness against the r9s Complete stage. It passed explicit Global confirmation, Keep/dismiss restoration, persistence of independent Profile and Instance named-save IDs, Later leaving MO2 open with the route saved, and Restart now queuing MO2's native restart.
- Ran fresh-process checks against the staged candidate: Global route write/read passed across separate processes, as did changing a named Profile save ID and reopening with that ID selected in a new process. Fixtures contain synthetic saves only.
- The named-profile operation checks passed for creation/import, `.sl2` and `.bak` copying, duplicate-name rejection, Global-mode restrictions, and source-boundary validation. The six route-INI regression cases passed, including malformed/read-failure protection and failed atomic replacement. Runtime-route, quarantine/recovery, mode-label, and Qt/Python INI interoperability checks also passed.
- The outdated audit harnesses were updated in the audit workspace to include the current restart prompt and `QMetaObject` import. No product source changed during this revalidation. r9t's Complete and Isolated payload archives passed ZIP/manifest hash validation and core-parity checks. The rebuilt native bridge is the same size as Full's bridge and differs only in the PE build-timestamp bytes, as expected from recompilation. No GitHub upload has occurred.

## Complete uninstall and Portable routing review — October 9, 2026

- The current Full install has a completed 1.0.1 installer state for `C:\MO2 Revamped\Full`; the user-data root remains separate at `%LOCALAPPDATA%\MO2 Revamped`, containing the `ELDEN RING` instance and `MO2-Revamped-Global.ini`.
- Static review of r9t's uninstall flow confirms that the **delete all data and selected folder** checkbox defaults off. In keep-data mode, tracked program files are removed while user-data/migration paths are preserved; the unregistered path removes only known MO2 program files and preserves unrecognized files. Full removal is scoped to the selected install directory. Installer recovery metadata has a separate root and is deleted only when empty. Protected Windows/user-directory boundaries and reparse-point checks reject unsafe targets. This is source review, not a live uninstall test.
- Full's installed `unins000.exe` and r9t's generated uninstaller are both 293,888 bytes but have different SHA-256 hashes (`69B144254BB5C8F5E0D9C51C74AA91C45B27C483B33CFB90A73E4F6B6AE64838` vs. `D298C5687E1020101EC83E0B7E82BCFD333CA41CC1343D828232260004E29C64`). The existing uninstaller's behavior was not exercised; keep Full until the r9t installer has been manually checked.
- The Complete installer's Portable selection writes `portable.txt`; MO2 then stores Revamped global settings beside the portable executable instead of under the standard `%LOCALAPPDATA%` data root. At the latest check, `C:\MO2 Revamped\Portable` did not exist, so it is a separate available test destination. The user plans to test Portable there; that install has not yet appeared.

## Portable path trace — October 9, 2026

- Traced the installer selection through the MO2 path logic. A fresh Complete install selected as Portable writes `portable.txt` in its target folder. With that marker, Revamped stores `MO2-Revamped-Global.ini` beside the executable and uses that installation's `GlobalInstances` directory. The r9t staged Complete executable has the same SHA-256 as the installed Full executable: `550AE56A1861B201C2BC26423A65E414A4484B4A1981F7061CF20CE50D70FA69`; the Complete installer checksum rechecked as `55DE5C058C69B3723970D397FEEDC9939252C827B231CC1E3A8808059E36B04C`.
- The Elden Ring Isolated marker selects `EldenRingOnly.ini` next to the executable. The instance manager exposes only the portable game instance from that installation and blocks changing to another instance. In standard Full, global instance data resolves under `%LOCALAPPDATA%\MO2 Revamped\Instances`, while Revamped settings resolve under `%LOCALAPPDATA%\MO2 Revamped`.
- Read-only checks show Full's global settings file and Elden Ring instance under `%LOCALAPPDATA%\MO2 Revamped`; there is no Revamped global INI under `%LOCALAPPDATA%\ModOrganizer` and none beside Full's executable. `C:\MO2 Revamped\Portable` is still absent.
- This is source, package, and current-path evidence. It does not prove the installed Portable behavior. The user will install the r9t Complete candidate to `C:\MO2 Revamped\Portable`; verify the marker, launch path, instance list, and settings location before treating Portable isolation as runtime-confirmed. No installer was launched and no user data was changed during this review.
- Re-ran the Save Profiles logic harness and the dialog confirmation/restart callback harness against the exact r9t Complete stage. Both passed: Profile/Instance/Global routing, independent named-save IDs, named-save creation/import and `.bak` handling, duplicate/source-boundary rejection, explicit Global confirmation, Later, and queued Restart now. The staged plugin hash matches Full (`A3DEB400FF535810CEC6B472B20672275BAEB37E3D09160E358EB9791DC4841D`). This validates the packaged plugin logic; it does not replace the pending manual Portable install and launch check.
- Ran four separate Python/Qt processes against the staged r9t plugin using a temporary profile: a named Game Save Profile selection persisted and reopened in the next process; the Global route was then saved and reopened in a separate process. All four steps passed. The temporary directory was removed. The real Default route INI remains at `mode=profile`, hash `58347E40BF0123456069B7812EB290746F1A630600284C958320CA89807D1193`.

## Installer follow-up: selection and per-mode state — 9 October 2026

- The screenshot reproduces the double-selection cause: Portable and Full controls live in separate cards, so Windows Forms does not treat them as one radio group. The old handlers updated each control independently, while `StartOperation` read the Portable radio even when Full also appeared selected.
- Portable/Full selection now has one authoritative value. Both radios, card highlighting, automatic destination, and `InstanceKind` use it. The destination switches between `C:\MO2 Revamped\Portable` and `C:\MO2 Revamped\Full` while still automatic; custom paths are preserved. The confirmation identifies the chosen install type.
- Reviewed all six installer actions. `ActionBox.SelectedIndex` was already the source of truth; `UpdateActionCardSelection` synchronizes the cards and keeps one selected. The new harness traversed all six actions and selected each radio directly.
- Found a hidden-option crossover when changing modes. `BuildOptions` retained references to checkboxes removed from the screen. A checked full-delete option in Uninstall could reach Restore: `RestoreFiles` would skip restoring files and the previous backup would then be cleaned up. Each rebuild now clears old control fields, and `SetupRequest` accepts only options relevant to the current mode. The engine also ignores full-delete and forced-close flags in Restore mode.
- Old location-control references are cleared during screen rebuilds; chosen paths are still saved per mode before the controls are removed.
- Complete form verification passed: Portable/Full is exclusive in both directions; automatic destinations follow the selection; custom paths remain unchanged; each of the six actions has exactly one selected card; switching from Uninstall to Restore clears delete, force-close, migration, shortcut, and launch controls. No installation, restore, or uninstall action was run.
- `Build-Release.ps1` completed for Complete and Elden Ring Isolated: payloads contain 1,301 and 1,201 files; hashes, manifests, parity, embedded resources, and local-path checks passed. The first attempt used OpenSSL DLLs from the build tree and correctly detected local paths; the candidate was rebuilt with verified copies identical to Full and r9t's OpenSSL 3.5.9 DLLs.
- Local candidate: `work/release-candidate-installer-ui-state-fix-r2-20261009/release`. Complete SHA-256: `299F712D5798DB9F6609DB6AE819DD15A2ED2C4BF50E1863E3583EE48E82BA82`. Isolated SHA-256: `D076DC315E855B2132E88528C14E0AA4DAC9D3F4916DBC7A1C472DB7B49FF949`.
- Full was not modified; `ModOrganizer.exe` remains SHA-256 `550AE56A1861B201C2BC26423A65E414A4484B4A1981F7061CF20CE50D70FA69`. No MO2/installer process is active, and `C:\MO2 Revamped\Portable` remains absent. Nothing was published to GitHub.
- Extended the harness to the isolated installer: all four actions are exclusive; install defaults for shortcuts and launch are as intended; full deletion and force close start unchecked; Restore clears hidden uninstall controls; the new confirmation label translates in all six languages. All checks passed.
## Current single-installer candidate — 2026-10-09 (r9w)

- The local candidate is `work/release-candidate-single-installer-fit-window-r9w-20261009/release/MO2-Revamped-Setup-1.0.1.exe`. SHA-256: `58BECA89B722AB79822AEEC900DCFFB0E5AF9496CA7E0662EFEBA7A89A0989B0`. `SHA256SUMS.txt` matches. The release directory contains one setup executable; the Complete and Elden Ring Isolated payloads are embedded in it (1,301 and 1,201 files). No separate isolated-only setup is emitted.
- The Complete and Isolated stages both contain the path-clean organizer executable `12732D1DCBFA31DFE38CA4FD7BBA3A03CD35EE37519ECC02C20E01D38AC9E7A0`. The setup binary scan found no local user-profile path. The installed Full executable remains `EB02B73637CE76E69B2638CC26D4A29BF343AB3B2DB21AF9FD8D8BAD374E3769`; it was not replaced.
- Installer layout update: `FitWindowToContent` sizes the form from the visible sections, expands the window when the progress log appears, keeps the prior center position, and enables scrolling only when the screen cannot fit the content. Language changes now re-run layout sizing.
- The installer source compiled and the release builder validated both embedded payload archives and resources. A second static layout pass found no additional clear spacing or alignment defect in the fixed-size controls. Native-window visual validation was unavailable in this environment; verify the progress view and translated card text on Windows before publication.
- This is a local candidate only. Nothing was pushed to GitHub or published.

## General installer review — r9z — 9 October 2026

- An isolated form-construction check found that the new circular action icon set a transparent background before enabling transparent-background support. Reordered initialization; the installer form now constructs without the exception.
- Offscreen review found that the Portable, Full, and isolated edition descriptions expanded beyond their cards and clipped. Their labels now use the available card width, wrap within the card, and fit the card height. The three cards still share one consistent layout.
- Added the missing translation for the data-separation note in Spanish, English, French, German, Italian, and Brazilian Portuguese.
- The state harness passed 18 checks: five general tasks; one selected edition at a time; correct Portable, Full, and isolated targets; isolated-mode routing; target validation; automatic path switching; custom-path preservation; and both new localized strings across all six languages.
- Generated offscreen views in all six languages. Reviewed the spacing, round icon backgrounds, card text, and footer in the rendered forms. The installer has not been launched as a visible desktop window and no install operation was started.
- Candidate: `work/release-candidate-single-installer-round-icons-r9z-20261009/release/MO2-Revamped-Setup-1.0.1.exe`; SHA-256 `6801F0C47B340D267110545F0A752C203B9107B1ADDC2DFB55770835C8ADB88D`. The release folder contains one setup executable; Complete and isolated payloads remain embedded.
- The four embedded resources match the previously verified payload ZIPs and manifests byte for byte. Complete has 1,301 files and isolated has 1,201. The installer and release documents passed the local-path scan; no account or source-worktree path appears in the release folder.
- No install action ran, and Full, Portable, Elden Ring, saves, and GitHub were not modified.

- Simulated the installer progress state offscreen without starting its worker: the window grew to fit the progress bar and log, disabled the setup controls, and kept the footer visible with room below it. No installation ran.

## Qt installer interface candidate — 11 October 2026

- Replaced the installer presentation with a Qt 6.10.3 Widgets/C++ frontend while retaining the C# setup engine for installation, update, data copy, restore, and uninstall operations. The Qt executable and its dynamically linked runtime are embedded in the main setup and both uninstaller stubs.
- Built the local candidate at `work/mo2-qt-installer-candidate-20261011/release/MO2-Revamped-Setup-1.0.1.exe`. SHA-256: `a667f8d6b8d8cd40d2f20619af107f626aa3dd9ccdf58ebf3f87678cbf48eb8f`; size: 387,056,640 bytes.
- The installer embeds five resources: Complete and Elden Ring Isolated payload archives and manifests, plus the Qt runtime archive. The release builder validated 1,300 Complete files and 1,200 isolated files, compared the embedded Qt runtime with the deployed build, and passed its local-path scan.
- The frontend rendered in Spanish from the packaged runtime in Windows-native mode. Screenshots cover the standard installer, isolated installer, and uninstaller. No install or uninstall operation was run; this remains a local candidate and was not published.

### UI polish candidate — 11 October 2026 (r2)

- Redrew the installer task icons as distinct install, sync/update, copy, restore, and delete symbols. Replaced the native language combo box with a rounded, theme-matched selector and a custom language menu with consistent row spacing and a marked active language.
- Built the local candidate at `work/mo2-qt-installer-candidate-20261011-r2/release/MO2-Revamped-Setup-1.0.1.exe`. SHA-256: `A8A32203D91A66D39A1DC2429F0A67BD5951105DBE3AB708A2D380B3950C917E`; size: 387074048 bytes.
- The builder validated 1,300 Complete files and 1,200 Elden Ring isolated files, the embedded resource set, and the release local-path scan. The Qt runtime ZIP is 25429245 bytes with SHA-256 `813F0F48F8FF5827A05EB9C4833B26D55BD211FB7FA1FFFDE9FB29618A13B781`; it was extracted from the setup executable and compared byte for byte with the build runtime.
- Captured and reviewed the standard installer, language menu, isolated installer, and uninstaller from the extracted runtime in Windows-native mode. No install or uninstall operation was run; this remains a local candidate and was not published.
### Installer icon and language alignment — 11 October 2026 (r3)

- Aligned the language selector to the content cards and centered it vertically in the header. Its globe now sits in a circular badge matching the task-icon treatment. Redrew the update, copy, restore, and uninstall glyphs for clearer silhouettes; kept the New installation glyph unchanged.
- Built the local candidate at `work/mo2-qt-installer-candidate-20261011-r3/release/MO2-Revamped-Setup-1.0.1.exe`. SHA-256: `17B9052F3B7113433B69F5C4EA2844D4271001E2BB4830390E8D28F62B011530`; size: 387076608 bytes.
- The builder validated 1,300 Complete files and 1,200 Elden Ring isolated files, embedded resources, and the release local-path scan. The Qt runtime ZIP is 25429994 bytes with SHA-256 `AAA16A2205DEBABC3A0B6269379E3F116C8A87B621C638CF38CEDF0C68F26AA3`; the copy extracted from the setup executable matched the build ZIP byte for byte.
- Captured and reviewed the standard installer, language menu, isolated installer, and uninstaller from that extracted runtime in Windows-native mode. No install or uninstall operation was run; this remains a local candidate and was not published.
