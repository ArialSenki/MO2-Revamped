# Linux, Wine and Proton

MO2 Revamped is a Windows program. On Linux and the Steam Deck it runs under
Wine or Proton, in the same prefix as the game or in its own.

## What MO2 Revamped does under Wine

- It recognizes Wine at start and adds the Wine version to its reports.
- The setup puts the Visual C++ runtime DLLs next to `ModOrganizer.exe`. They are updated, restored and
  uninstalled with the rest of the files. Microsoft's DLLs installed in the
  prefix with winetricks or protontricks are accepted too.
- MO2 doesn't crash at start when Wine reports no screen.
- Window transparency (Mica) and rounded corners are off.
- Icons are drawn from pictures, because the Windows icon fonts aren't in a
  prefix. The optional Qt Quick windows still need the font.
- Without .NET in the prefix, C# FOMOD and OMOD archives go to the other
  installers, with a warning in the log.
- The built-in browser runs without its sandbox, which Wine doesn't support.
- Startup and memory diagnostics are on by default, so a failed start leaves a
  log (see below).
- Nexus `nxm://` links are accepted even when Wine can't say which program sent
  them.
- Shortcuts that Wine can't create are only a warning, and **Open MO2 when
  finished** starts MO2 directly.

## Installing

Run the setup in the prefix, for example with `protontricks-launch` or
`wine MO2-Revamped-Setup-<version>.exe`, and install into a folder inside the
prefix's `C:` drive, such as `C:\MO2 Revamped`. Then create an instance as on
Windows.

## When it doesn't start

1. Look for `logs\startup_diagnostics.log` next to `ModOrganizer.exe` (or
   `%TEMP%\ModOrganizer-startup_diagnostics.log` when that folder can't be
   written). The last lines show how far it
   got.
2. If there is no startup log at all, a DLL failed to load before MO2 could
   write anything, usually the Visual C++ runtime. Run the game or MO2 with
   `PROTON_LOG=1` and look for `err:module` lines in the Proton log.
3. If MO2 crashed, it saves a `ModOrganizer-*.dmp` file in the instance's
   `crashDumps` folder. Attach it to the issue with a bug report
   (see [Troubleshooting](troubleshooting.md)).

`MO2_REVAMPED_DIAGNOSTICS=0` turns the diagnostics off.

## Known limits

- The Wine and Proton fixes have only been checked on Windows so far. Please
  report problems with your distribution, your Wine or Proton version and the
  logs above.
