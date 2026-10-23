# MO2 Revamped

**MO2 Revamped** is an independent community edition of
[Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer), built on
MO2 v2.5.2. It keeps the MO2 workflow you already know for every game MO2
supports, and adds a refreshed interface, a guided installer and dedicated
tools for **Elden Ring** and **Elden Ring Nightreign**. It is maintained by
**ArialSenki**.

> MO2 Revamped is not an official Mod Organizer 2 release and is not affiliated
> with Nexus Mods, FromSoftware, or Bandai Namco Entertainment.

## Download

**Latest: [v1.0.2](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.2)**
— `MO2-Revamped-Setup-1.0.2.exe` with its `SHA256SUMS.txt`.

Earlier releases, [v1.0.1](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.1)
and [v1.0.0](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.0),
remain available.

### Installing

1. Run `MO2-Revamped-Setup-1.0.2.exe` and pick your language.
2. Choose **Install MO2 Revamped** and the installation folder.
3. When setup finishes, MO2 starts in the language you chose. Create an
   instance for your game; for Elden Ring or Nightreign the New instance
   wizard also offers an **isolated** instance that keeps its profiles, mods,
   downloads and settings in its own folder.

Run the same setup again to **update**, **restore**, **copy data** or
**uninstall**. Updates keep your data and back up the previous files first;
isolated Elden Ring installations from 1.0.1 are updated the same way.

## What's new in 1.0.2

- **Elden Ring Nightreign.** Save isolation, named save profiles, save
  backups, the launch save guard, native DLL loading, the Native DLL Profile,
  startup options and the archive installer, as in Elden Ring.
- **Safer saves.** While the game runs from an isolated profile, the Steam
  save folders are set aside and come back unchanged when it closes, with
  every launcher (Mod Engine 2, Seamless Co-op, me3). Saves are backed up
  before each launch and can be restored from Save Isolation.
- **Complete installation.** One MO2 Revamped for every game MO2 supports;
  isolated copies for one game are made from the New instance wizard.
- **Fluent dialogs.** Profiles, Executables, Notifications, Manage Overwrite,
  notices and the Nexus links window follow the instance manager's card
  layout and Windows 11 menus.
- **A more careful installer.** Names programs still using the files,
  reinstalls next to data an uninstall kept, follows the Windows accent colour
  and light or dark mode, and stays responsive while it checks.

The full list is in the [changelog](CHANGELOG.md).

## Features

- **Archive installer** (Elden Ring, Nightreign) — keep a mod's original
  folder layout or use MO2's standard layout.
- **Native DLL Profile** — per-profile order of native DLL mods, loaded through
  the Revamped native bridge, also with Seamless Co-op, Mod Engine 2 and me3.
- **Startup Options** — launch minimized, black background, and skip the
  intro logos (Elden Ring).
- **Save Isolation** — separate save routes per profile or instance, named
  save profiles, automatic backups and restore, so playthroughs never
  overwrite each other.
- **Six languages** — English, Spanish, German, French, Italian and Brazilian
  Portuguese.
- **Themes** — Light, Dark Blue and Dark Black.

### Where your data lives

MO2 Revamped keeps its global instances in `%LOCALAPPDATA%\MO2 Revamped` and
never reads the original MO2 global instance folder. Isolated instances keep
everything inside their own folder. The optional Steam save route still uses
the game's own Windows save directory, and data migration only reads a folder
you select yourself.

## Repository layout

| Path | Contents |
| --- | --- |
| `source/modorganizer/` | Mod Organizer 2 application source with Revamped changes |
| `source/revamped/` | Revamped plugins, Elden Ring native bridge, installer, themes and translations |
| `source/mo2-python-plugins/` | Baseline MO2 Python plugins |
| `source/usvfs/` | USVFS v0.5.7.2 source |
| `licenses/` | Full license texts and component notices |
| `docs/` | Build notes, upstream provenance, release audit and history |

The source tree is a consolidated snapshot; [upstream provenance](docs/UPSTREAMS.md)
records the versions and revisions of each component.

## Building

Start with the [build notes](docs/BUILDING.md). MO2's development environment
is normally prepared with the upstream `mob` project; USVFS and the Revamped
components have their own requirements. The release installer is produced by
`source/revamped/installer/Build-Release.ps1`. Checks and hashes for each
candidate are recorded in the [release audit](docs/RELEASE-AUDIT.md).

## Reporting problems

Open a [GitHub issue](https://github.com/ArialSenki/MO2-Revamped/issues) with
the MO2 Revamped version, the affected feature, and the steps to reproduce it.
Attaching the bug-report ZIP that MO2 Revamped can create helps. Please do not attach game saves,
account credentials or unrelated personal files.

## License

Mod Organizer 2 is distributed under GPL-3.0-or-later. USVFS and the bundled
dependencies keep their own licenses. See [`LICENSE`](LICENSE),
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md) and [`licenses/`](licenses/)
before redistributing a modified build.
