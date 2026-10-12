# MO2 Revamped

**MO2 Revamped** is an independent community edition of
[Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer), built on
MO2 v2.5.2 and focused on modding **Elden Ring**. It keeps the MO2 workflow you
already know and adds Elden Ring tools, a refreshed interface, and a guided
installer. It is maintained by **ArialSenki**.

> MO2 Revamped is not an official Mod Organizer 2 release and is not affiliated
> with Nexus Mods, FromSoftware, or Bandai Namco Entertainment.

## Download

**Latest: [v1.0.1](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.1)**
— `MO2-Revamped-Setup-1.0.1.exe` with its `SHA256SUMS.txt`.

The previous release, [v1.0.0](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.0),
remains available.

### Installing

1. Run `MO2-Revamped-Setup-1.0.1.exe` and pick your language.
2. Choose **Install isolated Elden Ring**. This creates a self-contained,
   portable MO2 for Elden Ring that keeps all of its data (profiles, mods,
   downloads, settings) beside the installation.
3. When setup finishes, MO2 starts in the language you chose.

Run the same setup again to **update**, **restore** or **uninstall** an
existing installation; updates keep your data and back up the previous files
first. The Standard multi-instance installation and "Copy data" are shown as
*coming soon* in this version.

## What's new in 1.0.1

- **Isolated installer.** One setup file installs, updates, restores and
  uninstalls the Elden Ring edition, and starts MO2 afterwards without
  administrator rights.
- **Six languages.** English, Spanish, German, French, Italian and Brazilian
  Portuguese across the interface, tutorials and every Revamped tool.
- **Reliable startup options.** "Start minimized" and "Black background" now
  apply on every launch.
- **Skip the intro logos.** A new Startup option goes straight to the title
  screen. To also avoid the online connection notices, set
  **System > Network > Launch Setting** to **Play Offline** in the game.
- **Refreshed interface.** Light, Dark Blue and Dark Black themes, redesigned
  dialogs and prompts, and translated tool names.

The full list is in the [changelog](CHANGELOG.md).

## Features

- **Elden Ring archive installer** — keep a mod's original folder layout or use
  MO2's standard layout.
- **Native DLL Profile** — per-profile order of native DLL mods, loaded through
  the Revamped native bridge.
- **Startup Options** — launch minimized, black background, and skip the intro logos.
- **Save Isolation** — separate Elden Ring save routes per MO2 instance or
  profile, so playthroughs never overwrite each other.
- **Themes** — Light, Dark Blue and Dark Black.

### Where your data lives

The isolated edition keeps everything inside its own folder. MO2 Revamped
never reads the original MO2 global instance folder. The optional Steam save
route still uses Elden Ring's own Windows save directory, and data migration
only reads a folder you select yourself.

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
