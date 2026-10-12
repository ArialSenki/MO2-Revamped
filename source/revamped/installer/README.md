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
  -ZigPath $zigPath `
  -OpenSslRuntimeDirectory $openSslRuntimeDirectory `
  -PythonRuntimeDirectory $pythonRuntimeDirectory `
  -OutputDirectory $outputDirectory `
  -QtSdkDirectory $qtSdkDirectory
```

The builder compiles the Qt UI, deploys its Windows platform plugin and runtime libraries, embeds that bundle in the C# installer and uninstaller stubs, and checks that the expected embedded resources are present.

## Architecture

The Qt interface exchanges a small JSON request, progress stream, and result file with the C# executable. The C# executable remains the source of truth for install, update, migration, restore, and uninstall operations. Its existing payloads and manifests remain embedded in the installer. The Qt runtime is extracted to a unique temporary directory at launch and cleaned up after the window closes.

The UI follows the Windows light/dark preference, uses the existing six installer languages, and keeps the original confirmation and validation flow. Qt and MinGW notices are listed in the repository's `THIRD-PARTY-NOTICES.md` and license files.
