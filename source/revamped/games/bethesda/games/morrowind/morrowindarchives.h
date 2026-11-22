// MO2 Revamped: Morrowind, the archive list in Morrowind.ini.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's DataArchives feature for Morrowind: the BSAs the game loads besides
// Morrowind.bsa are its INI's [Archives] keys "Archive 0", "Archive 1", ...
// (the profile's Morrowind.ini with local settings, the game folder's
// without), one name each; the list is written again from scratch on every
// change, as MO2's game_morrowind did.
#pragma once

#include <dataarchives.h>

namespace MOBase
{
class IPluginGame;
}

namespace mo2r::bethesda
{

class MorrowindDataArchives : public MOBase::DataArchives
{
public:
  explicit MorrowindDataArchives(const MOBase::IPluginGame* game) : m_Game(game) {}

  QStringList vanillaArchives() const override;
  QStringList archives(const MOBase::IProfile* profile) const override;
  void addArchive(MOBase::IProfile* profile, int index, const QString& archiveName) override;
  void removeArchive(MOBase::IProfile* profile, const QString& archiveName) override;

private:
  QString iniPath(const MOBase::IProfile* profile) const;
  void write(MOBase::IProfile* profile, const QStringList& archives);

  const MOBase::IPluginGame* m_Game;
};

}  // namespace mo2r::bethesda
