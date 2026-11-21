// MO2 Revamped: Tale of Two Wastelands, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_ttw plugin: the same game to MO2 (name, short names,
// IDs, settings, folders, lists), written on the framework. TTW is Fallout 3
// played in New Vegas: New Vegas' folders, executables, saves and NVSE, with
// both games' masters primary and mods from both Nexus sections. Code only
// for the names MO2 shows and LOOT knows, and the Steam app.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class TtwGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameFalloutTTW" FILE "gamettw.json")

public:
  TtwGame();

  // "Tale of Two Wastelands" (the game's name is "TTW")
  QString displayGameName() const override;
  // LOOT sorts it as New Vegas
  QString lootGameName() const override;
  // New Vegas' mods, from its Nexus section
  QStringList primarySources() const override;
  // New Vegas' apps, PCR edition included
  QString steamAPPId() const override;
};

}  // namespace mo2r::bethesda
