// MO2 Revamped: Fallout 4 VR, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_fallout4vr (by the MO2 contributors; splash screen by nekoyoubi),
// which instances, Nexus and Wabbajack lists already know the game by.
#include "gamefallout4vr.h"

#include "plugins/asteriskplugins.h"

#include <imoinfo.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView ValidShortNames[] = {u"Fallout4"};
  constexpr QStringView Variants[]        = {u"Regular"};

  constexpr QStringView IniFiles[] = {u"fallout4.ini", u"fallout4custom.ini",
                                      u"fallout4prefs.ini"};
  // the Creation Club ones from Fallout4.ccc follow these
  constexpr QStringView Primary[] = {u"fallout4.esm", u"fallout4_vr.esm"};
  constexpr QStringView Dlc[]     = {u"dlcrobot.esm",      u"dlcworkshop01.esm",
                                     u"dlccoast.esm",      u"dlcworkshop02.esm",
                                     u"dlcworkshop03.esm", u"dlcnukaworld.esm",
                                     u"dlcultrahighresolution.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Fallout4 - Textures1.ba2",   u"Fallout4 - Textures2.ba2",
      u"Fallout4 - Textures3.ba2",   u"Fallout4 - Textures4.ba2",
      u"Fallout4 - Textures5.ba2",   u"Fallout4 - Textures6.ba2",
      u"Fallout4 - Textures7.ba2",   u"Fallout4 - Textures8.ba2",
      u"Fallout4 - Textures9.ba2",   u"Fallout4 - Meshes.ba2",
      u"Fallout4 - MeshesExtra.ba2", u"Fallout4 - Voices.ba2",
      u"Fallout4 - Sounds.ba2",      u"Fallout4 - Interface.ba2",
      u"Fallout4 - Animations.ba2",  u"Fallout4 - Materials.ba2",
      u"Fallout4 - Shaders.ba2",     u"Fallout4 - Startup.ba2",
      u"Fallout4 - Misc.ba2",        u"Fallout4_VR - Main.ba2",
      u"Fallout4_VR - Shaders.ba2",  u"Fallout4_VR - Textures.ba2"};

  constexpr QStringView DataFolders[] = {
      u"interface", u"meshes",     u"music",         u"scripts",
      u"sound",     u"strings",    u"textures",      u"trees",
      u"video",     u"materials",  u"f4se",          u"distantlod",
      u"asi",       u"Tools",      u"MCM",           u"distantland",
      u"mits",      u"dllplugins", u"CalienteTools", u"NetScriptFramework",
      u"shadersfx", u"aaf"};
  // no esl: the game loads them only with Fallout 4 VR ESL Support
  constexpr QStringView DataExtensions[] = {u"esp", u"esm", u"ba2", u"modgroups", u"ini"};

  // unlike the earlier games, the DLC's file names aren't their names
  constexpr ModDisplayName DlcNames[] = {
      {u"dlcrobot", u"Automatron"},
      {u"dlcworkshop01", u"Wasteland Workshop"},
      {u"dlccoast", u"Far Harbor"},
      {u"dlcworkshop02", u"Contraptions Workshop"},
      {u"dlcworkshop03", u"Vault-Tec Workshop"},
      {u"dlcnukaworld", u"Nuka-World"},
      {u"dlcultrahighresolution", u"Ultra High Resolution Texture Pack"}};

  // no F4SE VR entry: MO2's plugin had none (nor the extender feature)
  constexpr ExecutableDefinition Executables[] = {
      {u"Fallout 4 VR", u"Fallout4VR.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Fallout4VR\"", false, false}};

  // only Bethesda's key, as MO2's plugin looked (Steam's installs write it)
  constexpr Store StoreOrder[] = {Store::Bethesda};

  constexpr const char* Description =
      QT_TRANSLATE_NOOP("GameFallout4VR", "Adds support for %1.\nSplash screen by %2.");

  constexpr GameDefinition Fallout4VR{
      .pluginName          = u"Fallout 4 VR Support Plugin",
      .translationContext  = "GameFallout4VR",
      .displayName         = u"Fallout 4 VR",
      // sic, as the old plugin had it
      .author              = u"MO2 Contibutors",
      .version             = {1, 6, 1},
      .gameName            = u"Fallout 4 VR",
      .shortName           = u"Fallout4VR",
      .validShortNames     = ValidShortNames,
      .nexusName           = {},
      .nexusGameId         = 0,
      .nexusModOrganizerId = 0,
      .variants            = Variants,
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"Fallout4VR.exe",
      // no launcher: the game itself
      .launcher                  = u"Fallout4VR.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Fallout4VR",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      // the game folder's fallout4.ini holds its defaults
      .defaultIni                = u"fallout4.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = u"Fallout4.ccc",
      .loadOrder                 = LoadOrderKind::PluginsTxtStar,
      // with Fallout 4 VR ESL Support only (registerFeatures)
      .lightPlugins              = false,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Fallout4,
      .saveExtension = u"fos",
      .localSavesIni = u"fallout4custom.ini",

      // no loader: MO2's plugin registered no script extender; the co-save
      // still goes with its save
      .extender = {.loader = {}, .pluginPath = u"f4se/plugins", .coSaveExtension = u"f4se"},
      // the game reads 255 characters of each key; no invalidation (MO2's
      // plugin had none)
      .archives   = {.iniFile                = u"fallout4.ini",
                     .listKey                = u"SResourceArchiveList",
                     .maxListLength          = 256,
                     .listKey2               = u"SResourceArchiveList2",
                     .splitLength            = 255,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = {},
                     .invalidationBsaVersion = 0},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::SkyProc),
                     .materialsContent = true},
      .unmanaged   = {.archiveExtension = u".ba2", .displayNames = DlcNames},
      .executables = Executables,
      .stores      = {.order       = StoreOrder,
                      .bethesdaKey = u"Software\\Bethesda Softworks\\Fallout 4 VR",
                      .steamAppId  = u"611660",
                      .steamFolder = u"Fallout 4 VR"},
      .settings    = {}};

  // light plugins only with Fallout 4 VR ESL Support's (or Daytripper's) DLL
  // in a mod
  class Fallout4VRGamePlugins : public AsteriskGamePlugins
  {
  public:
    explicit Fallout4VRGamePlugins(MOBase::IOrganizer* organizer)
        : AsteriskGamePlugins(organizer, true), m_Organizer(organizer)
    {}

    bool lightPluginsAreSupported() override
    {
      return !m_Organizer->findFiles("f4se\\plugins", {"falloutvresl.dll", "Daytripper4.dll"})
                  .isEmpty();
    }

  private:
    MOBase::IOrganizer* m_Organizer;
  };
}  // namespace

Fallout4VRGame::Fallout4VRGame() : BethesdaGame(Fallout4VR) {}

QString Fallout4VRGame::description() const
{
  return translate(Description).arg(definition().displayName.toString()).arg("nekoyoubi");
}

QString Fallout4VRGame::steamAPPId() const
{
  return definition().stores.steamAppId.toString();
}

QStringList Fallout4VRGame::CCPlugins() const
{
  QStringList plugins = BethesdaGame::CCPlugins();
  for (QString& p : plugins) {
    p = p.toLower();
  }
  return plugins;
}

void Fallout4VRGame::registerFeatures()
{
  BethesdaGame::registerFeatures();
  // replaces the default plugin lists
  registerFeature(std::make_shared<Fallout4VRGamePlugins>(organizer()));
}

}  // namespace mo2r::bethesda
