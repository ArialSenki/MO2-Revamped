// MO2 Revamped: Bethesda game framework, saves kept in the profile.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The steps and values are MO2's (game_gamebryo's GamebryoLocalSavegames).
#include "localsavegames.h"

#include "core/bethesdagame.h"
#include "ini/inifiles.h"

#include <iprofile.h>

#include <QDir>
#include <QFile>

#include <utility>

namespace mo2r::bethesda
{

namespace
{
  // the game reads sLocalSavePath relative to its My Games folder
  constexpr QStringView SavesDummy = u"__MO_Saves\\";
  // markers for "the key isn't there" (no real value looks like them)
  constexpr QStringView Skip   = u"SKIP_ME";
  constexpr QStringView Delete = u"DELETE_ME";
  constexpr int MaxValue       = 260;  // MAX_PATH, as the games read them
}  // namespace

QDir IniLocalSavegames::localSavesDirectory() const
{
  return QDir(m_Game->myGamesPath()).absoluteFilePath(SavesDummy.toString());
}

MappingType IniLocalSavegames::mappings(const QDir& profileSaveDir) const
{
  return {{profileSaveDir.absolutePath(), localSavesDirectory().absolutePath(), true, true}};
}

bool IniLocalSavegames::prepareProfile(MOBase::IProfile* profile)
{
  const bool enable = profile->localSavesEnabled();
  const QString ini = profileIniFolder(profile, m_Game->myGamesPath()) + "/" +
                      m_Game->definition().localSavesIni.toString();
  const QString saved = profile->absolutePath() + "/savepath.ini";

  const QString path    = iniValue(ini, "General", "sLocalSavePath", Skip.toString(), MaxValue);
  const QString myGames = iniValue(ini, "General", "bUseMyGamesDirectory", Skip.toString(), MaxValue);
  const bool alreadyEnabled = path == SavesDummy;

  if (enable) {
    QDir saves = localSavesDirectory();
    if (!saves.exists()) {
      saves.mkdir(".");
    }
  }

  if (enable && !alreadyEnabled) {
    // the user's own values, for when local saves are turned off again
    if (path != Skip) {
      setIniValue(saved, "General", "sLocalSavePath", path);
    }
    if (myGames != Skip) {
      setIniValue(saved, "General", "bUseMyGamesDirectory", myGames);
    }
    setIniValue(ini, "General", "sLocalSavePath", SavesDummy.toString());
    setIniValue(ini, "General", "bUseMyGamesDirectory", "1");
  }

  if (!enable && alreadyEnabled) {
    if (QFile::exists(saved)) {
      const QString oldPath =
          iniValue(saved, "General", "sLocalSavePath", Delete.toString(), MaxValue);
      const QString oldMyGames =
          iniValue(saved, "General", "bUseMyGamesDirectory", Delete.toString(), MaxValue);
      // a value the user didn't have is removed again
      for (const auto& [key, value] : {std::pair{QStringLiteral("sLocalSavePath"), oldPath},
                                       std::pair{QStringLiteral("bUseMyGamesDirectory"),
                                                 oldMyGames}}) {
        if (value != Delete) {
          setIniValue(ini, "General", key, value);
        } else {
          removeIniValue(ini, "General", key);
        }
      }
      QFile::remove(saved);
    } else {
      removeIniValue(ini, "General", "sLocalSavePath");
      removeIniValue(ini, "General", "bUseMyGamesDirectory");
    }
  }

  return enable != alreadyEnabled;
}

}  // namespace mo2r::bethesda
