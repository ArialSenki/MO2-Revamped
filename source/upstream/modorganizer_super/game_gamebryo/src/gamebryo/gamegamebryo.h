#ifndef GAMEGAMEBRYO_H
#define GAMEGAMEBRYO_H

#include "iplugingame.h"

class BSAInvalidation;
class DataArchives;
class LocalSavegames;
class SaveGameInfo;
class BSAInvalidation;
class LocalSavegames;
class ScriptExtender;
class GamePlugins;
class UnmanagedMods;

#include <QObject>
#include <QString>
#include <ShlObj.h>
#include <dbghelp.h>
#include <ipluginfilemapper.h>
#include <iplugingame.h>
#include <QDateTime>
#include <QStringList>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "gamebryosavegame.h"
#include "igamefeatures.h"

class GameGamebryo : public MOBase::IPluginGame, public MOBase::IPluginFileMapper
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame MOBase::IPluginFileMapper)

  friend class GamebryoScriptExtender;
  friend class GamebryoSaveGameInfo;
  friend class GamebryoSaveGameInfoWidget;
  friend class GamebryoSaveGame;

  /**
   * Some Bethesda games do not have a valid file version but a valid product
   * version. If the file version starts with FALLBACK_GAME_VERSION, the product
   * version will be tried.
   */
  static constexpr const char* FALLBACK_GAME_VERSION = "1.0.0";

public:
  GameGamebryo();

  void detectGame() override;
  bool init(MOBase::IOrganizer* moInfo) override;

public:  // IPluginGame interface
  // getName
  // initializeProfile
  virtual std::vector<std::shared_ptr<const MOBase::ISaveGame>>
  listSaves(QDir folder) const override;

  virtual bool isInstalled() const override;
  virtual QIcon gameIcon() const override;
  virtual QDir gameDirectory() const override;
  virtual QDir dataDirectory() const override;
  // secondaryDataDirectories
  virtual void setGamePath(const QString& path) override;
  virtual QDir documentsDirectory() const override;
  virtual QDir savesDirectory() const override;
  // executables
  // steamAPPId
  // primaryPlugins
  // enabledPlugins
  // gameVariants
  virtual void setGameVariant(const QString& variant) override;
  virtual QString binaryName() const override;
  // gameShortName
  // primarySources
  // validShortNames
  // iniFiles
  // DLCPlugins
  // CCPlugins
  virtual LoadOrderMechanism loadOrderMechanism() const override;
  virtual SortMechanism sortMechanism() const override;
  // nexusModOrganizerID
  // nexusGameID
  virtual bool looksValid(QDir const&) const override;
  virtual QString gameVersion() const override;
  virtual QString getLauncherName() const override;

public:  // IPluginFileMapper interface
  virtual MappingType mappings() const;

public:  // Other (e.g. for game features)
  QString myGamesPath() const;

protected:
  // Retrieve the saves extension for the game.
  virtual QString savegameExtension() const   = 0;
  virtual QString savegameSEExtension() const = 0;

  // Create a save game.
  virtual std::shared_ptr<const GamebryoSaveGame>
  makeSaveGame(QString filepath) const = 0;

  QFileInfo findInGameFolder(const QString& relativePath) const;
  QString selectedVariant() const;
  WORD getArch(QString const& program) const;

  static QString localAppFolder();
  // Arguably this shouldn't really be here but every gamebryo program seems to
  // use it
  static QString getLootPath();

  // This function is not terribly well named as it copies exactly where it's told
  // to, irrespective of whether it's in the profile...
  static void copyToProfile(const QString& sourcePath, const QDir& destinationDirectory,
                            const QString& sourceFileName);

  static void copyToProfile(const QString& sourcePath, const QDir& destinationDirectory,
                            const QString& sourceFileName,
                            const QString& destinationFileName);

  virtual QString identifyGamePath() const;

  virtual bool prepareIni(const QString& exec);

  static std::unique_ptr<BYTE[]> getRegValue(HKEY key, LPCWSTR path, LPCWSTR value,
                                             DWORD flags, LPDWORD type);

  static QString findInRegistry(HKEY baseKey, LPCWSTR path, LPCWSTR value);

  static QString getKnownFolderPath(REFKNOWNFOLDERID folderId, bool useDefault);

  static QString getSpecialPath(const QString& name);

  static QString determineMyGamesPath(const QString& gameName);

  static QString parseEpicGamesLocation(const QStringList& manifests);

  static QString parseSteamLocation(const QString& appid, const QString& directoryName);

protected:
  void registerFeature(std::shared_ptr<MOBase::GameFeature> feature);

  // MO2 Revamped: the trimmed, non-empty, non-comment lines of a small text
  // file (Skyrim.ccc, Fallout4.ccc), read again only when its size or time
  // changes. primaryPlugins() runs many times per refresh, and each call used
  // to open and read the file.
  QStringList cachedFileLines(const QString& path) const;

  // MO2 Revamped: a value computed from a file (the sTestFile entries of an
  // INI), kept until the file's size or time changes
  QStringList cachedFromFile(const QString& path,
                             const std::function<QStringList()>& compute) const;

  // MO2 Revamped: the first `parts` numbers of the game exe's version joined
  // by `separator` ("1_6_1170"), empty if the version can't be read; kept
  // until the exe changes
  QString gameVersionJoined(int parts, const QString& separator) const;

  // MO2 Revamped: the script extender's loader is in the game folder but not
  // its runtime for this game version (prefix + version + ".dll", the name
  // the loader looks for), so the game would start without it. The expected
  // name goes to *expected.
  bool scriptExtenderRuntimeMissing(const QString& runtimePrefix,
                                    QString* expected) const;

  // MO2 Revamped: script extender plugins (DLLs in pluginsDir, in the virtual
  // Data folder) but no Address Library file for this game version, which
  // most of them need. The expected name goes to *expected.
  bool addressLibraryMissing(const QString& pluginsDir, QString* expected) const;

protected:
  // to access organizer for game features, avoid having to pass it to all saves since
  // we already pass the game
  friend class GamebryoSaveGame;

  QString m_GamePath;
  QString m_MyGamesPath;
  QString m_GameVariant;
  MOBase::IOrganizer* m_Organizer;

private:
  struct FileCacheEntry
  {
    bool exists = false;
    qint64 size = -1;
    QDateTime modified;
    QStringList value;
  };
  mutable std::mutex m_FileCacheMutex;
  mutable std::map<QString, FileCacheEntry> m_FileCache;
};

#endif  // GAMEGAMEBRYO_H
