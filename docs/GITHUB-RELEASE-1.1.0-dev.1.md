> **Experimental build.** This is a test version, published apart from the beta.
> It may have problems the beta doesn't. The recommended version is still
> [1.1.0 beta 11](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.11).

## Download

- `MO2-Revamped-Setup-1.1.0-dev.1.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data. To try it without touching your usual MO2 Revamped, install it into
  another folder.
- `SHA256SUMS.txt`: the installer's checksum.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## What this build is

An **experimental** build, published apart from the beta. It has everything in
1.1.0-beta.12 (below; not released on its own), the collection fixes made since,
and a new feature still being tested: **installing Wabbajack modlists natively**.
Keep using beta 11 if you want the safer version. The update notice offers this
build only to people who choose the Development channel in Settings.

## Wabbajack modlists (new, experimental)

MO2 Revamped can read a `.wabbajack` file, download what it needs and install
it into an instance of its own, without Wabbajack or the original Mod Organizer
the list brings.

- **Open a list:** Tools > Wabbajack lists > Open a Wabbajack list…, or open a
  `.wabbajack` file with MO2 Revamped. Nexus collections and Wabbajack lists
  now have their own submenus in Tools.
- **Before downloading:** the window shows the list's image, author, version,
  game and links. It also shows the download and install sizes, where each
  archive comes from, the game files it takes from your game folder, and the
  free space on the drive.
- **Downloads:**
  - **Nexus:** Premium accounts download everything by themselves. Free
    accounts open each file's page and press "Slow download"; the list takes
    the link.
  - **Direct links and Wabbajack's own servers:** downloaded in parts, and each
    part is checked.
  - **Pause and continue:** a download picks up where it stopped. Files already
    in the downloads folder are recognised by their hash, and the game's own
    files are taken from the game folder.
- **Files only a browser can get** (Google Drive, Mega, MediaFire, ModDB,
  LoversLab and similar):
  - Double-click one to open its page.
  - MO2 Revamped watches your browser's Downloads folder and takes each file
    when it arrives, by its hash.
  - A file saved elsewhere goes in with a right click.
- **Install:** pick a name and a folder. The install:
  - extracts the archives, including BSA/BA2 and archives inside archives;
  - applies the list's patches and writes the list's own files;
  - rebuilds its BSA/BA2 archives and transforms textures (size, mipmaps,
    compression);
  - fixes the paths the list points at and sets your screen size in the game's
    INI files.

  Every file is checked by its hash.
- **The new instance keeps the author's setup:** the author's profiles, mod
  order, separators with their colours, INI files, programs and Stock Game
  folder. You are offered a restart into it.
- **What is left out:** the list's own Mod Organizer files aren't installed,
  because MO2 Revamped takes their place. MO2 plugins the list adds are set
  aside and named in the summary.
- **Summary:** `.mo2r-install\summary.txt` in the install folder lists what was
  done and names the folders the author leaves for you to copy by hand.
- **It stays out of your way while you play:**
  - MO2's notices go to the log while a list runs.
  - One sound plays when the downloads end.
  - Hashing runs at background priority, the install at a lower priority, and
    texture compression uses at most half of the processor.
- **Sounds the author kept uncompressed** in BSA/BA2 archives stay
  uncompressed. The games don't play them compressed.
- **Not done yet:**
  - MergedPatch (zEdit merges).
  - Installing the MO2 plugins a list brings.
  - Very large lists haven't been tried yet.
- **Tested with:**
  - Baldur's Gate 3 Enhanced Edition (282 archives, 1.7 GB) and Viva New Vegas
    (146 archives, 3 GB) installed with no failures, then opened with the
    author's profile, separators and colours.
  - A small Skyrim Special Edition sample that rebuilds a BSA.
  - Viva New Vegas still needs its guide's manual steps, as with Wabbajack.

## Downloads

- **Downloads that ended empty or short now continue.** Some downloads used to
  end with "Unknown error (0)", or ended early with the file taken as done.
  They now continue from where they stopped. Automatic retries use a connection
  of their own (HTTP/1.1), and the log records the details.
- **A download waiting for its retry** shows "Retrying…" in amber instead of a
  red "Error".

## Nexus collections (since beta 12)

- **Separators:** collection and category separators are made for every
  install path and reused when you continue or update. Empty old ones go to the
  Recycle Bin.
- **Quiet downloads:** no sound for each file, and one sound at the end.
- **Retries:**
  - Failed downloads are retried 3 times (after 5, 20 and 60 seconds), with a
    fresh link for Premium and the file's page again for free accounts.
  - Retry failed tries the rest.
- **Order:** category grouping keeps the author's order. Mods whose rules cross
  categories go to "Patches and overrides" at the end.
- **Large collections are much faster:** grouping 3,000 mods by category takes
  56 ms instead of 8.6 s. The window and the downloads list update without
  freezing.

## Also in this build: 1.1.0 beta 12 (never released on its own)

- **Nexus collections, complete:**
  - binary patches;
  - exact copies of the author's install;
  - file overrides and INI tweaks;
  - updating a collection to a new revision;
  - browsing collections from MO2.
- **Safety fixes** from an audit of the collections feature.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-dev.1/CHANGELOG.md).

## Reporting problems

Please report problems with Wabbajack lists or downloads in the
[issues](https://github.com/ArialSenki/MO2-Revamped/issues). Include the list's
name and, if possible, `.mo2r-install\summary.txt` from the install folder and
the instance's `logs\mo_interface.log`.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, FromSoftware or
Bandai Namco Entertainment.
