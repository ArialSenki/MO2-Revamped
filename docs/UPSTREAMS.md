# Upstream provenance

The repository preserves the source versions and revision references recorded
when the consolidated source was prepared. It is not a mirror of the upstream
Git histories.

| Component | Recorded base | Notes |
| --- | --- | --- |
| Mod Organizer 2 | v2.5.2 plus commits `9581744` and `aaad42f` | Revamped changes are layered onto this source. [Upstream repository](https://github.com/ModOrganizer2/modorganizer) |
| USVFS | v0.5.7.2, commit `a50d84c64c9244f80dc67e9fe7af209bfe514d5b` | Built from `source/usvfs/` by `Build-UpstreamPlugins.ps1` (x64 and x86); the release no longer takes the 0.5.7.2 binaries. [Upstream repository](https://github.com/ModOrganizer2/usvfs) |
| NXM handler | modorganizer-nxmhandler 1.2.0, commit `9876f57cd508c56028ec16ba0967ef1ead6fc47d` (the `master` used for MO2 2.5.2; its `nxmhandler.exe` reports file version 1.2.0.0) | Source in `source/nxmhandler/`; the handler window is rebuilt in the Fluent style, the command line, registration and `nxmhandler.ini` format are kept. [Upstream repository](https://github.com/ModOrganizer2/modorganizer-nxmhandler) |
| Mod Organizer 2 libraries and plugins | The 2.5.2 commits of uibase, cmake_common, archive, githubpp, bsatk, esptk, game_gamebryo, the 14 game plugins, the installer, preview, diagnose and tool plugins, plugin_python, lootcli and helper, listed in `source/upstream/UPSTREAM-COMMITS.txt` | Sources in `source/upstream/`, built by `source/revamped/installer/Build-UpstreamPlugins.ps1`. Fixes from later MO2 commits are brought in by hand and marked "from MO2 <sha>" (see `Check-Upstream.ps1`). Revamped changes, marked "MO2 Revamped" in the code: BSA Extractor, BSA Packer, the BSA preview and the INI Editor declare which games they apply to, the FNIS check also covers Skyrim SE and VR, and diagnose_basic skips the missing-master search in games without plugin files. The build checks uibase's exports and each binary's dependencies and plugin interfaces against the 2.5.2 binaries (`source/upstream/abi/`). The release no longer takes these binaries from the 2.5.2 runtime. [Upstream projects](https://github.com/ModOrganizer2) |
| Mod Organizer 2 translations | The `.qm` files of the 2.5.2 release (669 files, Transifex) | Kept as `.ts` in `source/upstream/translations/`, written with `lconvert`; `lrelease` reproduces each `.qm` byte for byte. |
| BAIN wizard interpreter | bain-wizard-interpreter 1.0.4, the package 2.5.2 bundles with installer_wizard | Unchanged in `source/mo2-python-plugins/installer_wizard/lib/`, checked against its wheel `RECORD`. [Upstream repository](https://github.com/Holt59/bain-wizard-interpreter) |
| YAFSML Elden Ring resolver | Upstream patch by Soar Qin, 2024 | Adaptation and MIT notice are documented in [`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md). |
| MinHook | Vendored source under `source/revamped/plugins/basic_games/games/vendor/minhook/` | BSD 2-Clause notice is preserved beside the source and in `licenses/`. |

## Release runtime versions

| Component | Version used | Source or decision |
| --- | --- | --- |
| Mod Organizer 2 | 2.5.2 | [Upstream releases](https://github.com/ModOrganizer2/modorganizer/releases) |
| USVFS | 0.5.7.2 | [Upstream releases](https://github.com/ModOrganizer2/usvfs/releases) |
| Qt / PyQt | 6.7.1 | The Qt DLLs are the official 6.7.1 binaries, byte for byte. PyQt6 6.7.1 and PyQt6-sip 13.8.0 are built from their PyPI source distributions in `source/upstream/third-party/`, as `mob` built them for 2.5.2; their `.pyi` stubs come out identical to 2.5.2's. Standard support for Qt 6.7 has ended. [Qt 6.12.0](https://www.qt.io/blog/qt-6.12-released) became the current LTS on 2026-09-30. Riverbank's [PyQt 6.11.0](https://riverbankcomputing.com/news/PyQt_v6.11.0_Released) officially added Qt 6.11 support; [PyQt-builder 1.20.0](https://riverbankcomputing.com/news/PyQt-builder_v1.20.0_Released) added support for building against Qt 6.12 on 2026-10-04. This makes a migration buildable, but not yet verified for MO2: Qt, WebEngine, PyQt, and all native extensions need a coordinated rebuild and UI regression before adoption. |
| Python | 3.12.15 | Rebuilt from the official [source release](https://www.python.org/downloads/release/python-31215/). The embedded standard-library bytecode and CPython extensions are updated; `_ssl` and `_hashlib` are built against OpenSSL 3.5.9. MO2 and PyQt extensions remain on their matching CPython 3.12 ABI. |
| OpenSSL | 3.5.9 | Rebuilt from the official [3.5.9 LTS source release](https://openssl-library.org/source/); see the runtime audit and third-party notices. |
| 7-Zip runtime | 26.06 | Updated from 2.5.2's 24.05, which has known vulnerabilities fixed in later releases (among them CVE-2024-11477 in Zstandard decoding, fixed in 24.07). Built from the official 26.06 source in `source/upstream/third-party/` (GitHub `ip7z/7zip` release of 2026-11-07, identical to SourceForge's) with `mob`'s `nmake` options plus `PLATFORM=x64`; `archive` compiles against the same headers. It keeps every export and dependency of 2.5.2's `7z.dll` and adds `SetLargePageMode2`. Checked by installing ZIP, 7z and RAR archives through the Quick, FOMOD, BAIN and BAIN Wizard installers. |
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
