# Settings

**Settings** (Ctrl+S) has these pages. The **Help** button opens the in-app
guide at the page you are looking at.

- [General](#general)
- [Appearance](#appearance)
- [Mod list](#mod-list)
- [Paths](#paths)
- [Nexus](#nexus)
- [Plugins](#plugins)
- [Workarounds](#workarounds)
- [Diagnostics](#diagnostics)
- [Developer](#developer-mode) (only in developer mode)

Options that don't apply to the current game are hidden.

## General

- **Language.**
- **Notices: sound only, without the card** plays Windows' notification sound
  instead of showing the small cards at the bottom of the window. The notices
  are still in MO2's log, and questions and errors still show.
- **Download list:** **Show meta information**, **Compact list** and **Hide
  downloads after installation**.
- **Updates:** **Tell me when a new MO2 Revamped version is out** and
  **Versions to hear about**. See
  [the update notice](installing-and-updating.md#the-update-notice).
- **Profile defaults:** **Local INIs**, **Local saves** and **Automatic archive
  invalidation** for new profiles.
- **Windows and dialogs:** **Always center dialogs**, **Ask before switching
  instances**, **Open previews on double-click** and **Reset dialog choices**.

## Appearance

### Theme

**Interface theme** offers:

- **Light** and **Dark**;
- **OLED Black**: Dark with a pure black background, for OLED screens;
- your own themes, shown with their base, such as "Ember (on Dark)".

**New theme...** asks for a name, a base (Light or Dark) and six colors, and
the window shows them as you pick. **Save** keeps them and switches to the new
theme; **Cancel** goes back. **Edit theme...** changes one of yours.

Themes are `.qss` files in the `stylesheets` folder (**Open themes folder**),
and MO2 reloads one when its file is saved. Don't edit `Light.qss` or
`dark.qss`: updates replace them. Older MO2 themes load on top of Dark or Light;
[THEMES.md](../THEMES.md) explains how to write one.

### Display

- **Interface size:** 80%, 90%, 100% (normal), 110% or 125%. It scales every
  window.
- **Smooth text in grey (OLED screens)** draws letters without colored edges,
  which look fringed on OLED screens.
- **Use the new interface (Qt Quick) where available** draws the Instances
  window, the dashboard's summary tiles and the theme question for a new
  instance with Qt Quick. They work the same and use a little more memory. If
  one of them fails to load, MO2 uses the classic version.

These three apply when MO2 starts again.

### Animations

| Level | What it does |
| --- | --- |
| **Off** | Every change shows at once. |
| **Saver (30 Hz)** | Tabs, switches, menus, the log panel and the Run button animate at 30 frames a second. |
| **Standard (60 Hz)** | The same at 60 frames a second. The default. |
| **Monitor (N Hz)** | Follows your screen's refresh rate, up to 240 Hz, using a little more CPU while something moves. |

The animation level applies when you press **OK**.

### Toolbar and panels

**Toolbar icon size** (Small, Medium, Large), **Toolbar button style** (Icons
only, Text only, Icons and text), **Show status bar** and **Show log panel**.

### Highlight colors

**Mod and plugin highlight colors** sets the conflict colors; **Restore default
colors** brings them back.

## Mod list

How the mod list shows mods, separators and conflicts, and how installing and
dragging behave: separator colors (in the scrollbar, and the color of new
separators), **Display mods installed outside MO2**, **Remember filters after
restart**, **Check for updates after installing**, **Auto-collapse items while
dragging**, collapsible separators and their status icons, **Keep separator
collapse state per profile** and **Manage categories**.

## Paths

Where this instance keeps its downloads, mods, caches, profiles and Overwrite,
and the managed game's folder. Use `%BASE_DIR%` to refer to the base directory.
All directories must be writable.

## Nexus

- **Connect to Nexus** links your account in the browser; **Enter API key
  manually** works too; **Disconnect from Nexus** removes it.
- The linked account and its **API request usage** (daily and hourly).
- **Register MO2 for Nexus download links** makes the **Mod manager download**
  buttons on Nexus send files to this MO2.
- Endorsement and tracked-mod actions, Nexus category mappings, the API request
  counter and **Clear Nexus cache**.
- **Download server priority:** the servers you prefer, top first.

## Plugins

Settings > **Plugins** lists MO2's plugins by kind: games, installers, tools,
previews and checks. Choose one to see its author, version and description.

- **Enable plugin:** clear it to turn a plugin off. MO2 asks first when others
  need it; some plugins can't be turned off.
- **Plugin-specific options** lists a plugin's settings; double-click a value
  to change it. The game pages in [Games](games.md) list the useful ones.
- **Plugin blacklist:** blacklisted plugin files are skipped when MO2 starts.
  Select an entry and press Delete to take it off the list.
- If a plugin crashed while loading, the next start offers **Skip this
  plugin**, **Blacklist this plugin** or **Load this plugin**.

**Other games' plugins.** A plugin made for one game, such as the FNIS and OMOD
tools or the Elden Ring tools, isn't loaded in other games' instances, so menus
stay short and MO2 starts faster. This is automatic; there is nothing to set.
Developer mode loads them all.

When a game's plugin names a modding wiki, **…** > **Game support wiki** opens
it.

## Workarounds

Settings for problem cases:

- **Keep core game files enabled**, **Check archive contents for conflicts
  (experimental)** and **Keep MO2 locked while a program runs**.
- Steam launch settings (user name and App ID override) for games that need
  Steam to start.
- **Use offline mode**, **Use system HTTP proxy** and **Use custom browser**.
- **Reset window geometry** and, for Skyrim, **Back-date BSA archives**.
- **Virtual file system exclusions:** programs, file suffixes and folders the
  virtual file system leaves alone.

## Diagnostics

- **MO2 log detail:** Debug, Info (recommended), Warning or Error.
- **Crash dump detail:** None, Mini (recommended), Data or Full, and **Maximum
  dumps to keep**.
- **LOOT log detail**, for games LOOT sorts.
- Links to the logs and crash dumps folders.

See [Troubleshooting](troubleshooting.md) for what to send when something goes
wrong.

## Developer mode

Developer mode shows features that are still being tested. None of them is
needed to play.

- **Turn it on:** in **…** > **About MO2 Revamped** > **Components**, tap the
  **MO2 base** row seven times, as with a phone's build number.
- A **Developer** page appears at the end of Settings with the features in
  testing.
- **Turn it off:** clear **Developer mode** on that page. The page is gone the
  next time Settings opens.

Developer mode also loads every plugin, including those made for other games.
