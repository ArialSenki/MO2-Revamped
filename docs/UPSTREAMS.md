# Upstream provenance

The repository preserves the source versions and revision references recorded
when the consolidated source was prepared. It is not a mirror of the upstream
Git histories.

| Component | Recorded base | Notes |
| --- | --- | --- |
| Mod Organizer 2 | v2.5.2 plus commits `9581744` and `aaad42f` | Revamped changes are layered onto this source. [Upstream repository](https://github.com/ModOrganizer2/modorganizer) |
| USVFS | v0.5.7.2, commit `a50d84c64c9244f80dc67e9fe7af209bfe514d5b` | [Upstream repository](https://github.com/ModOrganizer2/usvfs) |
| YAFSML Elden Ring resolver | Upstream patch by Soar Qin, 2024 | Adaptation and MIT notice are documented in [`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md). |
| MinHook | Vendored source under `source/revamped/plugins/basic_games/games/vendor/minhook/` | BSD 2-Clause notice is preserved beside the source and in `licenses/`. |

## Release runtime versions

| Component | Version used | Source or decision |
| --- | --- | --- |
| Mod Organizer 2 | 2.5.2 | [Upstream releases](https://github.com/ModOrganizer2/modorganizer/releases) |
| USVFS | 0.5.7.2 | [Upstream releases](https://github.com/ModOrganizer2/usvfs/releases) |
| Qt / PyQt | 6.7.1 | Kept with the runtime and compiled extensions. [Qt 6.12.0](https://doc.qt.io/qt-6.12/qt-releases.html) and [PyQt 6.11.0](https://www.riverbankcomputing.com/software/pyqt/download) require a coordinated Qt, WebEngine, PyQt, and native-extension rebuild. |
| Python | 3.12.15 | Rebuilt from the official [source release](https://www.python.org/downloads/release/python-31215/). The embedded standard-library bytecode and CPython extensions are updated; `_ssl` and `_hashlib` are built against OpenSSL 3.5.9. MO2 and PyQt extensions remain on their matching CPython 3.12 ABI. |
| OpenSSL | 3.5.9 | Rebuilt from the official [3.5.9 LTS source release](https://openssl-library.org/source/); see the runtime audit and third-party notices. |
| libloot / LOOT CLI | 0.23.0 / 1.6.0 | Retained to match the MO2 2.5.2 integration; [libloot 0.29.6](https://github.com/loot/libloot/releases) needs API and sorting validation before adoption. |
| Boost | 1.85.0 | Build-time dependency, not a runtime component; [Boost 1.92.0](https://www.boost.org/releases/latest/) was not substituted without a native rebuild and regression pass. |
| Zig | 0.13.0 | Pinned compiler for the Elden Ring bridge build. Zig 0.17.0 compiled the current source but changed the DLL export table; it remains deferred until exports are explicit. |

The exact update decisions and deferred platform migrations are recorded in
[`RELEASE-AUDIT.md`](RELEASE-AUDIT.md).

The Elden Ring MO2 support plugins, Revamped installer, native bridge, and
themes are maintained in `source/revamped/`. Their changes are part of this
community edition; the upstream projects retain their own authorship and
licenses.
