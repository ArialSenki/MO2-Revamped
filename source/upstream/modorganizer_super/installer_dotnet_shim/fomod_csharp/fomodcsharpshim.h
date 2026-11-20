// MO2 Revamped: installer_fomod_csharp.dll as MO2 loads it. The real plugin
// (C++/CLI, plugins\installer_fomod_csharp\dotnet\installer_fomod_csharp.dll) is loaded the first time
// an archive with a C# script FOMOD is installed; see common\dotnetplugin.h.
// The answers below are the real plugin's (installer_fomod_csharp.h).
#pragma once

#include <iplugininstallersimple.h>

#include <QPluginLoader>

#include <memory>

class FomodCSharpShim : public MOBase::IPluginInstallerSimple
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
  Q_PLUGIN_METADATA(IID "org.holt59.InstallerFomodCSharp")

public:
  ~FomodCSharpShim() override;
  bool init(MOBase::IOrganizer* moInfo) override;
  QString name() const override { return "Fomod Installer C#"; }
  QString localizedName() const override;
  QString author() const override { return "Holt59"; }
  QString description() const override;
  MOBase::VersionInfo version() const override
  {
    return MOBase::VersionInfo(1, 0, 0, MOBase::VersionInfo::RELEASE_BETA);
  }
  QList<MOBase::PluginSetting> settings() const override;
  unsigned int priority() const override;
  bool isManualInstaller() const override { return false; }
  void onInstallationStart(QString const& archive, bool reinstallation,
                           MOBase::IModInterface* currentMod) override;
  void onInstallationEnd(EInstallResult result, MOBase::IModInterface* newMod) override;
  bool isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;
  EInstallResult install(MOBase::GuessedValue<QString>& modName,
                         std::shared_ptr<MOBase::IFileTree>& tree, QString& version,
                         int& modID) override;

private:
  // the real plugin, loaded and initialized on the first call that needs it
  MOBase::IPluginInstallerSimple* real();

  MOBase::IOrganizer* m_MOInfo = nullptr;
  std::unique_ptr<QPluginLoader> m_Loader;
  MOBase::IPluginInstallerSimple* m_Real = nullptr;
  bool m_Failed                          = false;
};
