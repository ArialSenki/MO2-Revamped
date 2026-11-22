// MO2 Revamped: Bethesda game framework, archive invalidation without a BSA.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The keys, values and the INI are MO2's (game_fallout4's
// Fallout4BSAInvalidation).
#include "resourcedirsinvalidation.h"

#include "core/bethesdagame.h"
#include "ini/inifiles.h"

#include <iprofile.h>
#include <log.h>

#include <cwchar>

namespace mo2r::bethesda
{

bool ResourceDirsInvalidation::prepareProfile(MOBase::IProfile* profile)
{
  if (!profile->invalidationActive(nullptr)) {
    return false;
  }
  const QString name = m_Game->definition().archives.resourceDirsIni.toString();
  const QString ini =
      profileIniFolder(profile, m_Game->documentsDirectory().absolutePath()) + "/" + name;
  const auto warn = [&name] {
    MOBase::log::warn("failed to override data directory in \"{}\"", name);
  };
  bool dirty = false;

  const auto older = iniValue(ini, "Archive", "bInvalidateOlderFiles", "0").toStdWString();
  if (older.empty() || std::wcstol(older.c_str(), nullptr, 10) != 1) {
    dirty = true;
    if (!setIniValue(ini, "Archive", "bInvalidateOlderFiles", "1")) {
      warn();
    }
  }

  // a missing key reads as the game's default, STRINGS\; the old plugin wrote
  // the empty value again (and asked MO2 for a refresh) even when it was there
  if (!iniValue(ini, "Archive", "sResourceDataDirsFinal", "STRINGS\\").isEmpty()) {
    dirty = true;
    if (!setIniValue(ini, "Archive", "sResourceDataDirsFinal", "")) {
      warn();
    }
  }
  return dirty;
}

}  // namespace mo2r::bethesda
