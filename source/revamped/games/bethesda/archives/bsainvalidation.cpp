// MO2 Revamped: Bethesda game framework, BSA (archive) invalidation.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The steps and the INI values are MO2's (game_gamebryo's
// GamebryoBSAInvalidation).
#include "bsainvalidation.h"

#include "archives/dummybsa.h"
#include "core/bethesdagame.h"
#include "ini/inifiles.h"

#include <dataarchives.h>
#include <iprofile.h>
#include <log.h>
#include <utility.h>

#include <QFile>

#include <cwchar>

namespace mo2r::bethesda
{

namespace
{
  constexpr QStringView InvalidationFile = u"ArchiveInvalidation.txt";
}

QString DummyBsaInvalidation::bsaName() const
{
  return m_Game->definition().archives.invalidationBsa.toString();
}

bool DummyBsaInvalidation::isInvalidationBSA(const QString& name)
{
  return name.compare(bsaName(), Qt::CaseInsensitive) == 0;
}

bool DummyBsaInvalidation::prepareProfile(MOBase::IProfile* profile)
{
  const ArchiveDefinition& a = m_Game->definition().archives;
  const QString ini =
      profileIniFolder(profile, m_Game->documentsDirectory().absolutePath()) + "/" +
      a.iniFile.toString();
  const auto warn = [&] {
    MOBase::log::warn("failed to activate BSA invalidation in \"{}\"", a.iniFile.toString());
  };
  bool dirty = false;

  const auto older =
      iniValue(ini, "Archive", "bInvalidateOlderFiles", "0").toStdWString();
  if (older.empty() || std::wcstol(older.c_str(), nullptr, 10) != 1) {
    dirty = true;
    if (!setIniValue(ini, "Archive", "bInvalidateOlderFiles", "1")) {
      warn();
    }
  }

  const QString bsa = m_Game->dataDirectory().absoluteFilePath(bsaName());
  if (profile->invalidationActive(nullptr)) {
    bool listed = false;
    for (const QString& archive : m_Archives->archives(profile)) {
      listed = listed || isInvalidationBSA(archive);
    }
    if (!listed) {
      m_Archives->addArchive(profile, 0, bsaName());
      dirty = true;
    }
    if (!QFile::exists(bsa)) {
      if (!writeDummyBsa(bsa, a.invalidationBsaVersion)) {
        MOBase::log::error("failed to write \"{}\"", bsa);
      }
      dirty = true;
    }
    // a missing key reads as the default, which isn't empty
    if (!iniValue(ini, "Archive", "SInvalidationFile", InvalidationFile.toString()).isEmpty()) {
      dirty = true;
      if (!setIniValue(ini, "Archive", "SInvalidationFile", "")) {
        warn();
      }
    }
  } else {
    for (const QString& archive : m_Archives->archives(profile)) {
      if (isInvalidationBSA(archive)) {
        m_Archives->removeArchive(profile, archive);
        dirty = true;
      }
    }
    if (QFile::exists(bsa)) {
      MOBase::shellDeleteQuiet(bsa);
      dirty = true;
    }
    if (iniValue(ini, "Archive", "SInvalidationFile", "") != InvalidationFile) {
      dirty = true;
      if (!setIniValue(ini, "Archive", "SInvalidationFile", InvalidationFile.toString())) {
        warn();
      }
    }
  }
  return dirty;
}

}  // namespace mo2r::bethesda
