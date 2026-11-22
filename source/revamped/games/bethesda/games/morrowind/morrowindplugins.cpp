// MO2 Revamped: Morrowind, the plugin list in Morrowind.ini.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// When each list is read or written, the keys and the messages are those of
// MO2's game_morrowind (MorrowindGamePlugins); loadorder.txt's format is the
// framework's (plugins/loadorderfiles).
#include "morrowindplugins.h"

#include "ini/inifiles.h"
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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringEncoder>

#include <Windows.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <set>

using MOBase::IPluginList;

namespace mo2r::bethesda
{

namespace
{
  constexpr qint64 MaxListFile = 64 * 1024 * 1024;
  // GetPrivateProfileString's buffer in the old plugin: longer names are cut
  constexpr int MaxName = 256;
  // the game reads far fewer; a broken INI can't make the read loop forever
  constexpr int MaxGameFiles = 65536;

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

  QStringList byPriority(const IPluginList* pluginList)
  {
    QStringList names = pluginList->pluginNames();
    std::sort(names.begin(), names.end(), [pluginList](const QString& a, const QString& b) {
      return pluginList->priority(a) < pluginList->priority(b);
    });
    return names;
  }
}  // namespace

MorrowindGamePlugins::MorrowindGamePlugins(MOBase::IOrganizer* organizer)
    : m_Organizer(organizer)
{}

QString MorrowindGamePlugins::iniPath() const
{
  const MOBase::IProfile* profile = m_Organizer->profile();
  if (profile->localSettingsEnabled()) {
    return profile->absolutePath() + "/Morrowind.ini";
  }
  return m_Organizer->managedGame()->gameDirectory().absolutePath() + "/Morrowind.ini";
}

QString MorrowindGamePlugins::loadOrderPath() const
{
  return m_Organizer->profile()->absolutePath() + "/loadorder.txt";
}

void MorrowindGamePlugins::readPluginLists(IPluginList* pluginList)
{
  // loadorder.txt, unless MO2 read the lists before and since then only the
  // INI changed (a tool wrote it)
  const QFileInfo loadOrder(loadOrderPath());
  const QFileInfo ini(iniPath());
  const bool loadOrderIsNew = !m_LastRead.isValid() || !loadOrder.exists() ||
                              loadOrder.lastModified() > m_LastRead;
  const bool iniIsNew = !m_LastRead.isValid() || ini.lastModified() > m_LastRead;
  if (loadOrderIsNew || !iniIsNew) {
    pluginList->setLoadOrder(readLoadOrder(pluginList));
    readGameFiles(pluginList);
  } else {
    pluginList->setLoadOrder(readGameFiles(pluginList));
  }
  m_LastRead = QDateTime::currentDateTime();
}

QStringList MorrowindGamePlugins::getLoadOrder()
{
  return readLoadOrder(m_Organizer->pluginList());
}

QStringList MorrowindGamePlugins::readLoadOrder(IPluginList* pluginList) const
{
  const auto bytes = readListFile(loadOrderPath());
  if (!bytes) {
    return readGameFiles(pluginList);
  }
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

QStringList MorrowindGamePlugins::readGameFiles(IPluginList* pluginList) const
{
  const QStringList primary = m_Organizer->managedGame()->primaryPlugins();
  for (const QString& name : primary) {
    if (pluginList->state(name) != IPluginList::STATE_MISSING) {
      pluginList->setState(name, IPluginList::STATE_ACTIVE);
    }
  }

  // the others by file time, the game's own order; the primary plugins keep
  // the order primaryPlugins() gives them
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
  // same time: by name, so the order is the same on every read
  std::sort(plugins.begin(), plugins.end(), [&](const QString& a, const QString& b) {
    const QDateTime ta = times.value(a);
    const QDateTime tb = times.value(b);
    if (ta != tb) {
      return ta < tb;
    }
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });

  // the active ones: GameFile0, GameFile1, ... up to the first missing one
  const QString ini = QDir::toNativeSeparators(iniPath());
  QSet<QString> active;
  for (int i = 0; i < MaxGameFiles; ++i) {
    const QString name =
        iniValue(ini, "Game Files", "GameFile" + QString::number(i), "", MaxName).trimmed();
    if (name.isEmpty()) {
      break;
    }
    pluginList->setState(name, IPluginList::STATE_ACTIVE);
    active.insert(name.toCaseFolded());
  }
  // the rest inactive (compared ignoring case, as the game does; the old
  // plugin compared case and turned off a plugin listed as Foo.esp but
  // found as foo.esp)
  for (const QString& name : plugins) {
    if (!active.contains(name.toCaseFolded())) {
      pluginList->setState(name, IPluginList::STATE_INACTIVE);
    }
  }
  return primary + plugins;
}

void MorrowindGamePlugins::writeGameFiles(const QStringList& order,
                                          const IPluginList* pluginList) const
{
  const QString path    = iniPath();
  const std::wstring w  = path.toStdWString();
  // the section is written again from scratch
  ::WritePrivateProfileSectionW(L"Game Files", nullptr, w.c_str());

  // the game reads its INI in the system code page
  QStringEncoder encoder(QStringConverter::Encoding::System);
  bool invalidNames = false;
  bool writeFailed  = false;
  int written       = 0;
  for (const QString& name : order) {
    if (pluginList->state(name) != IPluginList::STATE_ACTIVE) {
      continue;
    }
    (void)encoder.encode(name);
    if (encoder.hasError()) {
      invalidNames = true;
      MOBase::log::error("invalid plugin name {}", name);
      encoder.resetState();
    } else if (!setIniValue(path, "Game Files", "GameFile" + QString::number(written), name)) {
      MOBase::log::warn("failed to set game files in \"{}\"", path);
      writeFailed = true;
    }
    // a name that can't be written still takes its number, as MO2 did
    ++written;
  }

  if (invalidNames) {
    MOBase::reportError(QObject::tr("Some plugins have names with characters Morrowind can't "
                                    "read, so the game won't load them. mo_interface.log lists "
                                    "them: rename them."));
  }

  // once per file; the advice only for the game's own folder (a profile's
  // copy that can't be written is read-only, or another program holds it)
  static std::mutex toldMutex;
  static QSet<QString> told;
  const QString fileKey = QDir::cleanPath(path).toLower();
  bool tell             = false;
  if (writeFailed) {
    std::scoped_lock lock(toldMutex);
    tell = !told.contains(fileKey);
    told.insert(fileKey);
  }
  if (tell) {
    const QString gameFolder =
        QDir::cleanPath(m_Organizer->managedGame()->gameDirectory().absolutePath()).toLower();
    if (fileKey.startsWith(gameFolder + QLatin1Char('/'))) {
      MOBase::reportError(
          QObject::tr("Mod Organizer can't write the plugin list into %1, so the plugins "
                      "turned on in Mod Organizer won't load. Windows protects that folder: "
                      "move the game out of Program Files, or turn on the profile's own game "
                      "INI files in Profiles.")
              .arg(QDir::toNativeSeparators(path)));
    } else {
      MOBase::reportError(
          QObject::tr("Mod Organizer can't write the plugin list into %1, so the plugins "
                      "turned on in Mod Organizer won't load. Check that the file isn't "
                      "read-only or open in another program.")
              .arg(QDir::toNativeSeparators(path)));
    }
  }

  if (written == 0) {
    MOBase::log::warn("the plugin list in {} is empty", path);
  }
}

void MorrowindGamePlugins::writePluginLists(const IPluginList* pluginList)
{
  if (!m_LastRead.isValid()) {
    // never read: writing now would replace the lists with an empty state
    return;
  }
  const QStringList order = byPriority(pluginList);
  writeGameFiles(order, pluginList);

  std::vector<PluginState> states;
  states.reserve(static_cast<std::size_t>(order.size()));
  for (const QString& name : order) {
    states.push_back({name.toStdWString(), pluginList->state(name) == IPluginList::STATE_ACTIVE});
  }
  if (states.empty()) {
    MOBase::log::warn("plugin list would be empty, this is almost certainly wrong. Not "
                      "saving.");
  } else {
    const std::string bytes = writeLoadOrderTxt(states);
    MOBase::SafeWriteFile file(loadOrderPath());
    file->resize(0);
    file->write(bytes.data(), static_cast<qint64>(bytes.size()));
    file.commitIfDifferent(m_LastLoadOrder);
  }
  m_LastRead = QDateTime::currentDateTime();
}

}  // namespace mo2r::bethesda
