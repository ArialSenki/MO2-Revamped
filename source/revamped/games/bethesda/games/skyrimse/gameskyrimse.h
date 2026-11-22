// MO2 Revamped: Skyrim Special Edition, on the Bethesda game framework.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Replaces MO2's game_skyrimse plugin: the same game to MO2 (name, short
// names, IDs, settings, folders, lists), written on the framework. Code only
// for the Enderal short names and the script extender warnings.
#pragma once

#include "core/bethesdagame.h"
#include "diagnose/extenderdiagnose.h"

#include <iplugindiagnose.h>

namespace mo2r::bethesda
{

class SkyrimSEGame : public BethesdaGame, public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginFileMapper
                   MOBase::IPluginDiagnose)
  // the old plugin's IID; MO2 loads either the same way
  Q_PLUGIN_METADATA(IID "com.soundcontactstudio.GameSkyrimSE" FILE "gameskyrimse.json")

public:
  SkyrimSEGame();

  // Enderal's Nexus sections too, with the enderal_downloads setting
  QStringList validShortNames() const override;

public:  // IPluginDiagnose
  std::vector<unsigned int> activeProblems() const override;
  QString shortDescription(unsigned int key) const override;
  QString fullDescription(unsigned int key) const override;
  bool hasGuidedFix(unsigned int) const override { return false; }
  void startGuidedFix(unsigned int) const override {}

private:
  // the old plugin's keys
  static constexpr unsigned int ProblemExtenderVersion = 1;
  static constexpr unsigned int ProblemAddressLibrary  = 2;

  ExtenderDiagnose m_ExtenderDiagnose;
};

}  // namespace mo2r::bethesda
