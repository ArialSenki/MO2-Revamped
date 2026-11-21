# Instances

An instance is one game's setup: its mods, downloads, profiles, Overwrite and
settings. **Manage instances…** on the command bar lists them, switches
between them and creates new ones.

When you create an instance, you choose where it lives:

| Kind | Where | When to use it |
| --- | --- | --- |
| **Global (recommended)** | `%LOCALAPPDATA%\MO2 Revamped\Instances\<name>` | Most setups. Kept in your user folder and managed by this MO2. |
| **Portable** | `<MO2 folder>\PortableInstances\<name>` | Everything next to the program, for example on another drive. |
| **Isolated copy** | `<MO2 folder>\IsolatedInstances\<name>` | One game kept completely apart. Elden Ring and Nightreign for now. |

Then choose the game, its edition (such as Steam or GOG) and a name.

Each instance keeps its own settings in its `ModOrganizer.ini`. Settings that
belong to MO2 itself, such as the theme and the language, are in
`MO2-Revamped-Global.ini` in `%LOCALAPPDATA%\MO2 Revamped`, or next to the
program when MO2 runs portable.

## Global instances

Global instances are in `%LOCALAPPDATA%\MO2 Revamped\Instances`. MO2 Revamped
never reads the original MO2's global instance folder
(`%LOCALAPPDATA%\ModOrganizer`), so the two never change each other's
instances. To bring a setup from the original MO2, use **Copy data** in the
setup (see [Installing and updating](installing-and-updating.md#copying-data)).

When MO2 Revamped itself is installed as a portable copy, its global instances
go to `<MO2 folder>\GlobalInstances` instead.

## Portable instances

A portable instance keeps everything in its folder, next to the program. You
can move the whole MO2 folder with it.

## Isolated instances

An isolated instance is for one game and keeps its profiles, mods, downloads
and settings inside its own folder. The isolated Elden Ring and Nightreign
installation from the setup shows only its own instance and no global ones.

## Linked Nolvus instances

[Nolvus](https://www.nolvus.net) installs a large Skyrim SE list with its own
MO2. MO2 Revamped can open a Nolvus installation without changing it.

MO2 Revamped looks for Nolvus in `C:\Nolvus` and in folders you add. When it
finds one, it offers **Open <name> in MO2 Revamped?**; **Make the instance**
creates a linked instance.

A **linked** instance:

- is a portable instance that uses Nolvus' mods, Overwrite and stock game
  folder directly, with copies of Nolvus' profiles;
- keeps its own settings, downloads and web cache;
- never writes into the Nolvus folder, except when you change a mod itself;
- shows the Nolvus archives in the Downloads tab as read-only;
- doesn't take `nxm://` links and doesn't update the list: keep using the
  Nolvus Dashboard for that.

After a Nolvus update, MO2 Revamped can copy the profiles again; the old copy is
kept in `profiles-backup-<time>`.

To make it a normal instance instead, **transfer** it:

- **Move** (same drive only): the list's folders move into the instance in
  seconds, but the Nolvus Dashboard can no longer update that installation.
- **Copy**: the folders are copied; Nolvus keeps working, and its updates only
  reach its own copy.
- **In place**: the instance uses and owns the folders where they are;
  nothing is moved.

Every step of a transfer is written to `nolvus-transfer.jsonl` in the instance
folder first, so a failed or interrupted transfer can be put back, and a
finished move can be undone later.

## Profiles

Each instance has profiles, and each profile keeps its own mod list and plugin
order. **Profiles** (Ctrl+P) creates, duplicates, renames and deletes them;
**Use this profile** switches to one, and so does the top bar.

- **Profile-specific saves** and **Profile-specific game INI files** keep saves
  and game settings apart for each profile. **Transfer saves...** copies saves
  between profiles.
- Settings > General > **Profile defaults** chooses the options a new profile
  starts with.
- A profile can't be renamed or deleted while it is the current one or while
  the game runs.
