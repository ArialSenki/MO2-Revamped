// MO2 Revamped: Bethesda game framework, MO2's game plugin interface.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// Return values and their timing follow MO2's Gamebryo game plugins
// (game_gamebryo and the game_* plugins built on it), which this replaces.
#include "bethesdagame.h"

#include "archives/bsainvalidation.h"
#include "archives/iniarchives.h"
#include "archives/resourcedirsinvalidation.h"
#include "content/datachecker.h"
#include "content/unmanagedmods.h"
#include "detection/stores.h"
#include "extender/bethesdaextender.h"
#include "ini/inifiles.h"
#include "ini/localsavegames.h"
#include "plugins/asteriskplugins.h"
#include "plugins/filetimeplugins.h"
#include "plugins/loadorderfiles.h"
#include "saves/bethesdasavegame.h"
#include "saves/savegameinfo.h"
#include "util/peheader.h"
#include "util/winfolders.h"

#include <executableinfo.h>
#include <igamefeatures.h>
#include <scriptextender.h>
#include <imoinfo.h>
#include <iprofile.h>
#include <log.h>
#include <pluginsetting.h>
#include <utility.h>
#include <versioninfo.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>

#include <cwchar>

namespace mo2r::bethesda
{

namespace
{
  QStringList toList(Names names)
  {
    QStringList out;
    out.reserve(static_cast<qsizetype>(names.size()));
    for (const QStringView n : names) {
      out.append(n.toString());
    }
    return out;
  }

  // a game exe whose file version is this placeholder has its real version
  // as the product version only (MO2's rule)
  constexpr QStringView FallbackVersion = u"1.0.0";
  constexpr QStringView Epic            = u"Epic Games";
}  // namespace

BethesdaGame::BethesdaGame(const GameDefinition& definition) : m_Def(definition) {}

QString BethesdaGame::translate(const char* source) const
{
  return QCoreApplication::translate(m_Def.translationContext, source);
}

QString BethesdaGame::variantFolder() const
{
  for (const VariantFolder& v : m_Def.variantFolders) {
    if (m_GameVariant == v.variant) {
      return v.folder.toString();
    }
  }
  return m_Def.myGamesFolder.toString();
}

QFileInfo BethesdaGame::findInGameFolder(const QString& relativePath) const
{
  return QFileInfo(m_GamePath + "/" + relativePath);
}

std::uint16_t BethesdaGame::executableArch(const QString& relativePath) const
{
  return peMachineOfFile(gameDirectory().absoluteFilePath(relativePath));
}

QString BethesdaGame::localAppFolder()
{
  return localAppData();
}

QString BethesdaGame::lootPath()
{
  // LOOT's installer key (per user, then per machine), then its own old key
  const QString uninstall = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                            "{BF634210-A0D4-443F-A657-0DCE38040374}_is1";
  QString path = registryString(HKEY_CURRENT_USER, uninstall, "InstallLocation");
  if (path.isEmpty()) {
    path = registryString(HKEY_LOCAL_MACHINE, uninstall, "InstallLocation");
  }
  if (path.isEmpty()) {
    path = registryString(HKEY_LOCAL_MACHINE, "Software\\LOOT", "Installed Path");
  }
  return path + "/Loot.exe";
}

// --- IPlugin ---

bool BethesdaGame::init(MOBase::IOrganizer* organizer)
{
  m_Organizer = organizer;
  m_Organizer->onAboutToRun([this](const QString& executable) {
    return prepareIni(executable);
  });
  registerFeatures();
  return true;
}

QString BethesdaGame::name() const
{
  return m_Def.pluginName.toString();
}

QString BethesdaGame::localizedName() const
{
  return translate("%1 support").arg(m_Def.displayName.toString());
}

QString BethesdaGame::author() const
{
  return m_Def.author.toString();
}

QString BethesdaGame::description() const
{
  return translate("Adds support for %1.").arg(m_Def.displayName.toString());
}

MOBase::VersionInfo BethesdaGame::version() const
{
  return MOBase::VersionInfo(m_Def.version[0], m_Def.version[1], m_Def.version[2],
                             MOBase::VersionInfo::RELEASE_FINAL);
}

QList<MOBase::PluginSetting> BethesdaGame::settings() const
{
  QList<MOBase::PluginSetting> out;
  for (const BoolSetting& s : m_Def.settings) {
    out.append(MOBase::PluginSetting(s.key.toString(), translate(s.description),
                                     s.defaultValue));
  }
  return out;
}

void BethesdaGame::registerFeatures()
{
  // the order MO2's Gamebryo plugins register them in
  if (!m_Def.extender.loader.isEmpty()) {
    registerFeature(std::make_shared<BethesdaScriptExtender>(this));
  }
  auto archives = std::make_shared<IniDataArchives>(this);
  registerFeature(archives);
  if (!m_Def.archives.invalidationBsa.isEmpty()) {
    registerFeature(std::make_shared<DummyBsaInvalidation>(archives.get(), this));
  } else if (!m_Def.archives.resourceDirsIni.isEmpty()) {
    registerFeature(std::make_shared<ResourceDirsInvalidation>(this));
  }
  registerFeature(std::make_shared<BethesdaSaveGameInfo>(this));
  registerFeature(std::make_shared<IniLocalSavegames>(this));
  registerFeature(std::make_shared<BethesdaDataChecker>(m_Def));
  registerFeature(std::make_shared<BethesdaDataContent>(m_Def, m_Organizer->gameFeatures()));
  switch (m_Def.loadOrder) {
  case LoadOrderKind::FileTime:
    registerFeature(std::make_shared<FileTimeGamePlugins>(m_Organizer));
    break;
  case LoadOrderKind::PluginsTxtStar:
    registerFeature(std::make_shared<AsteriskGamePlugins>(m_Organizer, m_Def.lightPlugins));
    break;
  }
  registerFeature(std::make_shared<DataFolderMods>(this));
}

void BethesdaGame::registerFeature(std::shared_ptr<MOBase::GameFeature> feature)
{
  // a game plugin's features have the lowest priority anyway
  m_Organizer->gameFeatures()->registerFeature(this, feature, 0, true);
}

// --- detection ---

QString BethesdaGame::identifyGamePath() const
{
  const StoreLocations& s = m_Def.stores;
  for (const Store store : s.order) {
    QString path;
    switch (store) {
    case Store::Bethesda:
      path = findBethesdaInstall(s.bethesdaKey.toString());
      break;
    case Store::Gog:
      for (const QStringView id : s.gogIds) {
        path = findGogInstall(id.toString());
        if (!path.isEmpty()) {
          break;
        }
      }
      break;
    case Store::Steam:
      path = findSteamInstall(s.steamAppId.toString(), s.steamFolder.toString());
      if (path.isEmpty() && !s.steamValidFile.isEmpty()) {
        path = findSteamFolder(s.steamFolder.toString(), s.steamValidFile.toString());
      }
      for (const SteamApp& app : s.moreSteamApps) {
        if (!path.isEmpty()) {
          break;
        }
        path = findSteamInstall(app.appId.toString(), app.folder.toString());
      }
      break;
    case Store::Epic:
      path = findEpicInstall(toList(s.epicAppNames));
      // the install location holds the game in a subfolder
      if (!s.epicSubfolder.isEmpty() && QFileInfo(path).isDir()) {
        const QDir location(path);
        const QStringList sub =
            location.entryList({s.epicSubfolder.toString()}, QDir::Dirs | QDir::NoDotAndDotDot);
        if (!sub.isEmpty()) {
          path = location.absoluteFilePath(sub.first());
        }
      }
      break;
    }
    if (!path.isEmpty()) {
      return path;
    }
  }
  return {};
}

void BethesdaGame::detectVariant()
{
  if (m_Def.variantMarkers.empty()) {
    return;
  }
  for (const VariantMarker& m : m_Def.variantMarkers) {
    if (QFileInfo(m_GamePath + "\\" + m.file.toString()).exists()) {
      m_GameVariant = m.variant.toString();
      return;
    }
  }
  m_GameVariant = m_Def.defaultVariant.toString();
}

void BethesdaGame::updatePaths()
{
  detectVariant();
  m_MyGamesPath = myGamesFolder(variantFolder());
}

void BethesdaGame::detectGame()
{
  m_GamePath = identifyGamePath();
  updatePaths();
}

void BethesdaGame::setGamePath(const QString& path)
{
  m_GamePath = path;
  updatePaths();
}

bool BethesdaGame::isInstalled() const
{
  return !m_GamePath.isEmpty();
}

bool BethesdaGame::looksValid(QDir const& path) const
{
  return path.exists(binaryName());
}

// --- folders ---

QIcon BethesdaGame::gameIcon() const
{
  return MOBase::iconForExecutable(gameDirectory().absoluteFilePath(binaryName()));
}

QDir BethesdaGame::gameDirectory() const
{
  return QDir(m_GamePath);
}

QDir BethesdaGame::dataDirectory() const
{
  return gameDirectory().absoluteFilePath(m_Def.dataFolder.toString());
}

QDir BethesdaGame::documentsDirectory() const
{
  return QDir(m_MyGamesPath);
}

QDir BethesdaGame::savesDirectory() const
{
  return QDir(m_MyGamesPath + "/Saves");
}

// --- profiles ---

void BethesdaGame::initializeProfile(const QDir& directory, ProfileSettings settings) const
{
  if (settings.testFlag(IPluginGame::MODS)) {
    copyIfMissing(localAppFolder() + "/" + variantFolder(), directory, "plugins.txt",
                  "plugins.txt");
    if (m_Def.profileGetsLoadOrderTxt) {
      copyIfMissing(localAppFolder() + "/" + variantFolder(), directory, "loadorder.txt",
                    "loadorder.txt");
    }
  }

  if (settings.testFlag(IPluginGame::CONFIGURATION) && !m_Def.iniFiles.empty()) {
    const QString mainIni = m_Def.iniFiles.front().toString();
    // the game's defaults when asked for or when the user has no INI yet
    if (!m_Def.defaultIni.isEmpty() &&
        (settings.testFlag(IPluginGame::PREFER_DEFAULTS) ||
         !QFileInfo(m_MyGamesPath + "/" + mainIni).exists())) {
      copyIfMissing(gameDirectory().absolutePath(), directory, m_Def.defaultIni.toString(),
                    mainIni);
    } else {
      copyIfMissing(m_MyGamesPath, directory, mainIni, mainIni);
    }
    for (const QStringView ini : m_Def.iniFiles.subspan(1)) {
      copyIfMissing(m_MyGamesPath, directory, ini.toString(), ini.toString());
    }
  }
}

bool BethesdaGame::prepareIni(const QString&)
{
  if (m_Def.iniFiles.empty() || m_Organizer == nullptr || m_Organizer->profile() == nullptr) {
    return true;
  }
  // the launcher's "Data Files" choice is what makes the game read
  // plugins.txt at all (MO2 has always set it)
  const QString ini = profileIniFolder(m_Organizer->profile(),
                                       documentsDirectory().absolutePath()) +
                      "/" + m_Def.iniFiles.front().toString();
  const auto value = iniValue(ini, "Launcher", "bEnableFileSelection", "0", 512).toStdWString();
  if (value.empty() || std::wcstol(value.c_str(), nullptr, 10) != 1) {
    setIniValue(ini, "Launcher", "bEnableFileSelection", "1");
  }
  return true;
}

// --- saves ---

std::vector<std::shared_ptr<const MOBase::ISaveGame>>
BethesdaGame::listSaves(QDir folder) const
{
  std::vector<std::shared_ptr<const MOBase::ISaveGame>> saves;
  if (m_Def.saveFormat == SaveFormat::None) {
    return saves;
  }
  const QStringList filter{"*." + m_Def.saveExtension.toString()};
  for (const QFileInfo& info : folder.entryInfoList(filter, QDir::Files)) {
    if (auto save = makeSaveGame(info.filePath())) {
      saves.push_back(std::move(save));
    }
  }
  return saves;
}

std::shared_ptr<const MOBase::ISaveGame> BethesdaGame::makeSaveGame(const QString& path) const
{
  switch (m_Def.saveFormat) {
  case SaveFormat::None:
    return nullptr;
  case SaveFormat::Fallout3:
    return readFallout3SaveGame(this, path, false);
  case SaveFormat::FalloutNV:
    return readFallout3SaveGame(this, path, true);
  case SaveFormat::SkyrimSE:
    return readSkyrimSaveGame(this, path);
  case SaveFormat::Fallout4:
    return readFallout4SaveGame(this, path);
  }
  return nullptr;
}

// --- programs ---

QList<MOBase::ExecutableInfo> BethesdaGame::executables() const
{
  const bool epic = m_GameVariant == Epic;
  QList<MOBase::ExecutableInfo> out;
  for (const ExecutableDefinition& e : m_Def.executables) {
    if (epic && e.notOnEpic) {
      continue;
    }
    QFileInfo binary;
    switch (e.kind) {
    case ExecutableKind::GameFolder:
      binary = findInGameFolder(e.binary.toString());
      break;
    case ExecutableKind::Launcher:
      binary = findInGameFolder(getLauncherName());
      break;
    case ExecutableKind::ScriptExtender: {
      // the extender MO2 has for the game (a plugin may replace ours)
      QString loader = m_Def.extender.loader.toString();
      if (m_Organizer != nullptr) {
        if (auto se = m_Organizer->gameFeatures()->gameFeature<MOBase::ScriptExtender>()) {
          loader = se->loaderName();
        }
      }
      binary = findInGameFolder(loader);
      break;
    }
    case ExecutableKind::Loot:
      binary = QFileInfo(lootPath());
      break;
    }
    MOBase::ExecutableInfo info(e.title.toString(), binary);
    if (!e.argument.isEmpty()) {
      info.withArgument(e.argument.toString());
    }
    if (epic && e.epicPortal) {
      info.withArgument("-EpicPortal");
    }
    if (!e.steamAppId.isEmpty()) {
      info.withSteamAppId(e.steamAppId.toString());
    }
    out.append(info);
  }
  return out;
}

QList<MOBase::ExecutableForcedLoadSetting> BethesdaGame::executableForcedLoads() const
{
  return {};
}

// --- identity ---

QString BethesdaGame::gameName() const
{
  return m_Def.gameName.toString();
}

QString BethesdaGame::gameShortName() const
{
  return m_Def.shortName.toString();
}

QStringList BethesdaGame::validShortNames() const
{
  return toList(m_Def.validShortNames);
}

QString BethesdaGame::gameNexusName() const
{
  return m_Def.nexusName.toString();
}

int BethesdaGame::nexusModOrganizerID() const
{
  return m_Def.nexusModOrganizerId;
}

int BethesdaGame::nexusGameID() const
{
  return m_Def.nexusGameId;
}

QString BethesdaGame::steamAPPId() const
{
  if (m_Def.variants.empty() || m_GameVariant == u"Steam") {
    return m_Def.stores.steamAppId.toString();
  }
  return {};
}

QStringList BethesdaGame::gameVariants() const
{
  return toList(m_Def.variants);
}

void BethesdaGame::setGameVariant(const QString& variant)
{
  m_GameVariant = variant;
}

QString BethesdaGame::binaryName() const
{
  return m_Def.binary.toString();
}

QString BethesdaGame::getLauncherName() const
{
  return m_Def.launcher.toString();
}

QStringList BethesdaGame::iniFiles() const
{
  return toList(m_Def.iniFiles);
}

QStringList BethesdaGame::primaryPlugins() const
{
  // the Creation Club plugins load right after the game's own masters
  QStringList out = toList(m_Def.primaryPlugins);
  out.append(CCPlugins());
  return out;
}

QStringList BethesdaGame::CCPlugins() const
{
  if (m_Def.ccFile.isEmpty()) {
    return {};
  }
  // read again only when the file changes: MO2 asks for the primary plugins
  // many times per refresh
  const QString path = gameDirectory().filePath(m_Def.ccFile.toString());
  return m_CcCache.get(path, [&path] {
    QStringList plugins;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return plugins;
    }
    // a few KB with every Creation Club item; anything far larger isn't one
    constexpr qint64 MaxCcFile = 1024 * 1024;
    if (file.size() > MaxCcFile) {
      MOBase::log::error("'{}' is {} bytes, not read", path, file.size());
      return plugins;
    }
    const QByteArray bytes = file.readAll();
    // one name per line, UTF-8; comments, blank lines and repeats (ignoring
    // case) skipped, as MO2 has read it
    for (const std::wstring& name :
         readLoadOrderTxt(std::string_view(bytes.constData(), bytes.size()))) {
      plugins.append(QString::fromStdWString(name));
    }
    return plugins;
  });
}

QStringList BethesdaGame::DLCPlugins() const
{
  return toList(m_Def.dlcPlugins);
}

MOBase::IPluginGame::LoadOrderMechanism BethesdaGame::loadOrderMechanism() const
{
  return m_Def.loadOrder == LoadOrderKind::FileTime ? LoadOrderMechanism::FileTime
                                                    : LoadOrderMechanism::PluginsTxt;
}

MOBase::IPluginGame::SortMechanism BethesdaGame::sortMechanism() const
{
  if (m_Def.lootSortingSetting.isEmpty()) {
    return SortMechanism::LOOT;
  }
  if (m_Organizer != nullptr &&
      m_Organizer->pluginSetting(name(), m_Def.lootSortingSetting.toString()).toBool()) {
    return SortMechanism::LOOT;
  }
  return SortMechanism::NONE;
}

QString BethesdaGame::gameVersion() const
{
  const QString exe = gameDirectory().absoluteFilePath(binaryName());
  // read again when the exe changes (a game update)
  return m_VersionCache.get(exe, [&exe] {
    QString version = MOBase::getFileVersion(exe);
    if (version.startsWith(FallbackVersion)) {
      const QString product = MOBase::getProductVersion(exe);
      if (!product.isEmpty()) {
        version = product;
      }
    }
    return version;
  });
}

// --- file mapper ---

MappingType BethesdaGame::mappings() const
{
  MappingType result;
  if (m_Organizer == nullptr) {
    return result;
  }
  // the profile's lists where the game reads them: the variant's AppData
  // folder, and for some games the default one as well
  const QString appData = localAppFolder();
  const QString own     = variantFolder();
  const QString base    = m_Def.myGamesFolder.toString();
  for (const QString file : {QStringLiteral("plugins.txt"), QStringLiteral("loadorder.txt")}) {
    const QString source = m_Organizer->profilePath() + "/" + file;
    if (m_Def.listsAlsoInDefaultAppData) {
      result.push_back({source, appData + "/" + base + "/" + file, false, false});
    }
    if (!m_Def.listsAlsoInDefaultAppData || own != base) {
      result.push_back({source, appData + "/" + own + "/" + file, false, false});
    }
  }
  return result;
}

}  // namespace mo2r::bethesda
