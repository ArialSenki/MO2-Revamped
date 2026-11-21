// MO2 Revamped: Bethesda game framework, archive lists in the game's INI.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// How the list is split and joined (", ") is MO2's (game_gamebryo).
#include "iniarchives.h"

#include "core/bethesdagame.h"
#include "ini/inifiles.h"

#include <iprofile.h>
#include <log.h>

#include <QDir>

#include <climits>

namespace mo2r::bethesda
{

QStringList IniDataArchives::vanillaArchives() const
{
  QStringList out;
  for (const QStringView name : m_Game->definition().archives.vanilla) {
    out.append(name.toString());
  }
  return out;
}

QString IniDataArchives::iniPath(const MOBase::IProfile* profile) const
{
  return QDir(profileIniFolder(profile, m_Game->myGamesPath()))
      .absoluteFilePath(m_Game->definition().archives.iniFile.toString());
}

QStringList IniDataArchives::archives(const MOBase::IProfile* profile) const
{
  const ArchiveDefinition& a = m_Game->definition().archives;
  const QString ini          = QDir::toNativeSeparators(iniPath(profile));
  QStringList out;
  for (const QStringView key : {a.listKey, a.listKey2}) {
    if (key.isEmpty()) {
      continue;
    }
    const QString value = iniValue(ini, "Archive", key.toString(), "", a.maxListLength);
    if (value.isEmpty()) {
      continue;
    }
    for (const QString& name : value.split(',')) {
      out.append(name.trimmed());
    }
  }
  return out;
}

void IniDataArchives::addArchive(MOBase::IProfile* profile, int index,
                                 const QString& archiveName)
{
  QStringList current = archives(profile);
  if (current.contains(archiveName, Qt::CaseInsensitive)) {
    return;
  }
  current.insert(index != INT_MAX ? index : current.size(), archiveName);
  write(profile, current);
}

void IniDataArchives::removeArchive(MOBase::IProfile* profile, const QString& archiveName)
{
  QStringList current = archives(profile);
  if (!current.contains(archiveName, Qt::CaseInsensitive)) {
    return;
  }
  current.removeIf([&archiveName](const QString& archive) {
    return archive.compare(archiveName, Qt::CaseInsensitive) == 0;
  });
  write(profile, current);
}

void IniDataArchives::write(MOBase::IProfile* profile, const QStringList& archives)
{
  const ArchiveDefinition& a = m_Game->definition().archives;
  const QString ini          = iniPath(profile);
  const auto set             = [&ini](QStringView key, const QString& value) {
    if (!setIniValue(ini, "Archive", key.toString(), value)) {
      MOBase::log::warn("failed to set archives in \"{}\"", ini);
    }
  };
  const QString list = archives.join(", ");
  if (a.listKey2.isEmpty() || list.length() <= a.splitLength) {
    // a second key keeps what it had (MO2's rule; the list is read from both)
    set(a.listKey, list);
    return;
  }
  // split after the last name that fits, as MO2 does
  const qsizetype split = list.lastIndexOf(',', a.splitLength + 1);
  set(a.listKey, list.mid(0, split));
  set(a.listKey2, list.mid(split + 2));
}

}  // namespace mo2r::bethesda
