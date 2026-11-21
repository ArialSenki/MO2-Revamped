// MO2 Revamped: Bethesda game framework, load order in plugins.txt with '*'.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// When each list is read or written, and what happens with missing or broken
// files, is MO2's behaviour (game_creation's CreationGamePlugins on top of
// game_gamebryo's GamebryoGamePlugins); the text formats are in
// loadorderfiles.
#include "asteriskplugins.h"

#include "plugins/loadorderfiles.h"

#include <imoinfo.h>
#include <iplugingame.h>
#include <ipluginlist.h>
#include <iprofile.h>
#include <log.h>
#include <report.h>
#include <safewritefile.h>

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>

#include <algorithm>
#include <optional>

using MOBase::IPluginList;

namespace mo2r::bethesda
{

namespace
{
  // a profile's lists are a few hundred KB with thousands of plugins
  constexpr qint64 MaxListFile = 64 * 1024 * 1024;

  // the file's bytes; nullopt if it can't be opened
  std::optional<std::string> readListFile(const QString& path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return std::nullopt;
    }
    if (file.size() > MaxListFile) {
      MOBase::log::error("'{}' is {} bytes, not read", path, file.size());
      return std::string();
    }
    const QByteArray bytes = file.readAll();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
  }

  std::vector<std::wstring> toWide(const QStringList& names)
  {
    std::vector<std::wstring> out;
    out.reserve(static_cast<std::size_t>(names.size()));
    for (const QString& name : names) {
      out.push_back(name.toStdWString());
    }
    return out;
  }
}  // namespace

AsteriskGamePlugins::AsteriskGamePlugins(MOBase::IOrganizer* organizer, bool lightPlugins,
                                         bool mediumPlugins,
                                         std::span<const QStringView> firstActive)
    : m_Organizer(organizer), m_LightPlugins(lightPlugins), m_MediumPlugins(mediumPlugins),
      m_FirstActive(firstActive)
{}

QString AsteriskGamePlugins::profileFile(const QString& name) const
{
  return m_Organizer->profile()->absolutePath() + "/" + name;
}

bool AsteriskGamePlugins::loadOrderFileWins() const
{
  // loadorder.txt, unless MO2 read the lists before and since then only
  // plugins.txt changed (a tool wrote it): then both the states and the
  // order come from it
  const QFileInfo loadOrder(profileFile("loadorder.txt"));
  const QFileInfo plugins(profileFile("plugins.txt"));
  const bool loadOrderIsNew = !m_LastRead.isValid() || !loadOrder.exists() ||
                              loadOrder.lastModified() > m_LastRead;
  const bool pluginsIsNew = !m_LastRead.isValid() || plugins.lastModified() > m_LastRead;
  return loadOrderIsNew || !pluginsIsNew;
}

void AsteriskGamePlugins::readPluginLists(IPluginList* pluginList)
{
  if (loadOrderFileWins()) {
    pluginList->setLoadOrder(readLoadOrder(pluginList));
    readPluginsTxt(pluginList);
  } else {
    pluginList->setLoadOrder(readPluginsTxt(pluginList));
  }
  m_LastRead = QDateTime::currentDateTime();
}

QStringList AsteriskGamePlugins::getLoadOrder()
{
  return loadOrderFileWins() ? readLoadOrder(m_Organizer->pluginList())
                             : readPluginsTxt(m_Organizer->pluginList());
}

QStringList AsteriskGamePlugins::complete(const IPluginList* pluginList,
                                          const QStringList& primary,
                                          const QStringList& listed) const
{
  QStringList out;
  for (const std::wstring& name :
       completeLoadOrder(toWide(primary), toWide(listed), toWide(pluginList->pluginNames()))) {
    out.append(QString::fromStdWString(name));
  }
  return out;
}

QStringList AsteriskGamePlugins::readLoadOrder(IPluginList* pluginList) const
{
  const auto bytes = readListFile(profileFile("loadorder.txt"));
  if (!bytes) {
    return readPluginsTxt(pluginList);
  }
  QStringList listed;
  for (const std::wstring& name : readLoadOrderTxt(*bytes)) {
    listed.append(QString::fromStdWString(name));
  }
  return complete(pluginList, m_Organizer->managedGame()->primaryPlugins(), listed);
}

QStringList AsteriskGamePlugins::readPluginsTxt(IPluginList* pluginList) const
{
  const QStringList primary = m_Organizer->managedGame()->primaryPlugins();
  if (!pluginsTxtInUse()) {
    // the game doesn't read it: states stay as they are
    return complete(pluginList, primary, {});
  }
  for (const QString& name : primary) {
    if (pluginList->state(name) != IPluginList::STATE_MISSING) {
      pluginList->setState(name, IPluginList::STATE_ACTIVE);
    }
  }

  // MO2 always writes its header there, so an empty file is a broken one;
  // then, as with no file, the other plugins keep their states
  const QString path = profileFile("plugins.txt");
  const auto bytes   = readListFile(path);
  if (!bytes) {
    MOBase::log::warn("{} not found", path);
    return complete(pluginList, primary, {});
  }
  if (bytes->empty()) {
    MOBase::log::warn("{} empty", path);
    return complete(pluginList, primary, {});
  }

  QSet<QString> primarySet;
  for (const QString& name : primary) {
    primarySet.insert(name.toCaseFolded());
  }
  QStringList listed;
  QSet<QString> found;
  for (const PluginState& entry : bethesda::readPluginsTxt(*bytes, PluginsTxtStyle::Asterisk)) {
    const QString name = QString::fromStdWString(entry.name);
    const QString key  = name.toCaseFolded();
    found.insert(key);
    // a primary plugin listed (with '*' or without) keeps its place and state
    if (primarySet.contains(key)) {
      continue;
    }
    pluginList->setState(name, entry.active ? IPluginList::STATE_ACTIVE
                                            : IPluginList::STATE_INACTIVE);
    listed.append(name);
  }
  // not in plugins.txt: inactive (a primary plugin stays loaded, MO2 forces it)
  for (const QString& name : pluginList->pluginNames()) {
    if (!found.contains(name.toCaseFolded())) {
      pluginList->setState(name, IPluginList::STATE_INACTIVE);
    }
  }
  return complete(pluginList, primary, listed);
}

void AsteriskGamePlugins::writePluginLists(const IPluginList* pluginList)
{
  if (!m_LastRead.isValid()) {
    // never read: writing now would replace the lists with an empty state
    return;
  }

  QStringList names = pluginList->pluginNames();
  std::sort(names.begin(), names.end(), [pluginList](const QString& a, const QString& b) {
    return pluginList->priority(a) < pluginList->priority(b);
  });
  std::vector<PluginState> order;
  order.reserve(static_cast<std::size_t>(names.size()));
  for (const QString& name : names) {
    order.push_back({name.toStdWString(), pluginList->state(name) == IPluginList::STATE_ACTIVE});
  }

  // the Creation Club plugins are primary ones too: the game loads them from
  // its .ccc, so they're left out like the masters
  if (pluginsTxtInUse()) {
    WrittenList plugins = writePluginsTxt(
        order, toWide(m_Organizer->managedGame()->primaryPlugins()), PluginsTxtStyle::Asterisk);
    if (!m_FirstActive.empty()) {
      std::string first;
      for (const QStringView name : m_FirstActive) {
        first += '*';
        first += name.toString().toLocal8Bit().toStdString();
        first += "\r\n";
      }
      plugins.bytes.insert(MO2Header.size(), first);
    }
    for (const std::wstring& name : plugins.unencodable) {
      MOBase::log::error("invalid plugin name {}", QString::fromStdWString(name));
    }
    if (!plugins.unencodable.empty()) {
      MOBase::reportError(QObject::tr(
          "Some of your plugins have invalid names! These plugins can not be loaded by "
          "the game. Please see mo_interface.log for a list of affected plugins and "
          "rename them."));
    }
    // written even with nothing but the header, as MO2 does for these games
    // (with every plugin inactive that is the right file)
    commit(profileFile("plugins.txt"), plugins.bytes);
  }

  if (order.empty()) {
    MOBase::log::warn("plugin list would be empty, this is almost certainly wrong. Not "
                      "saving.");
  } else {
    commit(profileFile("loadorder.txt"), writeLoadOrderTxt(order));
  }

  m_LastRead = QDateTime::currentDateTime();
}

void AsteriskGamePlugins::commit(const QString& path, const std::string& bytes)
{
  MOBase::SafeWriteFile file(path);
  file->resize(0);
  file->write(bytes.data(), static_cast<qint64>(bytes.size()));
  file.commitIfDifferent(m_LastWritten[path]);
}

}  // namespace mo2r::bethesda
