// MO2 Revamped: Bethesda game framework, MO2's game plugin interface.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// BethesdaGame implements IPluginGame and IPluginFileMapper from a
// GameDefinition. What each function returns, and when, follows MO2's own
// Gamebryo plugins (game_gamebryo), so a game plugin built on it can replace
// the old one in an existing instance.
#pragma once

#include "core/gamedefinition.h"
#include "util/filecache.h"

#include <ipluginfilemapper.h>
#include <iplugingame.h>

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>

namespace MOBase
{
class GameFeature;
}

namespace mo2r::bethesda
{

class BethesdaGame : public MOBase::IPluginGame, public MOBase::IPluginFileMapper
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginFileMapper)

public:
  explicit BethesdaGame(const GameDefinition& definition);

  const GameDefinition& definition() const { return m_Def; }

  // null before init()
  MOBase::IOrganizer* organizer() const { return m_Organizer; }

  // the My Games folder in use (with the variant's name)
  QString myGamesPath() const { return m_MyGamesPath; }
  QString selectedVariant() const { return m_GameVariant; }
  // the variant's My Games and AppData folder name
  QString variantFolder() const;
  // a file of the game folder (it may not exist)
  QFileInfo findInGameFolder(const QString& relativePath) const;
  // the machine type of an executable in the game folder, 0 if unknown
  std::uint16_t executableArch(const QString& relativePath) const;
  // the script extender's co-save extension ("nvse")
  QString coSaveExtension() const { return m_Def.extender.coSaveExtension.toString(); }

  // translated in the game's translation context
  QString translate(const char* source) const;

  static QString localAppFolder();
  static QString lootPath();

public:  // IPlugin
  bool init(MOBase::IOrganizer* organizer) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

public:  // IPluginGame
  QString gameName() const override;
  void detectGame() override;
  void initializeProfile(const QDir& directory, ProfileSettings settings) const override;
  std::vector<std::shared_ptr<const MOBase::ISaveGame>>
  listSaves(QDir folder) const override;
  bool isInstalled() const override;
  QIcon gameIcon() const override;
  QDir gameDirectory() const override;
  QDir dataDirectory() const override;
  void setGamePath(const QString& path) override;
  QDir documentsDirectory() const override;
  QDir savesDirectory() const override;
  QList<MOBase::ExecutableInfo> executables() const override;
  QList<MOBase::ExecutableForcedLoadSetting> executableForcedLoads() const override;
  QString steamAPPId() const override;
  QStringList primaryPlugins() const override;
  QStringList gameVariants() const override;
  void setGameVariant(const QString& variant) override;
  QString binaryName() const override;
  QString gameShortName() const override;
  QStringList validShortNames() const override;
  QString gameNexusName() const override;
  QStringList iniFiles() const override;
  QStringList DLCPlugins() const override;
  // from the definition's ccFile, read again when it changes
  QStringList CCPlugins() const override;
  LoadOrderMechanism loadOrderMechanism() const override;
  SortMechanism sortMechanism() const override;
  int nexusModOrganizerID() const override;
  int nexusGameID() const override;
  bool looksValid(QDir const& path) const override;
  QString gameVersion() const override;
  QString getLauncherName() const override;

public:  // IPluginFileMapper
  MappingType mappings() const override;

protected:
  // the features MO2 asks the game for; by default the standard set the
  // definition describes
  virtual void registerFeatures();
  void registerFeature(std::shared_ptr<MOBase::GameFeature> feature);

  // the game folder, from the stores in the definition's order
  virtual QString identifyGamePath() const;
  // the variant from the marker files in the game folder; a game without
  // markers keeps the one MO2 set
  virtual void detectVariant();
  // before a program runs: what the game needs in its INI
  virtual bool prepareIni(const QString& executable);
  // a save in the game's format; null if it can't be read (logged)
  virtual std::shared_ptr<const MOBase::ISaveGame> makeSaveGame(const QString& path) const;

  QString m_GamePath;
  QString m_MyGamesPath;
  QString m_GameVariant;

private:
  void updatePaths();

  const GameDefinition& m_Def;
  MOBase::IOrganizer* m_Organizer = nullptr;
  FileCache<QString> m_VersionCache;
  FileCache<QStringList> m_CcCache;
};

}  // namespace mo2r::bethesda
