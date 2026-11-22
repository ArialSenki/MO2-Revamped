// MO2 Revamped: Bethesda game framework, load order by file time.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's GamePlugins feature for the games that order plugins by their file
// times (Oblivion, Fallout 3, New Vegas, Skyrim LE). The profile keeps
// plugins.txt (the active plugins) and loadorder.txt (all, in order); MO2 sets
// the file times from that order when a program starts.
//
// Which file wins on reading is MO2's rule: loadorder.txt, unless only
// plugins.txt changed since MO2 last read the lists (a tool edited it), in
// which case the order comes from the plugins' file times again.
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

class FileTimeGamePlugins : public MOBase::GamePlugins
{
public:
  // with orderFromLoadOrderTxt, reading the lists always takes the order
  // from loadorder.txt, also when only plugins.txt changed (Skyrim LE and
  // Enderal: the game reads the order from the lists, not from file times)
  explicit FileTimeGamePlugins(MOBase::IOrganizer* organizer,
                               bool orderFromLoadOrderTxt = false);

  void writePluginLists(const MOBase::IPluginList* pluginList) override;
  void readPluginLists(MOBase::IPluginList* pluginList) override;
  QStringList getLoadOrder() override;
  bool lightPluginsAreSupported() override { return false; }
  bool mediumPluginsAreSupported() override { return false; }

private:
  QString profileFile(const QString& name) const;
  // loadorder.txt should be read (rather than the order from file times)
  bool loadOrderFileWins() const;
  // the primary plugins and loadorder.txt's; from file times if it can't be read
  QStringList readLoadOrder(MOBase::IPluginList* pluginList) const;
  // states from plugins.txt, and the order from file times
  QStringList readActiveAndFileTimes(MOBase::IPluginList* pluginList) const;
  // writes bytes unless they're what was written last (and the file exists)
  void commit(const QString& path, const std::string& bytes);

  MOBase::IOrganizer* m_Organizer;
  bool m_OrderFromLoadOrderTxt;
  QDateTime m_LastRead;
  std::map<QString, QByteArray> m_LastWritten;
};

}  // namespace mo2r::bethesda
