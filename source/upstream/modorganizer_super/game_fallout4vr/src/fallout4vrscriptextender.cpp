#include "fallout4vrscriptextender.h"

#include <QString>
#include <QStringList>

Fallout4VRScriptExtender::Fallout4VRScriptExtender(GameGamebryo const* game)
    : GamebryoScriptExtender(game)
{}

QString Fallout4VRScriptExtender::BinaryName() const
{
  // MO2 Revamped: F4SEVR's loader (f4se_loader.exe is the flat game's)
  return "f4sevr_loader.exe";
}

QString Fallout4VRScriptExtender::PluginPath() const
{
  return "f4se/plugins";
}
