// MO2 Revamped: Bethesda game framework, archive lists in the game's INI.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's DataArchives feature: the BSAs the game loads by itself are listed in
// its INI ([Archive] SArchiveList for Fallout 3 and New Vegas), comma
// separated; Skyrim SE splits it over SResourceArchiveList and
// SResourceArchiveList2. The profile's INI with local settings, the My Games
// one without.
#pragma once

#include <dataarchives.h>

namespace mo2r::bethesda
{

class BethesdaGame;

class IniDataArchives : public MOBase::DataArchives
{
public:
  explicit IniDataArchives(const BethesdaGame* game) : m_Game(game) {}

  QStringList vanillaArchives() const override;
  QStringList archives(const MOBase::IProfile* profile) const override;
  void addArchive(MOBase::IProfile* profile, int index, const QString& archiveName) override;
  void removeArchive(MOBase::IProfile* profile, const QString& archiveName) override;

private:
  QString iniPath(const MOBase::IProfile* profile) const;
  void write(MOBase::IProfile* profile, const QStringList& archives);

  const BethesdaGame* m_Game;
};

}  // namespace mo2r::bethesda
