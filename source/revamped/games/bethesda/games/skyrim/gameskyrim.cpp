// MO2 Revamped: Skyrim (2011), on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_skyrim (by Tannin and the MO2 team), which instances, Nexus and
// Wabbajack lists already know the game by.
#include "gameskyrim.h"

#include "plugins/filetimeplugins.h"
#include "saves/bethesdasavegame.h"

#include <imoinfo.h>
#include <utility.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView IniFiles[] = {u"skyrim.ini", u"skyrimprefs.ini"};
  constexpr QStringView Primary[]  = {u"skyrim.esm", u"update.esm"};
  constexpr QStringView Dlc[]      = {u"Dawnguard.esm",
                                      u"Dragonborn.esm",
                                      u"HearthFires.esm",
                                      u"HighResTexturePack01.esp",
                                      u"HighResTexturePack02.esp",
                                      u"HighResTexturePack03.esp"};

  constexpr QStringView VanillaArchives[] = {
      u"Skyrim - Misc.bsa",        u"Skyrim - Shaders.bsa",
      u"Skyrim - Textures.bsa",    u"HighResTexturePack01.bsa",
      u"HighResTexturePack02.bsa", u"HighResTexturePack03.bsa",
      u"Skyrim - Interface.bsa",   u"Skyrim - Animations.bsa",
      u"Skyrim - Meshes.bsa",      u"Skyrim - Sounds.bsa",
      u"Skyrim - Voices.bsa",      u"Skyrim - VoicesExtra.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",      u"interface",        u"menus",         u"meshes",
      u"music",      u"scripts",          u"shaders",       u"sound",
      u"strings",    u"textures",         u"trees",         u"video",
      u"facegen",    u"materials",        u"skse",          u"distantlod",
      u"asi",        u"Tools",            u"MCM",           u"distantland",
      u"mits",       u"dllplugins",       u"SkyProc Patchers", u"CalienteTools",
      u"NetScriptFramework", u"shadersfx", u"Nemesis_Engine"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"SKSE", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"SBW", u"SBW.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Skyrim", u"TESV.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Skyrim Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      {u"BOSS", u"BOSS/BOSS.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Skyrim\"", false, false},
      // Steam has the Creation Kit as an app of its own
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false,
       u"202480"}};

  // only Bethesda's key, as MO2's plugin looked (Steam's installs write it)
  constexpr Store StoreOrder[] = {Store::Bethesda};

  constexpr BoolSetting Settings[] = {
      {u"sse_downloads",
       QT_TRANSLATE_NOOP("GameSkyrim",
                         "Accept downloads from the Skyrim Special Edition section of Nexus"),
       false}};

  constexpr GameDefinition Skyrim{
      .pluginName          = u"Skyrim Support Plugin",
      .translationContext  = "GameSkyrim",
      .displayName         = u"Skyrim",
      .author              = u"Tannin & MO2 Team",
      .version             = {1, 6, 1},
      .gameName            = u"Skyrim",
      .shortName           = u"Skyrim",
      .validShortNames     = {},
      .nexusName           = u"skyrim",
      .nexusGameId         = 110,
      .nexusModOrganizerId = 0,
      .variants            = {},
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"TESV.exe",
      .launcher                  = u"SkyrimLauncher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Skyrim",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"skyrim_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      // Skyrim SE's reader reads the 2011 saves too (header version 9)
      .saveFormat    = SaveFormat::SkyrimSE,
      .saveExtension = u"ess",
      .localSavesIni = u"skyrim.ini",

      .extender = {.loader          = u"skse_loader.exe",
                   .pluginPath      = u"skse/plugins",
                   .coSaveExtension = u"skse"},
      // the game reads 255 characters of each key
      .archives   = {.iniFile                = u"skyrim.ini",
                     .listKey                = u"SResourceArchiveList",
                     .maxListLength          = 256,
                     .listKey2               = u"SResourceArchiveList2",
                     .splitLength            = 255,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Skyrim - Invalidation.bsa",
                     .invalidationBsaVersion = 0x68},
      // every content shown, as MO2's plugin had it
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = 0},
      .executables = Executables,
      .stores      = {.order       = StoreOrder,
                      .bethesdaKey = u"Software\\Bethesda Softworks\\Skyrim",
                      .steamAppId  = u"72850",
                      .steamFolder = u"Skyrim"},
      .settings    = Settings};

  // the first version that reads the order from plugins.txt
  constexpr int PluginsTxtVersion[] = {1, 4, 26};
}  // namespace

SkyrimGame::SkyrimGame() : BethesdaGame(Skyrim) {}

QStringList SkyrimGame::validShortNames() const
{
  QStringList names = BethesdaGame::validShortNames();
  if (organizer() != nullptr && organizer()->pluginSetting(name(), "sse_downloads").toBool()) {
    names.append("SkyrimSE");
  }
  return names;
}

MOBase::IPluginGame::LoadOrderMechanism SkyrimGame::loadOrderMechanism() const
{
  const QString exe = gameDirectory().absoluteFilePath(binaryName());
  // read again only when the exe changes (MO2 asks for this often)
  return m_MechanismCache.get(exe, [&exe] {
    // the exe's fixed file version; none (no exe, no version resource): file
    // times, as MO2's plugin fell back to
    const QStringList parts = MOBase::getFileVersion(exe).split('.');
    if (parts.size() < 3) {
      return LoadOrderMechanism::FileTime;
    }
    for (int i = 0; i < 3; ++i) {
      const int part = parts[i].toInt();
      if (part != PluginsTxtVersion[i]) {
        return part > PluginsTxtVersion[i] ? LoadOrderMechanism::PluginsTxt
                                           : LoadOrderMechanism::FileTime;
      }
    }
    return LoadOrderMechanism::PluginsTxt;
  });
}

void SkyrimGame::registerFeatures()
{
  BethesdaGame::registerFeatures();
  // the order always from loadorder.txt (replaces the default one)
  registerFeature(std::make_shared<FileTimeGamePlugins>(organizer(), true));
}

std::shared_ptr<const MOBase::ISaveGame> SkyrimGame::makeSaveGame(const QString& path) const
{
  return readSkyrimSaveGame(this, path,
                            {.lightPlugins = false, .locationInCodePage = true});
}

}  // namespace mo2r::bethesda
