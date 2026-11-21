// MO2 Revamped: Starfield, the features that differ from the framework's.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// - The plugin list is the framework's '*' style with light and medium
//   plugins, left alone while StarfieldCustom.ini's sTestFile lines make the
//   game ignore it.
// - The archive list is only read: from StarfieldCustom.ini's five resource
//   keys, each falling back to the game folder's Starfield.ini; MO2 never
//   writes it (the game loads a plugin's archives by itself).
// - The unmanaged mods are the plugins in both Data folders (My Games' and
//   the game's), the Creations among them named and given their files from
//   AppData's ContentCatalog.txt.
// - The content column adds Materials, Geometries and Video.
// All as MO2's game_starfield (by Silarn) did.
#pragma once

#include "content/datachecker.h"
#include "plugins/asteriskplugins.h"

#include <dataarchives.h>
#include <unmanagedmods.h>

#include <QString>
#include <QStringList>

#include <map>

namespace MOBase
{
class IOrganizer;
class IPluginGame;
}

namespace mo2r::bethesda
{

class BethesdaGame;

class StarfieldGamePlugins : public AsteriskGamePlugins
{
public:
  explicit StarfieldGamePlugins(MOBase::IOrganizer* organizer)
      : AsteriskGamePlugins(organizer, true, true), m_Organizer(organizer)
  {}

protected:
  // only while the load order is plugins.txt's (no sTestFile lines)
  bool pluginsTxtInUse() const override;

private:
  MOBase::IOrganizer* m_Organizer;
};

class StarfieldDataArchives : public MOBase::DataArchives
{
public:
  explicit StarfieldDataArchives(const BethesdaGame* game) : m_Game(game) {}

  QStringList vanillaArchives() const override;
  QStringList archives(const MOBase::IProfile* profile) const override;
  // the list isn't MO2's to change
  void addArchive(MOBase::IProfile*, int, const QString&) override {}
  void removeArchive(MOBase::IProfile*, const QString&) override {}

private:
  const BethesdaGame* m_Game;
};

// a Creation in ContentCatalog.txt, by each of its plugins
struct StarfieldCreation
{
  QString name;
  QStringList files;
};
std::map<QString, StarfieldCreation> parseContentCatalog(const QString& appDataFolder,
                                                         const QString& shortName);

class StarfieldUnmanagedMods : public MOBase::UnmanagedMods
{
public:
  StarfieldUnmanagedMods(const BethesdaGame* game, QString appDataFolder)
      : m_Game(game), m_AppDataFolder(std::move(appDataFolder))
  {}

  QStringList mods(bool onlyOfficial) const override;
  QString displayName(const QString& modName) const override;
  QFileInfo referenceFile(const QString& modName) const override;
  QStringList secondaryFiles(const QString& modName) const override;

private:
  const BethesdaGame* m_Game;
  QString m_AppDataFolder;
};

class StarfieldDataContent : public BethesdaDataContent
{
public:
  using BethesdaDataContent::BethesdaDataContent;

  std::vector<Content> getAllContents() const override;
  std::vector<int>
  getContentsFor(std::shared_ptr<const MOBase::IFileTree> fileTree) const override;
};

}  // namespace mo2r::bethesda
