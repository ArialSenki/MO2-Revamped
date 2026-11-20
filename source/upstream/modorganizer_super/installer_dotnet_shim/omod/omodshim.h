// MO2 Revamped: installer_omod\installer_omod.dll as MO2 loads it. The real
// plugin (C++/CLI, installer_omod\dotnet\installer_omod.dll, with the .NET
// libraries it uses) is loaded the first time an OMOD is installed, or when a
// program starts with OMOD shader replacements (Shaders\OMOD\<n>) to combine;
// see common\dotnetplugin.h. The answers below are the real plugin's
// (installerOmod.cpp).
#pragma once

#include <ipluginfilemapper.h>
#include <iplugininstallercustom.h>

#include <QPluginLoader>

#include <memory>

class LaunchHooksOrganizer;

class OmodShim : public MOBase::IPluginInstallerCustom, public MOBase::IPluginFileMapper
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerCustom
                   MOBase::IPluginFileMapper)
  Q_PLUGIN_METADATA(IID "org.AnyOldName3.InstallerOmod")

public:
  OmodShim();
  ~OmodShim() override;

  bool init(MOBase::IOrganizer* moInfo) override;
  QString name() const override { return "Omod Installer"; }
  QString localizedName() const override;
  QString author() const override { return "AnyOldName3 & erril120"; }
  QString description() const override;
  MOBase::VersionInfo version() const override
  {
    return MOBase::VersionInfo(1, 1, 0, MOBase::VersionInfo::RELEASE_FINAL);
  }
  std::vector<std::shared_ptr<const MOBase::IPluginRequirement>> requirements() const override;
  QList<MOBase::PluginSetting> settings() const override;

  unsigned int priority() const override { return 500; }
  bool isManualInstaller() const override { return false; }
  void onInstallationStart(QString const& archive, bool reinstallation,
                           MOBase::IModInterface* currentMod) override;
  void onInstallationEnd(EInstallResult result, MOBase::IModInterface* newMod) override;
  bool isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;
  void setParentWidget(QWidget* parent) override;
  bool isArchiveSupported(const QString& archiveName) const override;
  std::set<QString> supportedExtensions() const override { return {"omod"}; }
  EInstallResult install(MOBase::GuessedValue<QString>& modName, QString gameName,
                         const QString& archiveName, const QString& version,
                         int nexusID) override;

  MappingType mappings() const override;

private:
  MOBase::IPluginInstallerCustom* real();
  bool aboutToRun(const QString& name, const QDir& dir, const QString& args);
  bool hasShaderReplacements() const;

  MOBase::IOrganizer* m_MOInfo = nullptr;
  std::unique_ptr<LaunchHooksOrganizer> m_Hooks;
  std::unique_ptr<QPluginLoader> m_Loader;
  MOBase::IPluginInstallerCustom* m_Real = nullptr;
  bool m_Failed                          = false;
};
