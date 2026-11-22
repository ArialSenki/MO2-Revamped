// MO2 Revamped: Skyrim VR, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_skyrimvr plugin: the same game to MO2 (name, short
// names, IDs, settings, folders, lists), written on the framework. Code only
// for what the definition can't say: the Enderal short names behind a
// setting, the Skyrim SE mods it takes, the Steam ID on its only variant, the
// lower-case Creation Club names and light plugins only with Skyrim VR ESL
// Support installed.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class SkyrimVRGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "com.soundcontactstudio.GameSkyrimVR" FILE "gameskyrimvr.json")

public:
  SkyrimVRGame();

  // Enderal's Nexus sections too, with the enderal_downloads setting
  QStringList validShortNames() const override;
  QStringList primarySources() const override;
  // on its only variant, "Regular"
  QString steamAPPId() const override;
  // in lower case, as the old plugin listed them
  QStringList CCPlugins() const override;

protected:
  void registerFeatures() override;
};

}  // namespace mo2r::bethesda
