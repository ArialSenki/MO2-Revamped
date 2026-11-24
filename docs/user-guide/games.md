# Games

MO2 Revamped supports every game Mod Organizer 2 supports. For the games on
this page it has its own game plugin, written for MO2 Revamped, with checks
for the problems players run into most.

For every game: **start it from MO2.** Choose the program in the top bar and
press **Run**. Started from Steam, GOG or its own launcher, the game doesn't
see the mods.

A plugin made for one game, such as the Elden Ring tools, isn't loaded in other
games' instances (see [Settings > Plugins](settings.md#plugins)). Problems a
plugin finds show in the notification bell; **Fix it** solves them when it can.

- [Cyberpunk 2077](#cyberpunk-2077)
- [Baldur's Gate 3](#baldurs-gate-3)
- [The Witcher 3](#the-witcher-3)
- [Stardew Valley](#stardew-valley)
- [Valheim](#valheim)
- [Bethesda games](#bethesda-games)
- [Elden Ring and Nightreign](#elden-ring-and-nightreign)

## Cyberpunk 2077

**What MO2 Revamped does**

- Mods are laid out the way the game reads them: a mod needs one of
  `archive`, `bin`, `engine`, `r6`, `red4ext`, `mods` or `root` at its top.
  Other layouts are moved there when installing; unclear ones are left to the
  manual installer.
- Cyber Engine Tweaks and RED4ext load through MO2's virtual file system;
  nothing is copied into the game folder.
- **REDmod:** changed REDmods are deployed before the game starts.
  - **Cyberpunk 2077 - skip REDmod deploy** starts without it.
  - **Manually deploy REDmod** runs the deploy alone.
  - REDmods need the free REDmod DLC.
- The game's `r6/cache` files are copied to Overwrite, and refreshed after a
  game update.
- **Archive order:** the game loads `.archive` files by name. To use the mod
  order instead, turn on the Cyberpunk 2077 plugin's
  **enforce_archive_load_order** and **reverse_archive_load_order** in
  Settings > Plugins: then the mod lower in the list wins.
- Tools > **Archive conflicts** shows which `.archive` wins where two mods
  change the same files.

Programs: **Cyberpunk 2077**, **Cyberpunk 2077 - skip REDmod deploy**,
**Manually deploy REDmod** and **REDprelauncher**.

Plugin options (Settings > Plugins > Cyberpunk 2077 Support Plugin):

| Option | Default | What it does |
| --- | --- | --- |
| `skipStartScreen` | off | Skips the start screen. |
| `enforce_archive_load_order` | off | Loads `.archive` files in mod order. |
| `reverse_archive_load_order` | off | With the one above, the mod lower in the list wins. |
| `enforce_redmod_load_order` | on | Deploys REDmods in mod order. |
| `reverse_redmod_load_order` | off | Reverses that order. |
| `auto_deploy_redmod` | on | Deploys changed REDmods before the game starts. |
| `clear_cache_after_game_update` | on | Refreshes the cache copy after a game update. |

**Common problems**

- **Missing framework.** Most mods need RED4ext, Cyber Engine Tweaks,
  redscript, ArchiveXL, TweakXL or Codeware; Input Loader and Audioware are
  checked too. A notification names the missing one.
- **Scripts that don't compile.** When redscript can't compile the mods'
  scripts, the notification names the mods. The full log is
  `r6/logs/redscript_rCURRENT.log` in Overwrite. Update or turn off the mods it
  names.
- **REDmod deploy failed.** MO2 says where the deploy log is and asks whether
  to start the game without the REDmods' latest changes.
- **REDmod mods without the REDmod DLC**, and **REDmod files deployed outside
  MO2**, each get a notification.

Community wiki: <https://wiki.redmodding.org/cyberpunk-2077-modding>

## Baldur's Gate 3

**What MO2 Revamped does**

- `.pak` mods go to the game's `Mods` folder in your documents, Script
  Extender files to `Script Extender`, and each profile's own
  `modsettings.lsx` to `PlayerProfiles/Public`, all through the virtual file
  system.
- The layouts used by Wabbajack lists (`Root/`, `PAK_FILES/`, `SE_CONFIG/`)
  and loose folders such as `Public`, `Mods`, `Generated` or `Localization`
  are accepted.
- The `.pak` mods go into the load order in the **Plugins** tab;
  `modsettings.lsx` is written when the game starts.
- The Script Extender (`bin/DWrite.dll`, also in a `Root` folder) is detected,
  and mod DLLs load into the game only, not into the Larian Launcher.
- Tools > **BG3 Tools**:
  - **Load Order** lists the modules with their problems; **Move needed mods
    up** puts every mod after the ones it needs.
  - **Check For LsLib Updates**, **Reparse Pak Metadata** and **Convert JSONS
    to YAML**.

Programs: **Baldur's Gate 3: DX11**, **Baldur's Gate 3: Vulkan** and, when it is
installed, **Larian Launcher**.

**LSLib and .NET.** Reading `.pak` files needs LSLib, downloaded from GitHub
when you agree, and the .NET 8 Runtime (x64). **Check For LsLib Updates**
downloads LSLib again.

**Common problems**

Before the game starts, MO2 warns when:

- mods need mods that aren't active, or a newer version of a mod;
- mods load before the mods they need;
- mods need the Script Extender;
- one mod is installed twice, or mods that conflict are both active;
- the game turned off mods on its last start.

Community wiki: <https://wiki.bg3.community>

## The Witcher 3

**What MO2 Revamped does**

- Mods go to the game's `Mods` folder. When installing, MO2 fixes the usual
  layout mistakes: it adds a missing `mod` prefix, removes an extra `Mods\`
  folder around the mod, and sends `dlc` and `bin` folders to the mod's
  root folder.
- **Root folder:** with the plugin option `root_folder_to_game_folder` (on by
  default), each mod's `root` folder maps to the game folder. With Root Builder
  on, Root Builder handles it instead.
- The DX12 build (`bin\x64_dx12`) is found when there is no DX11 one.
- Saves show with their picture in the **Saves** tab.

Programs: **The Witcher 3: Wild Hunt**, **The Witcher 3: Wild Hunt (DX12)** and,
when it is found, **Script Merger**.

**Common problems**

- **Mods change the same scripts.** Run Script Merger to build
  `mod0000_MergedFiles`.
- **Menus the next-gen game won't load.** A mod's menu XML files are missing
  from `dx11filelist.txt` or `dx12filelist.txt`.

## Stardew Valley

**What MO2 Revamped does**

- Mods go to the game's `Mods` folder. A mod is a SMAPI folder with a
  `manifest.json`, found up to four levels deep, so packs with several mods
  install as they come.
- Saves are read from `%APPDATA%\StardewValley\Saves`, with the farm's details.

Programs: **SMAPI** (`StardewModdingAPI.exe`) and **Stardew Valley**. Start the
game with SMAPI to load mods.

**Common problems**

- **SMAPI isn't installed.** Install SMAPI with its own installer into the game
  folder, not as a mod.
- **Mods need other mods that aren't there**, or **newer versions of other
  mods**, or **a newer SMAPI**.
- **The same mod is installed twice** (the same UniqueID in two mods).

## Valheim

**What MO2 Revamped does**

- Mods go to the game folder. BepInEx plugins, InSlimVML mods (`*_vml.dll`),
  Advanced Builder `.vbuild` files and `.assets` files are recognized.
- BepInEx's loader (`winhttp.dll`) is loaded from its mod when it is installed.
- With the plugin option `sync_overwrite` (on by default), BepInEx `.cfg` files
  that land in Overwrite move into the mod whose plugin they belong to, when MO2
  starts and after the game closes.

**Common problems**

- **BepInEx isn't installed.** Install BepInExPack_Valheim as a mod.
- **Plugins need other plugins that aren't there**, such as Jotunn.
- **InSlimVML mods without their loader.**

## Bethesda games

Skyrim (Special Edition, original and VR), Fallout 4 (and VR), Fallout 3, New
Vegas, Tale of Two Wastelands, Oblivion, Morrowind, Starfield, Enderal and
Nehrim use MO2 Revamped's own Bethesda plugins, written from scratch on one
shared base. They keep the file names, names and settings of MO2's plugins,
so instances made with MO2 2.5.2 or an earlier beta open as they are.

**What MO2 Revamped does**

- **Plugin order:** the **Plugins** tab sets the order of the `.esp`, `.esm`
  and `.esl` files. **Sort** uses LOOT. For New Vegas and TTW it's off unless
  you turn on `enable_loot_sorting`, as most of their guides advise against it.
- **Script extenders** (SKSE, F4SE, xNVSE/NVSE, FOSE, OBSE, SFSE and the VR
  versions): when you install one, MO2 offers **Install it in the game
  folder**, **I'll copy the files myself** or **Install it as a regular mod**,
  and then starts the game through it. If its version doesn't match the game,
  MO2 warns before starting.
- **Root folders without Root Builder:** script extender loaders, ENB, ReShade
  and DXVK files in a mod's `Root` folder load from the mod where that is
  reliable; otherwise they are copied in for the run, any game file they
  replace is backed up, and they are taken out again once the game closes, or
  on MO2's next start. Files a program creates there go to `Overwrite/Root`.
- **Skyrim SE and Fallout 4:** MO2 checks that the script extender matches the
  game version and that Address Library is installed (option
  `script_extender_checks`, on by default).
- **Loose files and archives:** **Automatic archive invalidation**, in each
  profile, makes the game load loose files over the ones in its archives.
- **DLC and Creation Club** content shows in the mod list as
  "Creation Club: name", also in Skyrim VR and Fallout 4 VR; order their
  plugins in the Plugins tab.
- **Borderless window**, in the arrow menu of **Run**, for Skyrim, Skyrim SE,
  Enderal, Enderal SE, Fallout 3, New Vegas, TTW, Fallout 4, Oblivion and
  Nehrim.
- Saves in local time, read with size checks so a damaged save can't crash
  MO2; Fallout 3 co-saves; GOG and any-Steam-library detection.

**Common problems**

- **Script extender plugins that don't load.** A notification says which and
  why, such as an Address Library update or a missing DLL.
- **Plugins made for the original Skyrim** in Skyrim SE (form 43) are reported.
- **Starfield** warns about active ESP plugins and `sTestFile` lines in
  `StarfieldCustom.ini`.
- Add the script extender, LOOT, xEdit and other tools as programs in
  **Executables**, so they also see the mods.

Tested with mods so far: Skyrim SE, Fallout 4, Fallout 3, New Vegas and Nolvus.
Oblivion, the original Skyrim, Morrowind and Starfield haven't been played with
mods yet, and TTW, Enderal and the VR versions haven't been tried.

## Elden Ring and Nightreign

Elden Ring and Elden Ring Nightreign have MO2 Revamped's own tools. Mods go to
the game's `Game` folder. The program is **ELDEN RING (MO2)**, or **ELDEN RING
NIGHTREIGN (MO2)** for Nightreign, plus **ELDEN RING Reforged (online)** when
that mod is on. The full guide is in the app: **…** >
**Elden Ring guide** (or **Nightreign guide**).

**Archive installer.** When installing, choose **Organize for MO2
(recommended)** or **Install as it comes in the archive**. **Manual...** opens
**Arrange the files by hand**. It recognizes ELDEN RING Reforged, Convergence,
Seamless Co-op and FRZN Merge, and warns when a Seamless Co-op is made for the
other game.

**Native DLLs.** Tools > **Native DLL profile** lists the DLLs of the enabled
mods and their load order, for each profile. They load through MO2 Revamped's
native bridge, also with Seamless Co-op and ELDEN RING Reforged.

**Reforged UI images.** ELDEN RING Reforged's tutorial and item pictures come
from an archive (`menu\hi\00_Solo`, about 2 GB) that its own tool, Gideon,
makes from your game files. After installing or updating Reforged, MO2 runs
Gideon and puts the archive in the mod; the game folder isn't changed. To make
it again: Tools > **Reforged UI images...**. Without it, the tutorials show
"You are missing Reforged UI images". MO2 warns before starting the game when
the archive is missing, and when another active mod (The Convergence brings
one) has its own `00_Solo` with a higher priority than Reforged. Reforged
installed with a version before beta 18 has no Gideon: reinstall it from its
archive.

**Saves.** Tools > **Save isolation** chooses where saves go:

- **Only this profile (recommended)**;
- **Shared by this instance's profiles**;
- **Steam's saves (not isolated)**.

Named save profiles (**New save profile…**, **Rename…**, **Delete…**) keep
playthroughs apart. Saves are backed up before each start when they changed
(five kept by default, option `save_backups_kept`); **Restore selected backup**
brings one back.

While the game runs from an isolated profile, the Steam save folders are set
aside and come back unchanged when it closes, with every launcher.

Some mods save under their own name, so their characters stay apart from the
game without mods: ELDEN RING Reforged `ER0000.err`, Seamless Co-op
`ER0000.co2`, The Convergence `ER0000.cnv`, and The Convergence with Seamless
Co-op `ER0000.cnvco2`. With The Convergence, Seamless Co-op starts from a copy
of its folder in the instance with your own settings; the mod isn't changed.
If you played The Convergence from MO2 before beta 16, that character is in
`ER0000.sl2` (or `ER0000.co2` with Seamless): copy it as `ER0000.cnv` (or
`ER0000.cnvco2`) in the same folder to go on with it.

**Startup options.** Tools > **Startup options**: **Skip the intro logo
screens**, **Faster game start (BootBoost)**, **Start Elden Ring minimized**,
**Use a black startup background**, process priority, Overwrite cleanup and
the bridge logs.

**Online play.** MO2 doesn't change Easy Anti-Cheat or the online mode. Keep
modded files and saves for offline play.

**Common problems**

- **The game closes right after starting, or runs without MO2's bridge.**
  Check that **Force load libraries** is on for its program, and read **Open
  last bridge log** in Startup options.
- **MO2 doesn't recognize this version of Elden Ring.** A game update can come
  before MO2 Revamped supports it; wait for an MO2 Revamped update.
- **Several active mods bring their own regulation.bin.** Only one can win;
  keep the one the others are made for.
- **Seamless Co-op needs a session password.**
- **Steam saves still hidden** or **save recovery needed** after a crash: MO2
  puts the saves back on its next start; follow the notice.
- Some DLL mods need an extension API that MO2 Revamped doesn't provide. me3
  and Mod Engine 2 aren't used as launchers; their `mod/` packages still
  install.
- Nightreign has no Reforged online launcher.
