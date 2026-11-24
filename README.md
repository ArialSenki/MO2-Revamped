# MO2 Revamped

**MO2 Revamped** is an independent community edition of
[Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer), built on
MO2 v2.5.2. It keeps the MO2 workflow you already know for every game MO2
supports, and adds a refreshed interface, a guided installer and dedicated
tools for **Elden Ring** and **Elden Ring Nightreign**. It is maintained by
**ArialSenki**.

> MO2 Revamped is not an official Mod Organizer 2 release and is not affiliated
> with Nexus Mods, FromSoftware, or Bandai Namco Entertainment.

## Download

**Latest: [v1.1.0-beta.18](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.18)**
— `MO2-Revamped-Setup-1.1.0-beta.18.exe` with its `SHA256SUMS.txt`. It is a beta,
and the newest version.

New in beta 18: **Merge** into an installed mod no longer leaves it half
changed, and a merge interrupted by closing or a crash is offered at the next
start (Finish, Undo or Later); **ELDEN RING Reforged's UI images** are made by
its tool Gideon after the install (Tools > Reforged UI images... makes them
again); **zEdit merges** of older Wabbajack lists are built; changing the
**theme** is seamless; and small fixes. Beta 17 was beta 16 with two fixes
(switching themes, and the "different game" indicator).

New in beta 16:

- **Our own game plugins**, written from scratch, for the 14 Bethesda games,
  Cyberpunk 2077, Baldur's Gate 3, The Witcher 3, Stardew Valley and Valheim.
- **Faster start and large lists:** MO2 loads only the instance's game, and mod
  toggles and problem checks are much faster with lists like Nolvus.
- **Help pages and first steps**, Settings reorganised with animation levels,
  and rewritten installer dialogs with previews; every install window shows
  the archive's name in its title.
- **Your own themes:** any .qss in the stylesheets folder appears in Settings,
  and themes made for the original MO2 colour Revamped's own parts too.
- Separate saves for The Convergence, Wine and Proton fixes, icons that no
  longer need the Windows icon fonts, and security and stability fixes.

New in beta 15: install several mods at once, drop archives from Explorer, a
Nexus Collections gallery and an OLED Black theme.

New in beta 14: **Wabbajack modlists install natively** (Tools > Wabbajack lists),
with Wabbajack's gallery, the list's own profile, load order and programs, and
its manual steps. Lists tested so far installed with no failures but haven't been
played yet.

Beta 13 came from a full audit of every window and option, plus two rounds of
hands-on fixes:

- **Nolvus:** opens Nolvus lists in MO2 Revamped, either as a linked instance
  or transferred to a normal one.
- **Very large lists:** much faster. With Nolvus (3,597 mods) it starts twice as
  fast as the original MO2 2.5.2, and turns a mod on or off five times faster.
- **Nexus:** waits out Nexus' request limit instead of failing downloads.
- **Notices:** quieter, and they never take the focus.
- **Downloads:** grouped by origin or into your own categories.
- **Baldur's Gate 3:** the tools are fixed.

Nexus collections, Root folders without Root Builder and the Elden Ring and
Nightreign tools from earlier betas are all included. Tested with this
version: Skyrim SE, Nolvus, Cyberpunk 2077 and Baldur's Gate 3.

On Nexus Mods: [MO2 Revamped in Modding Tools](https://www.nexusmods.com/site/mods/2612),
with the beta.

The last stable release, [v1.0.2](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.2),
remains available, as do [v1.0.1](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.1)
and [v1.0.0](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.0.0).

### Installing

1. Run the setup (`MO2-Revamped-Setup-1.1.0-beta.18.exe`) and pick your language.
2. Choose **Install MO2 Revamped** and the installation folder.
3. When setup finishes, MO2 starts in the language you chose. Create an
   instance for your game; for Elden Ring or Nightreign the New instance
   wizard also offers an **isolated** instance that keeps its profiles, mods,
   downloads and settings in its own folder.

Run the same setup again to **update**, **restore**, **copy data** or
**uninstall**. Updates keep your data and back up the previous files first;
isolated Elden Ring installations from 1.0.1 are updated the same way.
Uninstalling removes the program folder, its shortcuts and its Nexus link
registration; you choose whether your instances and data go too. The setup also
has silent switches for scripted installs (`/S`, `/dir=`, `/uninstall`, ...).

## What's new in the 1.1.0 betas

- **Nexus collections:** download and install a collection (Premium downloads
  everything by itself), with its FOMOD choices, order, category separators,
  binary patches, exact copies of the author's install, file overrides and INI
  tweaks; update it to a new revision or browse collections from MO2.
- **Root folders without Root Builder:** script extender loaders, ENB, ReShade
  and DXVK DLLs from a mod's `Root` folder load from the mod where that's
  reliable; otherwise they're copied for the run and put back when the game
  closes, or on MO2's next start.
- **Skyrim SE, Fallout 4, New Vegas, Fallout 3:** saves in local time, FOSE
  co-saves, GOG and any-Steam-library detection, DLC and Creation Club plugins
  shown again; faster plugin list and conflicts with thousands of mods.
- **Baldur's Gate 3:** mods laid out for other MO2 plugins (Wabbajack lists),
  loose folders, profile saves on a first start, a check for the .NET runtime
  BG3 Mod Manager needs, and a quicker start.
- **Interface size** (80 to 125%) and **grey text for OLED screens**.
- **Linux / Steam Deck:** no crash at start under Wine or Proton.
- **More games tested with mods:** Skyrim Special Edition, Fallout 4, Fallout 3
  and New Vegas (script extenders, borderless window), Cyberpunk 2077 (CET,
  RED4ext, ArchiveXL, TweakXL, without copying anything into the game folder)
  and Baldur's Gate 3 (Script Extender, .pak load order).
- **Script extenders** install into the game folder from their archive, and the
  game starts through them.
- **Borderless window** in the Run menu for the Bethesda games.
- **Every window from Mod Organizer 2 rewritten** in MO2 Revamped's design, with
  plain-language texts in six languages.
- **Elden Ring and Nightreign:** a safer exit and start-up, the Steam save back
  sooner when a mod profile hides it, and safer restores.
- **Many fixes** from audits of downloads, the mod list, profiles, instances,
  game detection and the setup.

The full list is in the [changelog](CHANGELOG.md).

## What was new in 1.0.2

- **Elden Ring Nightreign.** Save isolation, named save profiles, save
  backups, the launch save guard, native DLL loading, the Native DLL Profile,
  startup options and the archive installer, as in Elden Ring.
- **Safer saves.** While the game runs from an isolated profile, the Steam
  save folders are set aside and come back unchanged when it closes, with
  every launcher (Seamless Co-op, ELDEN RING Reforged). Saves are backed up
  before each launch and can be restored from Save Isolation.
- **Complete installation.** One MO2 Revamped for every game MO2 supports;
  isolated copies for one game are made from the New instance wizard.
- **Fluent dialogs.** Profiles, Executables, Notifications, Manage Overwrite,
  notices and the Nexus links window follow the instance manager's card
  layout and Windows 11 menus.
- **A more careful installer.** Names programs still using the files,
  reinstalls next to data an uninstall kept, follows the Windows accent colour
  and light or dark mode, and stays responsive while it checks.

The full list is in the [changelog](CHANGELOG.md).

## Features

- **Archive installer** (Elden Ring, Nightreign) — keep a mod's original
  folder layout or use MO2's standard layout.
- **Native DLL Profile** — per-profile order of native DLL mods, loaded through
  the Revamped native bridge, also with Seamless Co-op and ELDEN RING Reforged.
- **Startup Options** — launch minimized, black background, skip the intro
  logos and a faster start (Elden Ring and Nightreign).
- **Save Isolation** — separate save routes per profile or instance, named
  save profiles, automatic backups and restore, so playthroughs never
  overwrite each other.
- **Script extenders and borderless window** — for Skyrim, Fallout, Oblivion,
  Enderal and Starfield's extenders, and a frameless full-screen window for the
  older Bethesda games.
- **Cyberpunk 2077 and Baldur's Gate 3** — CET and RED4ext load through MO2's
  virtual file system; BG3's Script Extender loads and its .pak mods go into the
  load order.
- **Six languages** — English, Spanish, German, French, Italian and Brazilian
  Portuguese.
- **Themes** — Light and Dark.

### Where your data lives

MO2 Revamped keeps its global instances in `%LOCALAPPDATA%\MO2 Revamped` and
never reads the original MO2 global instance folder. Isolated instances keep
everything inside their own folder. The optional Steam save route still uses
the game's own Windows save directory, and data migration only reads a folder
you select yourself.

## User guide

The [user guide](docs/user-guide/README.md) covers installing and updating,
instances, each game's support, Nexus collections and Wabbajack lists,
Settings, Linux under Wine or Proton, and bug reports. It is also in
[Spanish](docs/user-guide/es/README.md).

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
candidate are recorded in the [release audit](docs/RELEASE-AUDIT.md). What a release needs for the update notice (version, tag,
notes) is in [Releasing](docs/RELEASING.md).

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
