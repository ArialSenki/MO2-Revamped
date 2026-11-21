// MO2 Revamped: Morrowind, saves kept in the profile.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The folders and the steps are MO2's (game_morrowind's
// MorrowindLocalSavegames).
#include "morrowindlocalsaves.h"

#include <iplugingame.h>
#include <iprofile.h>
#include <log.h>

#include <QDir>

namespace mo2r::bethesda
{

bool MorrowindLocalSavegames::prepareProfile(MOBase::IProfile* profile)
{
  QDir game = m_Game->gameDirectory();
  if (profile->localSavesEnabled()) {
    if (game.exists("Saves")) {
      if (!game.rename("Saves", "_Saves")) {
        MOBase::log::error("Unable to enable Morrowind local save games!");
      }
      return true;
    }
  } else if (game.exists("_Saves")) {
    if (!game.rename("_Saves", "Saves")) {
      MOBase::log::error("Unable to disable Morrowind local save games!");
    }
    return true;
  }
  return false;
}

MappingType MorrowindLocalSavegames::mappings(const QDir& profileSaveDir) const
{
  return {{profileSaveDir.absolutePath(), m_Game->gameDirectory().absoluteFilePath("Saves"),
           true, true}};
}

}  // namespace mo2r::bethesda
