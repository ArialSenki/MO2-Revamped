// MO2 Revamped: Oblivion, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_oblivion (by Tannin and the MO2 team), which instances, Nexus and
// Wabbajack lists already know the game by.
#include "gameoblivion.h"

#include <executableinfo.h>
#include <imoinfo.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView IniFiles[] = {u"oblivion.ini", u"oblivionprefs.ini"};
  constexpr QStringView Primary[]  = {u"oblivion.esm", u"update.esm"};
  constexpr QStringView Dlc[]      = {
      u"DLCBattlehornCastle.esp", u"DLCShiveringIsles.esp", u"Knights.esp",
      u"DLCFrostcrag.esp",        u"DLCSpellTomes.esp",     u"DLCMehrunesRazor.esp",
      u"DLCOrrery.esp",           u"DLCThievesDen.esp",     u"DLCVileLair.esp",
      u"DLCHorseArmor.esp"};

  constexpr QStringView VanillaArchives[] = {
      u"Oblivion - Misc.bsa",   u"Oblivion - Textures - Compressed.bsa",
      u"Oblivion - Meshes.bsa", u"Oblivion - Sounds.bsa",
      u"Oblivion - Voices1.bsa", u"Oblivion - Voices2.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",      u"interface",     u"menus",      u"meshes",      u"music",
      u"scripts",    u"shaders",       u"sound",      u"strings",     u"textures",
      u"trees",      u"video",         u"facegen",    u"obse",        u"distantlod",
      u"asi",        u"distantland",   u"mits",       u"dllplugins",  u"CalienteTools",
      u"NetScriptFramework"};
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"Oblivion", u"Oblivion.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Oblivion Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      {u"Oblivion Mod Manager", u"OblivionModManager.exe", ExecutableKind::GameFolder, {},
       false, false},
      {u"BOSS", u"BOSS/BOSS.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Oblivion\"", false, false},
      {u"Construction Set", u"TESConstructionSet.exe", ExecutableKind::GameFolder, {}, false,
       false}};

  // only Bethesda's key, as the old plugin looked (Steam's and GOG's installs
  // write it when the game first runs from its launcher)
  constexpr Store StoreOrder[] = {Store::Bethesda};

  constexpr BoolSetting Settings[] = {
      {u"nehrim_downloads",
       QT_TRANSLATE_NOOP("GameOblivion", "Accept downloads from the Nehrim section of Nexus"),
       false}};

  constexpr GameDefinition Oblivion{
      .pluginName          = u"Oblivion Support Plugin",
      .translationContext  = "GameOblivion",
      .displayName         = u"Oblivion",
      .author              = u"Tannin & MO2 Team",
      .version             = {1, 6, 1},
      .gameName            = u"Oblivion",
      .shortName           = u"Oblivion",
      .validShortNames     = {},
      .nexusName           = u"Oblivion",
      .nexusGameId         = 101,
      .nexusModOrganizerId = 38277,
      .variants            = {},
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"Oblivion.exe",
      .launcher                  = u"OblivionLauncher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Oblivion",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"oblivion_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = {},
      .loadOrder                 = LoadOrderKind::FileTime,
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Oblivion,
      .saveExtension = u"ess",
      .localSavesIni = u"oblivion.ini",

      // OBSE writes a .obse co-save next to each save
      .extender = {.loader          = u"obse_loader.exe",
                   .pluginPath      = u"obse/plugins",
                   .coSaveExtension = u"obse"},
      // 255 characters of the list, as MO2 has always read it
      .archives   = {.iniFile                = u"oblivion.ini",
                     .listKey                = u"SArchiveList",
                     .maxListLength          = 256,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = u"Oblivion - Invalidation.bsa",
                     .invalidationBsaVersion = 0x67},
      // an archive of OBSE plugin DLLs alone goes into OBSE/Plugins
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
      .settings    = Settings};
}  // namespace

OblivionGame::OblivionGame() : BethesdaGame(Oblivion) {}

QStringList OblivionGame::validShortNames() const
{
  QStringList names;
  if (organizer() != nullptr && organizer()->pluginSetting(name(), "nehrim_downloads").toBool()) {
    names.append("Nehrim");
  }
  return names;
}

QList<MOBase::ExecutableForcedLoadSetting> OblivionGame::executableForcedLoads() const
{
  return {MOBase::ExecutableForcedLoadSetting("Oblivion.exe", "obse_1_2_416.dll")
              .withForced()
              .withEnabled(),
          MOBase::ExecutableForcedLoadSetting("TESConstructionSet.exe", "obse_editor_1_2.dll")
              .withForced()
              .withEnabled()};
}

}  // namespace mo2r::bethesda
