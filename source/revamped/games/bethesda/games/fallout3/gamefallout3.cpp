// MO2 Revamped: Fallout 3, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_fallout3 (by Tannin), which instances, Nexus and Wabbajack lists
// already know the game by.
#include "gamefallout3.h"

#include <QDir>
#include <QFileInfo>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"FalloutNV"};
  constexpr QStringView Variants[]        = {u"Regular", u"Game Of The Year"};

  constexpr QStringView IniFiles[] = {u"fallout.ini",       u"falloutprefs.ini",
                                      u"custom.ini",        u"FalloutCustom.ini",
                                      u"GECKCustom.ini",    u"GECKPrefs.ini"};
  constexpr QStringView Primary[]  = {u"fallout3.esm"};
  constexpr QStringView Dlc[]      = {u"ThePitt.esm", u"Anchorage.esm", u"BrokenSteel.esm",
                                      u"PointLookout.esm", u"Zeta.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Fallout - Textures.bsa", u"Fallout - Meshes.bsa",     u"Fallout - Voices.bsa",
      u"Fallout - Sound.bsa",    u"Fallout - MenuVoices.bsa", u"Fallout - Misc.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",    u"interface",  u"menus",         u"meshes",
      u"music",    u"scripts",    u"shaders",       u"sound",
      u"strings",  u"textures",   u"trees",         u"video",
      u"facegen",  u"materials",  u"fose",          u"distantlod",
      u"asi",      u"Tools",      u"MCM",           u"distantland",
      u"mits",     u"dllplugins", u"CalienteTools", u"NetScriptFramework",
      u"shadersfx"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"FOSE", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"Fallout 3", u"Fallout3.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Fallout Mod Manager", u"fomm/fomm.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Construction Kit", u"geck.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Fallout Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      {u"BOSS", u"BOSS/BOSS.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Fallout3\"", false, false}};

  // the Bethesda key, then Epic, GOG and Steam (the key is only written once
  // the game has run)
  constexpr Store StoreOrder[]      = {Store::Bethesda, Store::Epic, Store::Gog, Store::Steam};
  // Fallout 3: Game of the Year Edition
  constexpr QStringView EpicApps[]  = {u"adeae8bbfc94427db57c7dfecce3f1d4"};
  constexpr QStringView GogIds[]    = {u"1454315831"};
  // after the Game of the Year edition, the first release (delisted)
  constexpr SteamApp MoreSteamApps[] = {{u"22300", u"Fallout 3"}};

  constexpr GameDefinition Fallout3{
      .pluginName          = u"Fallout 3 Support Plugin",
      .translationContext  = "GameFallout3",
      .displayName         = u"Fallout 3",
      .author              = u"Tannin",
      .version             = {1, 4, 1},
      .gameName            = u"Fallout 3",
      .shortName           = u"Fallout3",
      .validShortNames     = ValidShortNames,
      .nexusName           = u"fallout3",
      .nexusGameId         = 120,
      .nexusModOrganizerId = 16348,
      .variants            = Variants,
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"Fallout3.exe",
      .launcher                  = u"FalloutLauncher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Fallout3",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = true,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"fallout_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Fallout3,
      .saveExtension = u"fos",
      .localSavesIni = u"fallout.ini",

      // FOSE writes a .fose co-save next to each save (FOSE's Serialization.cpp)
      .extender = {.loader          = u"fose_loader.exe",
                   .pluginPath      = u"fose/plugins",
                   .coSaveExtension = u"fose"},
      // 255 characters of the list, as MO2 has always read it
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
                      .bethesdaKey   = u"Software\\Bethesda Softworks\\Fallout3",
                      .steamAppId    = u"22370",
                      .steamFolder   = u"Fallout 3 goty",
                      .moreSteamApps = MoreSteamApps,
                      .gogIds        = GogIds,
                      .epicAppNames  = EpicApps,
                      .epicSubfolder = u"Fallout 3 GOTY*"},
      .settings    = {}};
}  // namespace

Fallout3Game::Fallout3Game() : BethesdaGame(Fallout3) {}

QString Fallout3Game::steamAPPId() const
{
  if (selectedVariant() == u"Game Of The Year") {
    return QStringLiteral("22370");
  }
  // Steam only sells the Game Of The Year edition (22300 is delisted); an
  // install Steam has as that one is that one, whatever edition was picked
  const QDir steamapps(gameDirectory().absoluteFilePath("../.."));
  if (QFileInfo::exists(steamapps.absoluteFilePath("appmanifest_22370.acf")) &&
      !QFileInfo::exists(steamapps.absoluteFilePath("appmanifest_22300.acf"))) {
    return QStringLiteral("22370");
  }
  return QStringLiteral("22300");
}

QString Fallout3Game::getLauncherName() const
{
  if (QFileInfo(gameDirectory().absoluteFilePath("Fallout3Launcher.exe")).isFile()) {
    return QStringLiteral("Fallout3Launcher.exe");
  }
  return definition().launcher.toString();
}

}  // namespace mo2r::bethesda
