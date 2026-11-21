# Installing and updating

MO2 Revamped comes as one setup program, `MO2-Revamped-Setup-<version>.exe`,
from the [GitHub releases](https://github.com/ArialSenki/MO2-Revamped/releases)
or [Nexus Mods](https://www.nexusmods.com/site/mods/2612). Each GitHub release
also has a `SHA256SUMS.txt` to check the download.

The setup speaks English, Spanish, German, French, Italian and Brazilian
Portuguese. The language you pick is also the one MO2 starts in.

## What the setup can do

Run the same setup again for any of these:

| Choice | What it does |
| --- | --- |
| **Install MO2 Revamped** | A new installation. The default folder is `C:\MO2 Revamped`. |
| **Update MO2 Revamped** | Replaces the program files of an installation with this version. |
| **Copy data** | Copies an existing MO2 setup's data into a new portable one. |
| **Restore previous backup** | Brings back the version saved by the last update. |
| **Uninstall MO2 Revamped** | Removes an installation. |

There is also **Install isolated Elden Ring and Nightreign**, a separate copy
of MO2 Revamped for those two games only, by default in
`C:\MO2 Revamped\Isolated\Elden Ring`. It keeps its profiles, mods, downloads
and settings in its own folder. See [Instances](instances.md#isolated-instances).

### Where to install

Use a folder you can write to, such as the default `C:\MO2 Revamped`. The setup
warns if you choose Program Files, where Windows protects the files and MO2
can run into permission problems.

### Options when it finishes

- **Open MO2 when finished** (on by default).
- A Start Menu shortcut (on) and a desktop shortcut (off).
- **Open Nexus `nxm://` links with this MO2** (off). Turn it on if this is the
  MO2 that should receive the **Mod manager download** buttons from Nexus. MO2
  can also register itself later in Settings > Nexus.
- **Force-close MO2 in this folder if it is still running.** Without it, the
  setup names the programs that still use the files and waits.

## Updating

Choose **Update MO2 Revamped** and the installation folder.

- Replaced files are backed up first.
- Profiles, mods, downloads and additional plugins are kept.
- Existing shortcuts are kept; the checkboxes add any that are missing.

The backup is in `%LOCALAPPDATA%\MO2 Revamped Installer\backups`. Only the
newest one is kept.

Isolated Elden Ring installations, including those from 1.0.1, are updated
the same way.

## Restoring the previous version

**Restore previous backup** puts back the program files saved by the last
update. Your profiles, mods, downloads and files you changed since then are
kept. It is only offered in the setup window, not in silent mode.

## Copying data

**Copy data** copies an existing setup into a new portable folder, by default
`C:\MO2 Revamped\Portable`. You choose what to copy: profiles and saves,
installed mods, downloads, Overwrite, MO2 preferences, and categories and the
Nexus map. The source folder isn't changed.

## Uninstalling

- **Keep my mods and data** (the default) removes the program and leaves your
  data.
- **Remove everything, including my mods, data and the folder** removes the
  whole folder, and the update backup if there is one.
- **Also remove my instances, mods, downloads and settings** also removes
  `%LOCALAPPDATA%\MO2 Revamped`, where the global instances live. It isn't
  offered while another standard installation is still registered.

Files in the folder that don't belong to MO2 are kept and listed at the end.
Uninstalling also removes the shortcuts and the Nexus link registration.

## Silent install

The setup has switches for scripted installs:

```text
MO2-Revamped-Setup-<version>.exe /S /dir="C:\MO2 Revamped" [/isolated] [/nodesktop] [/nostartmenu] [/nonxm] [/launch] [/forceclose]
MO2-Revamped-Setup-<version>.exe /S /uninstall /dir="C:\MO2 Revamped" [/keepdata | /removeall [/removeappdata]] [/forceclose]
```

- `/S` can also be written `/quiet`, `/silent` or `/verysilent`; `/D=` works
  like `/dir=`.
- A silent run with an existing installation updates it, and never installs
  an older version over a newer one.
- `/keepdata` is the default for uninstalling. On a standard installation,
  `/removeall` also needs `/removeappdata`.
- `unins000.exe /S` in the installation folder uninstalls that folder.
- Exit codes: 0 done, 1 failed, 2 wrong command line (nothing was changed).

The setup's logs are in `%ProgramFiles%\MO2 Revamped Setup\Installer Logs`, or
`%TEMP%\MO2 Revamped\Installer Logs` when it runs without administrator
rights. The last ten are kept.

## The update notice

MO2 Revamped asks GitHub for new releases at most once a day. When a newer
version is out, a window **MO2 Revamped <version> is out** lists every newer
release with its notes, with these buttons:

- **Open on GitHub** and **Open on Nexus** open the download page. Nothing is
  downloaded or installed by MO2 itself: you update with the setup.
- **Remind me later** asks again on a later start.
- **Skip this version** stops the window for that version; it then only shows
  in the status bar.

Settings > General > **Updates**:

- **Tell me when a new MO2 Revamped version is out** turns the check on or off
  (on by default).
- **Versions to hear about**: **Stable releases**, **Stable and beta**, or
  **Stable, beta and development builds**. By default a beta build hears about
  betas and a stable build only about stable releases.

In offline mode (Settings > Workarounds) there is no check.

## MO2 Revamped and the original MO2

MO2 Revamped keeps its global instances in `%LOCALAPPDATA%\MO2 Revamped` and
never reads or changes the original MO2's global instances. Both can be
installed, but only one of them runs at a time: starting one while the other is
open brings the open window to the front.
