// MO2 Revamped: Starfield, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_starfield plugin: the same game to MO2 (name, short
// name, IDs, settings, folders, lists), written on the framework. Starfield
// differs from the other Creation Engine games in where things are: mods go
// to My Games' Data (the game's own Data is a second data folder), the
// archive list is only read, and Creations are listed in AppData's
// ContentCatalog.txt. Like Fallout 4, sTestFile lines in StarfieldCustom.ini
// take the load order out of MO2's hands. With enable_loadorder_fix, MO2
// writes the game's masters into a Starfield.ccc of the profile and maps it
// into the game and My Games folders, so the game loads them first.
#pragma once

#include "core/bethesdagame.h"
#include "util/filecache.h"

#include <iplugindiagnose.h>

#include <set>

namespace mo2r::bethesda
{

class StarfieldGame : public BethesdaGame, public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginFileMapper
                   MOBase::IPluginDiagnose)
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.modorganizer.GameStarfield" FILE "gamestarfield.json")

public:
  StarfieldGame();

  // the plugins in the profile's StarfieldCustom.ini sTestFile1-10 lines
  QStringList testFilePlugins() const;

public:  // IPlugin
  bool init(MOBase::IOrganizer* organizer) override;

public:  // IPluginGame
  // My Games' Data, made if missing
  QDir dataDirectory() const override;
  QMap<QString, QDir> secondaryDataDirectories() const override;
  QString steamAPPId() const override;
  // the game's masters; with sTestFile lines, those plugins after them
  QStringList primaryPlugins() const override;
  // Starfield.ccc's (the profile's with enable_loadorder_fix) and
  // ContentCatalog.txt's Creations, lower case from the .ccc
  QStringList CCPlugins() const override;
  LoadOrderMechanism loadOrderMechanism() const override;

public:  // IPluginFileMapper
  MappingType mappings() const override;

public:  // IPluginDiagnose
  std::vector<unsigned int> activeProblems() const override;
  QString shortDescription(unsigned int key) const override;
  QString fullDescription(unsigned int key) const override;
  bool hasGuidedFix(unsigned int) const override { return false; }
  void startGuidedFix(unsigned int) const override {}

protected:
  void registerFeatures() override;
  // the game reads no INI MO2 has to set
  bool prepareIni(const QString&) override { return true; }

private:
  bool loadOrderFix() const;
  bool testFilePresent() const { return !testFilePlugins().isEmpty(); }
  bool activeEsp() const;
  // the profile's Starfield.ccc: the primary plugins, one per line
  void writeCccFile() const;

  // the old plugin's keys
  static constexpr unsigned int ProblemEsp      = 1;
  static constexpr unsigned int ProblemTestFile = 2;

  FileCache<QStringList> m_TestFileCache;
  mutable std::set<QString> m_ActiveEsps;
};

}  // namespace mo2r::bethesda
