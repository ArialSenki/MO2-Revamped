#ifndef ORGANIZERCORE_H
#define ORGANIZERCORE_H

#include "downloadmanager.h"
#include "envdump.h"
#include "executableinfo.h"
#include "executableslist.h"
#include "guessedvalue.h"
#include "installationmanager.h"
#include "memoizedlock.h"
#include "moddatacontent.h"
#include "modinfo.h"
#include "modlist.h"
#include "moshortcut.h"
#include "pluginlist.h"
#include "processrunner.h"
#include "rootlaunch.h"
#include "selfupdater.h"
#include "settings.h"
#include "uilocker.h"
#include "usvfsconnector.h"
#include <boost/signals2.hpp>
#include <delayedfilewriter.h>
#include <imoinfo.h>
#include <iplugindiagnose.h>
#include <log.h>
#include <versioninfo.h>

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QThread>
#include <QVariant>

class QTimer;

class ModListSortProxy;
class PluginListSortProxy;
class Profile;
class IUserInterface;
class GameFeatures;
class PluginContainer;
class DirectoryRefresher;

namespace MOBase
{
template <typename T>
class GuessedValue;
class IModInterface;
class IPluginGame;
}  // namespace MOBase

namespace MOShared
{
class DirectoryEntry;
}

class OrganizerCore : public QObject, public MOBase::IPluginDiagnose
{

  Q_OBJECT
  Q_INTERFACES(MOBase::IPluginDiagnose)

private:
  friend class OrganizerProxy;

  struct SignalCombinerAnd
  {
    using result_type = bool;

    template <typename InputIterator>
    bool operator()(InputIterator first, InputIterator last) const
    {
      while (first != last) {
        if (!(*first)) {
          return false;
        }
        ++first;
      }
      return true;
    }
  };

private:
  using SignalAboutToRunApplication =
      boost::signals2::signal<bool(const QString&, const QDir&, const QString&),
                              SignalCombinerAnd>;
  using SignalFinishedRunApplication =
      boost::signals2::signal<void(const QString&, unsigned int)>;
  using SignalUserInterfaceInitialized = boost::signals2::signal<void(QMainWindow*)>;
  using SignalProfileCreated = boost::signals2::signal<void(MOBase::IProfile*)>;
  using SignalProfileRenamed =
      boost::signals2::signal<void(MOBase::IProfile*, QString const&, QString const&)>;
  using SignalProfileRemoved = boost::signals2::signal<void(QString const&)>;
  using SignalProfileChanged =
      boost::signals2::signal<void(MOBase::IProfile*, MOBase::IProfile*)>;
  using SignalPluginSettingChanged = boost::signals2::signal<void(
      QString const&, const QString& key, const QVariant&, const QVariant&)>;
  using SignalPluginEnabled = boost::signals2::signal<void(const MOBase::IPlugin*)>;

public:
  /**
   * Small holder for the game content returned by the ModDataContent feature (the
   * list of all possible contents, not the per-mod content).
   */
  struct ModDataContentHolder
  {

    using Content = MOBase::ModDataContent::Content;

    /**
     * @return true if the hold list of contents is empty, false otherwise.
     */
    bool empty() const { return m_Contents.empty(); }

    /**
     * @param id ID of the content to retrieve.
     *
     * @return the content with the given ID, or a null pointer if it is not found.
     */
    const Content* findById(int id) const
    {
      auto it = std::find_if(std::begin(m_Contents), std::end(m_Contents),
                             [&id](auto const& content) {
                               return content.id() == id;
                             });
      return it == std::end(m_Contents) ? nullptr : &(*it);
    }

    /**
     * Apply the given function to each content whose ID is in the given set.
     *
     * @param ids The set of content IDs.
     * @param fn The function to apply.
     * @param includeFilter true to also apply the function to filter-only contents,
     * false otherwise.
     */
    template <class Fn>
    void forEachContentIn(std::set<int> const& ids, Fn const& fn,
                          bool includeFilter = false) const
    {
      for (const auto& content : m_Contents) {
        if ((includeFilter || !content.isOnlyForFilter()) &&
            ids.find(content.id()) != ids.end()) {
          fn(content);
        }
      }
    }

    /**
     * @brief Apply fnIn to each content whose ID is in the given set, and fnOut to each
     * content not in the given set, excluding filter-only content (from both cases)
     * unless includeFilter is true.
     *
     * @param ids The set of content IDs.
     * @param fnIn Function to apply to content whose IDs are in ids.
     * @param fnOut Function to apply to content whose IDs are not in ids.
     * @param includeFilter true to also apply the function to filter-only contents,
     * false otherwise.
     */
    template <class FnIn, class FnOut>
    void forEachContentInOrOut(std::set<int> const& ids, FnIn const& fnIn,
                               FnOut const& fnOut, bool includeFilter = false) const
    {
      for (const auto& content : m_Contents) {
        if ((includeFilter || !content.isOnlyForFilter())) {
          if (ids.find(content.id()) != ids.end()) {
            fnIn(content);
          } else {
            fnOut(content);
          }
        }
      }
    }

    /**
     * Apply the given function to each content.
     *
     * @param fn The function to apply.
     * @param includeFilter true to also apply the function to filter-only contents,
     * false otherwise.
     */
    template <class Fn>
    void forEachContent(Fn const& fn, bool includeFilter = false) const
    {
      for (const auto& content : m_Contents) {
        if (includeFilter || !content.isOnlyForFilter()) {
          fn(content);
        }
      }
    }

    ModDataContentHolder& operator=(ModDataContentHolder const&) = delete;
    ModDataContentHolder& operator=(ModDataContentHolder&&)      = default;

  private:
    std::vector<Content> m_Contents;

    /**
     * @brief Construct a ModDataContentHolder without any contents (e.g., if the
     * feature is missing).
     */
    ModDataContentHolder() {}

    /**
     * @brief Construct a ModDataContentHold holding the given list of contents.
     */
    ModDataContentHolder(std::vector<MOBase::ModDataContent::Content> contents)
        : m_Contents(std::move(contents))
    {}

    friend class OrganizerCore;
  };

  // enumeration for the mode when adding refresh callbacks
  //
  enum class RefreshCallbackMode : int
  {
    // run the callbacks immediately if no refresh is running
    RUN_NOW_IF_POSSIBLE = 0,

    // wait for the next refresh if none is running
    FORCE_WAIT_FOR_REFRESH = 1
  };

  // enumeration for the groups where refresh callbacks can be put
  //
  enum class RefreshCallbackGroup : int
  {
    // for callbacks by the core itself, highest priority
    CORE = 0,

    // internal MO2 callbacks
    INTERNAL = 1,

    // external callbacks, typically MO2 plugins
    EXTERNAL = 2
  };

public:
  OrganizerCore(Settings& settings);

  ~OrganizerCore();

  void setUserInterface(IUserInterface* ui);
  void connectPlugins(PluginContainer* container);

  void setManagedGame(MOBase::IPluginGame* game);

  void updateExecutablesList();

  // whether updateExecutablesList() took out an entry of a retired launcher
  // (me3, Mod Engine 2, Seamless Co-op's): the program picked before may
  // point elsewhere now
  bool removedBetaLaunchers() const { return m_RemovedBetaLaunchers; }
  void updateModInfoFromDisc();

  void checkForUpdates();
  Settings& settings();
  SelfUpdater* updater() { return &m_Updater; }
  InstallationManager* installationManager();
  MOShared::DirectoryEntry* directoryStructure() { return m_DirectoryStructure; }
  DirectoryRefresher* directoryRefresher() { return m_DirectoryRefresher.get(); }
  ExecutablesList* executablesList() { return &m_ExecutablesList; }
  void setExecutablesList(const ExecutablesList& executablesList)
  {
    m_ExecutablesList = executablesList;
  }

  Profile* currentProfile() const { return m_CurrentProfile.get(); }
  // reload: load the profile again even when it is the current one, so
  // settings changed on disk (an Elden Ring save route) take effect
  void setCurrentProfile(const QString& profileName, bool reload = false);

  std::vector<QString> enabledArchives();

  MOBase::VersionInfo getVersion() const { return m_Updater.getVersion(); }

  // return the plugin container
  //
  PluginContainer& pluginContainer() const;

  // return the game features
  GameFeatures& gameFeatures() const;

  MOBase::IPluginGame const* managedGame() const;

  /**
   * @brief Retrieve the organizer proxy of the currently managed game.
   *
   */
  MOBase::IOrganizer const* managedGameOrganizer() const;

  /**
   * @return the list of contents for the currently managed game, or an empty vector
   *     if the game plugin does not implement the ModDataContent feature.
   */
  const ModDataContentHolder& modDataContents() const { return m_Contents; }

  bool isArchivesInit() const { return m_ArchivesInit; }

  bool saveCurrentLists();

  ProcessRunner processRunner();

  bool beforeRun(const QFileInfo& binary, const QDir& cwd, const QString& arguments,
                 const QString& profileName, const QString& customOverwrite,
                 const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries);

  void afterRun(const QFileInfo& binary, DWORD exitCode);

  // MO2 Revamped: how the files of the mods' Root folders that the VFS can't
  // serve reach the program (rootlaunch.h); empty when Root/ isn't mapped
  RootLaunch::Plan planRootLaunch(const QString& profileName, const QFileInfo& binary,
                                  const QString& arguments);

  // the copy fallback for a run; its result says what failed (a game folder
  // that can't be written to also shows one notice per session)
  RootCopy::Result deployRootCopies(const std::vector<RootCopy::File>& files);

  // takes the copies out and restores the game's files once no program runs
  // in the VFS; true when nothing is left in the game folder
  bool restoreRootCopies(bool exiting = false);

  // a launch from its copies to its start counts as running; endRootRun()
  // takes the started process (or none) and starts the restore timer
  void beginRootRun();
  void endRootRun(HANDLE process);

  ProcessRunner::Results
  waitForAllUSVFSProcesses(UILocker::Reasons reason = UILocker::PreventExit);

  void refreshESPList(bool force = false);
  void refreshBSAList();

  void refreshDirectoryStructure();

  // MO2 Revamped: while a collection installs, each mod's refresh would rebuild
  // the whole virtual file structure (every mod's files) again, which grows with
  // the square of the mod count. While deferred, the rebuild waits: it runs
  // once when deferring ends, and before anything that needs the structure
  // (a program's start, waitForDirectoryRefresh())
  void setDirectoryRefreshDeferred(bool deferred);

  // returns once a directory refresh in progress has finished; plugins that
  // look up file origins (forced loads, for one) need the finished structure
  void waitForDirectoryRefresh();

  void updateModInDirectoryStructure(unsigned int index, ModInfo::Ptr modInfo);
  void updateModsInDirectoryStructure(QMap<unsigned int, ModInfo::Ptr> modInfos);

  void doAfterLogin(const std::function<void()>& function)
  {
    m_PostLoginTasks.append(function);
  }
  void loggedInAction(QWidget* parent, std::function<void()> f);

  bool previewFileWithAlternatives(QWidget* parent, QString filename,
                                   int selectedOrigin = -1);
  bool previewFile(QWidget* parent, const QString& originName, const QString& path);

  static bool createAndMakeWritable(const QString& path);
  bool checkPathSymlinks();
  bool bootstrap();
  void createDefaultProfile();

  void prepareVFS();

  void updateVFSParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
                       const QString& coreDumpsPath, std::chrono::seconds spawnDelay,
                       QString executableBlacklist, const QStringList& skipFileSuffixes,
                       const QStringList& skipDirectories);

  void setLogLevel(MOBase::log::Levels level);

  bool cycleDiagnostics();

  static env::CoreDumpTypes getGlobalCoreDumpType();
  static void setGlobalCoreDumpType(env::CoreDumpTypes type);
  static std::wstring getGlobalCoreDumpPath();

public:
  MOBase::IModRepositoryBridge* createNexusBridge() const;
  QString profileName() const;
  QString profilePath() const;
  QString downloadsPath() const;
  QString overwritePath() const;
  QString basePath() const;
  QString modsPath() const;
  MOBase::VersionInfo appVersion() const;
  MOBase::IPluginGame* getGame(const QString& gameName) const;
  MOBase::IModInterface* createMod(MOBase::GuessedValue<QString>& name);

  // MO2 Revamped: with finishReplace off, a mod that Replace set aside stays aside
  // until finishCreatedMod() says whether the new mod got its files
  MOBase::IModInterface* createMod(MOBase::GuessedValue<QString>& name,
                                   bool finishReplace);
  void finishCreatedMod(bool filled);

  // whether the last createMod() replaced an existing folder
  bool createdModReplaced() const;
  // whether the last createMod() made its folder itself (not Merge or Replace)
  bool createdModIsNew() const;

  void modDataChanged(MOBase::IModInterface* mod);
  QVariant pluginSetting(const QString& pluginName, const QString& key) const;
  void setPluginSetting(const QString& pluginName, const QString& key,
                        const QVariant& value);
  QVariant persistent(const QString& pluginName, const QString& key,
                      const QVariant& def) const;
  void setPersistent(const QString& pluginName, const QString& key,
                     const QVariant& value, bool sync);
  static QString pluginDataPath();
  virtual MOBase::IModInterface* installMod(const QString& fileName, int priority,
                                            bool reinstallation,
                                            ModInfo::Ptr currentMod,
                                            const QString& initModName);
  QString resolvePath(const QString& fileName) const;
  QStringList listDirectories(const QString& directoryName) const;
  QStringList findFiles(const QString& path,
                        const std::function<bool(const QString&)>& filter) const;
  QStringList getFileOrigins(const QString& fileName) const;
  QList<MOBase::IOrganizer::FileInfo> findFileInfos(
      const QString& path,
      const std::function<bool(const MOBase::IOrganizer::FileInfo&)>& filter) const;
  DownloadManager* downloadManager();
  PluginList* pluginList();
  ModList* modList();
  void refresh(bool saveChanges = true);

  // MO2 Revamped: after an install while directory refreshes are deferred (a
  // collection): only that mod's folder is read again; otherwise refresh()
  void refreshInstalledMod(const QString& modName);

  // MO2 Revamped: Nexus collection links (nxm://<game>/collections/...) go to
  // the handler the main window sets; links that come before it wait for it
  void setCollectionHandler(std::function<void(const QString&)> handler);
  void requestCollection(const QString& url);
  // a file link a collection waits for: its download gives no notice, the
  // collection's window shows it
  void setCollectionClaims(std::function<bool(const QString&)> claims);
  // a .wabbajack file from the command line or another MO2 start: the main
  // window shows it; one that comes before the window waits for it
  void setWabbajackHandler(std::function<void(const QString&)> handler);
  // an nxm link a Wabbajack list waits for (a free account's click): taken by
  // the list's window, not downloaded by the download manager
  void setWabbajackClaims(std::function<bool(const QString&)> claims);
  void requestWabbajackList(const QString& path);

  boost::signals2::connection onAboutToRun(
      const std::function<bool(const QString&, const QDir&, const QString&)>& func);
  boost::signals2::connection
  onFinishedRun(const std::function<void(const QString&, unsigned int)>& func);
  boost::signals2::connection
  onUserInterfaceInitialized(std::function<void(QMainWindow*)> const& func);
  boost::signals2::connection
  onProfileCreated(std::function<void(MOBase::IProfile*)> const& func);
  boost::signals2::connection onProfileRenamed(
      std::function<void(MOBase::IProfile*, QString const&, QString const&)> const&
          func);
  boost::signals2::connection
  onProfileRemoved(std::function<void(QString const&)> const& func);
  boost::signals2::connection onProfileChanged(
      std::function<void(MOBase::IProfile*, MOBase::IProfile*)> const& func);
  boost::signals2::connection onPluginSettingChanged(
      std::function<void(QString const&, const QString& key, const QVariant&,
                         const QVariant&)> const& func);
  boost::signals2::connection
  onPluginEnabled(std::function<void(const MOBase::IPlugin*)> const& func);
  boost::signals2::connection
  onPluginDisabled(std::function<void(const MOBase::IPlugin*)> const& func);

  // add a function to be called after the next refresh is done
  //
  // - group to add the function to
  // - if immediateIfReady is true, the function will be called immediately if no
  //   directory update is running
  boost::signals2::connection onNextRefresh(std::function<void()> const& func,
                                            RefreshCallbackGroup group,
                                            RefreshCallbackMode mode);

public:  // IPluginDiagnose interface
  virtual std::vector<unsigned int> activeProblems() const;
  virtual QString shortDescription(unsigned int key) const;
  virtual QString fullDescription(unsigned int key) const;
  virtual bool hasGuidedFix(unsigned int key) const;
  virtual void startGuidedFix(unsigned int key) const;

public slots:

  void syncOverwrite();

  void savePluginList();

  void refreshLists();

  ModInfo::Ptr installDownload(int downloadIndex, int priority = -1);
  ModInfo::Ptr installArchive(const QString& archivePath, int priority = -1,
                              bool reinstallation     = false,
                              ModInfo::Ptr currentMod = nullptr,
                              const QString& modName  = QString());

  void modPrioritiesChanged(QModelIndexList const& indexes);
  void modStatusChanged(unsigned int index);
  void modStatusChanged(QList<unsigned int> index);
  void downloadRequestedNXM(const QString& url);

  void profileCreated(MOBase::IProfile* profile);
  void profileRenamed(MOBase::IProfile* profile, QString const& oldName,
                      QString const& newName);
  void profileRemoved(QString const& profileName);

  bool nexusApi(bool retry = false);

signals:

  // emitted after a mod has been installed
  //
  void modInstalled(const QString& modName);

  // emitted when the managed game changes
  //
  void managedGameChanged(MOBase::IPluginGame const* gamePlugin);

  // emitted when the profile is changed, before notifying plugins
  //
  // the new profile can be stored but the old one is temporary and
  // should not be
  //
  void profileChanged(Profile* oldProfile, Profile* newProfile);

  // Notify that the directory structure is ready to be used on the main thread
  // Use queued connections
  void directoryStructureReady();

  // Notify of a general UI refresh
  void refreshTriggered();

private:
  std::pair<unsigned int, ModInfo::Ptr> doInstall(const QString& archivePath,
                                                  MOBase::GuessedValue<QString> modName,
                                                  ModInfo::Ptr currentMod, int priority,
                                                  bool reinstallation);

  void saveCurrentProfile();
  void storeSettings();

  void updateModActiveState(int index, bool active);
  void updateModsActiveState(const QList<unsigned int>& modIndices, bool active);

  // gives the origins the profile's priorities and sorts the files' origins:
  // all of them if a priority moved (returns true), else only the files of
  // changedMods
  bool syncOriginPriorities(const std::vector<unsigned int>& changedMods);

  // clear the conflict caches of all the given mods, and the mods in conflict
  // with the given mods
  //
  void clearCaches(std::vector<unsigned int> const& indices) const;

  bool createDirectory(const QString& path);

  QString oldMO1HookDll() const;

  /**
   * @brief return a descriptor of the mappings real file->virtual file
   */
  std::vector<Mapping> fileMapping(const QString& profile,
                                   const QString& customOverwrite);

  std::vector<Mapping> fileMapping(const QString& dataPath, const QString& relPath,
                                   const MOShared::DirectoryEntry* base,
                                   const MOShared::DirectoryEntry* directoryEntry,
                                   int createDestination);

private slots:

  void onDirectoryRefreshed();
  void onDirectoryRefreshFailed();
  void removeOrigin(const QString& name);
  void downloadSpeed(const QString& serverName, int bytesPerSecond);
  void loginSuccessful(bool necessary);
  void loginFailed(const QString& message);

private:
  static const unsigned int PROBLEM_MO1SCRIPTEXTENDERWORKAROUND = 1;

private:
  IUserInterface* m_UserInterface;
  PluginContainer* m_PluginContainer;
  QString m_GameName;
  MOBase::IPluginGame* m_GamePlugin;
  ModDataContentHolder m_Contents;

  std::unique_ptr<Profile> m_CurrentProfile;

  Settings& m_Settings;

  SelfUpdater m_Updater;

  SignalAboutToRunApplication m_AboutToRun;
  SignalFinishedRunApplication m_FinishedRun;
  SignalUserInterfaceInitialized m_UserInterfaceInitialized;
  SignalProfileCreated m_ProfileCreated;
  SignalProfileRenamed m_ProfileRenamed;
  SignalProfileRemoved m_ProfileRemoved;
  SignalProfileChanged m_ProfileChanged;
  SignalPluginSettingChanged m_PluginSettingChanged;
  SignalPluginEnabled m_PluginEnabled;
  SignalPluginEnabled m_PluginDisabled;

  boost::signals2::signal<void()> m_OnNextRefreshCallbacks;

  ModList m_ModList;
  PluginList m_PluginList;

  QList<std::function<void()>> m_PostLoginTasks;

  ExecutablesList m_ExecutablesList;
  bool m_RemovedBetaLaunchers = false;
  QStringList m_PendingDownloads;
  QStringList m_DefaultArchives;
  QStringList m_ActiveArchives;

  std::unique_ptr<DirectoryRefresher> m_DirectoryRefresher;
  MOShared::DirectoryEntry* m_DirectoryStructure;
  std::shared_ptr<std::atomic<bool>> m_VirtualFileTreeValid =
      std::make_shared<std::atomic<bool>>(true);
  MOBase::MemoizedLocked<std::shared_ptr<const MOBase::IFileTree>> m_VirtualFileTree;

  DownloadManager m_DownloadManager;
  InstallationManager m_InstallationManager;

  QThread m_RefresherThread;

  std::thread m_StructureDeleter;

  std::atomic<bool> m_DirectoryUpdate;

  // mods enabled, disabled or moved, or another profile chosen, while a
  // refresh was running: the structure it delivers is built from the mods as
  // they were when it started, so another one follows. UI thread only
  bool m_RefreshPending = false;
  bool m_DeferDirectoryRefresh  = false;
  bool m_DirectoryRefreshWaiting = false;

  // the last refresh failed, so the plugin list and the structure may not match
  // the current profile: they aren't rebuilt or saved until one works, while
  // programs can still be launched
  bool m_LastRefreshFailed = false;

  // full plugin list rebuilds so far: one a post-refresh task already did isn't
  // done again by refreshLists() at the end of the same refresh
  unsigned int m_ESPListRebuilds = 0;

  std::function<void(const QString&)> m_CollectionHandler;
  QStringList m_PendingCollections;
  std::function<bool(const QString&)> m_CollectionClaims;
  std::function<void(const QString&)> m_WabbajackHandler;
  std::function<bool(const QString&)> m_WabbajackClaims;
  QStringList m_PendingWabbajackLists;

  // the last createMod() set an existing folder aside (Replace)
  bool m_CreatedModReplaced = false;
  bool m_CreatedModNew      = false;

  // MO2 Revamped: asks for another name for a mod just installed under a name
  // isReservedModName() refuses, and renames its folder
  void renameReservedInstall(MOBase::GuessedValue<QString>& modName);

  // MO2 Revamped: plugins whose file time couldn't be set the last time the
  // load order was saved, so the user is told once per change
  QStringList m_UndatedPlugins;

  // MO2 Revamped: the copy fallback's state (<instance>/rootcopy-backup/<game>),
  // and the timer that restores it once the programs in the VFS end
  QString rootCopyBase() const;
  QString rootCopyStateDir() const;
  QTimer* m_RootCopyTimer    = nullptr;
  bool m_RootCopyNoticeShown = false;
  // launches between their copies and their start, the processes they
  // started, and when the last one started
  int m_RootRunsActive = 0;
  std::vector<HANDLE> m_RootRunHandles;
  HANDLE m_RootLastRun = nullptr;
  QElapsedTimer m_RootRunGrace;
  // files restore left because something else changed them: told once
  bool m_RootConflictNoticeShown   = false;
  bool m_RootConflictNoticePending = false;
  QStringList m_RootConflictFiles;
  bool rootProgramsRunning(bool ignoreGrace = false);
  void showRootConflictNotice();

  bool m_ArchivesInit;

  MOBase::DelayedFileWriter m_PluginListsWriter;
  // MO2 Revamped: the plugin list is saved at once, not after a running
  // refresh (before a profile switch)
  bool m_SavePluginListNow = false;
  UsvfsConnector m_USVFS;

  UILocker m_UILocker;
};

#endif  // ORGANIZERCORE_H
