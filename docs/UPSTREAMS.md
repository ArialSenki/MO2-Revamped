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
| Qt / PyQt | 6.7.1 | Kept with the runtime and compiled extensions. Standard support for Qt 6.7 has ended. [Qt 6.12.0](https://www.qt.io/blog/qt-6.12-released) became the current LTS on 2026-09-30. Riverbank's [PyQt 6.11.0](https://riverbankcomputing.com/news/PyQt_v6.11.0_Released) officially added Qt 6.11 support; [PyQt-builder 1.20.0](https://riverbankcomputing.com/news/PyQt-builder_v1.20.0_Released) added support for building against Qt 6.12 on 2026-10-04. This makes a migration buildable, but not yet verified for MO2: Qt, WebEngine, PyQt, and all native extensions need a coordinated rebuild and UI regression before adoption. |
| Python | 3.12.15 | Rebuilt from the official [source release](https://www.python.org/downloads/release/python-31215/). The embedded standard-library bytecode and CPython extensions are updated; `_ssl` and `_hashlib` are built against OpenSSL 3.5.9. MO2 and PyQt extensions remain on their matching CPython 3.12 ABI. |
| OpenSSL | 3.5.9 | Rebuilt from the official [3.5.9 LTS source release](https://openssl-library.org/source/); see the runtime audit and third-party notices. |
| 7-Zip runtime | 24.05 | Retained with MO2 2.5.2's archive integration. [7-Zip 26.04](https://www.7-zip.org/) is the upstream release dated 2026-10-05; changing the bundled DLL needs archive-format and MO2 installation-manager regression tests before adoption. |
| LOOT CLI / libloot | 1.6.0 / 0.23.0 | Retained to match the MO2 2.5.2 integration. Upstream [LOOT 0.29.2](https://github.com/loot/loot/releases) bundles [libloot 0.29.6](https://github.com/loot/libloot/releases); an update needs API and sorting regression tests. |
| Explorer++ | 1.4.0.2293 | The bundled file browser matches the latest [1.4.0 upstream release](https://github.com/derceg/explorerplusplus/releases). |
| Boost | 1.85.0 | Build-time dependency, not a runtime component; [Boost 1.92.0](https://www.boost.org/releases/latest/) was not substituted without a native rebuild and regression pass. |
| Zig | 0.13.0 | Pinned compiler for the Elden Ring bridge build. Stable [Zig 0.17.0](https://ziglang.org/news/0.17.0-released/) was released on 2026-10-02. It compiles the source but changed the DLL export table, so adoption remains deferred until exports are explicit and the bridge is validated with the native loader. |

The exact update decisions and deferred platform migrations are recorded in
[`RELEASE-AUDIT.md`](RELEASE-AUDIT.md).

The Elden Ring MO2 support plugins, Revamped installer, native bridge, and
themes are maintained in `source/revamped/`. Their changes are part of this
community edition; the upstream projects retain their own authorship and
licenses.
