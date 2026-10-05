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

The Elden Ring MO2 support plugins, Revamped installer, native bridge, and
themes are maintained in `source/revamped/`. Their changes are part of this
community edition; the upstream projects retain their own authorship and
licenses.
