# Initial release audit

Audit snapshot: October 8, 2026. Version claims below describe the files used
to prepare this release, not every upstream project that can be built with
another toolchain.

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
| Elden Ring components | game plugin 0.5.0-alpha.75; native bridge 0.5.0-alpha.33; archive installer alpha.72; save isolation alpha.68; startup options 0.5.0.64 | Version labels are preserved as reported by the included components. Product version 1.0.0 does not imply each component has independently reached 1.0.0. |

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
- The release builder scans staged payload files for the current developer
  profile and repository-root paths before archiving them. MO2 and OpenSSL use
  source-path mapping; GitHub++ is rebuilt without Qt's source-file logging
  context, which otherwise embeds an absolute source path. The Elden Ring
  bridge uses Zig source-path mapping and strips debug metadata pulled from
  Zig's bundled unwind library. CPython's locally built DLL and extension
  modules retain their CodeView identifiers while PDB directory paths are
  reduced to filenames. The check does not rewrite unrelated upstream
  binaries, which may retain their original upstream build-path strings.
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

## Build and validation evidence

- MO2 2.5.2 compiled successfully from the current source tree into a new
  external CMake build directory with the available MSVC/Qt 6.7.1 toolchain.
- The release builder checks both payload archives against sorted manifests,
  entry count, size, and SHA-256; it also checks the resources embedded in each
  installer executable.
- A path scan rejects local profile or repository paths from both staged
  payloads and the generated installers.
- The final candidate contains 1,317 files in the general payload and 1,218 in
  the Elden Ring-only payload. The isolated manifest contains no unrelated
  game plugins, PyCfg configuration editor, user data, or unsafe paths.
- Both installer variants compile as version 1.0.0 and contain only their
  intended payload resources. The generated installers are not Authenticode
  signed.
- A copy of the staged Elden Ring payload started in a temporary portable
  folder and reached MO2's first-start setup wizard. This did not install into
  Complete or launch the game.
- OpenSSL 3.5.9's CLI and default provider loaded from the staged runtime. A
  TLS 1.3 connection to Nexus Mods completed with hostname and certificate
  verification. The full upstream OpenSSL test suite was not run.
- The Python 3.12.15 runtime and OpenSSL 3.5.9 extensions passed 1,267 upstream
  SSL, tarfile, ZIP, and urllib tests; 73 were skipped by the Windows test
  environment. The embedded `pythoncore.zip` was rebuilt from the 3.12.15
  standard library and exercised without a source-tree fallback. A verified
  HTTPS request to python.org returned HTTP 200.
- The general package includes Complete/Portable and supported maintenance
  choices. The second installer exposes only the isolated portable Elden Ring
  edition plus update, restore, and uninstall for that edition.
- The isolated payload uses an explicit plugin allowlist and includes the
  required MO2/Python installation components. Game plugins for unrelated
  games are excluded.
- The Elden Ring payload also omits Skyrim/FNIS and OMOD installation tools and
  the PyCfg game-settings editor. It keeps the DDS preview modules and the
  common FOMOD/manual/quick installers that support Elden Ring mod packages.
- The game itself has not been launched as part of this source/build audit.
  The Elden Ring features were reviewed in MO2; gameplay behavior therefore
  remains unverified by this release-preparation pass.

## Deferred updates

Qt/PyQt remains at 6.7.1 because moving to a newer Qt line requires rebuilding
Qt WebEngine, PyQt, and MO2's native extensions, then repeating the UI review.
libloot and the Elden Ring modules also need their own API and integration
checks before independent updates. Rebuild the release if any of these
dependencies changes later.
