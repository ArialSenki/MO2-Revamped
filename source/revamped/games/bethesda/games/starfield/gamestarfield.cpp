// MO2 Revamped: Starfield, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The identity, folders, lists, settings and warnings are those of MO2's
// game_starfield (by Silarn), which instances, Nexus and Wabbajack lists
// already know the game by.
#include "gamestarfield.h"

#include "starfieldfeatures.h"

#include "archives/resourcedirsinvalidation.h"
#include "extender/bethesdaextender.h"
#include "ini/inifiles.h"
#include "ini/localsavegames.h"
#include "saves/savegameinfo.h"

#include <imoinfo.h>
#include <ipluginlist.h>
#include <iprofile.h>
#include <utility.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView Variants[] = {u"Regular"};

  constexpr QStringView IniFiles[] = {u"StarfieldPrefs.ini", u"StarfieldCustom.ini"};
  // with the Shattered Space expansion and the later official content
  constexpr QStringView Primary[] = {u"Starfield.esm",  u"Constellation.esm",
                                     u"OldMars.esm",    u"BlueprintShips-Starfield.esm",
                                     u"SFBGS007.esm",   u"SFBGS008.esm",
                                     u"SFBGS006.esm",   u"SFBGS003.esm",
                                     u"SFBGS004.esm",   u"ShatteredSpace.esm"};

  constexpr QStringView VanillaArchives[] = {
      u"Starfield - Animations.ba2",        u"Starfield - DensityMaps.ba2",
      u"Starfield - FaceAnimation01.ba2",   u"Starfield - FaceAnimation02.ba2",
      u"Starfield - FaceAnimation03.ba2",   u"Starfield - FaceAnimation04.ba2",
      u"Starfield - FaceAnimationPatch.ba2", u"Starfield - FaceMeshes.ba2",
      u"Starfield - GeneratedTextures.ba2", u"Starfield - Interface.ba2",
      u"Starfield - Localization.ba2",      u"Starfield - LODMeshes.ba2",
      u"Starfield - LODMeshesPatch.ba2",    u"Starfield - LODTextures.ba2",
      u"Starfield - Materials.ba2",         u"Starfield - Meshes01.ba2",
      u"Starfield - Meshes02.ba2",          u"Starfield - MeshesPatch.ba2",
      u"Starfield - Misc.ba2",              u"Starfield - Particles.ba2",
      u"Starfield - ParticlesTestData.ba2", u"Starfield - PlanetData.ba2",
      u"Starfield - Shaders.ba2",           u"Starfield - ShadersBeta.ba2",
      u"Starfield - Terrain01.ba2",         u"Starfield - Terrain02.ba2",
      u"Starfield - Terrain03.ba2",         u"Starfield - Terrain04.ba2",
      u"Starfield - TerrainPatch.ba2",      u"Starfield - Textures01.ba2",
      u"Starfield - Textures02.ba2",        u"Starfield - Textures03.ba2",
      u"Starfield - Textures04.ba2",        u"Starfield - Textures05.ba2",
      u"Starfield - Textures06.ba2",        u"Starfield - Textures07.ba2",
      u"Starfield - Textures08.ba2",        u"Starfield - Textures09.ba2",
      u"Starfield - Textures10.ba2",        u"Starfield - Textures11.ba2",
      u"Starfield - TexturesPatch.ba2",     u"Starfield - Voices01.ba2",
      u"Starfield - Voices02.ba2",          u"Starfield - VoicesPatch.ba2",
      u"Starfield - WwiseSounds01.ba2",     u"Starfield - WwiseSounds02.ba2",
      u"Starfield - WwiseSounds03.ba2",     u"Starfield - WwiseSounds04.ba2",
      u"Starfield - WwiseSounds05.ba2",     u"Starfield - WwiseSoundsPatch.ba2",
      u"Constellation - Localization.ba2",  u"Constellation - Textures.ba2",
      u"OldMars - Localization.ba2",        u"OldMars - Textures.ba2",
      u"BlueprintShips-Starfield - Localization.ba2"};

  constexpr QStringView DataFolders[] = {
      u"interface",  u"meshes",        u"geometries", u"music", u"scripts",     u"sound",
      u"strings",    u"textures",      u"trees",      u"video", u"materials",   u"sfse",
      u"distantlod", u"asi",           u"Tools",      u"MCM",   u"distantland", u"mits",
      u"dllplugins", u"CalienteTools", u"shadersfx",  u"aaf"};
  constexpr QStringView DataExtensions[] = {u"esp",       u"esm", u"esl", u"ba2",
                                            u"modgroups", u"ini", u"csg", u"cdx"};

  constexpr ExecutableDefinition Executables[] = {
      {u"SFSE", {}, ExecutableKind::ScriptExtender, {}, false, false},
      {u"Starfield", u"Starfield.exe", ExecutableKind::GameFolder, {}, false, false},
      // Steam has the Creation Kit as an app of its own
      {u"Creation Kit", u"CreationKit.exe", ExecutableKind::GameFolder, {}, false, false,
       u"2722710"},
      {u"LOOT", {}, ExecutableKind::Loot, u"--game=\"Starfield\"", false, false}};

  // a Steam library that lists the app, then one that has the game's folder
  constexpr Store StoreOrder[] = {Store::Steam};

  constexpr BoolSetting Settings[] = {
      {u"enable_esp_warning",
       QT_TRANSLATE_NOOP("GameStarfield", "Warn when ESP plugins are active"), true},
      {u"enable_management_warnings",
       QT_TRANSLATE_NOOP("GameStarfield", "Warn when sTestFile lines in StarfieldCustom.ini "
                                          "stop the plugin list from working"),
       true},
      {u"enable_loadorder_fix",
       QT_TRANSLATE_NOOP("GameStarfield", "Keep the game's own plugins at the top of the load "
                                          "order (MO2 gives the game its own Starfield.ccc)"),
       true}};

  constexpr const char* EspShort = QT_TRANSLATE_NOOP("GameStarfield", "ESP plugins are active");
  constexpr const char* EspFull  = QT_TRANSLATE_NOOP(
      "GameStarfield",
      "<p>Starfield handles ESP plugins worse than ESM ones: they can't be sorted among ESM "
      "and master-flagged plugins, and the game keeps some of their records loaded all the "
      "time, which uses memory it needs for other things.</p>"
      "<p>If a mod has a newer version saved as ESM, use that one. Mod authors should release "
      "ESM plugins; ESL (light) plugins also work, but they have limits of their own.</p>"
      "<p>Active ESP plugins:</p><p>%1</p>");
  constexpr const char* TestFileShort =
      QT_TRANSLATE_NOOP("GameStarfield", "sTestFile lines in StarfieldCustom.ini");
  constexpr const char* TestFileFull = QT_TRANSLATE_NOOP(
      "GameStarfield",
      "<p>StarfieldCustom.ini has sTestFile lines. While they are there, the game ignores the "
      "plugin list MO2 writes, so turning plugins on or off in MO2 does nothing.</p>"
      "<p>Remove the sTestFile lines from StarfieldCustom.ini (in the profile's folder if the "
      "profile has its own INI files, otherwise in Documents\\My Games\\Starfield) and start "
      "the game again.</p>");

  constexpr GameDefinition Starfield{
      .pluginName          = u"Starfield Support Plugin",
      .translationContext  = "GameStarfield",
      .displayName         = u"Starfield",
      .author              = u"Silarn",
      .version             = {1, 1, 0},
      .gameName            = u"Starfield",
      .shortName           = u"Starfield",
      .validShortNames     = {},
      .nexusName           = u"starfield",
      .nexusGameId         = 4187,
      .nexusModOrganizerId = 0,
      .variants            = Variants,
      .variantMarkers      = {},
      .defaultVariant      = {},

      .binary                    = u"Starfield.exe",
      .launcher                  = u"StarfieldLauncher.exe",
      .dataFolder                = u"Data",  // the game's; mods go to My Games'
      .myGamesFolder             = u"Starfield",
      .variantFolders            = {},
      .listsAlsoInDefaultAppData = false,
      .profileGetsLoadOrderTxt   = false,
      .iniFiles                  = IniFiles,
      .defaultIni                = {},
      .primaryPlugins            = Primary,
      .dlcPlugins                = {},
      .ccFile                    = {},  // CCPlugins() reads it
      .loadOrder                 = LoadOrderKind::PluginsTxtStar,
      .lightPlugins              = true,
      .lootSortingSetting        = {},

      .saveFormat    = SaveFormat::Starfield,
      .saveExtension = u"sfs",
      .localSavesIni = u"StarfieldCustom.ini",

      .extender = {.loader          = u"sfse_loader.exe",
                   .pluginPath      = u"sfse/plugins",
                   .coSaveExtension = u"sfse"},
      .archives   = {.iniFile                = u"StarfieldCustom.ini",
                     .listKey                = {},
                     .maxListLength          = 4096,
                     .vanilla                = VanillaArchives,
                     .invalidationBsa        = {},
                     .invalidationBsaVersion = 0,
                     .resourceDirsIni        = u"StarfieldCustom.ini"},
      // Materials comes with Geometries and Video (starfieldfeatures)
      .dataLayout = {.folders          = DataFolders,
                     .extensions       = DataExtensions,
                     .disabledContents = contentBit(Content::SkyProc),
                     .materialsContent = false},
      .unmanaged   = {},
      .executables = Executables,
      .stores      = {.order          = StoreOrder,
                      .bethesdaKey    = {},
                      .steamAppId     = u"1716740",
                      .steamFolder    = u"Starfield",
                      .moreSteamApps  = {},
                      .steamValidFile = u"Data\\Starfield.esm"},
      .settings    = Settings};
}  // namespace

StarfieldGame::StarfieldGame() : BethesdaGame(Starfield) {}

bool StarfieldGame::init(MOBase::IOrganizer* organizer)
{
  if (!BethesdaGame::init(organizer)) {
    return false;
  }
  // the profile's Starfield.ccc follows the plugin list
  if (organizer->pluginList() != nullptr) {
    organizer->pluginList()->onRefreshed([this] {
      writeCccFile();
    });
  }
  return true;
}

void StarfieldGame::registerFeatures()
{
  // the old plugin's order
  registerFeature(std::make_shared<BethesdaScriptExtender>(this));
  registerFeature(std::make_shared<StarfieldDataArchives>(this));
  registerFeature(std::make_shared<IniLocalSavegames>(this));
  registerFeature(std::make_shared<BethesdaDataChecker>(definition()));
  registerFeature(
      std::make_shared<StarfieldDataContent>(definition(), organizer()->gameFeatures()));
  registerFeature(std::make_shared<BethesdaSaveGameInfo>(this));
  registerFeature(std::make_shared<StarfieldGamePlugins>(organizer()));
  registerFeature(std::make_shared<StarfieldUnmanagedMods>(this, localAppFolder()));
  registerFeature(std::make_shared<ResourceDirsInvalidation>(this));
}

bool StarfieldGame::loadOrderFix() const
{
  return organizer() != nullptr &&
         organizer()->pluginSetting(name(), "enable_loadorder_fix").toBool();
}

void StarfieldGame::writeCccFile() const
{
  if (organizer() == nullptr || organizer()->profilePath().isEmpty() || !loadOrderFix()) {
    return;
  }
  QFile file(organizer()->profilePath() + "/Starfield.ccc");
  if (file.open(QIODevice::WriteOnly)) {
    for (const QString& plugin : primaryPlugins()) {
      file.write(plugin.toUtf8());
      file.write("\n");
    }
  }
}

QDir StarfieldGame::dataDirectory() const
{
  const QDir data(documentsDirectory().absoluteFilePath("Data"));
  if (!data.exists()) {
    data.mkdir(data.path());
  }
  return data;
}

QMap<QString, QDir> StarfieldGame::secondaryDataDirectories() const
{
  return {{"game_data", gameDirectory().absoluteFilePath("Data")}};
}

QString StarfieldGame::steamAPPId() const
{
  return definition().stores.steamAppId.toString();
}

QStringList StarfieldGame::testFilePlugins() const
{
  MOBase::IOrganizer* o = organizer();
  if (o == nullptr || o->profile() == nullptr) {
    return {};
  }
  // ten INI reads, asked for many times per refresh: once per change of it
  const QString ini = o->profile()->absoluteIniFilePath("StarfieldCustom.ini");
  return m_TestFileCache.get(ini, [&ini] {
    QStringList plugins;
    if (!QFile::exists(ini)) {
      return plugins;
    }
    for (int i = 1; i <= 10; ++i) {
      // MAX_PATH characters, as the old plugin read them
      const QString plugin = iniValue(ini, "General", "sTestFile" + QString::number(i), "", 260);
      if (!plugin.isEmpty() && !plugins.contains(plugin)) {
        plugins.append(plugin);
      }
    }
    return plugins;
  });
}

QStringList StarfieldGame::primaryPlugins() const
{
  QStringList plugins;
  for (const QStringView p : definition().primaryPlugins) {
    plugins.append(p.toString());
  }
  if (loadOrderMechanism() == LoadOrderMechanism::None) {
    plugins << enabledPlugins();
    plugins << testFilePlugins();
  }
  return plugins;
}

QStringList StarfieldGame::CCPlugins() const
{
  QStringList plugins;
  const QStringList core = primaryPlugins() + DLCPlugins();
  if (!testFilePresent()) {
    QString path = gameDirectory().absoluteFilePath("Starfield.ccc");
    if (loadOrderFix() && !organizer()->profilePath().isEmpty()) {
      path = organizer()->profilePath() + "/Starfield.ccc";
    }
    QFile file(path);
    // a few KB with every Creation; anything far larger isn't one
    if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024) {
      while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.at(0) == '#') {
          continue;
        }
        const QString name = QString::fromUtf8(line).toLower();
        if (!plugins.contains(name, Qt::CaseInsensitive) &&
            !core.contains(name, Qt::CaseInsensitive)) {
          plugins.append(name);
        }
      }
    }
  }
  // the installed Creations, as the game lists them
  if (organizer() != nullptr) {
    for (const auto& [plugin, creation] : parseContentCatalog(localAppFolder(), gameShortName())) {
      if (!plugins.contains(plugin, Qt::CaseInsensitive)) {
        plugins.append(plugin);
      }
    }
  }
  return plugins;
}

MOBase::IPluginGame::LoadOrderMechanism StarfieldGame::loadOrderMechanism() const
{
  return testFilePresent() ? LoadOrderMechanism::None : LoadOrderMechanism::PluginsTxt;
}

MappingType StarfieldGame::mappings() const
{
  MappingType result;
  if (organizer() == nullptr) {
    return result;
  }
  const bool fix = loadOrderFix();
  if (fix) {
    writeCccFile();
  }
  if (!testFilePresent()) {
    result = BethesdaGame::mappings();
  }
  if (fix) {
    // into the game folder and My Games (LOOT reads that one)
    const QString ccc = organizer()->profilePath() + "/Starfield.ccc";
    result.push_back({ccc, gameDirectory().absolutePath() + "/Starfield.ccc", false});
    result.push_back({ccc, myGamesPath() + "/Starfield.ccc", false});
  }
  return result;
}

bool StarfieldGame::activeEsp() const
{
  m_ActiveEsps.clear();
  const QStringList esps = organizer()->findFiles("", [](const QString& fileName) {
    return fileName.endsWith(".esp", MOBase::FileNameComparator::CaseSensitivity);
  });
  for (const QString& esp : esps) {
    const QString name = QFileInfo(esp).fileName();
    if (organizer()->pluginList()->state(name) == MOBase::IPluginList::STATE_ACTIVE) {
      m_ActiveEsps.insert(name);
    }
  }
  return !m_ActiveEsps.empty();
}

std::vector<unsigned int> StarfieldGame::activeProblems() const
{
  std::vector<unsigned int> problems;
  if (organizer() == nullptr || organizer()->managedGame() != this) {
    return problems;
  }
  if (organizer()->pluginSetting(name(), "enable_esp_warning").toBool() && activeEsp()) {
    problems.push_back(ProblemEsp);
  }
  if (organizer()->pluginSetting(name(), "enable_management_warnings").toBool() &&
      testFilePresent()) {
    problems.push_back(ProblemTestFile);
  }
  return problems;
}

QString StarfieldGame::shortDescription(unsigned int key) const
{
  switch (key) {
  case ProblemEsp:
    return translate(EspShort);
  case ProblemTestFile:
    return translate(TestFileShort);
  default:
    return {};
  }
}

QString StarfieldGame::fullDescription(unsigned int key) const
{
  switch (key) {
  case ProblemEsp: {
    QStringList esps(m_ActiveEsps.begin(), m_ActiveEsps.end());
    return translate(EspFull).arg(esps.join(", "));
  }
  case ProblemTestFile:
    return translate(TestFileFull);
  default:
    return {};
  }
}

}  // namespace mo2r::bethesda
