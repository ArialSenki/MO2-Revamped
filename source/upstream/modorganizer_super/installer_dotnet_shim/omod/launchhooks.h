// MO2 Revamped: the organizer the real OMOD installer is given. Everything goes
// to the stand-in's own organizer, but the callbacks for a program's start and
// end are kept here, so that the stand-in can run them itself: the real plugin
// may be loaded inside the stand-in's own "about to run" callback, and a
// callback registered during that one wouldn't be called for that start.
#pragma once

#include <imoinfo.h>

#include <functional>
#include <vector>

class LaunchHooksOrganizer : public MOBase::IOrganizer
{
public:
  LaunchHooksOrganizer(MOBase::IOrganizer* organizer, MOBase::IPlugin* self,
                       MOBase::IPlugin* real)
      : m_O(organizer), m_Self(self), m_Real(real)
  {}

  void setReal(MOBase::IPlugin* real) { m_Real = real; }

  // the callbacks the real plugin registered
  bool aboutToRun(const QString& name, const QDir& dir, const QString& args)
  {
    for (auto& f : m_AboutToRun) {
      if (!f(name)) {
        return false;
      }
    }
    for (auto& f : m_AboutToRun3) {
      if (!f(name, dir, args)) {
        return false;
      }
    }
    return true;
  }
  void finishedRun(const QString& name, unsigned int code)
  {
    for (auto& f : m_FinishedRun) {
      f(name, code);
    }
  }

  bool onAboutToRun(const std::function<bool(const QString&)>& func) override
  {
    m_AboutToRun.push_back(func);
    return true;
  }
  bool onAboutToRun(
      const std::function<bool(const QString&, const QDir&, const QString&)>& func) override
  {
    m_AboutToRun3.push_back(func);
    return true;
  }
  bool onFinishedRun(const std::function<void(const QString&, unsigned int)>& func) override
  {
    m_FinishedRun.push_back(func);
    return true;
  }

  // the rest as the stand-in's own organizer; the real plugin counts as the
  // stand-in where MO2 is asked about it
  MOBase::IModRepositoryBridge* createNexusBridge() const override { return m_O->createNexusBridge(); }
  QString profileName() const override { return m_O->profileName(); }
  QString profilePath() const override { return m_O->profilePath(); }
  QString downloadsPath() const override { return m_O->downloadsPath(); }
  QString overwritePath() const override { return m_O->overwritePath(); }
  QString basePath() const override { return m_O->basePath(); }
  QString modsPath() const override { return m_O->modsPath(); }
  MOBase::VersionInfo appVersion() const override { return m_O->appVersion(); }
  MOBase::IModInterface* createMod(MOBase::GuessedValue<QString>& name) override { return m_O->createMod(name); }
  MOBase::IPluginGame* getGame(const QString& gameName) const override { return m_O->getGame(gameName); }
  void modDataChanged(MOBase::IModInterface* mod) override { m_O->modDataChanged(mod); }
  bool isPluginEnabled(MOBase::IPlugin* plugin) const override
  {
    return m_O->isPluginEnabled(plugin == m_Real ? m_Self : plugin);
  }
  bool isPluginEnabled(QString const& pluginName) const override { return m_O->isPluginEnabled(pluginName); }
  QVariant pluginSetting(const QString& pluginName, const QString& key) const override
  {
    return m_O->pluginSetting(pluginName, key);
  }
  void setPluginSetting(const QString& pluginName, const QString& key, const QVariant& value) override
  {
    m_O->setPluginSetting(pluginName, key, value);
  }
  QVariant persistent(const QString& pluginName, const QString& key, const QVariant& def) const override
  {
    return m_O->persistent(pluginName, key, def);
  }
  void setPersistent(const QString& pluginName, const QString& key, const QVariant& value, bool sync) override
  {
    m_O->setPersistent(pluginName, key, value, sync);
  }
  QString pluginDataPath() const override { return m_O->pluginDataPath(); }
  MOBase::IModInterface* installMod(const QString& fileName, const QString& nameSuggestion) override
  {
    return m_O->installMod(fileName, nameSuggestion);
  }
  QString resolvePath(const QString& fileName) const override { return m_O->resolvePath(fileName); }
  QStringList listDirectories(const QString& directoryName) const override { return m_O->listDirectories(directoryName); }
  QStringList findFiles(const QString& path, const std::function<bool(const QString&)>& filter) const override
  {
    return m_O->findFiles(path, filter);
  }
  QStringList findFiles(const QString& path, const QStringList& filters) const override
  {
    return m_O->findFiles(path, filters);
  }
  QStringList getFileOrigins(const QString& fileName) const override { return m_O->getFileOrigins(fileName); }
  QList<FileInfo> findFileInfos(const QString& path, const std::function<bool(const FileInfo&)>& filter) const override
  {
    return m_O->findFileInfos(path, filter);
  }
  std::shared_ptr<const MOBase::IFileTree> virtualFileTree() const override { return m_O->virtualFileTree(); }
  MOBase::IDownloadManager* downloadManager() const override { return m_O->downloadManager(); }
  MOBase::IPluginList* pluginList() const override { return m_O->pluginList(); }
  MOBase::IModList* modList() const override { return m_O->modList(); }
  MOBase::IProfile* profile() const override { return m_O->profile(); }
  MOBase::IGameFeatures* gameFeatures() const override { return m_O->gameFeatures(); }
  HANDLE startApplication(const QString& executable, const QStringList& args, const QString& cwd,
                          const QString& profile, const QString& forcedCustomOverwrite,
                          bool ignoreCustomOverwrite) override
  {
    return m_O->startApplication(executable, args, cwd, profile, forcedCustomOverwrite,
                                 ignoreCustomOverwrite);
  }
  bool waitForApplication(HANDLE handle, bool refresh, LPDWORD exitCode) const override
  {
    return m_O->waitForApplication(handle, refresh, exitCode);
  }
  void refresh(bool saveChanges) override { m_O->refresh(saveChanges); }
  MOBase::IPluginGame const* managedGame() const override { return m_O->managedGame(); }
  bool onUserInterfaceInitialized(std::function<void(QMainWindow*)> const& func) override
  {
    return m_O->onUserInterfaceInitialized(func);
  }
  bool onNextRefresh(std::function<void()> const& func, bool immediateIfPossible) override
  {
    return m_O->onNextRefresh(func, immediateIfPossible);
  }
  bool onProfileCreated(std::function<void(MOBase::IProfile*)> const& func) override { return m_O->onProfileCreated(func); }
  bool onProfileRenamed(std::function<void(MOBase::IProfile*, QString const&, QString const&)> const& func) override
  {
    return m_O->onProfileRenamed(func);
  }
  bool onProfileRemoved(std::function<void(QString const&)> const& func) override { return m_O->onProfileRemoved(func); }
  bool onProfileChanged(std::function<void(MOBase::IProfile*, MOBase::IProfile*)> const& func) override
  {
    return m_O->onProfileChanged(func);
  }
  bool onPluginSettingChanged(std::function<void(QString const&, const QString&, const QVariant&, const QVariant&)> const& func) override
  {
    return m_O->onPluginSettingChanged(func);
  }
  bool onPluginEnabled(std::function<void(const MOBase::IPlugin*)> const& func) override { return m_O->onPluginEnabled(func); }
  bool onPluginEnabled(const QString& pluginName, std::function<void()> const& func) override
  {
    return m_O->onPluginEnabled(pluginName, func);
  }
  bool onPluginDisabled(std::function<void(const MOBase::IPlugin*)> const& func) override { return m_O->onPluginDisabled(func); }
  bool onPluginDisabled(const QString& pluginName, std::function<void()> const& func) override
  {
    return m_O->onPluginDisabled(pluginName, func);
  }

private:
  MOBase::IOrganizer* m_O;
  MOBase::IPlugin* m_Self;
  MOBase::IPlugin* m_Real;
  std::vector<std::function<bool(const QString&)>> m_AboutToRun;
  std::vector<std::function<bool(const QString&, const QDir&, const QString&)>> m_AboutToRun3;
  std::vector<std::function<void(const QString&, unsigned int)>> m_FinishedRun;
};
