# Installer build

The setup window uses Qt Widgets 6.10.3 with the matching MinGW 13.1.0 x64 kit. Its installation engine, payload verification, recovery, and uninstall logic remain in the existing C# 5/.NET Framework executable. The release builder embeds the deployed Qt runtime into that executable, so a release still launches as one self-contained setup file.

## Build requirements

- Windows with the .NET Framework 4.x C# compiler.
- The Qt SDK directory containing `6.10.3\mingw_64`, `Tools\mingw1310_64`, `Tools\CMake_64`, and `Tools\Ninja`.
- The existing MO2 runtime, rebuilt organizer executable, Zig compiler, OpenSSL runtime, and Python runtime overlay required by `Build-Release.ps1`.

The Qt SDK is a build dependency and is not checked into this repository. Pass its root directory using `-QtSdkDirectory` when building a release:

```powershell
.\source\revamped\installer\Build-Release.ps1 `
  -RuntimeDirectory $runtimeDirectory `
  -OrganizerExecutable $organizerExecutable `
  -NxmHandlerExecutable $nxmHandlerExecutable `
  -ZigPath $zigPath `
  -OpenSslRuntimeDirectory $openSslRuntimeDirectory `
  -PythonRuntimeDirectory $pythonRuntimeDirectory `
  -OutputDirectory $outputDirectory `
  -QtSdkDirectory $qtSdkDirectory
```

The builder compiles the Qt UI, deploys its Windows platform plugin and runtime libraries, embeds that bundle in the C# installer and uninstaller stubs, and checks that the expected embedded resources are present.

## Architecture

The Qt interface exchanges a small JSON request, progress stream, and result file with the C# executable. The C# executable remains the source of truth for install, update, migration, restore, and uninstall operations. Its existing payloads and manifests remain embedded in the installer. The Qt runtime is extracted at launch to a unique folder below `Program Files\MO2 Revamped Setup`, which only administrators can write, and cleaned up after the window closes; the JSON files live there too. Without elevation, or if Windows refuses that folder, `%TEMP%` is used.

The UI follows the Windows light/dark preference, uses the existing six installer languages, and keeps the original confirmation and validation flow. Qt and MinGW notices are listed in the repository's `THIRD-PARTY-NOTICES.md` and license files.

An `unins000.exe` inside the installation it removes copies itself to its work folder and runs from there, and every process the setup starts works in the Windows system folder, so nothing holds the installation folder and it can be deleted entirely.

## Silent runs

Both the setup and `unins000.exe` run without the window when given `/S` (or `/quiet`); the exit code is 0 on success, 1 on failure and 2 for a wrong command line, and the log goes to `%TEMP%\MO2 Revamped\Installer Logs`.

- Install or update: `/S [/dir=<folder>] [/isolated] [/nodesktop] [/nostartmenu] [/nonxm] [/launch] [/forceclose]`. A folder that holds MO2 Revamped is updated (never to an older version); any other gets a new installation.
- Uninstall: `/S /uninstall /dir=<folder> [/keepdata | /removeall [/removeappdata]] [/forceclose]`, or `unins000.exe /S` (the uninstall entry's `QuietUninstallString`; `unins000.exe` is the only one that may omit the folder, which is then its own). `/keepdata` is the default.
- An unknown switch, a stray word, a missing folder for an uninstall or `/restore` gives exit code 2 and changes nothing. A copied uninstaller waits for the `unins000.exe` that started it, which returns the copy's exit code.

What an uninstall removes: the files its record lists (or, without one, MO2's known program files), what MO2 writes while it runs (logs, web cache, crash dumps, `nxmhandler.ini`, the Python plugin settings) and, when asked, MO2's data. Anything else in the folder is kept and named in the result, and so is every folder an instance's `ModOrganizer.ini` places inside it. The folder is removed only when empty, and the folders above it only if the first installation created them. "Remove everything" clears the whole folder only when the first installation found it empty.

On a standard installation `/removeall` needs `/removeappdata`, because it removes `%LOCALAPPDATA%\MO2 Revamped`, which every standard installation shares. It is refused while another standard installation is registered, and when the setup runs as another account than the signed-in user.
