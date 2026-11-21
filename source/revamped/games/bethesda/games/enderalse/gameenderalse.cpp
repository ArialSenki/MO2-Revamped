// MO2 Revamped: Enderal Special Edition, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_enderalse (by Archost, ZachHaber and the MO2 team), which instances,
// Nexus and Wabbajack lists already know the game by.
#include "gameenderalse.h"

#include "detection/stores.h"
#include "ini/inifiles.h"
#include "plugins/asteriskplugins.h"

#include <imoinfo.h>
#include <utility.h>

#include <QFileInfo>
#include <QIcon>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Skyrim", u"SkyrimSE", u"Enderal"};
  constexpr QStringView Variants[]        = {u"Steam", u"GOG"};
  constexpr VariantMarker Markers[]       = {{u"Galaxy64.dll", u"GOG"}};
  constexpr VariantFolder Folders[]       = {{u"GOG", u"Enderal Special Edition GOG"}};

  constexpr QStringView IniFiles[] = {u"Enderal.ini", u"EnderalPrefs.ini"};
  constexpr QStringView Primary[]  = {u"skyrim.esm",
                                      u"update.esm",
                                      u"dawnguard.esm",
                                      u"hearthfires.esm",
                                      u"dragonborn.esm",
                                      u"enderal - forgotten stories.esm",
                                      u"skyui_se.esp"};
  // written first in plugins.txt, active, as MO2's plugin did
  constexpr QStringView FirstActive[] = {u"Enderal - Forgotten Stories.esm", u"SkyUI_SE.esp"};

  constexpr QStringView VanillaArchives[] = {
      u"Skyrim - Textures0.bsa", u"Skyrim - Textures1.bsa", u"Skyrim - Textures2.bsa",
      u"Skyrim - Textures3.bsa", u"Skyrim - Textures4.bsa", u"Skyrim - Textures5.bsa",
      u"Skyrim - Textures6.bsa", u"Skyrim - Textures7.bsa", u"Skyrim - Textures8.bsa",
      u"Skyrim - Meshes0.bsa",   u"Skyrim - Meshes1.bsa",   u"Skyrim - Voices_en0.bsa",
      u"Skyrim - Sounds.bsa",    u"Skyrim - Interface.bsa", u"Skyrim - Animations.bsa",
      u"Skyrim - Shaders.bsa",   u"Skyrim - Misc.bsa",      u"E - Meshes.bsa",
      u"E - SE.bsa",             u"E - Scripts.bsa",        u"E - Sounds.bsa",
      u"E - Textures1.bsa",      u"E - Textures2.bsa",      u"E - Textures3.bsa",
      u"L - Textures.bsa",       u"L - Voices.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",     u"interface",      u"menus",         u"meshes",
      u"music",     u"scripts",        u"shaders",       u"sound",
      u"strings",   u"textures",       u"trees",         u"video",
      u"facegen",   u"materials",      u"skse",          u"distantlod",
      u"asi",       u"Tools",          u"MCM",           u"distantland",
      u"mits",      u"dllplugins",     u"CalienteTools", u"NetScriptFramework",
      u"shadersfx", u"Nemesis_Engine"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"esl", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"Enderal Special Edition (SKSE)", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"Enderal Special Edition Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      // Steam has the Creation Kit as an app of its own
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false,
       u"1946180"},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Enderal Special Edition\"", false, false}};

  // Bethesda's key, then GOG's (identifyGamePath() adds Steam)
  constexpr Store StoreOrder[]   = {Store::Bethesda, Store::Gog};
  constexpr QStringView GogIds[] = {u"1708684988"};

  constexpr GameDefinition EnderalSE{
      .pluginName          = u"Enderal Special Edition Support Plugin",
      .translationContext  = "GameEnderalSE",
      .displayName         = u"Enderal Special Edition",
      .author              = u"Archost, ZachHaber & MO2 Team",
      .version             = {1, 2, 0},
      .gameName            = u"Enderal Special Edition",
      .shortName           = u"EnderalSE",
      .validShortNames     = ValidShortNames,
      .nexusName           = u"enderalspecialedition",
      .nexusGameId         = 3685,
      .nexusModOrganizerId = 0,
      .variants            = Variants,
      .variantMarkers      = Markers,
      .defaultVariant      = u"Steam",

      .binary                    = u"skse64_loader.exe",
      .launcher                  = u"Enderal Launcher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Enderal Special Edition",
      .variantFolders            = Folders,
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"enderal_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = {},
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::PluginsTxtStar,
      .lightPlugins              = true,
      .lootSortingSetting        = {},

      .saveFormat      = SaveFormat::SkyrimSE,
      .saveExtension   = u"ess",
      .localSavesIni   = u"enderal.ini",
      // the game reads it from Skyrim SE's My Games folder
      .localSavesDummy = u"..\\Enderal Special Edition\\__MO_Saves\\",

      .extender = {.loader          = u"skse64_loader.exe",
                   .pluginPath      = u"skse/plugins",
                   .coSaveExtension = u"skse"},
      // the game reads 511 characters of each key; the names are joined
      // with "," alone, as MO2's plugin wrote them
      .archives   = {.iniFile                = u"enderal.ini",
                     .listKey                = u"SResourceArchiveList",
                     .maxListLength          = 512,
                     .listKey2               = u"SResourceArchiveList2",
                     .splitLength            = 511,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = {},
                     .invalidationBsaVersion = 0,
                     .resourceDirsIni        = {},
                     .separator              = u","},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::SkyProc)},
      .executables = Executables,
      .stores      = {.order       = StoreOrder,
                      .bethesdaKey = u"Software\\Bethesda Softworks\\Enderal Special Edition",
                      .steamAppId  = u"976620",
                      .steamFolder = u"Enderal Special Edition",
                      .gogIds      = GogIds},
      .settings    = {}};
}  // namespace

EnderalSEGame::EnderalSEGame() : BethesdaGame(EnderalSE) {}

bool EnderalSEGame::looksValid(const QDir& path) const
{
  return path.exists(getLauncherName()) && path.exists(binaryName());
}

QIcon EnderalSEGame::gameIcon() const
{
  return MOBase::iconForExecutable(gameDirectory().absoluteFilePath(getLauncherName()));
}

void EnderalSEGame::initializeProfile(const QDir& directory, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    // MO2's plugin missed the '/' here, so it never found the game's own
    // plugins.txt and gave the profile an empty one
    copyIfMissing(localAppFolder() + "/" + variantFolder(), directory, "plugins.txt",
                  "plugins.txt");
  }
  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    // the game folder has the defaults of both INIs
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/Enderal.ini").exists()) {
      copyIfMissing(gameDirectory().absolutePath(), directory, "enderal_default.ini",
                    "Enderal.ini");
      copyIfMissing(gameDirectory().absolutePath(), directory, "enderalprefs_default.ini",
                    "EnderalPrefs.ini");
    } else {
      copyIfMissing(myGamesPath(), directory, "Enderal.ini", "Enderal.ini");
      copyIfMissing(myGamesPath(), directory, "EnderalPrefs.ini", "EnderalPrefs.ini");
    }
  }
}

void EnderalSEGame::registerFeatures()
{
  BethesdaGame::registerFeatures();
  // plugins.txt opens with the game's master and SkyUI (replaces the
  // default lists)
  registerFeature(std::make_shared<AsteriskGamePlugins>(organizer(), true, FirstActive));
}

QString EnderalSEGame::identifyGamePath() const
{
  const QString path = BethesdaGame::identifyGamePath();
  if (!path.isEmpty()) {
    return path;
  }
  return findSteamFolder("Enderal Special Edition", "Data\\Enderal - Forgotten Stories.esm");
}

}  // namespace mo2r::bethesda
