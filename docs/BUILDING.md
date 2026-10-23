# Build guidance

The repository contains a consolidated source snapshot. The application was
compiled from the current source tree on Windows with MSVC 14.44, Qt 6.7.1,
Python 3.12.15, and the MO2 build dependencies recorded in
[`UPSTREAMS.md`](UPSTREAMS.md). A new external CMake build directory completed
successfully. This does not yet prove that a fresh clone can recreate the full
runtime: MO2 runtime DLLs, plugin binaries, Qt/Python distributions, and other
redistributable files are supplied separately from the source tree.

For release builds, the Organizer target maps `%USERPROFILE%` to the neutral
`MO2_USER` prefix in MSVC Release builds with `/pathmap` and
`/experimental:deterministic`. Separately built MSVC libraries that include Qt
message context need the same mapping before they are linked. The GitHub++
static library is built separately from the Organizer target; rebuild it with
the mapping and link that candidate library into the Organizer executable. The
release builder rejects a payload that still contains the current profile or
repository root. The Elden Ring bridge build applies Zig's
`-ffile-prefix-map` and strips linked debug metadata, including paths from Zig's
bundled unwind library. The release builder checks staged payloads and installer
binaries for local profile and checkout paths before producing the installers.
It maps the reviewed Light, Dark Blue, and Dark Black styles to
`stylesheets/Light.qss`, `stylesheets/dark.qss`, and
`stylesheets/dark-black.qss`, which are the names the application loads.

## Mod Organizer 2

Use the official [MO2 build project (`mob`)](https://github.com/ModOrganizer2/mob)
as the starting point for preparing a Windows development environment. Use
the matching source in `source/modorganizer/` and the component versions in
[`UPSTREAMS.md`](UPSTREAMS.md). Do not mix Qt, Python, compiler, or USVFS
versions without rebuilding and checking the native extensions.

## USVFS

USVFS source is in `source/usvfs/`. Its upstream README and CMake files list
component build options. The source snapshot is USVFS v0.5.7.2, matching the
included MO2 runtime.

## NXM handler

`source/nxmhandler/` holds the nxm link handler (`nxmhandler.exe`), based on
upstream modorganizer-nxmhandler at the commit shipped with MO2 2.5.2 (see
[`UPSTREAMS.md`](UPSTREAMS.md)). MO2 Revamped replaces its handler list window
with a Fluent version; the command line, the registration and the
`nxmhandler.ini` format stay compatible. It builds like the Organizer target,
against the `mob` dependency tree (`cmake_common`, `uibase`) and Qt 6.7.1, in
a build directory of its own:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
cmake -G "Visual Studio 17 2022" -A x64 -S source/nxmhandler -B C:\path\to\nxmhandler-build ^
  -DDEPENDENCIES_DIR=C:/path/to/mo/build -DBOOST_ROOT=C:/path/to/mo/build/boost_1_85_0 ^
  -DQT_ROOT=C:/path/to/Qt/6.7.1/msvc2019_64 -DPYTHON_ROOT=C:/path/to/mo/build/python-3.12.3 ^
  -DCMAKE_INSTALL_PREFIX:PATH=C:/path/to/nxmhandler-install
cmake --build C:\path\to\nxmhandler-build --config Release --target nxmhandler
```

The Release build maps the source, dependency and profile paths with
`/pathmap` like the Organizer target. The result is
`<build>\src\Release\nxmhandler.exe`; `Build-Release.ps1` takes it as
`-NxmHandlerExecutable` and refuses the upstream `nxmhandler.exe`. Building
also refreshes `src/nxmhandler_en.ts` with `lupdate`. The handler loads
`translations/revamped_<language>.qm` (after `qtbase_` and `nxmhandler_`), so
translations of its strings belong in `source/revamped/translations/`.

## Revamped components

The Elden Ring bridge source is under
`source/revamped/plugins/basic_games/games/`; build it with the pinned Zig
0.13.0 toolchain using `build_bridge.ps1`. Zig 0.17.0 was also tried against
the current bridge source, but exports additional internal DLL symbols. Keep
0.13.0 pinned until the export list is explicit. The installer targets C# 5
and .NET Framework 4.x and is built by `Build-Release.ps1`.

Prepare a Python 3.12.15 runtime overlay from the official CPython source build
and the `_ssl.pyd` / `_hashlib.pyd` extensions compiled against OpenSSL 3.5.9.
`Build-PythonRuntimeOverlay.ps1` rebuilds the embedded `pythoncore.zip`, copies
only the CPython extensions already used by MO2, preserves MO2/PyQt extensions,
and writes a SHA-256 manifest. Example:

```powershell
powershell -ExecutionPolicy Bypass -File source/revamped/installer/Build-PythonRuntimeOverlay.ps1 `
  -PythonSourceDirectory C:\path\to\cpython-3.12.15 `
  -PythonBuildDirectory C:\path\to\cpython-3.12.15\PCbuild\amd64 `
  -BasePluginPythonDirectory C:\path\to\Complete\plugins\plugin_python `
  -OpenSslExtensionDirectory C:\path\to\python-ssl-openssl-3.5.9\amd64 `
  -OutputDirectory C:\path\to\python-3.12.15-overlay
```

The overlay builder rewrites only PE CodeView PDB path records in the locally
built CPython DLLs and extension modules to retain the PDB filename without
the build machine's directory. Executable code and PDB identifiers stay
unchanged; the resulting manifest is calculated after this metadata cleanup.

The release builder requires a complete MO2 runtime directory, the newly
compiled `ModOrganizer.exe` and `nxmhandler.exe` (both overlaid onto the
runtime), Zig 0.13.0, the verified OpenSSL 3.5.9 runtime,
and this Python overlay. It checks the overlay hashes and versions, then
creates one general single-file installer and its checksum in a new or empty
output directory. The installer embeds both the Elden Ring Isolated payload
used for new installations and the Complete payload used to maintain existing
standard and portable installations. It also compiles
`source/revamped/translations/revamped_*.ts` into the payloads with the Qt
SDK's `lrelease`. Example:

```powershell
powershell -ExecutionPolicy Bypass -File source/revamped/installer/Build-Release.ps1 `
  -RuntimeDirectory C:\path\to\Complete `
  -OrganizerExecutable C:\path\to\ModOrganizer.exe `
  -NxmHandlerExecutable C:\path\to\nxmhandler-build\src\Release\nxmhandler.exe `
  -ZigPath C:\path\to\zig.exe `
  -OpenSslRuntimeDirectory C:\path\to\openssl-3.5.9 `
  -PythonRuntimeDirectory C:\path\to\python-3.12.15-overlay `
  -QtSdkDirectory C:\path\to\QtSdk `
  -OutputDirectory C:\path\to\new-release-directory
```

Revamped plugin sources are overlaid from `source/revamped/plugins/` onto the
Complete runtime before packaging. This is the maintained release copy; older
Elden Ring Save Isolation code in a baseline runtime must not be packaged in
its place. The builder compares the staged Save Isolation hash with that
maintained source for both Complete and Isolated payloads.

The source repository does not contain the Complete runtime or user content.
The builder deliberately excludes profiles, mods, downloads, overwrite files,
logs, caches, user settings, and build work. It verifies each archive against
a sorted SHA-256 manifest and checks that the intended payload resources are
embedded in the single setup executable.

## MO2 data locations

The current general setup creates an Elden Ring Isolated installation. Existing
Complete Standard and Complete Portable installations continue to use their
own data locations when maintained by the general installer. Complete
Standard stores global instances under
`%LOCALAPPDATA%\MO2 Revamped\Instances` and global preferences in
`%LOCALAPPDATA%\MO2 Revamped\MO2-Revamped-Global.ini`. Complete Portable stores
instances and preferences beneath its own installation. Elden Ring Isolated
keeps its settings and files beside its own installation and does not enumerate
global instances. None of these paths use the original
`%LOCALAPPDATA%\ModOrganizer` store. The optional Steam save route remains in
`%APPDATA%\EldenRing`, which is the game's own save location.

The migration action reads only the source directory that the user selects;
it does not scan the original MO2 AppData folder. Release assembly starts from
the Complete stage and checks that Isolated has identical non-plugin core files
and matching hashes for every plugin it shares with Complete.

## Release contents

The general installer provides a fresh Elden Ring Isolated setup plus update,
restore, and uninstall tasks. The Standard multi-instance setup and the
data-copy task, which installs the Complete payload, are shown as coming soon
until support for other instances is extended; the Complete payload stays
embedded so existing standard and portable installations can still be updated,
restored, and uninstalled. The release builder emits no separate
Elden-Ring-only setup executable.

For package version decisions and evidence boundaries, see
[`RELEASE-AUDIT.md`](RELEASE-AUDIT.md). For license requirements, read
[`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md) and the full texts in
`../licenses/`.
