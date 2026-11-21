/*
Copyright (C) 2020 Holt59. All rights reserved.

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

#ifndef INSTALLER_FOMOD_CSHARP_H
#define INSTALLER_FOMOD_CSHARP_H

#include "iplugininstallersimple.h"
#include <QCoreApplication>

class InstallerFomodCSharp : public MOBase::IPluginInstallerSimple
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
  Q_PLUGIN_METADATA(IID "org.holt59.InstallerFomodCSharp")

public:
  InstallerFomodCSharp() {}

  virtual bool init(MOBase::IOrganizer* moInfo) override;

  virtual QString name() const override { return "Fomod Installer C#"; }

  virtual QString localizedName() const override { return tr("Fomod Installer C#"); }

  virtual QString author() const override { return "Holt59"; }

  virtual QString description() const override
  {
    return tr("Installer for C# based FOMOD archives.");
  }

  virtual MOBase::VersionInfo version() const override
  {
    return MOBase::VersionInfo(1, 0, 0, MOBase::VersionInfo::RELEASE_BETA);
  }

  virtual QList<MOBase::PluginSetting> settings() const override
  {
    return {
        MOBase::PluginSetting("enabled", QCoreApplication::translate("RevampedInstallerSettings", "Use this installer"), QVariant(true)),
        MOBase::PluginSetting("prefer", QCoreApplication::translate("RevampedInstallerSettings", "Try this installer before the FOMOD XML installer when an archive has both a script and a ModuleConfig.xml. It goes first only while the FOMOD XML installer's own setting to go first is off"),
                              QVariant(true))};
  }

  virtual unsigned int priority() const override
  {
    // MO2 Revamped: one step below the FOMOD XML installer's 110/90. Some
    // archives carry both a ModuleConfig.xml and a script (Marts Mutant Mod),
    // and with the same number MO2's sort picked either one. Now both
    // "prefer" settings on means the XML installer; turning off its "prefer"
    // lets the script go first
    return m_MOInfo->pluginSetting(name(), "prefer").toBool() ? 109 : 89;
  }

  virtual bool isManualInstaller() const override { return false; }

  virtual bool
  isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;

  virtual EInstallResult install(MOBase::GuessedValue<QString>& modName,
                                 std::shared_ptr<MOBase::IFileTree>& tree,
                                 QString& version, int& modID) override;

private:
  MOBase::IOrganizer* m_MOInfo;

  std::shared_ptr<const MOBase::IFileTree>
  findFomodDirectory(std::shared_ptr<const MOBase::IFileTree> tree) const;
  std::shared_ptr<const MOBase::FileTreeEntry>
  findScriptFile(std::shared_ptr<const MOBase::IFileTree> tree) const;
  std::shared_ptr<const MOBase::FileTreeEntry>
  findInfoFile(std::shared_ptr<const MOBase::IFileTree> tree) const;
};

#endif
