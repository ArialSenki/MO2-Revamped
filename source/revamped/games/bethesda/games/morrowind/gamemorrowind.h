// MO2 Revamped: Morrowind, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_morrowind plugin: the same game to MO2 (name, short
// name, IDs, folders, lists), written on the framework. Morrowind is older
// than the rest: everything is in its own folder (Morrowind.ini, Saves, "Data
// Files"), the plugin and archive lists are in Morrowind.ini, and there is no
// script extender MO2 knows of (MWSE is started by its launcher). Its own
// features are in this folder; the BSA invalidation, data checks and
// unmanaged mods are the framework's.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class MorrowindGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "com.schilduin.GameMorrowind" FILE "gamemorrowind.json")

public:
  MorrowindGame();

public:  // IPlugin
  QString description() const override;

public:  // IPluginGame
  // the game folder: Morrowind.ini and Saves are there
  QDir documentsDirectory() const override;
  QDir savesDirectory() const override;
  // plugins.txt from AppData, Morrowind.ini from the game folder
  void initializeProfile(const QDir& directory, ProfileSettings settings) const override;

protected:
  void registerFeatures() override;
  // Bethesda's key, then a Steam library's Morrowind folder
  QString identifyGamePath() const override;
  std::shared_ptr<const MOBase::ISaveGame> makeSaveGame(const QString& path) const override;
};

}  // namespace mo2r::bethesda
