// MO2 Revamped: Bethesda game framework, plugins MO2 didn't install.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's UnmanagedMods feature: the plugins in the game's own Data folder
// (DLC, Creation Club, a mod installed by hand) shown as mods, with their
// BSAs. The folder is listed again only when it changes.
#pragma once

#include <unmanagedmods.h>

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <mutex>

namespace mo2r::bethesda
{

class BethesdaGame;

class DataFolderMods : public MOBase::UnmanagedMods
{
public:
  explicit DataFolderMods(const BethesdaGame* game) : m_Game(game) {}

  QStringList mods(bool onlyOfficial) const override;
  QString displayName(const QString& modName) const override { return modName; }
  QFileInfo referenceFile(const QString& modName) const override;
  QStringList secondaryFiles(const QString& modName) const override;

private:
  // the files in Data, sorted as QDir sorts them (name, ignoring case)
  QStringList dataFiles() const;
  // dataFiles() starting with prefix and ending with suffix, ignoring case
  QStringList matching(const QString& prefix, const QString& suffix) const;

  const BethesdaGame* m_Game;
  mutable std::mutex m_Mutex;
  mutable QString m_Path;
  mutable QDateTime m_Modified;
  mutable QStringList m_Files;
};

}  // namespace mo2r::bethesda
