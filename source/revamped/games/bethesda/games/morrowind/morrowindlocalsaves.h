// MO2 Revamped: Morrowind, saves kept in the profile.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's LocalSavegames feature for Morrowind, which has no setting for where
// saves go: they are in the game folder's Saves. With the profile's local
// saves on, MO2 maps the profile's saves there and moves the game's own
// folder aside as _Saves (and back when they're turned off), as MO2's
// game_morrowind did.
#pragma once

#include <localsavegames.h>

namespace MOBase
{
class IPluginGame;
}

namespace mo2r::bethesda
{

class MorrowindLocalSavegames : public MOBase::LocalSavegames
{
public:
  explicit MorrowindLocalSavegames(const MOBase::IPluginGame* game) : m_Game(game) {}

  MappingType mappings(const QDir& profileSaveDir) const override;
  // true if the game's Saves folder was moved
  bool prepareProfile(MOBase::IProfile* profile) override;

private:
  const MOBase::IPluginGame* m_Game;
};

}  // namespace mo2r::bethesda
