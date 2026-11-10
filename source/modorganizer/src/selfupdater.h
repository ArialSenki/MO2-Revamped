/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SELFUPDATER_H
#define SELFUPDATER_H

#include <map>

#include "updatechannel.h"
#include <github.h>
#include <versioninfo.h>

class Archive;
class NexusInterface;
class PluginContainer;
namespace MOBase
{
class IPluginGame;
}

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <vector>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtGlobal>  //for qint64

class QNetworkReply;
class QProgressDialog;
class Settings;

/**
 * @brief checks GitHub for newer MO2 Revamped releases
 * testForUpdate() emits updateAvailable() when a newer release exists. MO2 Revamped
 * is updated with its own setup: the UI shows what changed and links to the
 * release pages, nothing is downloaded or installed here.
 **/
class SelfUpdater : public QObject
{

  Q_OBJECT

public:
  using UpdateChannel = ::UpdateChannel;

  // the channel of a release tag, or of a version; a release marked as a
  // pre-release on GitHub without a known suffix counts as a beta
  static UpdateChannel channelOf(const QString& tag, bool prerelease = false);

  // the channel of this build, which is the default for its users
  static UpdateChannel buildChannel();

  // a GitHub release newer than this build
  struct ReleaseInfo
  {
    MOBase::VersionInfo version;
    QString tag;
    QString name;
    QString notes;  // the release's text, Markdown
    QString url;    // its page on GitHub
    QDateTime published;
    bool prerelease       = false;
    UpdateChannel channel = UpdateChannel::Stable;
  };

  // the MO2 Revamped release this build belongs to, see revampedversion.h
  static QString revampedVersionString();


  /**
   * @brief constructor
   *
   * @param nexusInterface interface to query information from nexus
   * @param parent parent widget
   * @todo passing the nexus interface is unneccessary
   **/
  explicit SelfUpdater(NexusInterface* nexusInterface);

  virtual ~SelfUpdater();

  void setUserInterface(QWidget* widget);

  void setPluginContainer(PluginContainer* pluginContainer);

  /**
   * @brief request information about the current version
   **/
  void testForUpdate(const Settings& settings);

  /**
   * @return current version of Mod Organizer
   **/
  MOBase::VersionInfo getVersion() const { return m_MOVersion; }

  /**
   * @return the releases newer than this build found by the last check, newest
   *         first; empty when there is none
   **/
  const std::vector<ReleaseInfo>& newerReleases() const { return m_NewerReleases; }

signals:

  /**
   * @brief emitted after a check finds releases newer than this build, and
   *        after a check that no longer finds any once one did
   **/
  void updateAvailable();

private:
  void onReleases(const QJsonArray& releases);

  // GitHub's last answer, kept for a day, see testForUpdate()
  static QString releasesCachePath();
  static QJsonArray readReleasesCache(const QString& path);
  static void writeReleasesCache(const QString& path, const QJsonArray& releases);

  QWidget* m_Parent;
  MOBase::VersionInfo m_MOVersion;
  MOBase::VersionInfo m_RevampedVersion;
  std::vector<ReleaseInfo> m_NewerReleases;
  NexusInterface* m_Interface;

  GitHub m_GitHub;
};

#endif  // SELFUPDATER_H
