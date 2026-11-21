// MO2 Revamped: Morrowind, the plugin list in Morrowind.ini.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Morrowind has no plugins.txt: it loads the plugins its INI's [Game Files]
// section lists (GameFile0, GameFile1, ...), in the order of their file
// times. MO2 writes the active plugins there (the profile's Morrowind.ini
// with local settings, the game folder's without) and keeps loadorder.txt in
// the profile, all plugins in order, as for the other file-time games; the
// file times come from that order when a program starts.
//
// Which file wins on reading is MO2's rule: loadorder.txt, unless only the
// INI changed since MO2 last read the lists (a tool edited it), in which case
// the order comes from the file times again.
#pragma once

#include <gameplugins.h>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <map>

namespace MOBase
{
class IOrganizer;
}

namespace mo2r::bethesda
{

class MorrowindGamePlugins : public MOBase::GamePlugins
{
public:
  explicit MorrowindGamePlugins(MOBase::IOrganizer* organizer);

  void writePluginLists(const MOBase::IPluginList* pluginList) override;
  void readPluginLists(MOBase::IPluginList* pluginList) override;
  // loadorder.txt's order (the file times' if it can't be read), as MO2's
  // game_morrowind gave it
  QStringList getLoadOrder() override;
  bool lightPluginsAreSupported() override { return false; }
  bool mediumPluginsAreSupported() override { return false; }

private:
  // the INI with [Game Files]: the profile's or the game folder's
  QString iniPath() const;
  QString loadOrderPath() const;
  // the primary plugins and loadorder.txt's; from the INI if it can't be read
  QStringList readLoadOrder(MOBase::IPluginList* pluginList) const;
  // states from [Game Files], and the order from file times
  QStringList readGameFiles(MOBase::IPluginList* pluginList) const;
  void writeGameFiles(const QStringList& order, const MOBase::IPluginList* pluginList) const;

  MOBase::IOrganizer* m_Organizer;
  QDateTime m_LastRead;
  QByteArray m_LastLoadOrder;
};

}  // namespace mo2r::bethesda
