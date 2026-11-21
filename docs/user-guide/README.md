# MO2 Revamped user guide

MO2 Revamped is a community edition of Mod Organizer 2, based on MO2 2.5.2.
It keeps the MO2 workflow and adds a new interface, a guided setup, Nexus
collections, Wabbajack lists and its own support for several games.

This guide covers what MO2 Revamped does differently. For everything it
keeps from Mod Organizer 2, the
[Mod Organizer 2 documentation](https://github.com/ModOrganizer2/modorganizer/wiki)
still applies.

[Versión en español](es/README.md)

## Pages

- [Installing and updating](installing-and-updating.md): the setup, updates,
  restoring, copying data, uninstalling and the update notice.
- [Instances](instances.md): global, portable and isolated instances, and
  linked Nolvus instances.
- [Games](games.md): what MO2 Revamped does for Cyberpunk 2077, Baldur's
  Gate 3, The Witcher 3, Stardew Valley, Valheim, the Bethesda games and Elden
  Ring and Nightreign, and their common problems.
- [Nexus collections and Wabbajack lists](collections-and-wabbajack.md).
- [Settings](settings.md): every page, including Appearance, the animation
  levels, Qt Quick, OLED Black and plugins.
- [Linux, Wine and Proton](linux-wine-proton.md).
- [Troubleshooting and bug reports](troubleshooting.md).

## First steps

1. **Create an instance.** An instance is one game's setup: its mods,
   downloads and profiles. **Manage instances…** on the command bar creates
   one: choose **Global (recommended)**, the game, its edition and a name.
2. **Connect your Nexus account.** Settings > Nexus > **Connect to Nexus**
   links your account in the browser; **Enter API key manually** works too.
   Then the **Mod manager download** buttons on Nexus send files to MO2.
3. **Install mods.** Double-click an archive in **Downloads** to install it,
   or use **Install** (Ctrl+M) for one on your PC. Tick a mod in the list to
   turn it on; a mod lower in the list wins a conflict.
4. **Or a whole collection or list.** Tools > **Nexus collections** installs
   a Nexus collection, and Tools > **Wabbajack lists** a Wabbajack list in an
   instance of its own.
5. **Start the game.** Choose the game in the top bar and press **Run**.
   Always start it from MO2: started from Steam or its launcher, it doesn't
   see the mods.
6. **If something goes wrong.** The bell on the command bar lists the
   problems MO2 found. **…** > **Create bug report** makes a report you can
   share without your private data.

The same steps are in the app: **…** > **MO2 Revamped guide**. A new instance
with no mods offers them once. When the current game has its own page in the
guide, **…** > **Help for <game>** opens it, and the **Help** button in
Settings opens the guide at the page you are looking at.

## The main window

- **Top bar:** the game, the profile and the program to start. **Run** starts
  it with the active profile; the last line says when the game last saved.
- **Summary tiles:** **Active mods**, **With conflicts**, **Overwrite** and
  **Updates**. Their buttons filter the mod list or open Overwrite.
- **Mod list:** the chips **All**, **Active** and **Conflicts** filter it;
  drag a mod to change its priority.
- **Tabs on the right:** **Plugins**, **Archives**, **Data**, **Saves** and
  **Downloads**. Games without plugins or archives don't show those tabs.
- **Command bar:** Instances, **Install**, **Nexus**, **Profiles**,
  **Refresh**, **Executables**, **Tools**, **Settings** and the notification
  bell. **…** holds the links, updates and help.
- **Keyboard:** Ctrl+M install a mod, Ctrl+P profiles, Ctrl+E executables,
  Ctrl+I tools, Ctrl+S settings, Ctrl+N Nexus, Ctrl+H help, F5 refresh,
  Ctrl+F search the list, Esc clear the search.
- **Help on the interface:** **…** > **Help on the interface**, then click any
  part of the window to read what it does.
