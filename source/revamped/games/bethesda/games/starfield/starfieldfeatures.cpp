// MO2 Revamped: Starfield, the features that differ from the framework's.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The keys, folders and files read are those of MO2's game_starfield
// (StarfieldGamePlugins, StarfieldDataArchives, StarfieldUnmanagedMods,
// StarfieldModDataContent).
#include "starfieldfeatures.h"

#include "core/bethesdagame.h"
#include "ini/inifiles.h"

#include <imoinfo.h>
#include <iplugingame.h>
#include <iprofile.h>
#include <log.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

namespace mo2r::bethesda
{

namespace
{
  // ContentCatalog.txt lists every Creation: a few hundred KB at most
  constexpr qint64 MaxCatalog = 64 * 1024 * 1024;
  // the old plugin's buffer for each archive key
  constexpr int MaxArchiveList = 4096;

  // My Games' Data first, then the game's, as the old plugin looked
  QMap<QString, QDir> dataFolders(const BethesdaGame* game)
  {
    QMap<QString, QDir> folders{{"data", game->dataDirectory()}};
    folders.insert(game->secondaryDataDirectories());
    return folders;
  }
}  // namespace

// --- plugins ---

bool StarfieldGamePlugins::pluginsTxtInUse() const
{
  return m_Organizer->managedGame()->loadOrderMechanism() ==
         MOBase::IPluginGame::LoadOrderMechanism::PluginsTxt;
}

// --- archives ---

QStringList StarfieldDataArchives::vanillaArchives() const
{
  QStringList out;
  for (const QStringView name : m_Game->definition().archives.vanilla) {
    out.append(name.toString());
  }
  return out;
}

QStringList StarfieldDataArchives::archives(const MOBase::IProfile* profile) const
{
  const QString defaults = QDir::toNativeSeparators(
      m_Game->gameDirectory().absoluteFilePath("Starfield.ini"));
  const QString custom = QDir::toNativeSeparators(
      QDir(profileIniFolder(profile, m_Game->myGamesPath())).absoluteFilePath("StarfieldCustom.ini"));
  const auto list = [](const QString& ini, const QString& key) {
    QStringList out;
    const QString value = iniValue(ini, "Archive", key, "", MaxArchiveList);
    if (!value.isEmpty()) {
      for (const QString& name : value.split(',')) {
        out.append(name.trimmed());
      }
    }
    return out;
  };
  QStringList out;
  for (const QString key : {QStringLiteral("SResourceArchiveList"),
                            QStringLiteral("sResourceIndexFileList"),
                            QStringLiteral("SResourceArchiveMemoryCacheList"),
                            QStringLiteral("sResourceStartUpArchiveList"),
                            QStringLiteral("sResourceEnglishVoiceList")}) {
    QStringList archives = list(custom, key);
    if (archives.isEmpty()) {
      archives = list(defaults, key);
    }
    out.append(archives);
  }
  return out;
}

// --- Creations ---

std::map<QString, StarfieldCreation> parseContentCatalog(const QString& appDataFolder,
                                                         const QString& shortName)
{
  std::map<QString, StarfieldCreation> catalog;
  QFile file(appDataFolder + "/" + shortName + "/ContentCatalog.txt");
  if (!file.open(QIODevice::ReadOnly)) {
    return catalog;
  }
  if (file.size() > MaxCatalog) {
    MOBase::log::error("'{}' is {} bytes, not read", file.fileName(), file.size());
    return catalog;
  }
  // the game writes it in its code page; read as Latin-1, as the old plugin
  const QByteArray data = QString::fromLatin1(file.readAll()).toUtf8();
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError) {
    MOBase::log::warn("{}", QObject::tr("ContentCatalog.txt appears to be corrupt: %1")
                                .arg(error.errorString()));
    return catalog;
  }
  const QJsonObject root = doc.object();
  for (const QString& key : root.keys()) {
    if (key == "ContentCatalog") {
      continue;
    }
    const QJsonObject creation = root.value(key).toObject();
    QStringList files;
    QStringList plugins;
    for (const QJsonValue& value : creation.value("Files").toArray()) {
      const QString name = value.toString();
      files.append(name);
      if (name.endsWith(".esm", Qt::CaseInsensitive) ||
          name.endsWith(".esl", Qt::CaseInsensitive) ||
          name.endsWith(".esp", Qt::CaseInsensitive)) {
        plugins.append(name);
      }
    }
    for (const QString& plugin : plugins) {
      catalog[plugin] = {creation.value("Title").toString(), files};
    }
  }
  return catalog;
}

QStringList StarfieldUnmanagedMods::mods(bool onlyOfficial) const
{
  // the game's own plugins aren't mods; its Creations are
  QStringList masters = m_Game->primaryPlugins() + m_Game->enabledPlugins();
  QStringList others  = m_Game->DLCPlugins();
  others.append(m_Game->CCPlugins());
  for (const QString& plugin : others) {
    masters.removeAll(plugin);
  }
  QStringList out;
  for (const QDir& folder : dataFolders(m_Game)) {
    for (const QString& name : folder.entryList({"*.esp", "*.esl", "*.esm"})) {
      // (onlyOfficial gives none, as in the old plugin)
      if (!masters.contains(name, Qt::CaseInsensitive) &&
          (!onlyOfficial || masters.contains(name, Qt::CaseInsensitive))) {
        out.append(name.chopped(4));
      }
    }
  }
  return out;
}

QFileInfo StarfieldUnmanagedMods::referenceFile(const QString& modName) const
{
  QFileInfoList files;
  for (const QDir& folder : dataFolders(m_Game)) {
    files += folder.entryInfoList({modName + ".es*"});
  }
  return files.isEmpty() ? QFileInfo() : files.first();
}

QStringList StarfieldUnmanagedMods::secondaryFiles(const QString& modName) const
{
  QStringList files;
  for (const auto& [plugin, creation] : parseContentCatalog(m_AppDataFolder, m_Game->gameShortName())) {
    if (plugin.startsWith(modName, Qt::CaseInsensitive)) {
      files += creation.files;
      break;
    }
  }
  for (const QDir& folder : dataFolders(m_Game)) {
    for (const QString& archive : folder.entryList({modName + "*.ba2"})) {
      files.append(folder.absoluteFilePath(archive));
    }
  }
  return files;
}

QString StarfieldUnmanagedMods::displayName(const QString& modName) const
{
  for (const auto& [plugin, creation] : parseContentCatalog(m_AppDataFolder, m_Game->gameShortName())) {
    if (plugin.startsWith(modName, Qt::CaseInsensitive)) {
      return creation.name;
    }
  }
  return modName;
}

// --- contents ---

namespace
{
  // after MO2's own ids (Content::Material is 15, the old CONTENT_NEXT_VALUE)
  constexpr int Material   = static_cast<int>(Content::Material);
  constexpr int Geometries = Material + 1;
  constexpr int Video      = Material + 2;
}  // namespace

std::vector<StarfieldDataContent::Content> StarfieldDataContent::getAllContents() const
{
  auto contents = BethesdaDataContent::getAllContents();
  contents.push_back({Material, "Materials", ":/MO/gui/content/material"});
  contents.push_back({Geometries, "Geometries", ":/MO/gui/content/geometries"});
  contents.push_back({Video, "Video", ":/MO/gui/content/media"});
  return contents;
}

std::vector<int> StarfieldDataContent::getContentsFor(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  auto contents = BethesdaDataContent::getContentsFor(fileTree);
  for (const auto& e : *fileTree) {
    if (!e->isDir()) {
      continue;
    }
    if (e->compare("materials") == 0) {
      contents.push_back(Material);
    } else if (e->compare("geometries") == 0) {
      contents.push_back(Geometries);
    } else if (e->compare("video") == 0) {
      contents.push_back(Video);
    }
  }
  return contents;
}

}  // namespace mo2r::bethesda
