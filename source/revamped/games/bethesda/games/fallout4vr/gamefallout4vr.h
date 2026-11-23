// MO2 Revamped: Fallout 4 VR, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_fallout4vr plugin: the same game to MO2 (name, short
// names, IDs, folders, lists), written on the framework. Code only for what
// the definition can't say: the Fallout 4 mods it takes, the Steam ID on its
// only variant, the lower-case Creation Club names and
// light plugins only with Fallout 4 VR ESL Support (or Daytripper) installed.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class Fallout4VRGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameFallout4VR" FILE "gamefallout4vr.json")

public:
  Fallout4VRGame();

public:  // IPluginGame
  QStringList primarySources() const override { return validShortNames(); }
  // on its only variant, "Regular"
  QString steamAPPId() const override;
  // in lower case, as the old plugin listed them
  QStringList CCPlugins() const override;

protected:
  void registerFeatures() override;
};

}  // namespace mo2r::bethesda
