// MO2 Revamped: Morrowind, the archive list in Morrowind.ini.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The keys and when they're written are MO2's (game_morrowind's
// MorrowindDataArchives).
#include "morrowindarchives.h"

#include "ini/inifiles.h"

#include <iplugingame.h>
#include <iprofile.h>
#include <log.h>

#include <QDir>

#include <Windows.h>

#include <climits>

namespace mo2r::bethesda
{

namespace
{
  // the old plugin's buffer
  constexpr int MaxName = 256;
  // a broken INI can't make the read loop forever
  constexpr int MaxArchives = 65536;
}  // namespace

QStringList MorrowindDataArchives::vanillaArchives() const
{
  return {"Morrowind.bsa"};
}

QString MorrowindDataArchives::iniPath(const MOBase::IProfile* profile) const
{
  return profile->localSettingsEnabled()
             ? QDir(profile->absolutePath()).absoluteFilePath("morrowind.ini")
             : m_Game->gameDirectory().absoluteFilePath("morrowind.ini");
}

QStringList MorrowindDataArchives::archives(const MOBase::IProfile* profile) const
{
  const QString ini = QDir::toNativeSeparators(iniPath(profile));
  QStringList out;
  for (int i = 0; i < MaxArchives; ++i) {
    const QString name = iniValue(ini, "Archives", "Archive " + QString::number(i), "", MaxName);
    if (name.isEmpty()) {
      break;
    }
    out.append(name.trimmed());
  }
  return out;
}

void MorrowindDataArchives::addArchive(MOBase::IProfile* profile, int index,
                                       const QString& archiveName)
{
  QStringList current = archives(profile);
  if (current.contains(archiveName, Qt::CaseInsensitive)) {
    return;
  }
  current.insert(index != INT_MAX ? index : current.size(), archiveName);
  write(profile, current);
}

void MorrowindDataArchives::removeArchive(MOBase::IProfile* profile, const QString& archiveName)
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

void MorrowindDataArchives::write(MOBase::IProfile* profile, const QStringList& archives)
{
  const QString ini    = iniPath(profile);
  const std::wstring w = ini.toStdWString();
  ::WritePrivateProfileSectionW(L"Archives", nullptr, w.c_str());
  int written = 0;
  for (const QString& name : archives) {
    if (!setIniValue(ini, "Archives", "Archive " + QString::number(written), name)) {
      MOBase::log::warn("failed to set archives in \"{}\"", ini);
    }
    ++written;
  }
}

}  // namespace mo2r::bethesda
