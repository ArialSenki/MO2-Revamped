// MO2 Revamped: Fallout 4, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_fallout4 plugin: the same game to MO2 (name, short
// names, IDs, settings, folders, lists), written on the framework. Code only
// for what the definition can't say: the sTestFile lines of
// Fallout4Custom.ini (while there, the game ignores plugins.txt, so MO2 gets
// no load order to manage), the lower-case Creation Club names, the splash
// credit and the warnings.
#pragma once

#include "core/bethesdagame.h"
#include "diagnose/extenderdiagnose.h"
#include "util/filecache.h"

#include <iplugindiagnose.h>

namespace mo2r::bethesda
{

class Fallout4Game : public BethesdaGame, public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginFileMapper
                   MOBase::IPluginDiagnose)
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "org.tannin.GameFallout4" FILE "gamefallout4.json")

public:
  Fallout4Game();

  // the plugins in the profile's Fallout4Custom.ini sTestFile1-10 lines
  QStringList testFilePlugins() const;

public:  // IPlugin
  QString description() const override;

public:  // IPluginGame
  // on every variant (the old plugin has only "Regular")
  QString steamAPPId() const override;
  // with sTestFile lines, the game's masters and those; otherwise its masters
  // and the Creation Club ones
  QStringList primaryPlugins() const override;
  QStringList CCPlugins() const override;
  SortMechanism sortMechanism() const override;
  LoadOrderMechanism loadOrderMechanism() const override;

public:  // IPluginFileMapper
  // no plugin lists for the game while sTestFile lines decide
  MappingType mappings() const override;

public:  // IPluginDiagnose
  std::vector<unsigned int> activeProblems() const override;
  QString shortDescription(unsigned int key) const override;
  QString fullDescription(unsigned int key) const override;
  bool hasGuidedFix(unsigned int) const override { return false; }
  void startGuidedFix(unsigned int) const override {}

private:
  bool testFilePresent() const { return !testFilePlugins().isEmpty(); }

  // the old plugin's keys
  static constexpr unsigned int ProblemTestFile        = 1;
  static constexpr unsigned int ProblemExtenderVersion = 2;
  static constexpr unsigned int ProblemAddressLibrary  = 3;

  ExtenderDiagnose m_ExtenderDiagnose;
  FileCache<QStringList> m_TestFileCache;
};

}  // namespace mo2r::bethesda
