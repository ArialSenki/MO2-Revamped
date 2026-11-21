// MO2 Revamped: Skyrim VR, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_skyrimvr (by Brixified and the MO2 team), which instances, Nexus and
// Wabbajack lists already know the game by.
#include "gameskyrimvr.h"

#include "plugins/asteriskplugins.h"

#include <imoinfo.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Skyrim", u"SkyrimSE"};
  constexpr QStringView Variants[]        = {u"Regular"};

  constexpr QStringView IniFiles[] = {u"skyrimvr.ini", u"skyrimprefs.ini"};
  // the Creation Club ones from Skyrim.ccc follow these
  constexpr QStringView Primary[] = {u"skyrim.esm",      u"update.esm",     u"dawnguard.esm",
                                     u"hearthfires.esm", u"dragonborn.esm", u"skyrimvr.esm"};
  constexpr QStringView Dlc[]     = {u"dawnguard.esm", u"hearthfires.esm", u"dragonborn.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Skyrim - Textures0.bsa", u"Skyrim - Textures1.bsa", u"Skyrim - Textures2.bsa",
      u"Skyrim - Textures3.bsa", u"Skyrim - Textures4.bsa", u"Skyrim - Textures5.bsa",
      u"Skyrim - Textures6.bsa", u"Skyrim - Textures7.bsa", u"Skyrim - Textures8.bsa",
      u"Skyrim - Meshes0.bsa",   u"Skyrim - Meshes1.bsa",   u"Skyrim - Voices_en0.bsa",
      u"Skyrim - Sounds.bsa",    u"Skyrim - Interface.bsa", u"Skyrim - Animations.bsa",
      u"Skyrim - Shaders.bsa",   u"Skyrim - Misc.bsa",      u"Skyrim - Patch.bsa",
      u"Skyrim_VR - Main.bsa"};

  constexpr QStringView DataFolders[] = {
      u"fonts",     u"interface",      u"menus",         u"meshes",
      u"music",     u"scripts",        u"shaders",       u"sound",
      u"strings",   u"textures",       u"trees",         u"video",
      u"facegen",   u"materials",      u"skse",          u"distantlod",
      u"asi",       u"Tools",          u"MCM",           u"distantland",
      u"mits",      u"dllplugins",     u"CalienteTools", u"NetScriptFramework",
      u"shadersfx", u"Nemesis_Engine", u"LightPlacer"};
  // no esl: the game loads them only with Skyrim VR ESL Support
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"bsa", u"modgroups", u"ini"};

  constexpr ExecutableDefinition Executables[] = {
      {u"SKSE", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"Skyrim VR", u"SkyrimVR.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Skyrim VR\"", false, false}};

  // only Bethesda's key, as MO2's plugin looked (Steam's installs write it)
  constexpr Store StoreOrder[] = {Store::Bethesda};

  constexpr BoolSetting Settings[] = {
      {u"enderal_downloads",
       QT_TRANSLATE_NOOP("GameSkyrimVR", "Accept downloads from the Enderal and Enderal "
                                         "Special Edition sections of Nexus"),
       false}};

  constexpr GameDefinition SkyrimVR{
      .pluginName          = u"Skyrim VR Support Plugin",
      .translationContext  = "GameSkyrimVR",
      .displayName         = u"Skyrim VR",
      .author              = u"Brixified & MO2 Team",
      .version             = {1, 5, 1},
      .gameName            = u"Skyrim VR",
      .shortName           = u"SkyrimVR",
      .validShortNames     = ValidShortNames,
      .nexusName           = {},
      .nexusGameId         = 0,
      .nexusModOrganizerId = 0,
      .variants            = Variants,
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"SkyrimVR.exe",
      // no launcher: the game itself
      .launcher                  = u"SkyrimVR.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Skyrim VR",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      // the game folder's skyrim.ini is the defaults of skyrimvr.ini
      .defaultIni                = u"skyrim.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = u"Skyrim.ccc",
      .loadOrder                 = LoadOrderKind::PluginsTxtStar,
      // with Skyrim VR ESL Support only (registerFeatures)
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::SkyrimSE,
      .saveExtension = u"ess",
      .localSavesIni = u"SkyrimVR.ini",

      .extender = {.loader          = u"sksevr_loader.exe",
                   .pluginPath      = u"skse/plugins",
                   .coSaveExtension = u"skse"},
      // the game reads 255 characters of each key; no invalidation (MO2's
      // plugin had none)
      .archives   = {.iniFile                = u"skyrimvr.ini",
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
      .stores      = {.order       = StoreOrder,
                      .bethesdaKey = u"Software\\Bethesda Softworks\\Skyrim VR",
                      .steamAppId  = u"611670",
                      .steamFolder = u"SkyrimVR"},
      .settings    = Settings};

  constexpr QStringView PrimarySources[] = {u"SkyrimSE"};

  // light plugins only with Skyrim VR ESL Support's DLL in a mod
  class SkyrimVRGamePlugins : public AsteriskGamePlugins
  {
  public:
    explicit SkyrimVRGamePlugins(MOBase::IOrganizer* organizer)
        : AsteriskGamePlugins(organizer, true), m_Organizer(organizer)
    {}

    bool lightPluginsAreSupported() override
    {
      return !m_Organizer->findFiles("skse\\plugins", {"skyrimvresl.dll"}).isEmpty();
    }

  private:
    MOBase::IOrganizer* m_Organizer;
  };
}  // namespace

SkyrimVRGame::SkyrimVRGame() : BethesdaGame(SkyrimVR) {}

QStringList SkyrimVRGame::validShortNames() const
{
  QStringList names = BethesdaGame::validShortNames();
  if (organizer() != nullptr && organizer()->pluginSetting(name(), "enderal_downloads").toBool()) {
    names.append({"Enderal", "EnderalSE"});
  }
  return names;
}

QStringList SkyrimVRGame::primarySources() const
{
  QStringList out;
  for (const QStringView s : PrimarySources) {
    out.append(s.toString());
  }
  return out;
}

QString SkyrimVRGame::steamAPPId() const
{
  return definition().stores.steamAppId.toString();
}

QStringList SkyrimVRGame::CCPlugins() const
{
  QStringList plugins = BethesdaGame::CCPlugins();
  for (QString& p : plugins) {
    p = p.toLower();
  }
  return plugins;
}

void SkyrimVRGame::registerFeatures()
{
  BethesdaGame::registerFeatures();
  // replaces the default plugin lists
  registerFeature(std::make_shared<SkyrimVRGamePlugins>(organizer()));
}

}  // namespace mo2r::bethesda
