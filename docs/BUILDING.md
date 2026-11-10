# Build guidance

The repository contains a consolidated source snapshot. The application was
compiled from the current source tree on Windows with MSVC 14.44, Qt 6.7.1,
Python 3.12.15, and the MO2 build dependencies recorded in
[`UPSTREAMS.md`](UPSTREAMS.md). A new external CMake build directory completed
successfully. Every Mod Organizer 2 binary of a release is built from this
repository (see below), and so are its translations, the BAIN wizard
interpreter, and the two third-party binaries MO2 compiled itself, `7z.dll`
and PyQt6. Qt (with its own `qt_`/`qtbase_` translations), Python, lz4,
libbsarch, LOOT, Explorer++ and the third-party Python libraries are still
supplied separately, from the runtime and the overlays; when this was written,
the Qt, lz4, libbsarch and LOOT files there were byte for byte the projects'
official releases (compared by hand, not by the builder).

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
It maps the reviewed Light and Dark styles to `stylesheets/Light.qss` and
`stylesheets/dark.qss`, which are the names the application loads.

## Mod Organizer 2

Use the official [MO2 build project (`mob`)](https://github.com/ModOrganizer2/mob)
to prepare the third-party dependencies (Boost, spdlog, zlib, lz4, libbsarch,
DirectXTex, libloot) and Qt 6.7.1. Do not mix Qt, Python, compiler, or
USVFS versions without rebuilding and checking the native extensions.

The Mod Organizer 2 libraries and plugins come from `source/upstream/`, not
from `mob` or a release: uibase, cmake_common, archive, githubpp, bsatk, esptk,
the plugins, plugin_python, lootcli and helper, at the 2.5.2 commits in
`source/upstream/UPSTREAM-COMMITS.txt`. Build them first, into a folder outside
the repository:

```powershell
powershell -ExecutionPolicy Bypass -File source/revamped/installer/Build-UpstreamPlugins.ps1 `
  -OutputDirectory C:\path\to\upstream-out `
  -ThirdPartyDirectory C:\path\to\mo\build `
  -QtDirectory C:\path\to\Qt\6.7.1\msvc2019_64 `
  -PythonDirectory C:\path\to\python-3.12 `
  -BuildPython C:\path\to\installed\Python312\python.exe
```

`-PythonDirectory` is a CPython 3.12 tree with `Include\` and
`PCBuild\amd64\` (`python.exe`, `python312.lib`). `-BuildPython` is an
installed CPython 3.12 with `venv` and `pip`: the script makes a virtual
environment from it and installs sip and PyQt-builder there with
`pip --require-hashes` from `source/upstream/third-party/pyqt-build-requirements.txt`.
`-QtDirectory` is the official Qt 6.7.1 `msvc2019_64` with the modules PyQt6
binds, for example with aqtinstall:
`aqt install-qt windows desktop 6.7.1 win64_msvc2019_64 -m qtactiveqt qtconnectivity qtmultimedia qtquick3d qtshadertools qtremoteobjects qtsensors qtserialport qtspeech qtpositioning qtwebchannel qtwebsockets qtpdf`.

`7z.dll` and PyQt6 are built from the official sources in
`source/upstream/third-party/` (see its README), checked against
`SHA256SUMS.txt`: 7-Zip 26.06 with `nmake` in `CPP/7zip/Bundles/Format7zF`,
PyQt6 6.7.1 with `sip-install --link-full-dll`, and PyQt6-sip 13.8.0 as a wheel,
as `mob` built them for 2.5.2. PyQt-builder gets `pyqt-builder-link-full-dll.patch`,
the linker part of `mob`'s own patch, so the modules link `python312.dll` (the
release ships no `python3.dll`). The Designer and qmlscene plugins are not
built; MO2 does not ship them. The two C++/CLI installers
need the Visual Studio Build Tools components ".NET Framework 4.8 SDK",
".NET Framework 4.8 targeting pack" and ".NET Framework 4.7.2 targeting pack"
(CMake's C# detection asks for 4.7.2), and archive needs "C++ ATL for latest
build tools". installer_omod restores its .NET libraries from nuget.org; the
build checks they are byte for byte the ones 2.5.2 shipped.

The script builds in RelWithDebInfo, as the official plugin builds did, with
source, dependency, build and profile paths mapped, installs into `upstream-out\install` (the runtime layout plus the
import libraries) and checks every binary against `source/upstream/abi/`, which
was written from the official 2.5.2 binaries: uibase must export the same 663
symbols, USVFS, `7z.dll` and each PyQt6 module their exports, each file keeps
its DLL dependencies (the Universal CRT and Visual C++ runtime DLLs aside, which
follow the compiler version) and its plugin interface IDs, everything imported
from uibase must exist, and no local path may remain. Only
then does it fill `upstream-out\stage` with the shipped files and
`upstream-manifest.json`.

The Organizer then builds against those sources and that install tree, with
`-DDEPENDENCIES_DIR=<repository>/source/upstream` and
`-DMO2_INSTALL_PATH=C:/path/to/upstream-out/install` (plus the dependency roots
as for the NXM handler below).

## Keeping up with Mod Organizer 2

The sources in `source/upstream` and `source/usvfs` stay at the versions
recorded in `source/upstream/UPSTREAM-COMMITS.txt` and `UPSTREAMS.md` until a
fix is brought in by hand. `Check-Upstream.ps1` lists what MO2's repositories
have published since then, and the latest releases of 7-Zip, libloot,
pybind11, PyQt6 and PyQt6-sip:

```powershell
powershell -ExecutionPolicy Bypass -File source/revamped/installer/Check-Upstream.ps1 `
  -Report C:\path\to\upstream-report.md [-Since yyyy-MM-dd]
```

It reads public GitHub and PyPI data only, through the GitHub CLI's sign-in
when there is one (more requests an hour). MO2 has since merged the Bethesda
game plugins into `modorganizer-game_bethesda` and keeps the Python plugins in
repositories of their own; for those the report lists the commits since the
2.5.2 release. A fix taken from there is built and checked with
`Build-UpstreamPlugins.ps1` like any other change: a change that alters
uibase's exports or a plugin's interface fails its ABI checks.

## USVFS

USVFS source is in `source/usvfs/`. Its upstream README and CMake files list
component build options. The source snapshot is USVFS v0.5.7.2, matching the
included MO2 runtime. `Build-UpstreamPlugins.ps1` builds it too, x64 and x86,
with its presets and the vcpkg dependencies `vcpkg-configuration.json` pins
(Visual Studio's own vcpkg, or `-VcpkgRoot`), and checks both DLLs against the
114 exports of the 0.5.7.2 release. The Organizer links it with
`-DMO2_USVFS_OVERRIDE_ROOT=C:/path/to/upstream-out/install`.

## NXM handler

`source/nxmhandler/` holds the nxm link handler (`nxmhandler.exe`), based on
upstream modorganizer-nxmhandler at the commit shipped with MO2 2.5.2 (see
[`UPSTREAMS.md`](UPSTREAMS.md)). MO2 Revamped replaces its handler list window
with a Fluent version; the command line, the registration and the
`nxmhandler.ini` format stay compatible. It builds like the Organizer target,
against `source/upstream` (`cmake_common`, `uibase`), the install tree of
`Build-UpstreamPlugins.ps1` and Qt 6.7.1, in a build directory of its own:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
cmake -G "Visual Studio 17 2022" -A x64 -S source/nxmhandler -B C:\path\to\nxmhandler-build ^
  -DDEPENDENCIES_DIR=C:/path/to/repository/source/upstream ^
  -DMO2_INSTALL_PATH=C:/path/to/upstream-out/install -DBOOST_ROOT=C:/path/to/mo/build/boost_1_85_0 ^
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
output directory. The installer embeds both the Complete payload used for new
installations and for maintaining standard and portable ones, and the Elden Ring
Isolated payload used to update existing isolated installations. It also compiles
`source/revamped/translations/revamped_*.ts` into the payloads with the Qt
SDK's `lrelease`. Example:

```powershell
powershell -ExecutionPolicy Bypass -File source/revamped/installer/Build-Release.ps1 `
  -RuntimeDirectory C:\path\to\Complete `
  -OrganizerExecutable C:\path\to\ModOrganizer.exe `
  -NxmHandlerExecutable C:\path\to\nxmhandler-build\src\Release\nxmhandler.exe `
  -UpstreamStageDirectory C:\path\to\upstream-out\stage `
  -ZigPath C:\path\to\zig.exe `
  -OpenSslRuntimeDirectory C:\path\to\openssl-3.5.9 `
  -PythonRuntimeDirectory C:\path\to\python-3.12.15-overlay `
  -QtSdkDirectory C:\path\to\QtSdk `
  -OutputDirectory C:\path\to\new-release-directory
```

From the Complete runtime the builder takes only what has no source here (Qt,
lz4, libbsarch, LOOT, Explorer++, the third-party Python libraries the plugins
bundle, and Qt's `qt_`/`qtbase_` translations). It never
stages the runtime's copies of the files `-UpstreamStageDirectory` lists: those
come from the upstream stage, whose manifest must match its files and the
committed `source/upstream`. The Python plugins, with the BAIN wizard
interpreter, are copied from `source/mo2-python-plugins/`; the splash,
`resources/markdown.html` and `dlls/dlls.manifest` from
`source/modorganizer/src/`, and `MO2-Revamped.ico` from
`source/revamped/branding/`. The MO2 translations are compiled with `lrelease`
from `source/upstream/translations/`, and the build stops if the runtime has an
MO2 `.qm` file without a source there. Since they are part of
`source/upstream`, changing them means building the upstream stage again.

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
