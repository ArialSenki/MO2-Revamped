// MO2 Revamped: Nehrim, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_nehrim (by the MO2 team), which instances, Nexus and Wabbajack lists
// already know the game by.
#include "gamenehrim.h"

#include "detection/stores.h"
#include "ini/inifiles.h"

#include <executableinfo.h>

#include <QDir>
#include <QFileInfo>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Oblivion"};

  constexpr QStringView IniFiles[] = {u"oblivion.ini", u"oblivionprefs.ini"};
  constexpr QStringView Primary[]  = {u"Nehrim.esm", u"Translation.esp"};

  constexpr QStringView VanillaArchives[] = {u"N - Meshes.bsa",    u"N - Textures1.bsa",
                                             u"N - Textures2.bsa", u"N - Misc.bsa",
                                             u"N - Sounds.bsa",    u"L - Voices.bsa",
                                             u"L - Misc.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",      u"interface",     u"menus",      u"meshes",      u"music",
      u"scripts",    u"shaders",       u"sound",      u"strings",     u"textures",
      u"trees",      u"video",         u"facegen",    u"obse",        u"distantlod",
      u"asi",        u"distantland",   u"mits",       u"dllplugins",  u"CalienteTools",
      u"NetScriptFramework"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"Nehrim", u"Oblivion.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Nehrim Launcher", u"NehrimLauncher.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Oblivion Mod Manager", u"OblivionModManager.exe", ExecutableKind::GameFolder, {},
       false, false},
      {u"BOSS", u"BOSS/BOSS.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Nehrim\"", false, false},
      {u"Construction Set", u"TESConstructionSet.exe", ExecutableKind::GameFolder, {}, false,
       false}};

  constexpr Store StoreOrder[] = {Store::Bethesda};

  constexpr GameDefinition Nehrim{
      .pluginName          = u"Nehrim Support Plugin",
      .translationContext  = "GameNehrim",
      .displayName         = u"Nehrim",
      .author              = u"Tannin & MO2 Team",
      .version             = {1, 1, 1},
      .gameName            = u"Nehrim",
      .shortName           = u"Nehrim",
      .validShortNames     = ValidShortNames,
      .nexusName           = u"Nehrim",
      .nexusGameId         = 3312,
      .nexusModOrganizerId = -1,
      .variants            = {},
      .variantMarkers      = {},
      .defaultVariant      = {},

      // its own launcher tells a Nehrim folder from Oblivion's (the game's
      // icon and looksValid() come from it)
      .binary                    = u"NehrimLauncher.exe",
      .launcher                  = u"NehrimLauncher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Nehrim",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"oblivion_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = {},
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Oblivion,
      .saveExtension = u"ess",
      .localSavesIni = u"oblivion.ini",

      .extender = {.loader          = u"obse_loader.exe",
                   .pluginPath      = u"obse/plugins",
                   .coSaveExtension = u"obse"},
      .archives   = {.iniFile                = u"oblivion.ini",
                     .listKey                = u"SArchiveList",
                     .maxListLength          = 256,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Nehrim - Invalidation.bsa",
                     .invalidationBsaVersion = 0x67},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::Mcm) | contentBit(Content::SkyProc),
                     .materialsContent = false,
                     .fixablePrefix    = u"OBSE",
                     .fixFolder        = u"OBSE/Plugins"},
      .unmanaged   = {},
      .executables = Executables,
      .stores      = {.order       = StoreOrder,
                      .bethesdaKey = u"Software\\Bethesda Softworks\\Oblivion",
                      .steamAppId  = u"22330"},
      .settings    = {}};
}  // namespace

NehrimGame::NehrimGame() : BethesdaGame(Nehrim) {}

QStringList NehrimGame::primarySources() const
{
  return {"Oblivion"};
}

QList<MOBase::ExecutableForcedLoadSetting> NehrimGame::executableForcedLoads() const
{
  return {MOBase::ExecutableForcedLoadSetting("Oblivion.exe", "obse_1_2_416.dll")
              .withForced()
              .withEnabled(),
          MOBase::ExecutableForcedLoadSetting("TESConstructionSet.exe", "obse_editor_1_2.dll")
              .withForced()
              .withEnabled()};
}

void NehrimGame::initializeProfile(const QDir& directory, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    // the game writes Oblivion's (MO2 bc51fef)
    copyIfMissing(localAppFolder() + "/Oblivion", directory, "plugins.txt", "plugins.txt");
  }
  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    if (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
        !QFileInfo(m_MyGamesPath + "/oblivion.ini").exists()) {
      copyIfMissing(gameDirectory().absolutePath(), directory, "oblivion_default.ini",
                    "oblivion.ini");
    } else {
      copyIfMissing(m_MyGamesPath, directory, "oblivion.ini", "oblivion.ini");
    }
    copyIfMissing(m_MyGamesPath, directory, "oblivionprefs.ini", "oblivionprefs.ini");
  }
}

QString NehrimGame::identifyGamePath() const
{
  // the key is Oblivion's too: only a folder that has Nehrim
  const QString path = findBethesdaInstall(definition().stores.bethesdaKey.toString());
  if (path.isEmpty() || !QFileInfo::exists(QDir(path).filePath(binaryName()))) {
    return {};
  }
  return path;
}

}  // namespace mo2r::bethesda
