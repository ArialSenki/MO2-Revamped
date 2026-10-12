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
#include <QLibrary>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
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

SelfUpdater::SelfUpdater(NexusInterface* nexusInterface)
    : m_Parent(nullptr), m_Interface(nexusInterface)
{
  m_MOVersion = createVersionInfo();
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

  // TODO: if prereleases are disabled we could just request the latest release
  // directly
  try {
    m_GitHub.releases(
        GitHub::Repository("ArialSenki", "MO2-Revamped"),
        [this](const QJsonArray& releases) {
          if (releases.isEmpty()) {
            // error message already logged
            return;
          }

          // Releases ordered from newest to oldest. Updates are installed with
          // the MO2 Revamped setup, so only the newest version is needed here.
          std::map<VersionInfo, QJsonObject, std::greater<>> mreleases;
          for (const QJsonValue& releaseVal : releases) {
            QJsonObject release = releaseVal.toObject();
            if (!release["draft"].toBool() && (Settings::instance().usePrereleases() ||
                                               !release["prerelease"].toBool())) {
              auto version       = VersionInfo(release["tag_name"].toString());
              mreleases[version] = release;
            }
          }

          if (!mreleases.empty()) {
            auto lastKey = mreleases.begin()->first;
            if (lastKey > this->m_MOVersion) {
              log::info("update available: {} -> {}",
                        this->m_MOVersion.displayString(3), lastKey.displayString(3));
              emit updateAvailable();
            } else if (lastKey < this->m_MOVersion) {
              // this could happen if the user switches from using prereleases to
              // stable builds. Should we downgrade?
              log::debug("This version is newer than the latest released one: {} -> {}",
                         this->m_MOVersion.displayString(3), lastKey.displayString(3));
            }
          }
        });
  }
  // Catch all is bad by design, should be improved
  catch (...) {
    log::debug("Unable to connect to github.com to check version");
  }
}
