// MO2 Revamped: Skyrim (2011), on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_skyrim plugin: the same game to MO2 (name, short names,
// IDs, settings, folders, lists), written on the framework. Code only for
// what the definition can't say: the Special Edition's Nexus section behind a
// setting, the load order mechanism by the game's version, the plugin lists
// kept in loadorder.txt's order and the saves' location text.
#pragma once

#include "core/bethesdagame.h"
#include "util/filecache.h"

namespace mo2r::bethesda
{

class SkyrimGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameSkyrim" FILE "gameskyrim.json")

public:
  SkyrimGame();

  // the Special Edition's Nexus section too, with the sse_downloads setting
  QStringList validShortNames() const override;
  // plugins.txt from 1.4.26 on (every Steam release since 2012), file times
  // before, as the game exe's version says
  LoadOrderMechanism loadOrderMechanism() const override;

protected:
  void registerFeatures() override;
  std::shared_ptr<const MOBase::ISaveGame> makeSaveGame(const QString& path) const override;

private:
  FileCache<LoadOrderMechanism> m_MechanismCache;
};

}  // namespace mo2r::bethesda
