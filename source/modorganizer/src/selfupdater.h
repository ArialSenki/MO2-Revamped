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

#include <github.h>
#include <versioninfo.h>

class Archive;
class NexusInterface;
class PluginContainer;
namespace MOBase
{
class IPluginGame;
}

#include <QFile>
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
 * is updated with its own setup, so the UI only links to the release page.
 **/
class SelfUpdater : public QObject
{

  Q_OBJECT

public:
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

signals:

  /**
   * @brief emitted if an update is available
   **/
  void updateAvailable();

private:
  QWidget* m_Parent;
  MOBase::VersionInfo m_MOVersion;
  NexusInterface* m_Interface;

  GitHub m_GitHub;
};

#endif  // SELFUPDATER_H
