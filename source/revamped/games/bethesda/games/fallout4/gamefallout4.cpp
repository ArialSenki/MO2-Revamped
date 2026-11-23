// MO2 Revamped: Fallout 4, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists and store locations are those of MO2's
// game_fallout4 (by Tannin and the MO2 team),
// which instances, Nexus and Wabbajack lists already know the game by.
#include "gamefallout4.h"

#include "ini/inifiles.h"

#include <imoinfo.h>
#include <iprofile.h>

#include <QFile>
#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView Variants[] = {u"Regular"};

  constexpr QStringView IniFiles[] = {u"fallout4.ini", u"fallout4prefs.ini",
                                      u"fallout4custom.ini"};
  // the Creation Club ones from Fallout4.ccc follow these
  constexpr QStringView Primary[] = {u"fallout4.esm",      u"dlcrobot.esm",
                                     u"dlcworkshop01.esm", u"dlccoast.esm",
                                     u"dlcworkshop02.esm", u"dlcworkshop03.esm",
                                     u"dlcnukaworld.esm",  u"dlcultrahighresolution.esm"};
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
      u"Fallout4 - Misc.ba2"};

  // MO2's list and MO2 Revamped's: previs (Vis), SEQ files and LOD settings
  constexpr QStringView DataFolders[] = {
      u"interface",     u"meshes",    u"music", u"scripts",     u"sound", u"strings",
      u"textures",      u"trees",     u"video", u"materials",   u"f4se",  u"distantlod",
      u"asi",           u"Tools",     u"MCM",   u"distantland", u"mits",  u"dllplugins",
      u"CalienteTools", u"shadersfx", u"aaf",   u"Vis",         u"Seq",   u"LODSettings"};
  constexpr QStringView DataExtensions[] = {u"esp",       u"esm", u"esl", u"ba2",
                                            u"modgroups", u"ini", u"csg", u"cdx"};

  // unlike the earlier games, the DLC's file names aren't their names
  constexpr ModDisplayName DlcNames[] = {
      {u"dlcrobot", u"Automatron"},
      {u"dlcworkshop01", u"Wasteland Workshop"},
      {u"dlccoast", u"Far Harbor"},
      {u"dlcworkshop02", u"Contraptions Workshop"},
      {u"dlcworkshop03", u"Vault-Tec Workshop"},
      {u"dlcnukaworld", u"Nuka-World"},
      {u"dlcultrahighresolution", u"Ultra High Resolution Texture Pack"}};

  constexpr ExecutableDefinition Executables[] = {
      {u"F4SE", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"Fallout 4", u"Fallout4.exe", ExecutableKind::GameFolder, {}, false, false},
      {u"Fallout Launcher", {}, ExecutableKind::Launcher, {}, false, false},
      // Steam has the Creation Kit as an app of its own
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false,
       u"1946160"},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Fallout4\"", false, false}};

  // Bethesda's key (Steam's installs write it), GOG's Game of the Year
  // edition, then a Steam library without the key (Windows reinstalled, the
  // library moved)
  constexpr Store StoreOrder[]   = {Store::Bethesda, Store::Gog, Store::Steam};
  constexpr QStringView GogIds[] = {u"1998527297"};

  // F4SE's Address Library has only "version-" files, for the old and the
  // next-gen runtimes alike
  constexpr QStringView AddressLibraryPrefixes[] = {u"version-"};

  constexpr BoolSetting Settings[] = {
      {u"script_extender_checks",
       QT_TRANSLATE_NOOP("GameFallout4", "Warn when F4SE doesn't match the game version or "
                                         "the Address Library is missing"),
       true}};

  constexpr ExtenderProblemTexts ExtenderTexts{
      QT_TRANSLATE_NOOP("GameFallout4", "F4SE doesn't match the game version"),
      QT_TRANSLATE_NOOP("GameFallout4",
                        "<p>The game folder has F4SE's loader, but not %1, the F4SE runtime "
                        "for this game version (%2). Started through F4SE, the game won't "
                        "run, or will run without F4SE and its plugins.</p>"
                        "<p>Install the F4SE build for this game version from its official "
                        "site into the game folder, or change the game back to the version "
                        "that matches your F4SE.</p>"),
      QT_TRANSLATE_NOOP("GameFallout4", "Address Library for F4SE Plugins is missing"),
      QT_TRANSLATE_NOOP("GameFallout4",
                        "<p>Some active mods have F4SE plugins, but no mod has %1, the "
                        "Address Library for this game version (%2). Most F4SE plugins "
                        "need it, and without it they don't load or the game closes at "
                        "start.</p>"
                        "<p>Install \"Address Library for F4SE Plugins\" from Nexus (the "
                        "file for your game version) and activate it.</p>")};

  constexpr const char* TestFileShort =
      QT_TRANSLATE_NOOP("GameFallout4", "sTestFile lines in Fallout4Custom.ini");
  constexpr const char* TestFileFull = QT_TRANSLATE_NOOP(
      "GameFallout4",
      "<p>Fallout4Custom.ini has sTestFile lines. While they are there, the game ignores "
      "the plugin list MO2 writes, so turning plugins on or off in MO2 does nothing.</p>"
      "<p>Remove the sTestFile lines from Fallout4Custom.ini (in the profile's folder if "
      "the profile has its own INI files, otherwise in Documents\\My Games\\Fallout4) and "
      "start the game again.</p>");
  constexpr GameDefinition Fallout4{
      .pluginName          = u"Fallout 4 Support Plugin",
      .translationContext  = "GameFallout4",
      .displayName         = u"Fallout 4",
      .author              = u"Tannin & MO2 Team",
      .version             = {1, 8, 0},
      .gameName            = u"Fallout 4",
      .shortName           = u"Fallout4",
      .validShortNames     = {},
      .nexusName           = u"fallout4",
      .nexusGameId         = 1151,
      .nexusModOrganizerId = 28715,
      .variants            = Variants,
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"Fallout4.exe",
      .launcher                  = u"Fallout4Launcher.exe",
      .dataFolder                = u"data",
      .myGamesFolder             = u"Fallout4",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = u"fallout4_default.ini",
      .primaryPlugins            = Primary,
      .dlcPlugins                = Dlc,
      .ccFile                    = u"Fallout4.ccc",
      .loadOrder                 = LoadOrderKind::PluginsTxtStar,
      .lightPlugins              = true,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Fallout4,
      .saveExtension = u"fos",
      .localSavesIni = u"fallout4custom.ini",

      .extender = {.loader                 = u"f4se_loader.exe",
                   .pluginPath             = u"f4se/plugins",
                   .coSaveExtension        = u"f4se",
                   .runtimePrefix          = u"f4se_",
                   .addressLibraryFolder   = u"F4SE/Plugins",
                   .addressLibraryPrefixes = AddressLibraryPrefixes},
      // the game reads 255 characters of each key
      .archives   = {.iniFile                = u"fallout4.ini",
                     .listKey                = u"SResourceArchiveList",
                     .maxListLength          = 256,
                     .listKey2               = u"SResourceArchiveList2",
                     .splitLength            = 255,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = {},
                     .invalidationBsaVersion = 0,
                     .resourceDirsIni        = u"Fallout4Custom.ini"},
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::SkyProc),
                     .materialsContent = true},
      .unmanaged   = {.archiveExtension = u".ba2", .displayNames = DlcNames},
      .executables = Executables,
      .stores      = {.order          = StoreOrder,
                      .bethesdaKey    = u"Software\\Bethesda Softworks\\Fallout4",
                      .steamAppId     = u"377160",
                      .steamFolder    = u"Fallout 4",
                      .moreSteamApps  = {},
                      .steamValidFile = u"Data\\Fallout4.esm",
                      .gogIds         = GogIds,
                      .epicAppNames   = {},
                      .epicSubfolder  = {}},
      .settings    = Settings};
}  // namespace

Fallout4Game::Fallout4Game()
    : BethesdaGame(Fallout4),
      m_ExtenderDiagnose(this, ProblemExtenderVersion, ProblemAddressLibrary, ExtenderTexts)
{}

QString Fallout4Game::steamAPPId() const
{
  return definition().stores.steamAppId.toString();
}

QStringList Fallout4Game::testFilePlugins() const
{
  MOBase::IOrganizer* o = organizer();
  if (o == nullptr || o->profile() == nullptr) {
    return {};
  }
  // ten INI reads, asked for many times per refresh: once per change of it
  const QString ini = o->profile()->absoluteIniFilePath("Fallout4Custom.ini");
  return m_TestFileCache.get(ini, [&ini] {
    QStringList plugins;
    if (!QFile::exists(ini)) {
      return plugins;
    }
    for (int i = 1; i <= 10; ++i) {
      // MAX_PATH characters, as the old plugin (and the game) read them
      const QString plugin =
          iniValue(ini, "General", "sTestFile" + QString::number(i), "", 260);
      if (!plugin.isEmpty() && !plugins.contains(plugin)) {
        plugins.append(plugin);
      }
    }
    return plugins;
  });
}

QStringList Fallout4Game::primaryPlugins() const
{
  QStringList plugins;
  for (const QStringView p : definition().primaryPlugins) {
    plugins.append(p.toString());
  }
  const QStringList testPlugins = testFilePlugins();
  plugins.append(testPlugins.isEmpty() ? CCPlugins() : testPlugins);
  return plugins;
}

QStringList Fallout4Game::CCPlugins() const
{
  // in lower case, as the old plugin listed them (each once already)
  QStringList plugins = BethesdaGame::CCPlugins();
  for (QString& p : plugins) {
    p = p.toLower();
  }
  return plugins;
}

MOBase::IPluginGame::SortMechanism Fallout4Game::sortMechanism() const
{
  return testFilePresent() ? SortMechanism::NONE : SortMechanism::LOOT;
}

MOBase::IPluginGame::LoadOrderMechanism Fallout4Game::loadOrderMechanism() const
{
  return testFilePresent() ? LoadOrderMechanism::None : LoadOrderMechanism::PluginsTxt;
}

MappingType Fallout4Game::mappings() const
{
  if (testFilePresent()) {
    return {};
  }
  return BethesdaGame::mappings();
}

std::vector<unsigned int> Fallout4Game::activeProblems() const
{
  std::vector<unsigned int> problems;
  if (organizer() == nullptr || organizer()->managedGame() != this) {
    return problems;
  }
  if (testFilePresent()) {
    problems.push_back(ProblemTestFile);
  }
  m_ExtenderDiagnose.addActiveProblems(problems);
  return problems;
}

QString Fallout4Game::shortDescription(unsigned int key) const
{
  if (key == ProblemTestFile) {
    return translate(TestFileShort);
  }
  return m_ExtenderDiagnose.shortDescription(key);
}

QString Fallout4Game::fullDescription(unsigned int key) const
{
  if (key == ProblemTestFile) {
    return translate(TestFileFull);
  }
  return m_ExtenderDiagnose.fullDescription(key);
}

}  // namespace mo2r::bethesda
