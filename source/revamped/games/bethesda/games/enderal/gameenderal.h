// MO2 Revamped: Enderal (on Skyrim 2011), on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_enderal plugin: the same game to MO2 (name, short
// names, IDs, settings, folders, lists), written on the framework. Code only
// for what the definition can't say: SureAI's registry key, the game started
// through skse_loader.exe with its own launcher next to it, both INIs from
// the game's defaults, the Special Edition's Nexus section behind a setting
// and the plugin lists kept in loadorder.txt's order.
#pragma once

#include "core/bethesdagame.h"

namespace mo2r::bethesda
{

class EnderalGame : public BethesdaGame
{
  Q_OBJECT
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameEnderal" FILE "gameenderal.json")

public:
  EnderalGame();

public:  // IPluginGame
  // Skyrim's Nexus section, and the Special Edition's with sse_downloads
  QStringList validShortNames() const override;
  QStringList primarySources() const override;
  // plugins.txt, as MO2's plugin said (the lists are file-time style)
  LoadOrderMechanism loadOrderMechanism() const override;
  // skse_loader.exe is in Skyrim's folder too: the launcher tells them apart
  bool looksValid(const QDir& path) const override;
  QIcon gameIcon() const override;
  // enderal.ini and enderalprefs.ini both from the game's defaults
  void initializeProfile(const QDir& directory, ProfileSettings settings) const override;

protected:
  void registerFeatures() override;
  // SureAI's key, then a Steam library with Enderal's master
  QString identifyGamePath() const override;
  std::shared_ptr<const MOBase::ISaveGame> makeSaveGame(const QString& path) const override;
};

}  // namespace mo2r::bethesda
