#ifndef _GAMESKYRIMSE_H
#define _GAMESKYRIMSE_H

#include "gamegamebryo.h"
#include "iplugindiagnose.h"

#include <QObject>
#include <QtGlobal>

class GameSkyrimSE : public GameGamebryo, public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginDiagnose)

  Q_PLUGIN_METADATA(IID "com.soundcontactstudio.GameSkyrimSE")

public:
  GameSkyrimSE();

  virtual bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  virtual void detectGame() override;
  virtual QString gameName() const override;

  virtual QList<MOBase::ExecutableInfo> executables() const override;
  virtual QList<MOBase::ExecutableForcedLoadSetting>
  executableForcedLoads() const override;
  virtual void initializeProfile(const QDir& path,
                                 ProfileSettings settings) const override;
  virtual QString steamAPPId() const override;
  virtual QStringList primaryPlugins() const override;
  virtual QStringList gameVariants() const override;
  virtual QString gameShortName() const override;
  virtual QString gameNexusName() const override;
  virtual QStringList validShortNames() const override;
  virtual QStringList iniFiles() const override;
  virtual QStringList DLCPlugins() const override;
  virtual QStringList CCPlugins() const override;
  virtual LoadOrderMechanism loadOrderMechanism() const override;
  virtual int nexusModOrganizerID() const override;
  virtual int nexusGameID() const override;

  virtual bool isInstalled() const override;
  virtual void setGamePath(const QString& path) override;
  virtual QDir gameDirectory() const override;

public:  // IPlugin interface
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;
  virtual MappingType mappings() const override;

public:  // IPluginDiagnose interface
  // MO2 Revamped: SKSE that doesn't match the game, and a missing Address
  // Library
  virtual std::vector<unsigned int> activeProblems() const override;
  virtual QString shortDescription(unsigned int key) const override;
  virtual QString fullDescription(unsigned int key) const override;
  virtual bool hasGuidedFix(unsigned int) const override { return false; }
  virtual void startGuidedFix(unsigned int) const override {}

protected:
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString filePath) const override;
  QString savegameExtension() const override;
  QString savegameSEExtension() const override;

  QString gameDirectoryName() const;
  QDir documentsDirectory() const;
  QDir savesDirectory() const;
  QFileInfo findInGameFolder(const QString& relativePath) const;
  QString myGamesPath() const;

  void checkVariants();
  void setVariant(QString variant);

  virtual QString identifyGamePath() const override;

private:
  static const unsigned int PROBLEM_SCRIPT_EXTENDER_VERSION = 1;
  static const unsigned int PROBLEM_ADDRESS_LIBRARY         = 2;
};

#endif  // _GAMESKYRIMSE_H
