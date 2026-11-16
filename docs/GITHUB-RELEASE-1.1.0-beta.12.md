MO2 Revamped 1.1.0 beta 12 is the **newest version**, still a beta. It completes
**Nexus collections**: binary patches, exact copies of the author's install, file
overrides, INI tweaks, updating a collection to a new revision and browsing
collections from MO2, plus safety fixes from an audit of the feature.
It also brings **Root folders without Root Builder**, fixes for **Skyrim Special Edition, Fallout 4, New Vegas and Fallout 3**, and a round of fixes for **Baldur's Gate 3** mods and lists.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.12.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Nexus collections

- Open a collection with **Download with mod manager** on its Nexus page, or
  paste its address in **Nexus > Download a Nexus collection**.
- Install it into a **new instance** (the default, apart from your other mods),
  a new profile or the current profile.
- **Premium:** everything downloads and installs after Start. **Free accounts:**
  MO2 opens each file's page and you press "Slow download", as Nexus requires.
- FOMOD choices, mod order, a separator, categories (with a separator per
  category when the order allows it) and, for Bethesda games, the plugin order
  come from the collection. Progress is saved to continue later.

Tested with Night City FPV Collection (10 mods) and Welcome to Night City
(364 mods) on Cyberpunk 2077 2.5: CET, RED4ext and its plugins loaded, and the
game folder stayed unchanged.

C# script FOMODs don't take the collection's choices: their own window opens.

## Root folders without Root Builder

A mod's `Root` folder (Root Builder's layout, used by many Wabbajack lists) now
reaches the game folder with no plugin. Script extender loaders, ENB, ReShade and
DXVK DLLs are loaded from the mod where that's reliable. Otherwise they're
copied into the game folder for the run and put back when the game closes, or on
MO2's next start. With the Root Builder plugin turned on, it stays in charge.

This is new and was checked on test instances; it hasn't been played with every
game yet.

## Also in this beta

- Each mod goes under a separator for its category, in the collection's order.
- Failed downloads are retried on their own, with a Retry failed button, and no
  sound plays for each file.
- Much faster with collections of thousands of mods.
- **Linux / Steam Deck:** MO2 no longer crashes at start under Wine or Proton when
  Qt has no screen yet. If it still fails, please send the `ModOrganizer-*.dmp`
  crash dump.
- **Skyrim SE, Fallout 4, New Vegas, Fallout 3:** Fallout 3's launcher is found
  again and gets the right Steam ID; Fallout 3 and New Vegas are found from GOG
  and any Steam library; DLC and Creation Club plugins show again; script
  extenders and ENB from collections go into the mod's Root folder.
- **Saves** show your local time (Skyrim SE no longer shifts them 6 hours), and
  FOSE co-saves move with their save.
- **Plugin list:** Ctrl+Up/Down saves the order; plugins past the light or
  medium limit are marked.
- **Baldur's Gate 3:** loose folders of Wabbajack-style mods (Public, Generated,
  Video) load; profile saves work on a first start; Script Extender stays out of
  the Larian Launcher; paks with spaces or `&` in their names no longer cause
  trouble; a quicker start.
- **Interface size** (80 to 125%) and **grey text for OLED screens** in Settings.
- Faster conflicts, plugin list and Nexus checks with thousands of mods.

Known limit: installing a collection of hundreds of mods is still slow, because
MO2 refreshes its mod list after each one.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.12/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, FromSoftware or Bandai
Namco Entertainment.
