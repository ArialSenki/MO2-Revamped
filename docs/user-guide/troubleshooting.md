# Troubleshooting and bug reports

## Where to look first

- **The notification bell** on the command bar lists the problems that plugins
  found, such as a missing script extender or a mod that needs another. A dot
  on the bell means there is something to read; **Fix it** solves a problem
  when it can.
- **The log panel:** **…** > **Log panel** shows MO2's log at the bottom of the
  window.
- **The game's page** in [Games](games.md) lists its usual problems.

## Common problems

**The game starts without mods.** Start it from MO2 (**Run**), not from Steam,
GOG or its launcher. For a program you added yourself, check it in
**Executables**.

**A mod doesn't seem to do anything.** Another mod lower in the list may win
the conflict: the **With conflicts** tile and the mod's conflict colors show
it. For games with plugins, check that its plugin is ticked in the **Plugins**
tab.

**Files the game or a tool creates are missing.** They go to **Overwrite**;
the **Overwrite** tile opens it. Move them into a mod to keep them apart.

**Downloads from Nexus don't reach MO2.** Connect your account in Settings >
Nexus and turn on **Register MO2 for Nexus download links**. Only one MO2 can
receive the links.

**Nexus says the request limit is reached.** MO2 waits for the limit to reset;
collections and lists continue where they stopped.

**MO2 doesn't open.** Only one MO2 runs at a time, also counting the original
Mod Organizer 2: starting a second one brings the open window to the front.
Close the other one first.

**A plugin crashes MO2 at start.** The next start offers to skip or blacklist
it. Settings > Plugins > **Plugin blacklist** lists the blacklisted ones.

**The window looks wrong after a monitor change.** Settings > Workarounds >
**Reset window geometry**.

## After a crash

When MO2 closes unexpectedly, it saves a crash dump. On the next start a
notice says so, with these buttons:

- **Create bug report** makes the report described below;
- **Open the folder** opens the crash dumps folder;
- **Close**.

The dumps are in the instance's `crashDumps` folder as
`ModOrganizer-<version>-<date>.dmp`. Settings > Diagnostics sets how detailed
they are and how many are kept.

## Create a bug report

**…** > **Create bug report** shows a report with the logs and versions,
without your private data. It comes from the **MO2 Revamped Information**
plugin; if that plugin is off, turn it on in Settings > Plugins and restart MO2.

The preview, **This is exactly what the report will share**, lists every file
and shows its content. **Save ZIP…** saves it, and **Copy summary** copies the
main text.

**What it contains**

- `report.txt`: the MO2 Revamped version and channel, Qt, Python and the
  system, the game and its version, the instance, the profile with its mods,
  the plugins, and the crash dumps found (their names only).
- The end of MO2's log, of the newest virtual file system logs and of the
  memory diagnostics.
- For Elden Ring and Nightreign, the bridge logs and the tools' state.
- `included-files.txt`, the list of files.

**What it never contains:** mods, downloads, saves or the crash dumps
themselves.

**What it removes from the text**

| Private data | Replaced with |
| --- | --- |
| Your Windows user name, also in short and URL forms | `%USERNAME%` or `\Users\<user>` |
| Your PC's name and domain | `<pc>` |
| E-mail addresses | `<email>` |
| API keys, tokens, passwords, cookies and Nexus keys | `<removed>` |
| Steam account IDs | `<steamid>` |

## Report a problem

Open an [issue on GitHub](https://github.com/ArialSenki/MO2-Revamped/issues)
(**…** > **MO2 Revamped guide** > **Getting help** > **Open the issue
tracker** opens it too) and include:

- the MO2 Revamped version and the game;
- what you did, what you expected and what happened;
- the bug report ZIP;
- after a crash, the `.dmp` file from `crashDumps`, if you are willing to share
  it. A dump holds part of MO2's memory, so it isn't added to the report on its
  own.

Please don't attach game saves, account details or unrelated personal files.

## Logs and files

| What | Where |
| --- | --- |
| MO2's log | `<instance>\logs\mo_interface.log` (replaced at each start) |
| Virtual file system logs | `<instance>\logs\usvfs-*.log` (the last five are kept) |
| Crash dumps | `<instance>\crashDumps` |
| Startup and memory diagnostics | `<MO2 folder>\logs`, or `%TEMP%` |
| Instance settings | `<instance>\ModOrganizer.ini` |
| MO2's own settings | `%LOCALAPPDATA%\MO2 Revamped\MO2-Revamped-Global.ini` |
| Setup logs | `%ProgramFiles%\MO2 Revamped Setup\Installer Logs`, or `%TEMP%\MO2 Revamped\Installer Logs` |

Detailed startup diagnostics are off on Windows by default; set the environment
variable `MO2_REVAMPED_DIAGNOSTICS=1` before starting MO2 to turn them on.
[DIAGNOSTICS.md](../DIAGNOSTICS.md) has the details.
