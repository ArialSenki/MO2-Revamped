// MO2 Revamped: Bethesda game framework, the game's script extender.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "bethesdaextender.h"

#include "core/bethesdagame.h"

#include <utility.h>

namespace mo2r::bethesda
{

QString BethesdaScriptExtender::BinaryName() const
{
  return m_Game->definition().extender.loader.toString();
}

QString BethesdaScriptExtender::PluginPath() const
{
  return m_Game->definition().extender.pluginPath.toString();
}

QString BethesdaScriptExtender::loaderName() const
{
  return BinaryName();
}

QString BethesdaScriptExtender::loaderPath() const
{
  return m_Game->gameDirectory().absoluteFilePath(loaderName());
}

QString BethesdaScriptExtender::savegameExtension() const
{
  return m_Game->coSaveExtension();
}

bool BethesdaScriptExtender::isInstalled() const
{
  return m_Game->gameDirectory().exists(loaderName());
}

QString BethesdaScriptExtender::getExtenderVersion() const
{
  return MOBase::getFileVersion(loaderPath());
}

WORD BethesdaScriptExtender::getArch() const
{
  return m_Game->executableArch(loaderName());
}

}  // namespace mo2r::bethesda
