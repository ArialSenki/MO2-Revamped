#include "gamebryounmanagedmods.h"
#include "gamegamebryo.h"
#include <pluginsetting.h>

#include <QDir>
#include <QFileInfo>

GamebryoUnmangedMods::GamebryoUnmangedMods(const GameGamebryo* game) : m_Game(game) {}

GamebryoUnmangedMods::~GamebryoUnmangedMods() {}

QStringList GamebryoUnmangedMods::mods(bool onlyOfficial) const
{
  QStringList result;

  QStringList dlcPlugins  = m_Game->DLCPlugins();
  QStringList mainPlugins = m_Game->primaryPlugins();

  for (const QString& fileName : dataPlugins()) {
    if (!mainPlugins.contains(fileName, Qt::CaseInsensitive) &&
        (!onlyOfficial || dlcPlugins.contains(fileName, Qt::CaseInsensitive))) {
      result.append(fileName.chopped(4));  // trims the extension off
    }
  }

  return result;
}

QString GamebryoUnmangedMods::displayName(const QString& modName) const
{
  return modName;
}

QFileInfo GamebryoUnmangedMods::referenceFile(const QString& modName) const
{
  // MO2 Revamped: from the cached listing; the name is matched as text, not
  // as a wildcard pattern (a [ or ] in it no longer breaks the match)
  const QStringList files = dataFilesMatching(modName + ".es", QString());
  if (!files.isEmpty()) {
    return QFileInfo(m_Game->dataDirectory().absoluteFilePath(files.first()));
  } else {
    return QFileInfo();
  }
}

QStringList GamebryoUnmangedMods::secondaryFiles(const QString& modName) const
{
  QStringList archives;
  QDir dataDir = m_Game->dataDirectory();
  for (const QString& archiveName : dataFilesMatching(modName, ".bsa")) {
    archives.append(dataDir.absoluteFilePath(archiveName));
  }
  return archives;
}

QStringList GamebryoUnmangedMods::dataFileNames() const
{
  const QDir dataDir = m_Game->dataDirectory();
  const QString path = dataDir.absolutePath();
  const QFileInfo info(path);
  const QDateTime modified = info.exists() ? info.lastModified() : QDateTime();

  std::scoped_lock lock(m_DataMutex);
  if (path != m_DataPath || modified != m_DataModified || !modified.isValid()) {
    m_DataPath     = path;
    m_DataModified = modified;
    m_DataFiles    = dataDir.entryList(QDir::Files, QDir::Name | QDir::IgnoreCase);
  }
  return m_DataFiles;
}

QStringList GamebryoUnmangedMods::dataFilesMatching(const QString& prefix,
                                                    const QString& suffix) const
{
  QStringList result;
  for (const QString& name : dataFileNames()) {
    if (name.size() >= prefix.size() + suffix.size() &&
        name.startsWith(prefix, Qt::CaseInsensitive) &&
        name.endsWith(suffix, Qt::CaseInsensitive)) {
      result.append(name);
    }
  }
  return result;
}

QStringList GamebryoUnmangedMods::dataPlugins() const
{
  QStringList result;
  for (const QString& name : dataFileNames()) {
    if (name.endsWith(".esp", Qt::CaseInsensitive) ||
        name.endsWith(".esm", Qt::CaseInsensitive) ||
        name.endsWith(".esl", Qt::CaseInsensitive)) {
      result.append(name);
    }
  }
  return result;
}
