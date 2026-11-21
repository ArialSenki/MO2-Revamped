// MO2 Revamped: Skyrim Special Edition, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_skyrimse (by the MO2 team, originally Archost and ZachHaber), which
// instances, Nexus, Wabbajack and Nolvus already know the game by.
#include "gameskyrimse.h"

#include "extender/extenderchecks.h"

#include <imoinfo.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Skyrim"};
  constexpr QStringView Variants[]        = {u"Steam", u"GOG", u"Epic Games"};
  constexpr VariantMarker Markers[]       = {{u"Galaxy64.dll", u"GOG"},
                                             {u"EOSSDK-Win64-Shipping.dll", u"Epic Games"}};
  constexpr VariantFolder Folders[]       = {{u"GOG", u"Skyrim Special Edition GOG"},
                                             {u"Epic Games", u"Skyrim Special Edition EPIC"}};

  constexpr QStringView IniFiles[] = {u"skyrim.ini", u"skyrimprefs.ini", u"skyrimcustom.ini"};
  // the Creation Club ones from Skyrim.ccc follow these
  constexpr QStringView Primary[] = {u"skyrim.esm", u"update.esm", u"dawnguard.esm",
                                     u"hearthfires.esm", u"dragonborn.esm"};
  constexpr QStringView Dlc[]     = {u"dawnguard.esm", u"hearthfires.esm", u"dragonborn.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Skyrim - Textures0.bsa", u"Skyrim - Textures1.bsa", u"Skyrim - Textures2.bsa",
      u"Skyrim - Textures3.bsa", u"Skyrim - Textures4.bsa", u"Skyrim - Textures5.bsa",
      u"Skyrim - Textures6.bsa", u"Skyrim - Textures7.bsa", u"Skyrim - Textures8.bsa",
      u"Skyrim - Meshes0.bsa",   u"Skyrim - Meshes1.bsa",   u"Skyrim - Voices_en0.bsa",
      u"Skyrim - Sounds.bsa",    u"Skyrim - Interface.bsa", u"Skyrim - Animations.bsa",
      u"Skyrim - Shaders.bsa",   u"Skyrim - Misc.bsa"};

  // MO2's list (6fc7a61, 3257708, 9e5e021, c218a92, 1cb3e93) and MO2
  // Revamped's: SEQ files and LOD settings; Community Shaders' cache,
  // KiLoader's plugins and SunHelm's settings, as lists like Nolvus install them
  constexpr QStringView DataFolders[] = {
      u"fonts",          u"interface",     u"menus",              u"meshes",
      u"music",          u"scripts",       u"shaders",            u"sound",
      u"strings",        u"textures",      u"trees",              u"video",
      u"facegen",        u"materials",     u"skse",               u"distantlod",
      u"asi",            u"Tools",         u"MCM",                u"distantland",
      u"mits",           u"dllplugins",    u"CalienteTools",      u"NetScriptFramework",
      u"shadersfx",      u"Nemesis_Engine", u"Platform",          u"grass",
      u"LightPlacer",    u"mainmenuwallpapers", u"MainMenuVideo", u"PBRMaterialObjects",
      u"PBRNifPatcher",  u"PBRTextureSets", u"Pandora_Engine",    u"Seq",
      u"LODSettings",    u"ShaderCache",   u"KiLoader",           u"SunHelm"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"esl", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"SKSE", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"Skyrim Special Edition", u"SkyrimSE.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Skyrim Special Edition Launcher", u"SkyrimSELauncher.exe", ExecutableKind::GameFolder,
       {}, false, false},
      // Steam has the Creation Kit as an app of its own
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false,
       u"1946180"},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Skyrim Special Edition\"", false, false}};

  // Bethesda's key (written once the game has run from its own launcher),
  // GOG's two releases, Epic, then a Steam library without that key (Windows
  // reinstalled, the library moved, the game never started)
  constexpr Store StoreOrder[]     = {Store::Bethesda, Store::Gog, Store::Epic, Store::Steam};
  constexpr QStringView GogIds[]   = {u"1162721350", u"1711230643"};
  // the game and its Anniversary Edition update
  constexpr QStringView EpicApps[] = {u"ac82db5035584c7f8a2c548d98c86b2c",
                                      u"5d600e4f59974aeba0259c7734134e27"};

  constexpr BoolSetting Settings[] = {
      {u"enderal_downloads",
       QT_TRANSLATE_NOOP("GameSkyrimSE", "Accept downloads from the Enderal and Enderal "
                                         "Special Edition sections of Nexus"),
       false},
      {u"script_extender_checks",
       QT_TRANSLATE_NOOP("GameSkyrimSE", "Warn when SKSE doesn't match the game version or "
                                         "the Address Library is missing"),
       true}};

  constexpr GameDefinition SkyrimSE{
      .pluginName          = u"Skyrim Special Edition Support Plugin",
      .translationContext  = "GameSkyrimSE",
      .displayName         = u"Skyrim Special Edition",
      .author              = u"MO2 Team, Orig: Archost & ZachHaber",
      .version             = {1, 7, 1},
      .gameName            = u"Skyrim Special Edition",
      .shortName           = u"SkyrimSE",
      .validShortNames     = ValidShortNames,
      .nexusName           = u"skyrimspecialedition",
      .nexusGameId         = 1704,
      .nexusModOrganizerId = 6194,
      .variants            = Variants,
      .variantMarkers      = Markers,
      .defaultVariant      = u"Steam",

      .binary                    = u"SkyrimSE.exe",
      .launcher                  = u"SkyrimSELauncher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Skyrim Special Edition",
      .variantFolders            = Folders,
      .listsAlsoInDefaultAppData = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"skyrim_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = u"Skyrim.ccc",
      .loadOrder                 = LoadOrderKind::PluginsTxtStar,
      .lightPlugins              = true,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::SkyrimSE,
      .saveExtension = u"ess",
      .localSavesIni = u"Skyrimcustom.ini",

      .extender = {.loader               = u"skse64_loader.exe",
                   .pluginPath           = u"skse/plugins",
                   .coSaveExtension      = u"skse",
                   .runtimePrefix        = u"skse64_",
                   .addressLibraryFolder = u"SKSE/Plugins"},
      // the game reads 255 characters of each key
      .archives   = {.iniFile                = u"skyrim.ini",
                     .listKey                = u"SResourceArchiveList",
                     .maxListLength          = 256,
                     .listKey2               = u"SResourceArchiveList2",
                     .splitLength            = 255,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = {},
                     .invalidationBsaVersion = 0},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::SkyProc)},
      .executables = Executables,
      .stores      = {.order         = StoreOrder,
                      .bethesdaKey   = u"Software\\Bethesda Softworks\\Skyrim Special Edition",
                      .steamAppId    = u"489830",
                      .steamFolder   = u"Skyrim Special Edition",
                      .gogIds        = GogIds,
                      .epicAppNames  = EpicApps,
                      .epicSubfolder = {}},
      .settings    = Settings};
}  // namespace

SkyrimSEGame::SkyrimSEGame() : BethesdaGame(SkyrimSE) {}

QStringList SkyrimSEGame::validShortNames() const
{
  QStringList names = BethesdaGame::validShortNames();
  if (organizer() != nullptr && organizer()->pluginSetting(name(), "enderal_downloads").toBool()) {
    names.append({"Enderal", "EnderalSE"});
  }
  return names;
}

std::vector<unsigned int> SkyrimSEGame::activeProblems() const
{
  std::vector<unsigned int> problems;
  if (organizer() == nullptr || organizer()->managedGame() != this ||
      !organizer()->pluginSetting(name(), "script_extender_checks").toBool()) {
    return problems;
  }
  if (extenderRuntimeMissing(this, nullptr)) {
    problems.push_back(ProblemExtenderVersion);
  }
  if (addressLibraryMissing(this, nullptr)) {
    problems.push_back(ProblemAddressLibrary);
  }
  return problems;
}

QString SkyrimSEGame::shortDescription(unsigned int key) const
{
  switch (key) {
  case ProblemExtenderVersion:
    return translate("SKSE doesn't match the game version");
  case ProblemAddressLibrary:
    return translate("Address Library for SKSE Plugins is missing");
  }
  return {};
}

QString SkyrimSEGame::fullDescription(unsigned int key) const
{
  QString expected;
  switch (key) {
  case ProblemExtenderVersion:
    extenderRuntimeMissing(this, &expected);
    return translate(
               "<p>The game folder has SKSE's loader, but not %1, the SKSE runtime for "
               "this game version (%2). Started through SKSE, the game won't run, or "
               "will run without SKSE and its plugins.</p>"
               "<p>Install the SKSE build for this game version from its official site "
               "into the game folder, or change the game back to the version that "
               "matches your SKSE.</p>")
        .arg(expected, gameVersion());
  case ProblemAddressLibrary:
    addressLibraryMissing(this, &expected);
    return translate(
               "<p>Some active mods have SKSE plugins, but no mod has %1, the Address "
               "Library for this game version (%2). Most SKSE plugins need it, and "
               "without it they don't load or the game closes at start.</p>"
               "<p>Install \"Address Library for SKSE Plugins\" from Nexus (the file for "
               "your game version) and activate it.</p>")
        .arg(expected, gameVersion());
  }
  return {};
}

}  // namespace mo2r::bethesda
