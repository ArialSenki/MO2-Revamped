# Build guidance

The repository contains a consolidated source snapshot. The application was
compiled from the current source tree on Windows with MSVC 14.44, Qt 6.7.1,
Python 3.12.15, and the MO2 build dependencies recorded in
[`UPSTREAMS.md`](UPSTREAMS.md). A new external CMake build directory completed
successfully. This does not yet prove that a fresh clone can recreate the full
runtime: MO2 runtime DLLs, plugin binaries, Qt/Python distributions, and other
redistributable files are supplied separately from the source tree.

For release builds, map the developer checkout root to `.` in the C and C++
Release flags with MSVC `/pathmap` and `/experimental:deterministic`. The
GitHub++ static library must also undefine `QT_MESSAGELOGCONTEXT` in Release
with `/UQT_MESSAGELOGCONTEXT`; otherwise Qt embeds its absolute source filename
in diagnostic context. The Elden Ring bridge build applies Zig's
`-ffile-prefix-map` and strips linked debug metadata, including paths from Zig's
bundled unwind library. The release builder checks staged payloads and installer
binaries for local profile and checkout paths before producing the installers.
It maps the reviewed Light and Dark styles to `stylesheets/Light.qss` and
`stylesheets/dark.qss`, which are the names the application loads.

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
compiled `ModOrganizer.exe`, Zig 0.13.0, the verified OpenSSL 3.5.9 runtime,
and this Python overlay. It checks the overlay hashes and versions, then
creates two single-file installers and their checksums in a new or empty
output directory. Example:

```powershell
powershell -ExecutionPolicy Bypass -File source/revamped/installer/Build-Release.ps1 `
  -RuntimeDirectory C:\path\to\Complete `
  -OrganizerExecutable C:\path\to\ModOrganizer.exe `
  -ZigPath C:\path\to\zig.exe `
  -OpenSslRuntimeDirectory C:\path\to\openssl-3.5.9 `
  -PythonRuntimeDirectory C:\path\to\python-3.12.15-overlay `
  -OutputDirectory C:\path\to\new-release-directory
```

The source repository does not contain the Complete runtime or user content.
The builder deliberately excludes profiles, mods, downloads, overwrite files,
logs, caches, user settings, and build work. It verifies each archive against
a sorted SHA-256 manifest and checks that the intended payload resources are
embedded in the two installer executables.

## Release contents

The general installer provides fresh Complete/Portable setup, an Elden Ring
isolated portable option, supported update/migrate/restore flows, and
uninstall. The separate Elden Ring installer contains only the Elden Ring
isolated portable setup and its corresponding maintenance flows.

For package version decisions and evidence boundaries, see
[`RELEASE-AUDIT.md`](RELEASE-AUDIT.md). For license requirements, read
[`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md) and the full texts in
`../licenses/`.
