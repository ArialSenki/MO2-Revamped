// MO2 Revamped: Bethesda game framework, load order in plugins.txt with '*'.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's GamePlugins feature for the games whose plugins.txt lists every
// plugin in load order, the active ones with a leading '*' (Skyrim SE,
// Fallout 4 and the later ones). The primary plugins (the game's masters, its
// DLC and the Creation Club ones) are left out of plugins.txt: the game loads
// them by itself. The profile also keeps loadorder.txt (all, in order) for
// the tools that read it.
//
// Which file wins on reading is MO2's rule, as for the file-time games:
// loadorder.txt, unless only plugins.txt changed since MO2 last read the
// lists (a tool edited it), in which case the order comes from plugins.txt.
#pragma once

#include <gameplugins.h>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <map>
#include <span>

namespace MOBase
{
class IOrganizer;
}

namespace mo2r::bethesda
{

class AsteriskGamePlugins : public MOBase::GamePlugins
{
public:
  // firstActive: lines written as active right after the header, before the
  // plugins in load order, whatever their state (Enderal SE's plugin wrote
  // its master and SkyUI_SE.esp so); empty: none
  AsteriskGamePlugins(MOBase::IOrganizer* organizer, bool lightPlugins,
                      std::span<const QStringView> firstActive = {});

  void writePluginLists(const MOBase::IPluginList* pluginList) override;
  void readPluginLists(MOBase::IPluginList* pluginList) override;
  QStringList getLoadOrder() override;
  bool lightPluginsAreSupported() override { return m_LightPlugins; }
  bool mediumPluginsAreSupported() override { return false; }

private:
  QString profileFile(const QString& name) const;
  // loadorder.txt should be read (rather than the order in plugins.txt)
  bool loadOrderFileWins() const;
  // the order from loadorder.txt; from plugins.txt if it can't be read
  QStringList readLoadOrder(MOBase::IPluginList* pluginList) const;
  // states and order from plugins.txt
  QStringList readPluginsTxt(MOBase::IPluginList* pluginList) const;
  // the primary plugins, then listed, then the plugins neither placed
  QStringList complete(const MOBase::IPluginList* pluginList, const QStringList& primary,
                       const QStringList& listed) const;
  // writes bytes unless they're what was written last (and the file exists)
  void commit(const QString& path, const std::string& bytes);

  MOBase::IOrganizer* m_Organizer;
  bool m_LightPlugins;
  std::span<const QStringView> m_FirstActive;
  QDateTime m_LastRead;
  std::map<QString, QByteArray> m_LastWritten;
};

}  // namespace mo2r::bethesda
