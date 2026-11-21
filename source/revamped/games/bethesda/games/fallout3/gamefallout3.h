// MO2 Revamped: Fallout 3, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_fallout3 plugin: the same game to MO2 (name, short
// names, IDs, folders, lists), written on the framework. Code only for the
// Steam app and the launcher, which depend on the edition installed.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class Fallout3Game : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameFallout3" FILE "gamefallout3.json")

public:
  Fallout3Game();

  // the Game of the Year edition's app when that variant is chosen or Steam
  // has the install as that app, otherwise the first release's
  QString steamAPPId() const override;
  // Fallout3Launcher.exe where it exists (Steam's Game of the Year edition),
  // FalloutLauncher.exe otherwise
  QString getLauncherName() const override;
};

}  // namespace mo2r::bethesda
