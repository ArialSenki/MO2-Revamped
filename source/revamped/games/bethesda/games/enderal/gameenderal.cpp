// MO2 Revamped: Enderal (on Skyrim 2011), on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_enderal (by AL12 and the MO2 team), which instances, Nexus and
// Wabbajack lists already know the game by.
#include "gameenderal.h"

#include "detection/stores.h"
#include "ini/inifiles.h"
#include "plugins/filetimeplugins.h"
#include "saves/bethesdasavegame.h"
#include "util/winfolders.h"

#include <imoinfo.h>
#include <utility.h>

#include <QFileInfo>
#include <QIcon>
#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Skyrim"};

  constexpr QStringView IniFiles[] = {u"enderal.ini", u"enderalprefs.ini"};
  constexpr QStringView Primary[]  = {u"Skyrim.esm", u"Enderal - Forgotten Stories.esm",
                                      u"Update.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Skyrim - Misc.bsa",      u"Skyrim - Shaders.bsa",    u"Skyrim - Textures.bsa",
      u"Skyrim - Interface.bsa", u"Skyrim - Animations.bsa", u"Skyrim - Meshes.bsa",
      u"Skyrim - Sounds.bsa",    u"E - Meshes.bsa",          u"E - Music.bsa",
      u"E - Scripts.bsa",        u"E - Sounds.bsa",          u"E - Textures1.bsa",
      u"E - Textures2.bsa",      u"E - Textures3.bsa",       u"L - Textures.bsa",
      u"L - Voices.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",      u"interface",        u"menus",         u"meshes",
      u"music",      u"scripts",          u"shaders",       u"sound",
      u"strings",    u"textures",         u"trees",         u"video",
      u"facegen",    u"materials",        u"skse",          u"distantlod",
      u"asi",        u"Tools",            u"MCM",           u"distantland",
      u"mits",       u"dllplugins",       u"SkyProc Patchers", u"CalienteTools",
      u"NetScriptFramework", u"shadersfx"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      // the game is started through SKSE's loader: that is its binary
      {u"Enderal (SKSE)", u"skse_loader.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Enderal Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      {u"BOSS", u"BOSS/BOSS.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Enderal\"", false, false},
      // Steam has the Creation Kit as an app of its own
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false,
       u"202480"}};

  constexpr BoolSetting Settings[] = {
      {u"sse_downloads",
       QT_TRANSLATE_NOOP("GameEnderal",
                         "Accept downloads from the Skyrim Special Edition section of Nexus"),
       false}};

  constexpr GameDefinition Enderal{
      .pluginName          = u"Enderal Support Plugin",
      .translationContext  = "GameEnderal",
      .displayName         = u"Enderal",
      .author              = u"AL12 & MO2 Team",
      .version             = {1, 3, 1},
      .gameName            = u"Enderal",
      .shortName           = u"Enderal",
      .validShortNames     = ValidShortNames,
      .nexusName           = u"enderal",
      .nexusGameId         = 2736,
      .nexusModOrganizerId = 0,
      .variants            = {},
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"skse_loader.exe",
      .launcher                  = u"Enderal Launcher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Enderal",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"enderal_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = {},
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      // Skyrim SE's reader reads the 2011 saves too (header version 9)
      .saveFormat      = SaveFormat::SkyrimSE,
      .saveExtension   = u"ess",
      .localSavesIni   = u"enderal.ini",
      // the game reads it from Skyrim's My Games folder
      .localSavesDummy = u"..\\Enderal\\__MO_Saves\\",

      .extender = {.loader          = u"skse_loader.exe",
                   .pluginPath      = u"skse/plugins",
                   .coSaveExtension = u"skse"},
      // the game reads 255 characters of each key
      .archives   = {.iniFile                = u"enderal.ini",
                     .listKey                = u"SResourceArchiveList",
                     .maxListLength          = 256,
                     .listKey2               = u"SResourceArchiveList2",
                     .splitLength            = 255,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Enderal - Invalidation.bsa",
                     .invalidationBsaVersion = 0x68},
      // every content shown, as MO2's plugin had it
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = 0},
      .executables = Executables,
      // found by identifyGamePath()
      .stores      = {.order = {}, .bethesdaKey = {}, .steamAppId = u"933480"},
      .settings    = Settings};

  constexpr QStringView PrimarySources[] = {u"Skyrim"};
}  // namespace

EnderalGame::EnderalGame() : BethesdaGame(Enderal) {}

QStringList EnderalGame::validShortNames() const
{
  QStringList names = BethesdaGame::validShortNames();
  if (organizer() != nullptr && organizer()->pluginSetting(name(), "sse_downloads").toBool()) {
    names.append("SkyrimSE");
  }
  return names;
}

QStringList EnderalGame::primarySources() const
{
  QStringList out;
  for (const QStringView s : PrimarySources) {
    out.append(s.toString());
  }
  return out;
}

MOBase::IPluginGame::LoadOrderMechanism EnderalGame::loadOrderMechanism() const
{
  return LoadOrderMechanism::PluginsTxt;
}

bool EnderalGame::looksValid(const QDir& path) const
{
  return path.exists(getLauncherName()) && path.exists(binaryName());
}

QIcon EnderalGame::gameIcon() const
{
  return MOBase::iconForExecutable(gameDirectory().absoluteFilePath(getLauncherName()));
}

void EnderalGame::initializeProfile(const QDir& directory, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyIfMissing(localAppFolder() + "/" + variantFolder(), directory, "plugins.txt",
                  "plugins.txt");
  }
  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    // the game folder has the defaults of both INIs
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(myGamesPath() + "/enderal.ini").exists()) {
      copyIfMissing(gameDirectory().absolutePath(), directory, "enderal_default.ini",
                    "enderal.ini");
      copyIfMissing(gameDirectory().absolutePath(), directory, "enderalprefs_default.ini",
                    "enderalprefs.ini");
    } else {
      copyIfMissing(myGamesPath(), directory, "enderal.ini", "enderal.ini");
      copyIfMissing(myGamesPath(), directory, "enderalprefs.ini", "enderalprefs.ini");
    }
  }
}

void EnderalGame::registerFeatures()
{
  BethesdaGame::registerFeatures();
  // the order always from loadorder.txt (replaces the default one)
  registerFeature(std::make_shared<FileTimeGamePlugins>(organizer(), true));
}

QString EnderalGame::identifyGamePath() const
{
  const QString path =
      registryString(HKEY_CURRENT_USER, "Software\\SureAI\\Enderal", "Install_Path");
  if (!path.isEmpty()) {
    return path;
  }
  return findSteamFolder("Enderal", "Data\\Enderal - Forgotten Stories.esm");
}

std::shared_ptr<const MOBase::ISaveGame> EnderalGame::makeSaveGame(const QString& path) const
{
  // the location in UTF-8, unlike Skyrim's (MO2's plugin read it so)
  return readSkyrimSaveGame(this, path,
                            {.lightPlugins = false, .locationInCodePage = false});
}

}  // namespace mo2r::bethesda
