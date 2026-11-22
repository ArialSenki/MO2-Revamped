// MO2 Revamped: Oblivion, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_oblivion plugin: the same game to MO2 (name, short
// names, IDs, settings, folders, lists), written on the framework. Code only
// for the Nehrim short name, which a setting adds, and OBSE's forced loads.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class OblivionGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameOblivion" FILE "gameoblivion.json")

public:
  OblivionGame();

  // Nehrim's Nexus section, with the nehrim_downloads setting
  QStringList validShortNames() const override;
  // OBSE's DLLs, loaded into the game and the Construction Set
  QList<MOBase::ExecutableForcedLoadSetting> executableForcedLoads() const override;
};

}  // namespace mo2r::bethesda
