// MO2 Revamped: a game plugin of another game than the instance's, without its
// DLL. PluginContainer makes one from what the real plugin answered the last
// time it was loaded (plugin-games.json, "game"): the names, Nexus and Steam
// ids, binary, settings and version that MO2 asks every game for when it lists
// them, matches a link or a mod to its game, or shows the plugin settings. Any
// other call (where the game is installed, whether a folder is it, its icon,
// executables, saves...) loads the real plugin the first time and is handed to
// it, so the instance window and game detection still work for every game.
#pragma once

#include <iplugingame.h>

#include <QJsonObject>
#include <QPluginLoader>

#include <memory>

class GameStub : public MOBase::IPluginGame
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame)

public:
  // the "game" object of a plugin-games.json entry, and the real DLL's path
  GameStub(const QJsonObject& game, const QString& filepath);
  ~GameStub() override;

  // what plugin-games.json keeps of a loaded game plugin, or an empty object
  // when MO2 can't stand in for it (requirements, a master)
  static QJsonObject describe(MOBase::IPluginGame* game);

  // the real plugin, if it was loaded
  MOBase::IPluginGame* loadedReal() const { return m_Real; }
  const QJsonObject& metadata() const { return m_Game; }

  // IPlugin
  bool init(MOBase::IOrganizer* organizer) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

  // IPluginGame: from the metadata
  QString gameName() const override;
  QString displayGameName() const override;
  QString gameShortName() const override;
  QString lootGameName() const override;
  QStringList validShortNames() const override;
  QString gameNexusName() const override;
  int nexusModOrganizerID() const override;
  int nexusGameID() const override;
  QString steamAPPId() const override;
  QString binaryName() const override;
  QString getLauncherName() const override;
  QString getSupportURL() const override;
  QStringList gameVariants() const override;
  void detectGame() override;
  void setGamePath(const QString& path) override;
  void setGameVariant(const QString& variant) override;

  // IPluginGame: from the real plugin
  void initializeProfile(const QDir& directory, ProfileSettings settings) const override;
  std::vector<std::shared_ptr<const MOBase::ISaveGame>> listSaves(QDir folder) const override;
  bool isInstalled() const override;
  QIcon gameIcon() const override;
  QDir gameDirectory() const override;
  QDir dataDirectory() const override;
  QMap<QString, QDir> secondaryDataDirectories() const override;
  QDir documentsDirectory() const override;
  QDir savesDirectory() const override;
  QList<MOBase::ExecutableInfo> executables() const override;
  QList<MOBase::ExecutableForcedLoadSetting> executableForcedLoads() const override;
  QStringList primaryPlugins() const override;
  QStringList enabledPlugins() const override;
  QStringList primarySources() const override;
  QStringList iniFiles() const override;
  QStringList DLCPlugins() const override;
  QStringList CCPlugins() const override;
  LoadOrderMechanism loadOrderMechanism() const override;
  SortMechanism sortMechanism() const override;
  bool looksValid(QDir const& dir) const override;
  QString gameVersion() const override;

private:
  // the real plugin, loaded, initialized and detected (or given the path and
  // variant this one was given) on the first call; nullptr if it can't be
  MOBase::IPluginGame* real() const;

  QJsonObject m_Game;
  QString m_Filepath;
  MOBase::IOrganizer* m_Organizer = nullptr;
  QString m_GamePath;
  QString m_Variant;
  bool m_PathSet = false;
  bool m_VariantSet = false;
  mutable std::unique_ptr<QPluginLoader> m_Loader;
  mutable MOBase::IPluginGame* m_Real = nullptr;
  mutable bool m_Failed               = false;
};
