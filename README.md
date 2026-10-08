# MO2 Revamped

MO2 Revamped is an independent community edition of Mod Organizer 2, based on
Mod Organizer 2 v2.5.2 and focused on Elden Ring modding workflows. It is
maintained by **ArialSenki**.

This repository contains the consolidated application source, Revamped
components, build notes, and third-party license notices. It is not an official
release of Mod Organizer 2 and is not affiliated with Nexus Mods, FromSoftware,
or Bandai Namco Entertainment.

## Repository contents

| Path | Contents |
| --- | --- |
| `source/modorganizer/` | Mod Organizer 2 application source |
| `source/mo2-python-plugins/` | MO2 Python plugins included in the source snapshot |
| `source/revamped/` | Revamped plugins, Elden Ring native bridge source, installer source, and Light/Dark themes |
| `source/usvfs/` | USVFS v0.5.7.2 source |
| `licenses/` | Full license texts, component notices, and Python package license metadata |
| `docs/` | Upstream provenance and build guidance |

See the [historical changelog](docs/HISTORICAL-CHANGELOG.md) for the reviewed
work to date and the [release audit](docs/RELEASE-AUDIT.md) for component
versions, checks, and deferred upgrades.

## Revamped components

- Elden Ring support and archive installation choices that preserve the mod's
  original folder layout or use MO2's standard layout.
- Profile-aware native DLL ordering and startup options through the Revamped
  native bridge.
- Separate Elden Ring save routes for MO2 instances and profiles.
- Revamped Light and Dark interface themes and installer sources.

The source tree is a consolidated snapshot. See [upstream provenance](docs/UPSTREAMS.md)
for the upstream versions and revisions recorded for its components.

## Building

Start with [the build notes](docs/BUILDING.md). Mod Organizer 2's development
environment is normally prepared with the upstream `mob` project; USVFS and
Revamped components have their own build requirements.

The application compiled from the current source tree with MSVC and Qt 6.7.1.
A clean-clone build and a from-source rebuild of the complete bundled runtime
are still separate tasks.

## Licensing and notices

The Mod Organizer 2 source is distributed under GPL-3.0-or-later. USVFS and
bundled dependencies retain their own licenses. Review [`LICENSE`](LICENSE),
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md), and the complete license
files in [`licenses/`](licenses/) before redistributing a modified build.

## Releases and support

The v1.0.0 installers and checksums are published in [GitHub Releases](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.0):

- [General installer — Complete, Portable, and available setup options](https://github.com/ArialSenki/MO2-Revamped/releases/download/v1.0.0/MO2-Revamped-Setup-1.0.0.exe)
- [Elden Ring isolated installer](https://github.com/ArialSenki/MO2-Revamped/releases/download/v1.0.0/MO2-Revamped-Elden-Ring-Isolated-Setup-1.0.0.exe)
- [SHA-256 checksums](https://github.com/ArialSenki/MO2-Revamped/releases/download/v1.0.0/SHA256SUMS.txt)

Use GitHub Issues to report problems or suggest changes. When filing an issue,
include the MO2 Revamped revision, the affected feature, and steps to reproduce
it. Do not attach game saves, account credentials, or unrelated personal files.
