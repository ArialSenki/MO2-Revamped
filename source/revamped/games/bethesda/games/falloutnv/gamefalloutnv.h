// MO2 Revamped: Fallout New Vegas, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_falloutNV plugin: the same game to MO2 (name, short
// names, IDs, settings, folders, lists), written on the framework. The game is
// its definition; only the Steam ID needs code.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class FalloutNVGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameFalloutNV" FILE "gamefalloutnv.json")

public:
  FalloutNVGame();

  // the Steam store's PCR (Polish, Czech, Russian) edition has its own app
  QString steamAPPId() const override;
};

}  // namespace mo2r::bethesda
