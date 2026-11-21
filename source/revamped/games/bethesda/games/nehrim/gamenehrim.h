// MO2 Revamped: Nehrim, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_nehrim plugin: the same game to MO2 (name, short
// names, IDs, folders, lists), written on the framework. Nehrim is a total
// conversion installed over Oblivion's own files: Oblivion's registry key
// finds it, Oblivion.exe runs it and a new profile starts from Oblivion's
// plugins.txt, while its My Games and AppData folders are Nehrim's.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class NehrimGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameNehrim" FILE "gamenehrim.json")

public:
  NehrimGame();

  // Oblivion's mods, from its Nexus section
  QStringList primarySources() const override;
  // OBSE's DLLs, as for Oblivion
  QList<MOBase::ExecutableForcedLoadSetting> executableForcedLoads() const override;
  // plugins.txt from Oblivion's AppData folder, the INIs from Nehrim's My Games
  void initializeProfile(const QDir& directory, ProfileSettings settings) const override;

protected:
  // Oblivion's registry key, when that folder has Nehrim
  QString identifyGamePath() const override;
};

}  // namespace mo2r::bethesda
