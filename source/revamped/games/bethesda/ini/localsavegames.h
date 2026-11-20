// MO2 Revamped: Bethesda game framework, saves kept in the profile.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's LocalSavegames feature: with the profile's local saves on, the game's
// INI points sLocalSavePath at My Games/<game>/__MO_Saves\ and MO2 maps the
// profile's saves folder there. The user's own values are kept in the
// profile's savepath.ini and put back when local saves are turned off. The
// folder name, keys and savepath.ini are MO2's, so profiles move between the
// old and new plugins unchanged.
#pragma once

#include <localsavegames.h>

namespace mo2r::bethesda
{

class BethesdaGame;

class IniLocalSavegames : public MOBase::LocalSavegames
{
public:
  explicit IniLocalSavegames(const BethesdaGame* game) : m_Game(game) {}

  MappingType mappings(const QDir& profileSaveDir) const override;
  // true if the INI changed
  bool prepareProfile(MOBase::IProfile* profile) override;

private:
  QDir localSavesDirectory() const;

  const BethesdaGame* m_Game;
};

}  // namespace mo2r::bethesda
