// MO2 Revamped: Morrowind, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_morrowind (by Schilduin and the MO2 team), which instances, Nexus and Wabbajack lists already know the
// game by.
#include "gamemorrowind.h"

#include "morrowindarchives.h"
#include "morrowindlocalsaves.h"
#include "morrowindplugins.h"
#include "morrowindsavegame.h"

#include "archives/bsainvalidation.h"
#include "content/datachecker.h"
#include "content/unmanagedmods.h"
#include "detection/stores.h"
#include "ini/inifiles.h"

#include <imoinfo.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView IniFiles[] = {u"morrowind.ini"};
  constexpr QStringView Primary[]  = {u"morrowind.esm"};
  constexpr QStringView Dlc[]      = {u"Tribunal.esm", u"Bloodmoon.esm"};

  constexpr QStringView VanillaArchives[] = {u"Morrowind.bsa"};

  constexpr QStringView DataFolders[] = {u"fonts",       u"meshes", u"music",   u"shaders",
                                         u"sound",       u"textures", u"video", u"mwse",
                                         u"distantland", u"mits",   u"icons",   u"bookart",
                                         u"splash"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups"};

  constexpr ExecutableDefinition Executables[] = {
      {u"MWSE (Launcher Method)", u"MWSE Launcher.exe", ExecutableKind::GameFolder, {}, false,
       false},
      {u"Morrowind", u"Morrowind.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Morrowind Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      {u"MGE XE", u"MGEXEgui.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Morrowind\"", false, false}};

  constexpr Store StoreOrder[] = {Store::Bethesda, Store::Steam};

  constexpr GameDefinition Morrowind{
      .pluginName          = u"Morrowind Support Plugin",
      .translationContext  = "GameMorrowind",
      .displayName         = u"Morrowind",
      .author              = u"Schilduin & MO2 Team",
      .version             = {1, 5, 1},
      .gameName            = u"Morrowind",
      .shortName           = u"Morrowind",
      .validShortNames     = {},
      .nexusName           = u"Morrowind",
      .nexusGameId         = 100,
      .nexusModOrganizerId = 1334,
      .variants            = {},
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"Morrowind.exe",
      .launcher                  = u"Morrowind Launcher.exe",
      .dataFolder                = u"Data Files",
      // the AppData folder with plugins.txt (the game itself has no My Games)
      .myGamesFolder             = u"Morrowind",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = {},
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Custom,
      .saveExtension = u"ess",
      .localSavesIni = {},

      // no extender MO2 starts (MWSE has its launcher); the old plugin named
      // MWSE's co-saves all the same
      .extender = {.loader = {}, .pluginPath = {}, .coSaveExtension = u"mwse"},
      // the list itself is morrowindarchives' ([Archives] in Morrowind.ini)
      .archives   = {.iniFile                = u"morrowind.ini",
                     .listKey                = {},
                     .maxListLength          = 256,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Morrowind - Invalidation.bsa",
                     .invalidationBsaVersion = 0x68},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::Mcm) | contentBit(Content::SkyProc) |
                                         contentBit(Content::Interface) |
                                         contentBit(Content::Script),
                     .materialsContent = false},
      .unmanaged   = {},
      .executables = Executables,
      .stores      = {.order          = StoreOrder,
                      .bethesdaKey    = u"Software\\Bethesda Softworks\\Morrowind",
                      .steamAppId     = u"22320",
                      .steamFolder    = u"Morrowind",
                      .moreSteamApps  = {},
                      .steamValidFile = u"Data Files\\Morrowind.esm"},
      .settings    = {}};
}  // namespace

MorrowindGame::MorrowindGame() : BethesdaGame(Morrowind) {}

QDir MorrowindGame::documentsDirectory() const
{
  return gameDirectory();
}

QDir MorrowindGame::savesDirectory() const
{
  return QDir(gameDirectory().absoluteFilePath("Saves"));
}

void MorrowindGame::initializeProfile(const QDir& directory, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyIfMissing(localAppFolder() + "/Morrowind", directory, "plugins.txt", "plugins.txt");
  }
  if (settings.testFlag(IPluginGame::CONFIGURATION)) {
    copyIfMissing(gameDirectory().absolutePath(), directory, "Morrowind.ini", "Morrowind.ini");
  }
}

void MorrowindGame::registerFeatures()
{
  // the old plugin's order
  auto archives = std::make_shared<MorrowindDataArchives>(this);
  registerFeature(archives);
  registerFeature(std::make_shared<DummyBsaInvalidation>(archives.get(), this));
  registerFeature(std::make_shared<MorrowindSaveGameInfo>(this));
  registerFeature(std::make_shared<MorrowindLocalSavegames>(this));
  registerFeature(std::make_shared<BethesdaDataChecker>(definition()));
  registerFeature(
      std::make_shared<BethesdaDataContent>(definition(), organizer()->gameFeatures()));
  registerFeature(std::make_shared<MorrowindGamePlugins>(organizer()));
  registerFeature(std::make_shared<DataFolderMods>(this));
}

QString MorrowindGame::identifyGamePath() const
{
  // as the old plugin looked: no Steam library list, its folders directly
  const StoreLocations& s = definition().stores;
  QString path            = findBethesdaInstall(s.bethesdaKey.toString());
  if (path.isEmpty()) {
    path = findSteamFolder(s.steamFolder.toString(), s.steamValidFile.toString());
  }
  return path;
}

std::shared_ptr<const MOBase::ISaveGame> MorrowindGame::makeSaveGame(const QString& path) const
{
  return MorrowindSaveGame::read(this, path);
}

}  // namespace mo2r::bethesda
