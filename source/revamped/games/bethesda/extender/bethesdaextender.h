// MO2 Revamped: Bethesda game framework, the game's script extender.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's ScriptExtender feature for xSE (NVSE, FOSE, SKSE, F4SE): its loader in
// the game folder, its plugin folder in Data and its co-save extension, from
// the game's definition.
#pragma once

#include <scriptextender.h>

namespace mo2r::bethesda
{

class BethesdaGame;

class BethesdaScriptExtender : public MOBase::ScriptExtender
{
public:
  explicit BethesdaScriptExtender(const BethesdaGame* game) : m_Game(game) {}

  QString BinaryName() const override;
  QString PluginPath() const override;
  QString loaderName() const override;
  QString loaderPath() const override;
  QString savegameExtension() const override;
  // the loader is in the game folder (the runtime DLLs alone, without it,
  // aren't how the extenders are installed)
  bool isInstalled() const override;
  QString getExtenderVersion() const override;
  WORD getArch() const override;

private:
  const BethesdaGame* m_Game;
};

}  // namespace mo2r::bethesda
