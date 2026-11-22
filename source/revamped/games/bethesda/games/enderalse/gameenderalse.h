// MO2 Revamped: Enderal Special Edition, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_enderalse plugin: the same game to MO2 (name, short
// names, IDs, folders, lists), written on the framework. Code only for what
// the definition can't say: the Steam folder with Enderal's master, the game
// started through skse64_loader.exe with its own launcher next to it, both
// INIs from the game's defaults and plugins.txt opening with the game's
// master and SkyUI.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class EnderalSEGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "com.soundcontactstudio.GameEnderalSE" FILE "gameenderalse.json")

public:
  EnderalSEGame();

public:  // IPluginGame
  // skse64_loader.exe is in Skyrim SE's folder too: the launcher tells them
  // apart
  bool looksValid(const QDir& path) const override;
  QIcon gameIcon() const override;
  // Enderal.ini and EnderalPrefs.ini both from the game's defaults
  void initializeProfile(const QDir& directory, ProfileSettings settings) const override;

protected:
  void registerFeatures() override;
  // Bethesda's key and GOG's, then a Steam library with Enderal's master
  QString identifyGamePath() const override;
};

}  // namespace mo2r::bethesda
