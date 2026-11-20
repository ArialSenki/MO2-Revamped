// MO2 Revamped: Fallout New Vegas, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_falloutNV (by Tannin and the MO2 team), which instances, Nexus and
// Wabbajack lists already know the game by.
#include "gamefalloutnv.h"

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Fallout3"};
  constexpr QStringView Variants[]        = {u"Steam", u"GOG", u"Epic Games"};
  constexpr VariantMarker Markers[]       = {{u"Galaxy.dll", u"GOG"},
                                             {u"EOSSDK-Win32-Shipping.dll", u"Epic Games"}};
  constexpr VariantFolder Folders[]       = {{u"Epic Games", u"FalloutNV_Epic"}};

  constexpr QStringView IniFiles[] = {u"fallout.ini", u"falloutprefs.ini",
                                      u"falloutcustom.ini", u"GECKCustom.ini",
                                      u"GECKPrefs.ini"};
  constexpr QStringView Primary[]  = {u"falloutnv.esm"};
  constexpr QStringView Dlc[]      = {u"DeadMoney.esm",        u"HonestHearts.esm",
                                      u"OldWorldBlues.esm",    u"LonesomeRoad.esm",
                                      u"GunRunnersArsenal.esm", u"CaravanPack.esm",
                                      u"ClassicPack.esm",      u"MercenaryPack.esm",
                                      u"TribalPack.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Fallout - Textures.bsa", u"Fallout - Textures2.bsa", u"Fallout - Meshes.bsa",
      u"Fallout - Voices1.bsa",  u"Fallout - Sound.bsa",     u"Fallout - Misc.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",      u"interface",  u"menus",         u"meshes",     u"music",
      u"scripts",    u"shaders",    u"sound",         u"strings",    u"textures",
      u"trees",      u"video",      u"facegen",       u"materials",  u"nvse",
      u"distantlod", u"asi",        u"Tools",         u"MCM",        u"distantland",
      u"mits",       u"dllplugins", u"CalienteTools", u"shadersfx",  u"config",
      u"KEYWORDS",   u"BaseObjectSwapper", u"RaceMenuPresets", u"Devkit"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"New Vegas", u"FalloutNV.exe", ExecutableKind::GameFolder, {}, true, false},
      {u"Fallout Launcher", u"FalloutNVLauncher.exe", ExecutableKind::GameFolder, {}, true,
       false},
      // NVSE doesn't support the Epic edition
      {u"NVSE", {}, ExecutableKind::ScriptExtender, {}, false, true},
      {u"Fallout Mod Manager", u"fomm/fomm.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"BOSS", u"BOSS/BOSS.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"GECK", u"geck.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"FalloutNV\"", false, false}};

  // the Bethesda key first, then Epic, GOG and Steam (the key is only written
  // once the game has run from its own launcher)
  constexpr Store StoreOrder[]        = {Store::Bethesda, Store::Epic, Store::Gog, Store::Steam};
  // the base game's; the DLC have their own (Dead Money
  // b290229eb58045cbab9501640f3278f3, Honest Hearts 562d4a2c1b3147b089a7c453e3ddbcbe,
  // Old World Blues c8dae1ab0570475a8b38a9041e614840, Lonesome Road
  // 4fa3d8d9b2cb4714a19a38d1a598be8f, Gun Runners' Arsenal
  // 7dcfb9cd9d134728b2646466c34c7b3b, Courier's Stash ee9a44b4530942499ef1c8c390731fce)
  constexpr QStringView EpicApps[]    = {u"5daeb974a22a435988892319b3a4f476"};

  constexpr BoolSetting Settings[] = {
      {u"enable_loot_sorting",
       QT_TRANSLATE_NOOP("GameFalloutNV", "Allow sorting plugins with LOOT (most New Vegas "
                                          "mod guides advise against it)"),
       false}};

  constexpr GameDefinition FalloutNV{
      .pluginName          = u"Fallout NV Support Plugin",
      .translationContext  = "GameFalloutNV",
      .displayName         = u"Fallout New Vegas",
      .author              = u"Tannin & MO2 Team",
      .version             = {1, 6, 0},
      .gameName            = u"New Vegas",
      .shortName           = u"FalloutNV",
      .validShortNames     = ValidShortNames,
      .nexusName           = u"newvegas",
      .nexusGameId         = 130,
      .nexusModOrganizerId = 42572,
      .variants            = Variants,
      .variantMarkers      = Markers,
      .defaultVariant      = u"Steam",

      .binary             = u"FalloutNV.exe",
      .launcher           = u"FalloutNVLauncher.exe",
      .dataFolder         = u"data",
      .myGamesFolder      = u"FalloutNV",
      .variantFolders     = Folders,
      .iniFiles           = IniFiles,
      .defaultIni         = u"fallout_default.ini",
      .primaryPlugins     = Primary,
      .dlcPlugins         = Dlc,
      .loadOrder          = LoadOrderKind::FileTime,
      .lootSortingSetting = u"enable_loot_sorting",

      .saveFormat    = SaveFormat::Fallout3,
      .saveExtension = u"fos",
      .localSavesIni = u"fallout.ini",

      .extender = {.loader          = u"nvse_loader.exe",
                   .pluginPath      = u"nvse/plugins",
                   .coSaveExtension = u"nvse"},
      // NVAC raises the game's limit for the archive list
      .archives   = {.iniFile                = u"fallout.ini",
                     .listKey                = u"SArchiveList",
                     .maxListLength          = 8192,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Fallout - Invalidation.bsa",
                     .invalidationBsaVersion = 0x68},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::Mcm) | contentBit(Content::SkyProc)},
      .executables = Executables,
      .stores      = {.order         = StoreOrder,
                      .bethesdaKey   = u"Software\\Bethesda Softworks\\FalloutNV",
                      .steamAppId    = u"22380",
                      .steamFolder   = u"Fallout New Vegas",
                      .gogId         = u"1454587428",
                      .epicAppNames  = EpicApps,
                      .epicSubfolder = u"Fallout New Vegas*"},
      .settings    = Settings};
}  // namespace

FalloutNVGame::FalloutNVGame() : BethesdaGame(FalloutNV) {}

QString FalloutNVGame::steamAPPId() const
{
  if (selectedVariant() != u"Steam") {
    return {};
  }
  // the PCR edition installs to a folder ending so (MO2 8d663d5)
  return m_GamePath.endsWith(u"enplczru") ? QStringLiteral("22490")
                                          : QStringLiteral("22380");
}

}  // namespace mo2r::bethesda
