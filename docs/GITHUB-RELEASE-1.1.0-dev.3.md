> **Experimental build.** This is a test version, published apart from the beta.
> It is beta 12 plus native Wabbajack lists; beta 12 is the recommended version.

## Experimental status: what works and in which games

This is an **experimental** build, not a beta. Everything outside Wabbajack
lists is the same as beta 12 and works as described in the
[beta 12 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.12).
Wabbajack lists are new: back up your saves and your downloads folder, install
a list into a new folder, and please report what fails.

**Wabbajack lists tested, and how far each got** (Windows 11, from download to
the new instance opening in MO2 Revamped):

| List | Game | Size | Result |
|---|---|---|---|
| Baldur's Gate 3 Enhanced Edition | Baldur's Gate 3 | 282 archives, 1.7 GB | Installed with no failures (732 files); opens with the author's profile, load order, separators, colours and programs. Not played. |
| Viva New Vegas | Fallout: New Vegas | 146 archives, 3 GB | Installed with no failures; opens with the author's profile. Its guide's manual steps are still needed, as with Wabbajack. Not played. |
| The Midnight Ride | Fallout 4 | 86 archives, 5.8 GB | Installed with no failures (5,444 files); a 2.26 GB download paused and continued gave the same file. Not played. |
| Waters of Life | Fallout 3 | 148 archives, 4.5 GB | Installed with no failures (10,345 files), including a Google Drive file taken from the browser's Downloads folder. Not played. |
| Mini Wildlander | Skyrim Special Edition | small | Installed with no failures (216 files), with its rebuilt BSA; the Stock Game note shows when its copy of the game isn't filled yet. Not played. |

**Not tested yet:**
- playing any list in game;
- very large lists (Skyrim lists with hundreds of thousands of files);
- lists for games other than the five above;
- a few error paths that were only built: a file another program keeps open,
  a redirect to a local address, a broken hash in a list, Windows short names.

**Not done yet:** MergedPatch (zEdit merges), and the MO2 plugins a list brings
(they are set aside and named in the summary).

## Download

- `MO2-Revamped-Setup-1.1.0-dev.3.exe`: run it and choose **Update MO2
  Revamped** over 1.1.0-dev.2 (your data stays), or **Install MO2 Revamped** in
  a folder of its own to try it apart from your usual MO2 Revamped.
- `SHA256SUMS.txt`: the installer's checksum.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## What changed since dev.2

**Experimental.** This build is 1.1.0 beta 12 plus **native installation of
Wabbajack modlists**, which is still being tested. Beta 12 is the recommended
version; use this build only to try Wabbajack lists. Since dev.2 it has the
beta 12 changes and these fixes and features for Wabbajack lists.

### Wabbajack lists

- **Browse Wabbajack's gallery:** Tools > Wabbajack lists > Browse Wabbajack
  lists… shows every list from Wabbajack's own gallery, as Browse collections
  does for Nexus. It has search, the managed game first, and unofficial and
  adult lists behind their own boxes, as in Wabbajack. Sizes and dates can be
  sorted, and each list has its readme and links. **Open the list** downloads
  the `.wabbajack` file from Wabbajack's servers, checks it and opens it; a
  file already downloaded is checked and used again. Lists down for
  maintenance or with failing archives are marked.
- **The author's mod list as the author sees it:** the list's categories, the
  Notes column (where authors write "Optional" and similar) and the mods'
  colours now show in the new instance.
- **Baldur's Gate 3 lists start with their mods in the load order.** Baldur's
  Gate 3 Enhanced Edition started with only the base game: the list author's
  cache entries and some `meta.ini` files weren't read. A `&` in a mod's name
  no longer breaks `modsettings.lsx`.
- **The list's programs work on this PC:**
  - programs that point at a folder (Larian Launcher), or at a path on the
    author's machine (the author's own xEdit), are left out, and the summary
    names them;
  - the list's "Explore Virtual Folder" used an Explorer++ that is never
    installed, so MO2 Revamped's own is used;
  - a working folder on the author's machine is dropped, and `<game>\Data` is
    changed to the game's real data folder only when that folder doesn't
    exist (Baldur's Gate 3 keeps it);
  - a program the guide has you copy later, inside the install or the game
    folder, is kept.
- **Stock Game:** the list's own copy of the game becomes the instance's game
  folder only when it really holds the game. Otherwise the real game folder is
  used, and the summary says what to do once the guide has it filled.
- **The right profile:** when the list names none of its profiles, the instance
  starts with the list's first profile instead of an empty "Default".
- **Root folders:** mods with a `Root` folder no longer show as having no valid
  game data. MO2 Revamped puts their programs and DLLs in the game folder when
  the game starts (see beta 12), and the summary says so.
- **Windows that fit the screen:** the list window and the gallery open at a
  size that fits the screen they are on, and refit when moved to another
  screen. The very large size earlier versions saved is shrunk once.
- **Paused lists:** the list to reopen is kept per instance, so a paused list
  no longer opens in another instance. A list paused in dev.2 has to be opened
  again by hand once.
- **The install folder follows the instance name** until you pick a folder by
  hand. Install paths follow the same rules as collections: no names Windows
  refuses.
- **Downloads:**
  - a file another program keeps open stops as failed, with that reason,
    instead of being checked again forever;
  - a server that ignores where a paused download stopped starts it over, unless
    what it sends clearly isn't the file;
  - files from the browser are checked where they are, before being copied;
  - downloads that redirect to an address on this PC or its local network, or
    from https to http, are refused;
  - the space part-downloads already use counts in the free-space check.
- **Install checks:**
  - whole files are checked by their hash while they are copied;
  - a list that gives a file a broken hash fails that file instead of skipping
    the check;
  - parts of a list MO2 Revamped can't install yet are named in the summary;
  - the summary lists every failure (the log keeps the first 200).
- **Faster with big lists:** the window refreshes only the rows that changed,
  and the free-space and game checks aren't repeated on every change.
- **A list opened while the window has a question up** waits for the answer
  instead of closing the window under it.
- **Lists tested with this build** (installed in full with no failed files,
  then the new instance opened in MO2 Revamped; none was played in game):
  - Baldur's Gate 3 Enhanced Edition: 732 files, with its load order and
    programs;
  - Mini Wildlander (Skyrim SE): 216 files, with its rebuilt BSA and Stock Game;
  - The Midnight Ride (Fallout 4): a 2.26 GB download paused and continued,
    identical to the original;
  - Viva New Vegas: opened and closed cleanly.

Everything in beta 12 (Root folders without Root Builder, Skyrim SE, Fallout 4,
New Vegas and Fallout 3 fixes, saves, the plugin list, Baldur's Gate 3, Nexus
collections and performance with large lists) is in the
[changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-dev.3/CHANGELOG.md).

## What the experimental build is

Everything in beta 12, plus **native installation of Wabbajack modlists**. See
the [1.1.0-dev.1 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-dev.1)
for the whole feature.

## Reporting problems

Please report problems with Wabbajack lists or downloads in the
[issues](https://github.com/ArialSenki/MO2-Revamped/issues). Include the list's
name and, if possible, `.mo2r-install\summary.txt` from the install folder and
the instance's `logs\mo_interface.log`.

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, FromSoftware or
Bandai Namco Entertainment.
