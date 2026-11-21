// MO2 Revamped: Tale of Two Wastelands, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_ttw (by SuperSandro2000 and the MO2 team), which instances and
// Wabbajack lists already know the game by.
#include "gamettw.h"

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"FalloutNV", u"Fallout3"};
  constexpr QStringView Variants[]        = {u"Steam", u"GOG", u"Epic Games"};
  constexpr VariantMarker Markers[]       = {{u"Galaxy.dll", u"GOG"},
                                             {u"EOSSDK-Win32-Shipping.dll", u"Epic Games"}};
  constexpr VariantFolder Folders[]       = {{u"Epic Games", u"FalloutNV_Epic"}};

  constexpr QStringView IniFiles[] = {u"fallout.ini", u"falloutprefs.ini",
                                      u"falloutcustom.ini", u"GECKCustom.ini",
                                      u"GECKPrefs.ini"};
  // both games' masters and DLC, then TTW's own
  constexpr QStringView Primary[] = {
      u"falloutnv.esm",     u"deadmoney.esm",           u"honesthearts.esm",
      u"oldworldblues.esm", u"lonesomeroad.esm",        u"gunrunnersarsenal.esm",
      u"fallout3.esm",      u"anchorage.esm",           u"thepitt.esm",
      u"brokensteel.esm",   u"pointlookout.esm",        u"zeta.esm",
      u"caravanpack.esm",   u"classicpack.esm",         u"mercenarypack.esm",
      u"tribalpack.esm",    u"taleoftwowastelands.esm", u"YUPTTW.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Fallout - Textures.bsa", u"Fallout - Textures2.bsa", u"Fallout - Meshes.bsa",
      u"Fallout - Voices1.bsa",  u"Fallout - Sound.bsa",     u"Fallout - Misc.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",      u"interface",  u"menus",         u"meshes",     u"music",
      u"scripts",    u"shaders",    u"sound",         u"strings",    u"textures",
      u"trees",      u"video",      u"facegen",       u"materials",  u"nvse",
      u"distantlod", u"asi",        u"Tools",         u"MCM",        u"distantland",
      u"mits",       u"dllplugins", u"CalienteTools", u"shadersfx",  u"config",
      u"KEYWORDS",   u"BaseObjectSwapper", u"RaceMenuPresets"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"Tale of Two Wastelands", u"FalloutNV.exe", ExecutableKind::GameFolder, {}, true,
       false},
      {u"Fallout Launcher", {}, ExecutableKind::Launcher, {}, true, false},
      // NVSE doesn't support the Epic edition
      {u"NVSE", {}, ExecutableKind::ScriptExtender, {}, false, true},
      {u"GECK", u"geck.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"FalloutNV\"", false, false}};

  // New Vegas' Bethesda key, then its Epic edition, as the old plugin looked
  constexpr Store StoreOrder[]     = {Store::Bethesda, Store::Epic};
  constexpr QStringView EpicApps[] = {u"5daeb974a22a435988892319b3a4f476"};

  constexpr BoolSetting Settings[] = {
      {u"enable_loot_sorting",
       QT_TRANSLATE_NOOP("GameFalloutTTW", "Allow sorting plugins with LOOT (the Tale of Two "
                                           "Wastelands guides advise against it)"),
       false}};

  constexpr GameDefinition Ttw{
      .pluginName          = u"Fallout TTW Support Plugin",
      .translationContext  = "GameFalloutTTW",
      .displayName         = u"Tale of Two Wastelands",
      .author              = u"SuperSandro2000 & MO2 Team",
      .version             = {1, 6, 0},
      .gameName            = u"TTW",
      .shortName           = u"TTW",
      .validShortNames     = ValidShortNames,
      // no Nexus section of its own
      .nexusName           = u"",
      .nexusGameId         = 0,
      .nexusModOrganizerId = 0,
      .variants            = Variants,
      .variantMarkers      = Markers,
      .defaultVariant      = u"Steam",

      .binary                    = u"FalloutNV.exe",
      .launcher                  = u"FalloutNVLauncher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"FalloutNV",
      .variantFolders            = Folders,
      // mapped to FalloutNV on every variant, as New Vegas'
      .listsAlsoInDefaultAppData = true,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"fallout_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = {},
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = u"enable_loot_sorting",

      .saveFormat    = SaveFormat::FalloutNV,
      .saveExtension = u"fos",
      .localSavesIni = u"fallout.ini",

      .extender = {.loader          = u"nvse_loader.exe",
                   .pluginPath      = u"nvse/plugins",
                   .coSaveExtension = u"nvse"},
      // 255 characters, as the old plugin read it (New Vegas' reads 8 KB)
      .archives   = {.iniFile                = u"fallout.ini",
                     .listKey                = u"SArchiveList",
                     .maxListLength          = 256,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Fallout - Invalidation.bsa",
                     .invalidationBsaVersion = 0x68},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::Mcm) | contentBit(Content::SkyProc),
                     .materialsContent = false},
      .unmanaged   = {},
      .executables = Executables,
      .stores      = {.order         = StoreOrder,
                      .bethesdaKey   = u"Software\\Bethesda Softworks\\FalloutNV",
                      .steamAppId    = u"22380",
                      .steamFolder   = {},
                      .moreSteamApps = {},
                      .steamValidFile = {},
                      .gogIds        = {},
                      .epicAppNames  = EpicApps,
                      .epicSubfolder = u"Fallout New Vegas*"},
      .settings    = Settings};
}  // namespace

TtwGame::TtwGame() : BethesdaGame(Ttw) {}

QString TtwGame::displayGameName() const
{
  return definition().displayName.toString();
}

QString TtwGame::lootGameName() const
{
  return QStringLiteral("FalloutNV");
}

QStringList TtwGame::primarySources() const
{
  return {"FalloutNV"};
}

QString TtwGame::steamAPPId() const
{
  if (selectedVariant() != u"Steam") {
    return {};
  }
  // the PCR edition installs to a folder ending so (MO2 079a3c9)
  return m_GamePath.endsWith(u"enplczru") ? QStringLiteral("22490")
                                          : QStringLiteral("22380");
}

}  // namespace mo2r::bethesda
