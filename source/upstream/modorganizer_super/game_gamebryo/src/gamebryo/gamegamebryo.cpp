#include "gamegamebryo.h"

#include "bsainvalidation.h"
#include "dataarchives.h"
#include "gamebryomoddatacontent.h"
#include "gamebryosavegame.h"
#include "gameplugins.h"
#include "iprofile.h"
#include "log.h"
#include "registry.h"
#include "savegameinfo.h"
#include "scopeguard.h"
#include "scriptextender.h"
#include "igamefeatures.h"
#include "utility.h"
#include "vdf_parser.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonValue>

#include <QtDebug>
#include <QtGlobal>

#include <Knownfolders.h>
#include <Shlobj.h>
#include <Windows.h>
#include <winreg.h>
#include <winver.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

GameGamebryo::GameGamebryo() {}

void GameGamebryo::detectGame()
{
  m_GamePath    = identifyGamePath();
  m_MyGamesPath = determineMyGamesPath(gameName());
}

bool GameGamebryo::init(MOBase::IOrganizer* moInfo)
{
  m_Organizer = moInfo;
  m_Organizer->onAboutToRun([this](const auto& binary) {
    return prepareIni(binary);
  });
  return true;
}

bool GameGamebryo::isInstalled() const
{
  return !m_GamePath.isEmpty();
}

QIcon GameGamebryo::gameIcon() const
{
  return MOBase::iconForExecutable(gameDirectory().absoluteFilePath(binaryName()));
}

QDir GameGamebryo::gameDirectory() const
{
  return QDir(m_GamePath);
}

QDir GameGamebryo::dataDirectory() const
{
  return gameDirectory().absoluteFilePath("data");
}

void GameGamebryo::setGamePath(const QString& path)
{
  m_GamePath = path;
}

QDir GameGamebryo::documentsDirectory() const
{
  return m_MyGamesPath;
}

QDir GameGamebryo::savesDirectory() const
{
  return QDir(myGamesPath() + "/Saves");
}

std::vector<std::shared_ptr<const MOBase::ISaveGame>>
GameGamebryo::listSaves(QDir folder) const
{
  QStringList filters;
  filters << QString("*.") + savegameExtension();

  std::vector<std::shared_ptr<const MOBase::ISaveGame>> saves;
  for (auto info : folder.entryInfoList(filters, QDir::Files)) {
    try {
      saves.push_back(makeSaveGame(info.filePath()));
    } catch (std::exception& e) {
      MOBase::log::error("{}", e.what());
      continue;
    }
  }

  return saves;
}

void GameGamebryo::setGameVariant(const QString& variant)
{
  m_GameVariant = variant;
}

QString GameGamebryo::binaryName() const
{
  return gameShortName() + ".exe";
}

MOBase::IPluginGame::LoadOrderMechanism GameGamebryo::loadOrderMechanism() const
{
  return LoadOrderMechanism::FileTime;
}

MOBase::IPluginGame::SortMechanism GameGamebryo::sortMechanism() const
{
  return SortMechanism::LOOT;
}

bool GameGamebryo::looksValid(QDir const& path) const
{
  // Check for <prog>.exe for now.
  return path.exists(binaryName());
}

QString GameGamebryo::gameVersion() const
{
  // We try the file version, but if it looks invalid (starts with the fallback
  // version), we look the product version instead. If the product version is
  // not empty, we use it.
  QString binaryAbsPath = gameDirectory().absoluteFilePath(binaryName());
  QString version       = MOBase::getFileVersion(binaryAbsPath);
  if (version.startsWith(FALLBACK_GAME_VERSION)) {
    QString pversion = MOBase::getProductVersion(binaryAbsPath);
    if (!pversion.isEmpty()) {
      version = pversion;
    }
  }
  return version;
}

QString GameGamebryo::getLauncherName() const
{
  return gameShortName() + "Launcher.exe";
}

WORD GameGamebryo::getArch(QString const& program) const
{
  // MO2 Revamped: the machine field read from the PE headers (the first few
  // KB), not by mapping the whole exe as an image under a global name
  QFile file(this->gameDirectory().absoluteFilePath(program));
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }
  const QByteArray head = file.read(4096);
  if (head.size() < static_cast<int>(sizeof(IMAGE_DOS_HEADER))) {
    return 0;
  }
  IMAGE_DOS_HEADER dos;
  std::memcpy(&dos, head.constData(), sizeof(dos));
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
    return 0;
  }
  const qint64 ntOffset = dos.e_lfanew;
  QByteArray nt;
  if (ntOffset + qint64(sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)) <= head.size()) {
    nt = head.mid(ntOffset, sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER));
  } else if (file.seek(ntOffset)) {
    nt = file.read(sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER));
  }
  if (nt.size() < static_cast<int>(sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER))) {
    return 0;
  }
  DWORD signature;
  std::memcpy(&signature, nt.constData(), sizeof(signature));
  if (signature != IMAGE_NT_SIGNATURE) {
    return 0;
  }
  IMAGE_FILE_HEADER fileHeader;
  std::memcpy(&fileHeader, nt.constData() + sizeof(DWORD), sizeof(fileHeader));
  return fileHeader.Machine;
}

QFileInfo GameGamebryo::findInGameFolder(const QString& relativePath) const
{
  return QFileInfo(m_GamePath + "/" + relativePath);
}

QString GameGamebryo::identifyGamePath() const
{
  QString path = "Software\\Bethesda Softworks\\" + gameShortName();
  return findInRegistry(HKEY_LOCAL_MACHINE, path.toStdWString().c_str(),
                        L"Installed Path");
}

bool GameGamebryo::prepareIni(const QString& exec)
{
  MOBase::IProfile* profile = m_Organizer->profile();

  QString basePath = profile->localSettingsEnabled()
                         ? profile->absolutePath()
                         : documentsDirectory().absolutePath();

  if (!iniFiles().isEmpty()) {

    QString profileIni = basePath + "/" + iniFiles()[0];

    WCHAR setting[512];
    if (!GetPrivateProfileStringW(L"Launcher", L"bEnableFileSelection", L"0", setting,
                                  512, profileIni.toStdWString().c_str()) ||
        wcstol(setting, nullptr, 10) != 1) {
      MOBase::WriteRegistryValue(L"Launcher", L"bEnableFileSelection", L"1",
                                 profileIni.toStdWString().c_str());
    }
  }

  return true;
}

QString GameGamebryo::selectedVariant() const
{
  return m_GameVariant;
}

QString GameGamebryo::myGamesPath() const
{
  return m_MyGamesPath;
}

/*static*/ QString GameGamebryo::getLootPath()
{
  // MO2 Revamped: from MO2 c2b2597
  auto result = findInRegistry(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{BF634210-A0D4-443F-"
      "A657-0DCE38040374}_is1",
      L"InstallLocation");

  if (result.isEmpty())
    result = findInRegistry(
        HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{BF634210-A0D4-443F-"
        "A657-0DCE38040374}_is1",
        L"InstallLocation");

  if (result.isEmpty())
    result = findInRegistry(HKEY_LOCAL_MACHINE, L"Software\\LOOT", L"Installed Path");
  return result + "/Loot.exe";
}

QString GameGamebryo::localAppFolder()
{
  QString result = getKnownFolderPath(FOLDERID_LocalAppData, false);
  if (result.isEmpty()) {
    // fallback: try the registry
    result = getSpecialPath("Local AppData");
  }
  return result;
}

void GameGamebryo::copyToProfile(QString const& sourcePath,
                                 QDir const& destinationDirectory,
                                 QString const& sourceFileName)
{
  copyToProfile(sourcePath, destinationDirectory, sourceFileName, sourceFileName);
}

void GameGamebryo::copyToProfile(QString const& sourcePath,
                                 QDir const& destinationDirectory,
                                 QString const& sourceFileName,
                                 QString const& destinationFileName)
{
  QString filePath = destinationDirectory.absoluteFilePath(destinationFileName);
  if (!QFileInfo(filePath).exists()) {
    if (!MOBase::shellCopy(sourcePath + "/" + sourceFileName, filePath)) {
      // if copy file fails, create the file empty
      QFile(filePath).open(QIODevice::WriteOnly);
    }
  }
}

MappingType GameGamebryo::mappings() const
{
  MappingType result;

  for (const QString& profileFile : {"plugins.txt", "loadorder.txt"}) {
    result.push_back({m_Organizer->profilePath() + "/" + profileFile,
                      localAppFolder() + "/" + gameShortName() + "/" + profileFile,
                      false});
  }

  return result;
}

std::unique_ptr<BYTE[]> GameGamebryo::getRegValue(HKEY key, LPCWSTR path, LPCWSTR value,
                                                  DWORD flags, LPDWORD type = nullptr)
{
  DWORD size = 0;
  HKEY subKey;
  LONG res = ::RegOpenKeyExW(key, path, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &subKey);
  if (res != ERROR_SUCCESS) {
    res = ::RegOpenKeyExW(key, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &subKey);
    if (res != ERROR_SUCCESS)
      return std::unique_ptr<BYTE[]>();
  }
  res = ::RegGetValueW(subKey, L"", value, flags, type, nullptr, &size);
  if (res == ERROR_FILE_NOT_FOUND || res == ERROR_UNSUPPORTED_TYPE) {
    return std::unique_ptr<BYTE[]>();
  }
  if (res != ERROR_SUCCESS && res != ERROR_MORE_DATA) {
    throw MOBase::MyException(
        QObject::tr("failed to query registry path (preflight): %1").arg(res, 0, 16));
  }

  std::unique_ptr<BYTE[]> result(new BYTE[size]);
  res = ::RegGetValueW(subKey, L"", value, flags, type, result.get(), &size);

  if (res != ERROR_SUCCESS) {
    throw MOBase::MyException(
        QObject::tr("failed to query registry path (read): %1").arg(res, 0, 16));
  }

  return result;
}

QString GameGamebryo::findInRegistry(HKEY baseKey, LPCWSTR path, LPCWSTR value)
{
  std::unique_ptr<BYTE[]> buffer =
      getRegValue(baseKey, path, value, RRF_RT_REG_SZ | RRF_NOEXPAND);

  return QString::fromUtf16(reinterpret_cast<const ushort*>(buffer.get()));
}

QString GameGamebryo::getKnownFolderPath(REFKNOWNFOLDERID folderId, bool useDefault)
{
  PWSTR path = nullptr;
  ON_BLOCK_EXIT([&]() {
    if (path != nullptr)
      ::CoTaskMemFree(path);
  });

  if (::SHGetKnownFolderPath(folderId, useDefault ? KF_FLAG_DEFAULT_PATH : 0, NULL,
                             &path) == S_OK) {
    return QDir::fromNativeSeparators(QString::fromWCharArray(path));
  } else {
    return QString();
  }
}

QString GameGamebryo::getSpecialPath(const QString& name)
{
  QString base = findInRegistry(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
      name.toStdWString().c_str());

  WCHAR temp[MAX_PATH];
  if (::ExpandEnvironmentStringsW(base.toStdWString().c_str(), temp, MAX_PATH) != 0) {
    return QString::fromWCharArray(temp);
  } else {
    return base;
  }
}

QString GameGamebryo::determineMyGamesPath(const QString& gameName)
{
  const QString pattern = "%1/My Games/" + gameName;

  auto tryDir = [&](const QString& dir) -> std::optional<QString> {
    if (dir.isEmpty()) {
      return {};
    }

    const auto path = pattern.arg(dir);
    if (!QFileInfo(path).exists()) {
      return {};
    }

    return path;
  };

  // a) this is the way it should work. get the configured My Documents directory
  if (auto d = tryDir(getKnownFolderPath(FOLDERID_Documents, false))) {
    return *d;
  }

  // b) if there is no <game> directory there, look in the default directory
  if (auto d = tryDir(getKnownFolderPath(FOLDERID_Documents, true))) {
    return *d;
  }

  // c) finally, look in the registry. This is discouraged
  if (auto d = tryDir(getSpecialPath("Personal"))) {
    return *d;
  }

  // MO2 Revamped: the game never started yet. Where it will make the folder,
  // not an empty path: that is MO2's own folder, and INIs, saves and Data
  // would be written there
  const QString documents = getKnownFolderPath(FOLDERID_Documents, false);
  if (!documents.isEmpty()) {
    return pattern.arg(documents);
  }
  return {};
}

QString GameGamebryo::parseEpicGamesLocation(const QStringList& manifests)
{
  // Use the registry entry to find the EGL Data dir first, just in case something
  // changes
  QString manifestDir = findInRegistry(
      HKEY_LOCAL_MACHINE, L"Software\\Epic Games\\EpicGamesLauncher", L"AppDataPath");
  if (manifestDir.isEmpty())
    manifestDir = getKnownFolderPath(FOLDERID_ProgramData, false) +
                  "\\Epic\\EpicGamesLauncher\\Data\\";
  manifestDir += "Manifests";
  QDir epicManifests(manifestDir, "*.item",
                     QDir::SortFlags(QDir::Name | QDir::IgnoreCase), QDir::Files);
  if (epicManifests.exists()) {
    QDirIterator it(epicManifests);
    while (it.hasNext()) {
      QString manifestFile = it.next();
      QFile manifest(manifestFile);

      if (!manifest.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open Epic Games manifest file.");
        continue;
      }

      QByteArray manifestData = manifest.readAll();

      QJsonDocument manifestJson(QJsonDocument::fromJson(manifestData));

      if (manifests.contains(manifestJson["AppName"].toString())) {
        return manifestJson["InstallLocation"].toString();
      }
    }
  }
  return "";
}

QString GameGamebryo::parseSteamLocation(const QString& appid,
                                         const QString& directoryName)
{
  QString path = "Software\\Valve\\Steam";
  QString steamLocation =
      findInRegistry(HKEY_CURRENT_USER, path.toStdWString().c_str(), L"SteamPath");
  if (!steamLocation.isEmpty()) {
    QString steamLibraryLocation;
    QString steamLibraries(steamLocation + "\\" + "config" + "\\" +
                           "libraryfolders.vdf");
    if (QFile(steamLibraries).exists()) {
      // MO2 Revamped: a wide path (a user name outside the ANSI code page
      // couldn't be opened), no library without "apps", and a damaged file
      // can't throw out of detectGame()
      try {
        std::ifstream file(std::filesystem::path(steamLibraries.toStdWString()));
        if (file) {
          auto root = tyti::vdf::read(file);
          for (const auto& child : root.childs) {
            const tyti::vdf::object* library = child.second.get();
            if (!library) {
              continue;
            }

            const auto apps = library->childs.find("apps");
            if (apps == library->childs.end() || !apps->second) {
              continue;
            }

            if (apps->second->attribs.contains(appid.toStdString())) {
              const auto p = library->attribs.find("path");
              if (p != library->attribs.end()) {
                steamLibraryLocation = QString::fromStdString(p->second);
              }
              break;
            }
          }
        }
      } catch (const std::exception& e) {
        qWarning("Couldn't read Steam's library list: %s", e.what());
      }
    }
    if (!steamLibraryLocation.isEmpty()) {
      QString gameLocation = steamLibraryLocation + "\\" + "steamapps" + "\\" +
                             "common" + "\\" + directoryName;
      if (QDir(gameLocation).exists())
        return gameLocation;
    }
  }
  return "";
}

void GameGamebryo::registerFeature(std::shared_ptr<MOBase::GameFeature> feature)
{
  // priority does not matter, this is a game plugin so will get lowest priority in MO2
  m_Organizer->gameFeatures()->registerFeature(this, feature, 0, true);
}

QStringList GameGamebryo::cachedFromFile(const QString& path,
                                         const std::function<QStringList()>& compute) const
{
  const QFileInfo info(path);
  const bool exists      = info.exists();
  const qint64 size      = exists ? info.size() : -1;
  const QDateTime mtime  = exists ? info.lastModified() : QDateTime();
  const QString key      = path.toCaseFolded();

  {
    std::scoped_lock lock(m_FileCacheMutex);
    auto it = m_FileCache.find(key);
    if (it != m_FileCache.end() && it->second.exists == exists &&
        it->second.size == size && it->second.modified == mtime) {
      return it->second.value;
    }
  }

  QStringList value = compute();

  std::scoped_lock lock(m_FileCacheMutex);
  m_FileCache[key] = FileCacheEntry{exists, size, mtime, value};
  return value;
}

QStringList GameGamebryo::cachedFileLines(const QString& path) const
{
  return cachedFromFile(path, [&path] {
    QStringList lines;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return lines;
    }
    while (!file.atEnd()) {
      const QByteArray line = file.readLine().trimmed();
      if (!line.isEmpty() && line.at(0) != '#') {
        lines.append(QString::fromUtf8(line));
      }
    }
    return lines;
  });
}

QString GameGamebryo::gameVersionJoined(int parts, const QString& separator) const
{
  const QString exe = gameDirectory().absoluteFilePath(binaryName());
  // read again when the exe changes (a game update)
  const QStringList version = cachedFromFile(exe, [this] {
    return QStringList{gameVersion()};
  });
  QStringList numbers = version.value(0).split('.', Qt::SkipEmptyParts);
  if (numbers.isEmpty() || version.value(0).startsWith(FALLBACK_GAME_VERSION)) {
    return {};
  }
  while (numbers.size() < parts) {
    numbers.append("0");
  }
  return numbers.mid(0, parts).join(separator);
}

bool GameGamebryo::scriptExtenderRuntimeMissing(const QString& runtimePrefix,
                                                QString* expected) const
{
  if (m_Organizer == nullptr) {
    return false;
  }
  auto extender = m_Organizer->gameFeatures()->gameFeature<MOBase::ScriptExtender>();
  if (extender == nullptr || !extender->isInstalled()) {
    return false;
  }
  const QString version = gameVersionJoined(3, "_");
  if (version.isEmpty()) {
    return false;
  }
  const QString runtime = runtimePrefix + version + ".dll";
  if (expected != nullptr) {
    *expected = runtime;
  }
  return !gameDirectory().exists(runtime);
}

bool GameGamebryo::addressLibraryMissing(const QString& pluginsDir,
                                         QString* expected) const
{
  if (m_Organizer == nullptr) {
    return false;
  }
  const QString version = gameVersionJoined(4, "-");
  if (version.isEmpty()) {
    return false;
  }
  if (m_Organizer->findFiles(pluginsDir, QStringList{"*.dll"}).isEmpty()) {
    return false;
  }
  // "versionlib-" is the Anniversary Edition's, "version-" the older one's
  const QStringList names = {"versionlib-" + version + ".bin",
                             "version-" + version + ".bin"};
  if (expected != nullptr) {
    *expected = pluginsDir + "/" + names.first();
  }
  return m_Organizer->findFiles(pluginsDir, names).isEmpty();
}
