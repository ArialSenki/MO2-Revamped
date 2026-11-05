# Releasing

What a release needs so that the update notice in MO2 Revamped tells users
about it correctly. The build itself is described in [BUILDING.md](BUILDING.md),
and what was checked for each release goes in [RELEASE-AUDIT.md](RELEASE-AUDIT.md).

## Version

The version is written in two places, and they must agree:

- `source/modorganizer/src/revampedversion.h`: `MO2R_VERSION_STRING`, built into
  `ModOrganizer.exe`. The update check compares GitHub's releases with it.
  `ModOrganizer.exe`'s file version stays the one of the Mod Organizer 2 it is
  based on (2.5.2), so it can't be used for this.
- `source/revamped/installer/Build-Release.ps1`: `$version`, which names the
  setup.

Change both, then rebuild `ModOrganizer.exe`. `Build-Release.ps1` refuses to
package when the header or the executable names another version.

The same header holds the Nexus page (`MO2R_NEXUS_MOD_ID`, 11314 under Elden
Ring), which the notice links to and endorsements go to.

## Tag and channel

The GitHub tag is the version with a `v` in front, and its suffix says who hears
about the release:

| Tag | Channel | Who is told |
|---|---|---|
| `v1.2.0` | stable | everyone |
| `v1.2.0-beta.1`, `v1.2.0-rc.1` | beta | those on the beta or development channel |
| `v1.2.0-alpha.1`, `-dev.1`, `-test.1`, `-preview.1` | development | only those on the development channel |

Mark betas and development builds as pre-releases on GitHub. A pre-release
without one of these suffixes counts as a beta. Drafts are never shown.

Until a user picks a channel in Settings, General, Updates, it follows the
version they run: a stable build hears of stable releases, a beta build of
betas too, a development build of everything. Development builds sort before
the betas and the release they lead to, so `1.2.0-dev.3` is older than
`1.2.0-beta.1`, and `beta.10` is newer than `beta.9`.

## Release notes

The notice shows the GitHub release's text as a changelog, so keep its shape:

- A short summary first, as one paragraph.
- `##` sections. `## Download` is not shown (the notice says how to update), nor
  a closing paragraph that starts with "MO2 Revamped is an independent".
- Points as `- **Title**: text`; a point may continue on indented lines.
- `**bold**`, `` `code` `` and `[links](https://...)` are shown; images and HTML
  are not. Only web links open.

The other sections, such as `## Highlights` and `## Known issues`, are shown
with their titles, in English, as written.

## How often MO2 asks

MO2 asks GitHub at most once a day. The answer is kept in
`update-releases.json` beside `MO2-Revamped-Global.ini`, shared by all
instances. A new release reaches users within a day of being published, at
their next start of MO2. To see it at once on a test PC, delete that file. To
test without publishing anything, see `MO2_REVAMPED_UPDATE_TEST` in
[DIAGNOSTICS.md](DIAGNOSTICS.md).

## The first release with the notice

Versions up to 1.1.0-beta.3 can't tell their users about a new release: their
check compared GitHub's tags with 2.5.2. Say in the notes of the first release
that has the notice, and on Nexus, that users of earlier versions update by
hand once; after that, MO2 tells them.
