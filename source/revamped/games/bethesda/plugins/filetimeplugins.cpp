// MO2 Revamped: Bethesda game framework, load order by file time.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// When each list is read or written, and what happens with missing or broken
// files, is MO2's behaviour (game_gamebryo's GamebryoGamePlugins); the text
// formats are in loadorderfiles.
#include "filetimeplugins.h"

#include "plugins/loadorderfiles.h"

#include <imodinterface.h>
#include <imodlist.h>
#include <imoinfo.h>
#include <iplugingame.h>
#include <ipluginlist.h>
#include <iprofile.h>
#include <log.h>
#include <report.h>
#include <safewritefile.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <optional>
#include <set>

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
}  // namespace

FileTimeGamePlugins::FileTimeGamePlugins(MOBase::IOrganizer* organizer)
    : m_Organizer(organizer)
{}

QString FileTimeGamePlugins::profileFile(const QString& name) const
{
  return m_Organizer->profile()->absolutePath() + "/" + name;
}

bool FileTimeGamePlugins::loadOrderFileWins() const
{
  // loadorder.txt, unless MO2 read the lists before and since then only
  // plugins.txt changed (a tool wrote it): then the active plugins come from
  // it and the order from the file times
  const QFileInfo loadOrder(profileFile("loadorder.txt"));
  const QFileInfo plugins(profileFile("plugins.txt"));
  const bool loadOrderIsNew = !m_LastRead.isValid() || !loadOrder.exists() ||
                              loadOrder.lastModified() > m_LastRead;
  const bool pluginsIsNew = !m_LastRead.isValid() || plugins.lastModified() > m_LastRead;
  return loadOrderIsNew || !pluginsIsNew;
}

void FileTimeGamePlugins::readPluginLists(IPluginList* pluginList)
{
  if (loadOrderFileWins()) {
    pluginList->setLoadOrder(readLoadOrder(pluginList));
    readActiveAndFileTimes(pluginList);
  } else {
    pluginList->setLoadOrder(readActiveAndFileTimes(pluginList));
  }
  m_LastRead = QDateTime::currentDateTime();
}

QStringList FileTimeGamePlugins::getLoadOrder()
{
  return loadOrderFileWins() ? readLoadOrder(m_Organizer->pluginList())
                             : readActiveAndFileTimes(m_Organizer->pluginList());
}

QStringList FileTimeGamePlugins::readLoadOrder(IPluginList* pluginList) const
{
  const auto bytes = readListFile(profileFile("loadorder.txt"));
  if (!bytes) {
    return readActiveAndFileTimes(pluginList);
  }
  // the primary plugins first, always; then the file's, each once
  QStringList order = m_Organizer->managedGame()->primaryPlugins();
  std::set<QString> seen;
  for (const QString& name : order) {
    seen.insert(name.toLower());
  }
  for (const std::wstring& name : readLoadOrderTxt(*bytes)) {
    QString plugin = QString::fromStdWString(name);
    if (seen.insert(plugin.toLower()).second) {
      order.append(std::move(plugin));
    }
  }
  return order;
}

QStringList FileTimeGamePlugins::readActiveAndFileTimes(IPluginList* pluginList) const
{
  const QStringList primary = m_Organizer->managedGame()->primaryPlugins();
  for (const QString& name : primary) {
    if (pluginList->state(name) != IPluginList::STATE_MISSING) {
      pluginList->setState(name, IPluginList::STATE_ACTIVE);
    }
  }

  // the others by file time: the game's own order. Primary plugins keep the
  // order primaryPlugins() gives them.
  QStringList plugins;
  for (const QString& name : pluginList->pluginNames()) {
    if (!primary.contains(name, Qt::CaseInsensitive)) {
      plugins.append(name);
    }
  }
  QHash<QString, QDateTime> times;
  times.reserve(plugins.size());
  const QDir data = m_Organizer->managedGame()->dataDirectory();
  for (const QString& name : plugins) {
    const MOBase::IModInterface* mod =
        m_Organizer->modList()->getMod(pluginList->origin(name));
    const QDir folder = mod != nullptr ? QDir(mod->absolutePath()) : data;
    times.insert(name, QFileInfo(folder.absoluteFilePath(name)).lastModified());
  }
  // same time: by name, so the order is the same on every read (MO2's own
  // plugins left ties in whatever order the plugin list gave them)
  std::sort(plugins.begin(), plugins.end(), [&](const QString& a, const QString& b) {
    const QDateTime ta = times.value(a);
    const QDateTime tb = times.value(b);
    if (ta != tb) {
      return ta < tb;
    }
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });

  // the active ones from plugins.txt; MO2 always writes its header there, so
  // an empty file is a broken one and nothing is active
  const auto bytes = readListFile(profileFile("plugins.txt"));
  if (bytes && !bytes->empty()) {
    QSet<QString> active;
    for (const PluginState& entry : readPluginsTxt(*bytes, PluginsTxtStyle::ActiveOnly)) {
      const QString name = QString::fromStdWString(entry.name);
      pluginList->setState(name, IPluginList::STATE_ACTIVE);
      active.insert(name.toCaseFolded());
    }
    for (const QString& name : plugins) {
      if (!active.contains(name.toCaseFolded())) {
        pluginList->setState(name, IPluginList::STATE_INACTIVE);
      }
    }
  } else {
    for (const QString& name : plugins) {
      pluginList->setState(name, IPluginList::STATE_INACTIVE);
    }
  }

  return primary + plugins;
}

void FileTimeGamePlugins::writePluginLists(const IPluginList* pluginList)
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
  std::size_t activeCount = 0;
  for (const QString& name : names) {
    const bool active = pluginList->state(name) == IPluginList::STATE_ACTIVE;
    activeCount += active ? 1 : 0;
    order.push_back({name.toStdWString(), active});
  }

  const WrittenList plugins =
      writePluginsTxt(order, {}, PluginsTxtStyle::ActiveOnly);
  for (const std::wstring& name : plugins.unencodable) {
    MOBase::log::error("invalid plugin name {}", QString::fromStdWString(name));
  }
  if (!plugins.unencodable.empty()) {
    MOBase::reportError(QObject::tr(
        "Some of your plugins have invalid names! These plugins can not be loaded by "
        "the game. Please see mo_interface.log for a list of affected plugins and "
        "rename them."));
  }

  // a list with nothing in it is almost certainly wrong (MO2 not ready yet):
  // the files on disk stay as they are
  if (activeCount == 0) {
    MOBase::log::warn("plugin list would be empty, this is almost certainly wrong. Not "
                      "saving.");
  } else {
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

void FileTimeGamePlugins::commit(const QString& path, const std::string& bytes)
{
  MOBase::SafeWriteFile file(path);
  file->resize(0);
  file->write(bytes.data(), static_cast<qint64>(bytes.size()));
  file.commitIfDifferent(m_LastWritten[path]);
}

}  // namespace mo2r::bethesda
