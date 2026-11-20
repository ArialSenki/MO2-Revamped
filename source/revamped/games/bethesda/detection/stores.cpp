// MO2 Revamped: Bethesda game framework, where each store installs a game.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The registry keys and the Epic manifest folder are the ones MO2's game
// plugins look in (game_gamebryo).
#include "stores.h"

#include "detection/steamlibraries.h"
#include "util/winfolders.h"

#include <log.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <KnownFolders.h>

namespace mo2r::bethesda
{

namespace
{
  // a Steam library list or an Epic manifest is a few KB; anything far
  // larger is not one
  constexpr qint64 MaxSmallFile = 16 * 1024 * 1024;

  QByteArray readSmallFile(const QString& path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxSmallFile) {
      return {};
    }
    return file.read(MaxSmallFile);
  }
}  // namespace

QString findBethesdaInstall(const QString& key)
{
  const QString path = registryString(HKEY_LOCAL_MACHINE, key, "Installed Path");
  MOBase::log::debug("bethesda: registry {}: '{}'", key, path);
  return path;
}

QString findGogInstall(const QString& gogId)
{
  const QString key  = "Software\\GOG.com\\Games\\" + gogId;
  const QString path = registryString(HKEY_LOCAL_MACHINE, key, "path");
  MOBase::log::debug("bethesda: GOG {}: '{}'", gogId, path);
  return path;
}

QString findEpicInstall(const QStringList& appNames)
{
  // the launcher's data folder from its own key first, in case it moves
  QString manifests = registryString(HKEY_LOCAL_MACHINE,
                                     "Software\\Epic Games\\EpicGamesLauncher",
                                     "AppDataPath");
  if (manifests.isEmpty()) {
    manifests = knownFolder(FOLDERID_ProgramData) + "\\Epic\\EpicGamesLauncher\\Data\\";
  }
  manifests += "Manifests";

  const QDir dir(manifests);
  if (!dir.exists()) {
    MOBase::log::debug("bethesda: no Epic manifests in '{}'", manifests);
    return {};
  }
  for (const QFileInfo& info :
       dir.entryInfoList({"*.item"}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
    const QJsonDocument json = QJsonDocument::fromJson(readSmallFile(info.filePath()));
    if (!json.isObject()) {
      continue;
    }
    const QJsonObject manifest = json.object();
    if (appNames.contains(manifest.value("AppName").toString())) {
      const QString path = manifest.value("InstallLocation").toString();
      MOBase::log::debug("bethesda: Epic manifest {}: '{}'", info.fileName(), path);
      return path;
    }
  }
  MOBase::log::debug("bethesda: no Epic manifest for {}", appNames.join(", "));
  return {};
}

QString findSteamInstall(const QString& appId, const QString& folder)
{
  const QString steam =
      registryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
  if (steam.isEmpty()) {
    MOBase::log::debug("bethesda: Steam is not installed");
    return {};
  }
  const QByteArray vdf = readSmallFile(steam + "\\config\\libraryfolders.vdf");
  const auto libraries =
      parseSteamLibraries(std::string_view(vdf.constData(), vdf.size()));
  const auto library = steamLibraryForApp(libraries, appId.toStdString());
  if (!library) {
    MOBase::log::debug("bethesda: no Steam library lists app {}", appId);
    return {};
  }
  const QString path = QString::fromUtf8(library->data(), library->size()) +
                       "\\steamapps\\common\\" + folder;
  if (!QDir(path).exists()) {
    MOBase::log::debug("bethesda: Steam app {} not in '{}'", appId, path);
    return {};
  }
  MOBase::log::debug("bethesda: Steam app {}: '{}'", appId, path);
  return path;
}

}  // namespace mo2r::bethesda
