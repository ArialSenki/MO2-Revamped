#ifndef GAMEBRYOUNMANAGEDMODS_H
#define GAMEBRYOUNMANAGEDMODS_H

#include <unmanagedmods.h>

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <mutex>

class GameGamebryo;

class GamebryoUnmangedMods : public MOBase::UnmanagedMods
{
public:
  GamebryoUnmangedMods(const GameGamebryo* game);
  ~GamebryoUnmangedMods();

  virtual QStringList mods(bool onlyOfficial) const override;
  virtual QString displayName(const QString& modName) const override;
  virtual QFileInfo referenceFile(const QString& modName) const override;
  virtual QStringList secondaryFiles(const QString& modName) const override;

protected:
  const GameGamebryo* game() const { return m_Game; }

  // MO2 Revamped: the names of the files in the game's Data folder, sorted
  // as QDir sorts them, listed again only when the folder changes (an entry
  // added, removed or renamed). The mod list asks for the reference file and
  // the archives of each unmanaged mod (the DLC and every Creation Club
  // plugin), and each of those calls listed the folder.
  QStringList dataFileNames() const;

  // dataFileNames() that start with prefix and end with suffix, ignoring case
  QStringList dataFilesMatching(const QString& prefix, const QString& suffix) const;

  // the plugins (.esp, .esm, .esl) in dataFileNames()
  QStringList dataPlugins() const;

private:
  const GameGamebryo* m_Game;

  mutable std::mutex m_DataMutex;
  mutable QString m_DataPath;
  mutable QDateTime m_DataModified;
  mutable QStringList m_DataFiles;
};

#endif  // GAMEBRYOUNMANAGEDMODS_H
