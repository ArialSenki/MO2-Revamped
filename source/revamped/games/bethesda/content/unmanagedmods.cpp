// MO2 Revamped: Bethesda game framework, plugins MO2 didn't install.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// Which files make an unmanaged mod is MO2's rule (game_gamebryo).
#include "unmanagedmods.h"

#include "core/bethesdagame.h"

#include <QDir>
#include <QFileInfo>

namespace mo2r::bethesda
{

namespace
{
  bool isPlugin(const QString& name)
  {
    return name.endsWith(".esp", Qt::CaseInsensitive) ||
           name.endsWith(".esm", Qt::CaseInsensitive) ||
           name.endsWith(".esl", Qt::CaseInsensitive);
  }
}  // namespace

QStringList DataFolderMods::dataFiles() const
{
  const QDir data          = m_Game->dataDirectory();
  const QString path       = data.absolutePath();
  const QFileInfo info(path);
  const QDateTime modified = info.exists() ? info.lastModified() : QDateTime();
  // a folder's time changes when an entry is added, removed or renamed
  std::scoped_lock lock(m_Mutex);
  if (path != m_Path || modified != m_Modified || !modified.isValid()) {
    m_Path     = path;
    m_Modified = modified;
    m_Files    = data.entryList(QDir::Files, QDir::Name | QDir::IgnoreCase);
  }
  return m_Files;
}

QStringList DataFolderMods::matching(const QString& prefix, const QString& suffix) const
{
  QStringList out;
  for (const QString& name : dataFiles()) {
    if (name.size() >= prefix.size() + suffix.size() &&
        name.startsWith(prefix, Qt::CaseInsensitive) &&
        name.endsWith(suffix, Qt::CaseInsensitive)) {
      out.append(name);
    }
  }
  return out;
}

QStringList DataFolderMods::mods(bool onlyOfficial) const
{
  // the DLC and Creation Club plugins are mods of their own (official ones),
  // even where they count as primary plugins; the game's masters aren't
  QStringList official = m_Game->DLCPlugins();
  official.append(m_Game->CCPlugins());
  QStringList masters = m_Game->primaryPlugins();
  masters.removeIf([&official](const QString& name) {
    return official.contains(name, Qt::CaseInsensitive);
  });
  QStringList out;
  for (const QString& name : dataFiles()) {
    if (isPlugin(name) && !masters.contains(name, Qt::CaseInsensitive) &&
        (!onlyOfficial || official.contains(name, Qt::CaseInsensitive))) {
      out.append(name.chopped(4));
    }
  }
  return out;
}

QFileInfo DataFolderMods::referenceFile(const QString& modName) const
{
  // the name as text, not a wildcard pattern ([ and ] are common in names)
  const QStringList files = matching(modName + ".es", QString());
  return files.isEmpty() ? QFileInfo()
                         : QFileInfo(m_Game->dataDirectory().absoluteFilePath(files.first()));
}

QStringList DataFolderMods::secondaryFiles(const QString& modName) const
{
  QStringList out;
  const QDir data = m_Game->dataDirectory();
  for (const QString& name : matching(modName, ".bsa")) {
    out.append(data.absoluteFilePath(name));
  }
  return out;
}

}  // namespace mo2r::bethesda
