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

#include "selfupdater.h"

#include "bbcode.h"
#include "downloadmanager.h"
#include "iplugingame.h"
#include "messagedialog.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "revampedversion.h"
#include "settings.h"
#include "shared/util.h"
#include "utility.h"
#include <report.h>
#include <versioninfo.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <Qt>
#include <QtAlgorithms>
#include <QtDebug>

#include <boost/bind/bind.hpp>

#include <Windows.h>  //for VS_FIXEDFILEINFO, GetLastError

#include <exception>
#include <map>
#include <stddef.h>  //for size_t
#include <stdexcept>

using namespace MOBase;
using namespace MOShared;

// "v1.1.0-beta.10" becomes "v1.1.0beta10": VersionInfo compares what follows
// "beta" as text unless it is a bare number, and beta.10 would sort before beta.9
static VersionInfo parseRevampedVersion(QString tag)
{
  static const QRegularExpression before(QStringLiteral("[-.](?=[A-Za-z])"));
  static const QRegularExpression after(QStringLiteral("(?<=[A-Za-z])[-.]"));
  // VersionInfo knows alpha, beta and rc: a development build sorts as an
  // alpha, before the beta and the release it leads to
  static const QRegularExpression dev(
      QStringLiteral("(?<![A-Za-z])(dev|devel|test|preview|nightly)(?![A-Za-z])"),
      QRegularExpression::CaseInsensitiveOption);
  tag.replace(dev, QStringLiteral("alpha"));
  tag.replace(before, QString()).replace(after, QString());
  return VersionInfo(tag);
}

SelfUpdater::SelfUpdater(NexusInterface* nexusInterface)
    : m_Parent(nullptr), m_Interface(nexusInterface)
{
  m_MOVersion       = createVersionInfo();
  m_RevampedVersion = parseRevampedVersion(revampedVersionString());
}

SelfUpdater::~SelfUpdater() {}

void SelfUpdater::setUserInterface(QWidget* widget)
{
  m_Parent = widget;
}

void SelfUpdater::setPluginContainer(PluginContainer* pluginContainer)
{
  m_Interface->setPluginContainer(pluginContainer);
}

QString SelfUpdater::revampedVersionString()
{
  return QStringLiteral(MO2R_VERSION_STRING);
}

SelfUpdater::UpdateChannel SelfUpdater::channelOf(const QString& tag, bool prerelease)
{
  static const QRegularExpression dev(
      QStringLiteral("(?<![A-Za-z])(alpha|dev|devel|test|preview|nightly)(?![A-Za-z])"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression beta(QStringLiteral("(?<![A-Za-z])(beta|rc)(?![A-Za-z])"),
                                       QRegularExpression::CaseInsensitiveOption);
  if (dev.match(tag).hasMatch()) {
    return UpdateChannel::Dev;
  }
  if (beta.match(tag).hasMatch() || prerelease) {
    return UpdateChannel::Beta;
  }
  return UpdateChannel::Stable;
}

SelfUpdater::UpdateChannel SelfUpdater::buildChannel()
{
  return channelOf(revampedVersionString());
}

void SelfUpdater::testForUpdate(const Settings& settings)
{
  if (settings.network().offlineMode()) {
    log::debug("not checking for updates, in offline mode");
    return;
  }

  if (!settings.checkForUpdates()) {
    log::debug("not checking for updates, disabled");
    return;
  }

  // for testing the notice without publishing anything: a local file in the
  // format of GitHub's releases list stands in for GitHub
  const QString testFile = qEnvironmentVariable("MO2_REVAMPED_UPDATE_TEST");
  if (!testFile.isEmpty()) {
    QFile file(testFile);
    if (file.open(QIODevice::ReadOnly)) {
      log::warn("update check reads releases from '{}' (MO2_REVAMPED_UPDATE_TEST)",
                testFile);
      onReleases(QJsonDocument::fromJson(file.readAll()).array());
    } else {
      log::error("update check: can't read '{}' (MO2_REVAMPED_UPDATE_TEST)", testFile);
    }
    return;
  }

  // GitHub is asked at most once a day: its answer is kept beside the global
  // settings, shared by every instance, and a start within a day reads it. The
  // notice still opens at each start while a newer release is there, and a
  // changed channel applies at once.
  const QString cachePath = releasesCachePath();
  const QFileInfo cache(cachePath);
  if (cache.exists()) {
    const qint64 age = cache.lastModified().secsTo(QDateTime::currentDateTime());
    if (age >= 0 && age < 24 * 60 * 60) {
      const QJsonArray cached = readReleasesCache(cachePath);
      if (!cached.isEmpty()) {
        log::debug("update check: GitHub was asked {} min ago, using its answer",
                   age / 60);
        onReleases(cached);
        return;
      }
    }
  }

  try {
    m_GitHub.releases(GitHub::Repository("ArialSenki", "MO2-Revamped"),
                      [this, cachePath](const QJsonArray& releases) {
                        if (releases.isEmpty()) {
                          // offline or refused: the last answer, however old,
                          // still tells of the releases it knew
                          const QJsonArray old = readReleasesCache(cachePath);
                          if (!old.isEmpty()) {
                            log::debug("update check: GitHub didn't answer, "
                                       "using its last answer");
                          }
                          onReleases(old);
                          return;
                        }
                        writeReleasesCache(cachePath, releases);
                        onReleases(releases);
                      });
  }
  // Catch all is bad by design, should be improved
  catch (...) {
    log::debug("Unable to connect to github.com to check version");
  }
}

QString SelfUpdater::releasesCachePath()
{
  return QDir(GlobalSettings::directory()).filePath(QStringLiteral("update-releases.json"));
}

QJsonArray SelfUpdater::readReleasesCache(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).array();
}

// only what the check reads is kept, so the file stays small
void SelfUpdater::writeReleasesCache(const QString& path, const QJsonArray& releases)
{
  QJsonArray kept;
  for (const QJsonValue& value : releases) {
    const QJsonObject release = value.toObject();
    QJsonObject entry;
    for (const char* key :
         {"tag_name", "name", "body", "html_url", "published_at", "prerelease", "draft"}) {
      entry.insert(QLatin1String(key), release.value(QLatin1String(key)));
    }
    kept.append(entry);
  }

  // written whole or not at all, also with two MO2 running
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(QJsonDocument(kept).toJson(QJsonDocument::Compact)) < 0 ||
      !file.commit()) {
    log::debug("update check: can't keep GitHub's answer in '{}'", path);
  }
}

void SelfUpdater::onReleases(const QJsonArray& releases)
{
  if (releases.isEmpty()) {
    // error message already logged
    return;
  }

  // the channel follows the build unless the user picked one, see
  // Settings::updateChannel()
  const UpdateChannel channel = Settings::instance().updateChannel();

  // the releases newer than this build, newest first: the notice shows what
  // each of them changed, so someone who skipped a few still reads it all
  std::map<VersionInfo, ReleaseInfo, std::greater<>> newer;
  VersionInfo newest;
  for (const QJsonValue& releaseVal : releases) {
    const QJsonObject release = releaseVal.toObject();
    if (release["draft"].toBool()) {
      continue;
    }

    ReleaseInfo info;
    info.tag        = release["tag_name"].toString();
    info.version    = parseRevampedVersion(info.tag);
    info.name       = release["name"].toString();
    info.notes      = release["body"].toString();
    info.url        = release["html_url"].toString();
    info.published  = QDateTime::fromString(release["published_at"].toString(),
                                            Qt::ISODate);
    info.prerelease = release["prerelease"].toBool();
    info.channel    = channelOf(info.tag, info.prerelease);
    if (!info.version.isValid() ||
        static_cast<int>(info.channel) > static_cast<int>(channel)) {
      continue;
    }

    if (!newest.isValid() || info.version > newest) {
      newest = info.version;
    }
    if (info.version > m_RevampedVersion) {
      newer[info.version] = info;
    }
  }

  const bool hadNewer = !m_NewerReleases.empty();
  m_NewerReleases.clear();
  for (auto& [version, info] : newer) {
    m_NewerReleases.push_back(info);
  }

  if (m_NewerReleases.empty() && hadNewer) {
    // a narrower channel: what the last check showed no longer applies
    emit updateAvailable();
  }

  if (!m_NewerReleases.empty()) {
    log::info("update available: {} -> {}", m_RevampedVersion.displayString(3),
              m_NewerReleases.front().version.displayString(3));
    emit updateAvailable();
  } else if (newest.isValid() && newest < m_RevampedVersion) {
    // a beta or a development build on a narrower channel, or a build that
    // isn't out yet
    log::debug("this version is newer than the latest release: {} -> {}",
               m_RevampedVersion.displayString(3), newest.displayString(3));
  } else {
    log::debug("MO2 Revamped {} is up to date", m_RevampedVersion.displayString(3));
  }
}
