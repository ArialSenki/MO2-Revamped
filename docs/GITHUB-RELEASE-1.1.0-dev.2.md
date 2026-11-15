> **Experimental build.** This is a test version, published apart from the beta.
> The recommended version is still
> [1.1.0 beta 11](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.11).

## Download

- `MO2-Revamped-Setup-1.1.0-dev.2.exe`: run it and choose **Update MO2
  Revamped** over 1.1.0-dev.1 (your data stays), or **Install MO2 Revamped** in
  a folder of its own to try it apart from your usual MO2 Revamped.
- `SHA256SUMS.txt`: the installer's checksum.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## What changed since dev.1

Fixes found by testing more Wabbajack lists after 1.1.0-dev.1. Everything in
dev.1 is still here, and beta 11 is still the recommended version.

- **Update notice:** 1.1.0-dev.1 showed "MO2 Revamped 1.1.0-beta.11 is
  available", because a "dev" version counts as older than a beta. Following it
  would go back to the beta and lose Wabbajack lists. An experimental build now
  offers a beta or a release only when it came out after the experimental build.
  Note that dev.1 itself still shows that notice; ignore it and update to dev.2.
- **Files from the browser with Internet Download Manager:** IDM and similar
  download managers save files into folders such as `Downloads\Compressed`.
  MO2 Revamped now watches the folders right inside your Downloads folder too,
  so those files are picked up.
- **The game's edition, without asking:** the edition is also read from the
  game folder's name. For example, "Fallout 3 goty" is Game Of The Year, so the
  new instance no longer asks for it on its first start.
- **Download errors in your language:** network errors such as "the server
  stopped answering" or "the file isn't on the server any more" are no longer
  shown in English. Qt's original text goes to the log.
- **Tested with two more lists:**
  - The Midnight Ride (Fallout 4): 86 archives, 5.8 GB, 5,444 files installed
    in 46 seconds, no failures.
  - Waters of Life (Fallout 3): 148 archives, 4.5 GB, including one from Google
    Drive taken from the Downloads folder; 10,345 files installed in 91 seconds,
    no failures.

  Both opened with the author's profile, mods and plugins.

## What the experimental build is

Everything in 1.1.0 beta 11 and the unreleased beta 12 (Nexus collections,
complete), plus **native installation of Wabbajack modlists**. See the
[1.1.0-dev.1 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-dev.1)
for the whole feature, and the
[changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-dev.2/CHANGELOG.md).

## Reporting problems

Please report problems with Wabbajack lists or downloads in the
[issues](https://github.com/ArialSenki/MO2-Revamped/issues). Include the list's
name and, if possible, `.mo2r-install\summary.txt` from the install folder and
the instance's `logs\mo_interface.log`.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, FromSoftware or
Bandai Namco Entertainment.
