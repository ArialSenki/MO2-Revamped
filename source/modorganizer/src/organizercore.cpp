#include "organizercore.h"
#include <QDateTime>
#include "categoriesdialog.h"
#include "delayedfilewriter.h"
#include "directoryrefresher.h"
#include "eldenringsavesettings.h"
#include "env.h"
#include "envfs.h"
#include "envmodule.h"
#include "filedialogmemory.h"
#include "filesystemutilities.h"
#include "guessedvalue.h"
#include "imodinterface.h"
#include "imoinfo.h"
#include "instancemanager.h"
#include "iplugingame.h"
#include "iuserinterface.h"
#include "messagedialog.h"
#include "modlistsortproxy.h"
#include "modrepositoryfileinfo.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "plugincontainer.h"
#include "previewdialog.h"
#include "profile.h"
#include "shared/appconfig.h"
#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"
#include "shared/util.h"
#include "spawn.h"
#include "syncoverwritedialog.h"
#include "virtualfiletree.h"
#include <dataarchives.h>
#include <ipluginmodpage.h>
#include <localsavegames.h>
#include <questionboxmemory.h>
#include <report.h>
#include <scopeguard.h>
#include <scriptextender.h>
#include <usvfs.h>
#include <utility.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <QtDebug>
#include <QtGlobal>  // for qUtf8Printable, etc

#include <Psapi.h>
#include <Shlobj.h>
#include <tchar.h>  // for _tcsicmp
#include <tlhelp32.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>  // for memset, wcsrchr

#include <boost/algorithm/string/predicate.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <set>
#include <string>  //for wstring
#include <tuple>
#include <utility>

#include "bs_archive.h"

#include "organizerproxy.h"

using namespace MOShared;
using namespace MOBase;

static env::CoreDumpTypes g_coreDumpType = env::CoreDumpTypes::Mini;

template <typename InputIterator>
QStringList toStringList(InputIterator current, InputIterator end)
{
  QStringList result;
  for (; current != end; ++current) {
    result.append(*current);
  }
  return result;
}

OrganizerCore::OrganizerCore(Settings& settings)
    : m_UserInterface(nullptr), m_PluginContainer(nullptr), m_GamePlugin(nullptr),
      m_CurrentProfile(nullptr), m_Settings(settings),
      m_Updater(&NexusInterface::instance()), m_ModList(m_PluginContainer, this),
      m_PluginList(*this),
      m_DirectoryRefresher(new DirectoryRefresher(this, settings.refreshThreadCount())),
      m_DirectoryStructure(new DirectoryEntry(L"data", nullptr, 0)),
      m_VirtualFileTree([this]() {
        return VirtualFileTree::makeTree(m_DirectoryStructure, m_VirtualFileTreeValid);
      }),
      m_DownloadManager(&NexusInterface::instance(), this), m_DirectoryUpdate(false),
      m_ArchivesInit(false),
      m_PluginListsWriter(std::bind(&OrganizerCore::savePluginList, this))
{
  env::setHandleCloserThreadCount(settings.refreshThreadCount());
  m_DownloadManager.setOutputDirectory(m_Settings.paths().downloads(), false);

  NexusInterface::instance().setCacheDirectory(m_Settings.paths().cache());

  m_InstallationManager.setModsDirectory(m_Settings.paths().mods());
  m_InstallationManager.setDownloadDirectory(m_Settings.paths().downloads());

  connect(&m_DownloadManager, SIGNAL(downloadSpeed(QString, int)), this,
          SLOT(downloadSpeed(QString, int)));
  connect(m_DirectoryRefresher.get(), &DirectoryRefresher::refreshed, this,
          &OrganizerCore::onDirectoryRefreshed);
  connect(m_DirectoryRefresher.get(), &DirectoryRefresher::refreshFailed, this,
          &OrganizerCore::onDirectoryRefreshFailed);

  connect(&m_ModList, SIGNAL(removeOrigin(QString)), this, SLOT(removeOrigin(QString)));
  connect(&m_ModList, &ModList::modStatesChanged, [=] {
    currentProfile()->writeModlist();
  });
  connect(&m_ModList, &ModList::modPrioritiesChanged, [this](auto&& indexes) {
    modPrioritiesChanged(indexes);
  });

  connect(NexusInterface::instance().getAccessManager(),
          SIGNAL(validateSuccessful(bool)), this, SLOT(loginSuccessful(bool)));
  connect(NexusInterface::instance().getAccessManager(),
          SIGNAL(validateFailed(QString)), this, SLOT(loginFailed(QString)));

  // This seems awfully imperative
  connect(this, SIGNAL(managedGameChanged(MOBase::IPluginGame const*)), &m_Settings,
          SLOT(managedGameChanged(MOBase::IPluginGame const*)));
  connect(this, SIGNAL(managedGameChanged(MOBase::IPluginGame const*)),
          &m_DownloadManager, SLOT(managedGameChanged(MOBase::IPluginGame const*)));
  connect(&m_PluginList, &PluginList::writePluginsList, &m_PluginListsWriter,
          &DelayedFileWriterBase::write);

  // make directory refresher run in a separate thread
  m_RefresherThread.start();
  m_DirectoryRefresher->moveToThread(&m_RefresherThread);

  connect(&settings.plugins(), &PluginSettings::pluginSettingChanged,
          [this](auto const&... args) {
            m_PluginSettingChanged(args...);
          });
}

OrganizerCore::~OrganizerCore()
{
  m_RefresherThread.exit();
  m_RefresherThread.wait();

  if (m_StructureDeleter.joinable()) {
    m_StructureDeleter.join();
  }

  saveCurrentProfile();

  // profile has to be cleaned up before the modinfo-buffer is cleared
  m_CurrentProfile.reset();

  ModInfo::clear();
  m_ModList.setProfile(nullptr);
  //  NexusInterface::instance()->cleanup();

  delete m_DirectoryStructure;
}

void OrganizerCore::storeSettings()
{
  if (m_CurrentProfile != nullptr) {
    m_Settings.game().setSelectedProfileName(m_CurrentProfile->name());
  }

  m_ExecutablesList.store(m_Settings);

  FileDialogMemory::save(m_Settings);

  const auto result = m_Settings.sync();

  if (result != QSettings::NoError) {
    QString reason;

    if (result == QSettings::AccessError) {
      reason = tr("File is write protected");
    } else if (result == QSettings::FormatError) {
      reason = tr("Invalid file format (probably a bug)");
    } else {
      reason = tr("Unknown error %1").arg(result);
    }

    QMessageBox::critical(
        qApp->activeWindow(), tr("Failed to write settings"),
        tr("An error occurred trying to write back MO2 settings to %1: %2")
            .arg(m_Settings.filename(), reason));
  }
}

void OrganizerCore::updateExecutablesList()
{
  if (m_PluginContainer == nullptr) {
    log::error("can't update executables list now");
    return;
  }

  m_ExecutablesList.load(managedGame(), m_Settings);

  // the entries the me3 and Mod Engine 2 betas added (both are gone) leave
  // the instances that still have them
  for (const QString title : {QStringLiteral("me3 (beta)"), QStringLiteral("Mod Engine 2 (beta)")}) {
    if (m_ExecutablesList.titleExists(title)) {
      m_ExecutablesList.remove(title);
      m_RemovedBetaLaunchers = true;
    }
  }

  // Elden Ring and Nightreign run their mods from MO2 alone: me3, Mod Engine 2
  // (and its launchmod scripts) and Seamless Co-op's launchers have no part
  // in it any more, so their entries leave too, however they were added.
  // Seamless Co-op runs from its MO2 mod with the game.
  if (EldenRingSaveSettings::supports(managedGame())) {
    static const QStringList retired = {
        QStringLiteral("me3.exe"),           QStringLiteral("me3-launcher.exe"),
        QStringLiteral("modengine2_launcher.exe"), QStringLiteral("ersc_launcher.exe"),
        QStringLiteral("launch_elden_ring_seamlesscoop.exe"),
        QStringLiteral("nrsc_launcher.exe")};
    QStringList titles;
    for (const auto& exe : m_ExecutablesList) {
      const QString name = exe.binaryInfo().fileName().toLower();
      const bool script = name.startsWith(QStringLiteral("launchmod")) &&
                          (name.endsWith(QStringLiteral(".bat")) ||
                           name.endsWith(QStringLiteral(".cmd")));
      if (retired.contains(name) || script) {
        titles.push_back(exe.title());
      }
    }
    for (const auto& title : titles) {
      log::info("removing the retired launcher '{}' from the executables", title);
      m_ExecutablesList.remove(title);
      m_RemovedBetaLaunchers = true;
    }
  }
}

void OrganizerCore::updateModInfoFromDisc()
{
  ModInfo::updateFromDisc(m_Settings.paths().mods(), *this,
                          m_Settings.interface().displayForeign(),
                          m_Settings.refreshThreadCount());
}

void OrganizerCore::setUserInterface(IUserInterface* ui)
{
  storeSettings();

  m_UserInterface = ui;

  QWidget* w = nullptr;
  if (m_UserInterface) {
    w = m_UserInterface->mainWindow();
  }

  m_InstallationManager.setParentWidget(w);
  m_Updater.setUserInterface(w);
  m_UILocker.setUserInterface(w);
  m_DownloadManager.setParentWidget(w);

  checkForUpdates();
}

void OrganizerCore::checkForUpdates()
{
  // this currently wouldn't work reliably if the ui isn't initialized yet to
  // display the result
  if (m_UserInterface != nullptr) {
    m_Updater.testForUpdate(m_Settings);
  }
}

void OrganizerCore::connectPlugins(PluginContainer* container)
{
  m_PluginContainer = container;
  m_Updater.setPluginContainer(m_PluginContainer);
  m_InstallationManager.setPluginContainer(m_PluginContainer);
  m_InstallationManager.setGameAccess(
      [this]() -> const IPluginGame* {
        return managedGame();
      },
      [this]() -> std::shared_ptr<const ScriptExtender> {
        return gameFeatures().gameFeature<ScriptExtender>();
      });
  m_DownloadManager.setPluginContainer(m_PluginContainer);
  m_ModList.setPluginContainer(m_PluginContainer);

  if (!m_GameName.isEmpty()) {
    m_GamePlugin = m_PluginContainer->game(m_GameName);
    emit managedGameChanged(m_GamePlugin);
  }

  connect(m_PluginContainer, &PluginContainer::pluginEnabled, [&](IPlugin* plugin) {
    m_PluginEnabled(plugin);
  });
  connect(m_PluginContainer, &PluginContainer::pluginDisabled, [&](IPlugin* plugin) {
    m_PluginDisabled(plugin);
  });

  connect(&m_PluginContainer->gameFeatures(), &GameFeatures::modDataContentUpdated,
          [this](ModDataContent const* contentFeature) {
            if (contentFeature) {
              m_Contents = ModDataContentHolder(contentFeature->getAllContents());
            } else {
              m_Contents = ModDataContentHolder();
            }
          });
}

void OrganizerCore::setManagedGame(MOBase::IPluginGame* game)
{
  m_GameName   = game->gameName();
  m_GamePlugin = game;
  qApp->setProperty("managed_game", QVariant::fromValue(m_GamePlugin));
  emit managedGameChanged(m_GamePlugin);
}

Settings& OrganizerCore::settings()
{
  return m_Settings;
}

bool OrganizerCore::nexusApi(bool retry)
{
  auto* accessManager = NexusInterface::instance().getAccessManager();

  if ((accessManager->validateAttempted() || accessManager->validated()) && !retry) {
    // previous attempt, maybe even successful
    return false;
  } else {
    QString apiKey;
    if (GlobalSettings::nexusApiKey(apiKey)) {
      // credentials stored or user entered them manually
      log::debug("attempt to verify nexus api key");
      accessManager->apiCheck(apiKey);
      return true;
    } else {
      // no credentials stored and user didn't enter them
      accessManager->refuseValidation();
      return false;
    }
  }
}

void OrganizerCore::downloadRequestedNXM(const QString& url)
{
  // MO2 Revamped: without the link's key and expiry
  log::debug("download requested: {}", url.section(QLatin1Char('?'), 0, 0));
  if (nexusApi()) {
    m_PendingDownloads.append(url);
  } else {
    m_DownloadManager.addNXMDownload(url);
  }
}

void OrganizerCore::profileCreated(MOBase::IProfile* profile)
{
  m_ProfileCreated(profile);
}

void OrganizerCore::profileRenamed(MOBase::IProfile* profile, QString const& oldName,
                                   QString const& newName)
{
  m_ProfileRenamed(profile, oldName, newName);
}

void OrganizerCore::profileRemoved(QString const& profileName)
{
  m_ProfileRemoved(profileName);
}

// MO2 Revamped: a mod named like one of MO2's own origins ("data", a secondary data
// folder, "Overwrite") is left out of the file structure; looked up by its name,
// the game's origin (or Overwrite's) would be found and changed instead
static bool ownsOriginName(const ModInfo::Ptr& mod)
{
  return mod->isOverwrite() || !ModInfo::isReservedModName(mod->internalName());
}

void OrganizerCore::removeOrigin(const QString& name)
{
  // a mod removed before the structure knew it (or after a refresh dropped it);
  // only regular mods are removed, so a reserved name is never their own origin
  if (ModInfo::isReservedModName(name) ||
      !m_DirectoryStructure->originExists(ToWString(name))) {
    refreshLists();
    return;
  }

  FilesOrigin& origin = m_DirectoryStructure->getOriginByName(ToWString(name));
  origin.enable(false);
  refreshLists();
}

void OrganizerCore::downloadSpeed(const QString& serverName, int bytesPerSecond)
{
  m_Settings.network().setDownloadSpeed(serverName, bytesPerSecond);
}

InstallationManager* OrganizerCore::installationManager()
{
  return &m_InstallationManager;
}

bool OrganizerCore::createDirectory(const QString& path)
{
  if (!QDir(path).exists() && !QDir().mkpath(path)) {
    QMessageBox::critical(nullptr, QObject::tr("Error"),
                          QObject::tr("Failed to create \"%1\". Your user "
                                      "account probably lacks permission.")
                              .arg(QDir::toNativeSeparators(path)));
    return false;
  } else {
    return true;
  }
}

bool OrganizerCore::checkPathSymlinks()
{
  const bool hasSymlink = (QFileInfo(m_Settings.paths().profiles()).isSymLink() ||
                           QFileInfo(m_Settings.paths().mods()).isSymLink() ||
                           QFileInfo(m_Settings.paths().overwrite()).isSymLink());

  if (hasSymlink) {
    log::warn(
        "{}",
        QObject::tr(
            "One of the configured MO2 directories (profiles, mods, or overwrite) "
            "is on a path containing a symbolic (or other) link. This is likely to "
            "be incompatible with MO2's virtual filesystem."));

    return false;
  }

  return true;
}

bool OrganizerCore::bootstrap()
{
  const auto dirs = {m_Settings.paths().profiles(), m_Settings.paths().mods(),
                     m_Settings.paths().downloads(), m_Settings.paths().overwrite(),
                     QString::fromStdWString(getGlobalCoreDumpPath())};

  for (auto&& dir : dirs) {
    if (!createDirectory(dir)) {
      return false;
    }
  }

  if (!checkPathSymlinks()) {
    return false;
  }

  if (!cycleDiagnostics()) {
    return false;
  }

  // log if there are any dmp files
  const auto hasCrashDumps = !QDir(QString::fromStdWString(getGlobalCoreDumpPath()))
                                  .entryList({"*.dmp"}, QDir::Files)
                                  .empty();

  if (hasCrashDumps) {
    log::debug("there are crash dumps in '{}'",
               QString::fromStdWString(getGlobalCoreDumpPath()));
  }

  return true;
}

void OrganizerCore::createDefaultProfile()
{
  QString profilesPath = settings().paths().profiles();

  if (InstanceManager::singleton().isEldenRingOnlyPortableMode()) {
    QString selectedProfile =
        QString::fromStdWString(AppConfig::defaultProfileName());
    if (const auto savedProfile = settings().game().selectedProfileName()) {
      if (MOBase::validFileName(*savedProfile)) {
        selectedProfile = *savedProfile;
      }
    }

    const QString selectedProfilePath =
        QDir(profilesPath).filePath(selectedProfile);
    if (!QFileInfo(selectedProfilePath).isDir()) {
      Profile newProf(selectedProfile, managedGame(), gameFeatures(), false);
      m_ProfileCreated(&newProf);
    }
    return;
  }

  if (QDir(profilesPath).entryList(QDir::AllDirs | QDir::NoDotAndDotDot).size() == 0) {
    Profile newProf(QString::fromStdWString(AppConfig::defaultProfileName()),
                    managedGame(), gameFeatures(), false);

    m_ProfileCreated(&newProf);
  }
}

void OrganizerCore::prepareVFS()
{
  m_USVFS.updateMapping(fileMapping(m_CurrentProfile->name(), QString()));
}

void OrganizerCore::updateVFSParams(log::Levels logLevel,
                                    env::CoreDumpTypes coreDumpType,
                                    const QString& crashDumpsPath,
                                    std::chrono::seconds spawnDelay,
                                    QString executableBlacklist,
                                    const QStringList& skipFileSuffixes,
                                    const QStringList& skipDirectories)
{
  setGlobalCoreDumpType(coreDumpType);

  m_USVFS.updateParams(logLevel, coreDumpType, crashDumpsPath, spawnDelay,
                       executableBlacklist, skipFileSuffixes, skipDirectories);
}

void OrganizerCore::setLogLevel(log::Levels level)
{
  m_Settings.diagnostics().setLogLevel(level);

  updateVFSParams(
      m_Settings.diagnostics().logLevel(), m_Settings.diagnostics().coreDumpType(),
      QString::fromStdWString(getGlobalCoreDumpPath()),
      m_Settings.diagnostics().spawnDelay(), m_Settings.executablesBlacklist(),
      m_Settings.skipFileSuffixes(), m_Settings.skipDirectories());

  log::getDefault().setLevel(m_Settings.diagnostics().logLevel());
}

bool OrganizerCore::cycleDiagnostics()
{
  const auto maxDumps = settings().diagnostics().maxCoreDumps();
  const auto path     = QString::fromStdWString(getGlobalCoreDumpPath());

  if (maxDumps > 0) {
    const QFileInfoList dumps =
        QDir(path).entryInfoList({"*.dmp"}, QDir::Files,
                                 QDir::Time | QDir::Reversed);
    const qsizetype excess = dumps.size() > maxDumps ? dumps.size() - maxDumps : 0;
    for (qsizetype i = 0; i < excess; ++i) {
      QFile oldDump(dumps.at(i).absoluteFilePath());
      if (!oldDump.remove()) {
        log::warn("failed to remove old crash dump '{}': {}",
                  oldDump.fileName(), oldDump.errorString());
      }
    }
  }

  return true;
}

env::CoreDumpTypes OrganizerCore::getGlobalCoreDumpType()
{
  return g_coreDumpType;
}

void OrganizerCore::setGlobalCoreDumpType(env::CoreDumpTypes type)
{
  g_coreDumpType = type;
}

std::wstring OrganizerCore::getGlobalCoreDumpPath()
{
  if (qApp) {
    const auto dp = qApp->property("dataPath");
    if (!dp.isNull()) {
      return dp.toString().toStdWString() + L"/" + AppConfig::dumpsDir();
    }
  }

  return {};
}

void OrganizerCore::setCurrentProfile(const QString& profileName, bool reload)
{
  if (!reload && (m_CurrentProfile != nullptr) &&
      (profileName == m_CurrentProfile->name())) {
    return;
  }

  log::debug("selecting profile '{}'", profileName);

  QDir profileBaseDir(settings().paths().profiles());

  const auto subdirs = profileBaseDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);

  QString profileDir;

  // the profile name may not have the correct case, which breaks other parts
  // of the ui like the profile combobox, which walks directories on its own
  //
  // find the real name with the correct case by walking the directories
  for (auto&& dirName : subdirs) {
    if (QString::compare(dirName, profileName, Qt::CaseInsensitive) == 0) {
      profileDir = profileBaseDir.absoluteFilePath(dirName);
      break;
    }
  }

  if (profileDir.isEmpty()) {
    log::error("profile '{}' does not exist", profileName);

    // selected profile doesn't exist. Ensure there is at least one profile,
    // then pick any one
    createDefaultProfile();

    profileDir = profileBaseDir.absoluteFilePath(
        profileBaseDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot).at(0));

    log::error("picked profile '{}' instead", QDir(profileDir).dirName());

    reportError(tr("The selected profile '%1' does not exist. The profile '%2' will be "
                   "used instead")
                    .arg(profileName)
                    .arg(QDir(profileDir).dirName()));
  }

  // a change still waiting to be written goes to disk first: reloading the
  // same profile reads its modlist.txt, and the old object would otherwise
  // write it back after the new one has read the stale file
  if (m_CurrentProfile != nullptr) {
    m_CurrentProfile->writeModlistNow(true);
  }

  // first: one that can't be read throws here, and the current one stays
  auto newProfile =
      std::make_unique<Profile>(QDir(profileDir), managedGame(), gameFeatures());

  // Keep the old profile to emit signal-changed:
  auto oldProfile = std::move(m_CurrentProfile);

  m_CurrentProfile = std::move(newProfile);
  // MO2 Revamped: when it was last chosen, for the Profiles window (the mod
  // list's date changes whenever any profile's list is written)
  m_CurrentProfile->storeSetting(QString(), QStringLiteral("LastUsed"),
                                 QDateTime::currentDateTime());

  m_ModList.setProfile(m_CurrentProfile.get());

  if (m_CurrentProfile->invalidationActive(nullptr)) {
    m_CurrentProfile->activateInvalidation();
  } else {
    m_CurrentProfile->deactivateInvalidation();
  }

  m_Settings.game().setSelectedProfileName(m_CurrentProfile->name());

  connect(m_CurrentProfile.get(), qOverload<uint>(&Profile::modStatusChanged),
          [this](auto&& index) {
            modStatusChanged(index);
          });
  connect(m_CurrentProfile.get(), qOverload<QList<uint>>(&Profile::modStatusChanged),
          [this](auto&& indexes) {
            modStatusChanged(indexes);
          });

  // a refresh still running is for the previous profile's mods
  if (m_DirectoryUpdate) {
    m_RefreshPending = true;
  }
  refreshDirectoryStructure();

  m_CurrentProfile->debugDump();

  emit profileChanged(oldProfile.get(), m_CurrentProfile.get());
  m_ProfileChanged(oldProfile.get(), m_CurrentProfile.get());
}

MOBase::IModRepositoryBridge* OrganizerCore::createNexusBridge() const
{
  return new NexusBridge(m_PluginContainer);
}

QString OrganizerCore::profileName() const
{
  if (m_CurrentProfile != nullptr) {
    return m_CurrentProfile->name();
  } else {
    return "";
  }
}

QString OrganizerCore::profilePath() const
{
  if (m_CurrentProfile != nullptr) {
    return m_CurrentProfile->absolutePath();
  } else {
    return "";
  }
}

QString OrganizerCore::downloadsPath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().downloads());
}

QString OrganizerCore::overwritePath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().overwrite());
}

QString OrganizerCore::basePath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().base());
}

QString OrganizerCore::modsPath() const
{
  return QDir::fromNativeSeparators(m_Settings.paths().mods());
}

MOBase::VersionInfo OrganizerCore::appVersion() const
{
  return m_Updater.getVersion();
}

MOBase::IPluginGame* OrganizerCore::getGame(const QString& name) const
{
  for (IPluginGame* game : m_PluginContainer->plugins<IPluginGame>()) {
    if (game != nullptr &&
        game->gameShortName().compare(name, Qt::CaseInsensitive) == 0)
      return game;
  }
  return nullptr;
}

MOBase::IModInterface* OrganizerCore::createMod(GuessedValue<QString>& name)
{
  return createMod(name, true);
}

MOBase::IModInterface* OrganizerCore::createMod(GuessedValue<QString>& name,
                                                bool finishReplace)
{
  m_CreatedModReplaced = false;
  m_CreatedModNew      = false;

  // MO2 Revamped: a mod named like the game's own folders (data, a secondary data
  // folder) or Overwrite would have its files mixed with theirs
  if (ModInfo::isReservedModName(name)) {
    reportError(ModInfo::reservedModNameMessage(name));
    return nullptr;
  }

  auto result = m_InstallationManager.testOverwrite(name);
  if (!result) {
    return nullptr;
  }
  // with Merge the folder was there before; with Replace it is put back through
  // finishCreatedMod(); a folder this made itself is new
  const bool folderExisted =
      result.merged() ||
      QDir(QDir::fromNativeSeparators(m_Settings.paths().mods()) + "/" +
           static_cast<const QString&>(name))
          .exists();

  // the name typed for "install as a new mod" in the question above
  if (ModInfo::isReservedModName(name)) {
    m_InstallationManager.removeUnfinishedNewMod();
    reportError(ModInfo::reservedModNameMessage(name));
    return nullptr;
  }

  // runs after the settings file below has been written
  bool created = false;
  ON_BLOCK_EXIT([&]() {
    if (!created) {
      m_InstallationManager.restoreReplacedMod();
    }
  });

  m_InstallationManager.setModsDirectory(m_Settings.paths().mods());

  QString targetDirectory =
      QDir::fromNativeSeparators(m_Settings.paths().mods()).append("/").append(name);

  QSettings settingsFile(targetDirectory + "/meta.ini", QSettings::IniFormat);

  if (!result.merged()) {
    // MO2 Revamped: Replace put the old meta.ini back so the mod keeps its Nexus
    // id, version and categories; only what tied it to its old download goes
    if (!result.replaced()) {
      settingsFile.setValue("modid", 0);
      settingsFile.setValue("version", "");
      settingsFile.setValue("newestVersion", "");
      settingsFile.setValue("category", 0);
    }
    settingsFile.setValue("installationFile", "");

    settingsFile.remove("installedFiles");
    settingsFile.beginWriteArray("installedFiles", 0);
    settingsFile.endArray();
  }
  settingsFile.sync();

  // shouldn't this use the existing mod in case of a merge? also, this does not refresh
  // the indices in the ModInfo structure
  MOBase::IModInterface* mod = ModInfo::createFrom(QDir(targetDirectory), *this).data();
  created = mod != nullptr;
  m_CreatedModReplaced = created && result.replaced();
  m_CreatedModNew      = created && !result.replaced() && !folderExisted;
  // a custom installer fills the mod after this: install() finishes the job when it
  // knows how that went; so does a caller that fills it itself (finishReplace off),
  // through finishCreatedMod()
  if (created && finishReplace && !m_InstallationManager.isRunning()) {
    m_InstallationManager.discardReplacedMod();
  }
  return mod;
}

void OrganizerCore::finishCreatedMod(bool filled)
{
  if (!m_CreatedModReplaced) {
    return;
  }
  m_CreatedModReplaced = false;

  // MO2 Revamped: the old version goes to the Recycle Bin only once the new one
  // has its files; otherwise it is put back in place of the empty new folder
  if (filled) {
    m_InstallationManager.discardReplacedMod();
  } else {
    m_InstallationManager.restoreReplacedMod();
  }
}

bool OrganizerCore::createdModReplaced() const
{
  return m_CreatedModReplaced;
}

bool OrganizerCore::createdModIsNew() const
{
  return m_CreatedModNew;
}

void OrganizerCore::modDataChanged(MOBase::IModInterface*)
{
  refresh(false);
}

QVariant OrganizerCore::pluginSetting(const QString& pluginName,
                                      const QString& key) const
{
  return m_Settings.plugins().setting(pluginName, key);
}

void OrganizerCore::setPluginSetting(const QString& pluginName, const QString& key,
                                     const QVariant& value)
{
  m_Settings.plugins().setSetting(pluginName, key, value);
}

QVariant OrganizerCore::persistent(const QString& pluginName, const QString& key,
                                   const QVariant& def) const
{
  return m_Settings.plugins().persistent(pluginName, key, def);
}

void OrganizerCore::setPersistent(const QString& pluginName, const QString& key,
                                  const QVariant& value, bool sync)
{
  m_Settings.plugins().setPersistent(pluginName, key, value, sync);
}

QString OrganizerCore::pluginDataPath()
{
  return qApp->applicationDirPath() + "/" + ToQString(AppConfig::pluginPath()) +
         "/data";
}

MOBase::IModInterface* OrganizerCore::installMod(const QString& archivePath,
                                                 int priority, bool reinstallation,
                                                 ModInfo::Ptr currentMod,
                                                 const QString& initModName)
{
  return installArchive(archivePath, reinstallation ? -1 : priority, reinstallation,
                        currentMod, initModName)
      .get();
}

std::pair<unsigned int, ModInfo::Ptr>
OrganizerCore::doInstall(const QString& archivePath, GuessedValue<QString> modName,
                         ModInfo::Ptr currentMod, int priority, bool reinstallation)
{
  if (m_CurrentProfile == nullptr) {
    return {-1, nullptr};
  }

  if (m_InstallationManager.isRunning()) {
    QMessageBox::information(qApp->activeWindow(), tr("Installation cancelled"),
                             tr("Another installation is currently in progress."),
                             QMessageBox::Ok);
    return {-1, nullptr};
  }

  bool hasIniTweaks = false;
  m_CurrentProfile->writeModlistNow();
  m_InstallationManager.setModsDirectory(m_Settings.paths().mods());
  m_InstallationManager.notifyInstallationStart(archivePath, reinstallation,
                                                currentMod);
  auto result = m_InstallationManager.install(archivePath, modName, hasIniTweaks);

  if (result && ModInfo::isReservedModName(modName)) {
    // MO2 Revamped: the installers' name fields allow "data", a secondary data
    // folder's name or "Overwrite"; the folder gets another name before the mod
    // list reads it, or its files would count as the game's or Overwrite's
    renameReservedInstall(modName);
  }

  if (result) {
    MessageDialog::showMessage(tr("Installation successful"), qApp->activeWindow());

    // we wait for the directory structure to be ready before notifying the mod list,
    // this prevents issue with third-party plugins, e.g., if the installed mod is
    // activated before the structure is ready
    //
    // we need to fetch modIndex() within the call back because the index is only
    // valid after the call to refresh(), but we do not want to connect after
    // refresh()
    //
    connect(
        this, &OrganizerCore::directoryStructureReady, this,
        [=] {
          const int modIndex = ModInfo::getIndex(modName);
          if (modIndex != UINT_MAX) {
            const auto modInfo = ModInfo::getByIndex(modIndex);
            m_ModList.notifyModInstalled(modInfo.get());
          }
        },
        Qt::SingleShotConnection);

    // the new files aren't in a refresh that is already running
    if (m_DirectoryUpdate) {
      m_RefreshPending = true;
    }
    refresh();

    const auto modIndex  = ModInfo::getIndex(modName);
    ModInfo::Ptr modInfo = nullptr;
    if (modIndex != UINT_MAX) {
      modInfo = ModInfo::getByIndex(modIndex);

      if (priority != -1 && !result.mergedOrReplaced()) {
        m_ModList.changeModPriority(modIndex, priority);
      }

      if (hasIniTweaks && m_UserInterface != nullptr &&
          (QMessageBox::question(qApp->activeWindow(), tr("Configure mod"),
                                 tr("This mod contains ini tweaks. Do you "
                                    "want to configure them now?"),
                                 QMessageBox::Yes | QMessageBox::No) ==
           QMessageBox::Yes)) {
        m_UserInterface->displayModInformation(modInfo, modIndex,
                                               ModInfoTabIDs::IniFiles);
      }

      m_InstallationManager.notifyInstallationEnd(result, modInfo);
    } else {
      // installer plugins still hear that the installation ended: one that
      // set something up for it keeps it until it does (the bundled
      // installers accept no mod)
      m_InstallationManager.notifyInstallationEnd(result, nullptr);
      reportError(tr("mod not found: %1").arg(qUtf8Printable(modName)));
    }
    emit modInstalled(modName);
    return {modIndex, modInfo};
  } else {
    if (result.result() == MOBase::IPluginInstaller::RESULT_CATEGORYREQUESTED) {
      m_InstallationManager.notifyInstallationEnd(result, nullptr);
      CategoriesDialog dialog(qApp->activeWindow());

      if (dialog.exec() == QDialog::Accepted) {
        dialog.commitChanges();
        refresh();
      }
    } else {
      m_InstallationManager.notifyInstallationEnd(result, nullptr);
      if (m_InstallationManager.wasCancelled()) {
        QMessageBox::information(
            qApp->activeWindow(), tr("Extraction cancelled"),
            m_InstallationManager.replacedModRestored()
                ? tr("The installation was cancelled while extracting files. The "
                     "installed mod is back as it was.")
            : m_InstallationManager.newModRemoved()
                ? tr("The installation was cancelled while extracting files. Nothing was installed.")
                : tr("The installation was cancelled while extracting files. "
                     "If this was prior to a FOMOD setup, this warning may be ignored. "
                     "However, if this was during installation, the mod will likely be "
                     "missing "
                     "files."),
            QMessageBox::Ok);
        refresh();
      } else if (m_InstallationManager.replacedModRestored() ||
                 m_InstallationManager.newModRemoved()) {
        refresh();
      }
    }
  }

  return {-1, nullptr};
}

void OrganizerCore::renameReservedInstall(GuessedValue<QString>& modName)
{
  const QDir mods(QDir::fromNativeSeparators(m_Settings.paths().mods()));
  const QString current = modName;
  QString suggested     = current + QStringLiteral(" (mod)");

  for (;;) {
    bool ok = false;
    QString typed = QInputDialog::getText(
        qApp->activeWindow(), tr("Choose another name"),
        tr("The mod was installed as \"%1\", a name Mod Organizer uses for the game's "
           "own files or for Overwrite, so its files wouldn't be used. Enter another "
           "name for it:")
            .arg(current),
        QLineEdit::Normal, suggested, &ok);
    if (!ok) {
      // kept as it is: the mod list says why its files aren't used
      log::warn("mod '{}' kept a reserved name", current);
      return;
    }

    QString fixed = typed.trimmed();
    suggested     = typed;
    if (!fixDirectoryName(fixed) || fixed.isEmpty() ||
        ModInfo::isReservedDeviceName(fixed)) {
      reportError(tr("Windows doesn't allow \"%1\" as a folder name. Choose another name.")
                      .arg(typed));
      continue;
    }
    if (ModInfo::isReservedModName(fixed)) {
      reportError(ModInfo::reservedModNameMessage(fixed));
      continue;
    }
    if (ModInfo::isBackupName(fixed) || ModInfo::isSeparatorName(fixed)) {
      reportError(tr("Names ending in \"_backup\" or \"_separator\" are for backups and "
                     "separators. Choose another name."));
      continue;
    }
    if (mods.exists(fixed)) {
      reportError(tr("A mod with this name already exists"));
      continue;
    }
    if (!QDir(mods).rename(current, fixed)) {
      reportError(tr("\"%1\" couldn't be renamed. Close programs using its files and try "
                     "again.")
                      .arg(current));
      continue;
    }

    modName.update(fixed, GUESS_USER);
    return;
  }
}

ModInfo::Ptr OrganizerCore::installDownload(int index, int priority)
{
  ScopedDisableDirWatcher scopedDirwatcher(&m_DownloadManager);

  try {
    QString fileName        = m_DownloadManager.getFilePath(index);
    QString gameName        = m_DownloadManager.getGameName(index);
    int modID               = m_DownloadManager.getModID(index);
    int fileID              = m_DownloadManager.getFileInfo(index)->fileID;
    ModInfo::Ptr currentMod = nullptr;
    GuessedValue<QString> modName;

    // see if there already are mods with the specified mod id
    if (modID > 0) {
      std::vector<ModInfo::Ptr> modInfo = ModInfo::getByModID(gameName, modID);
      for (auto iter = modInfo.begin(); iter != modInfo.end(); ++iter) {
        std::vector<ModInfo::EFlag> flags = (*iter)->getFlags();
        if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_BACKUP) ==
            flags.end()) {
          modName.update((*iter)->name(), GUESS_PRESET);
          currentMod = *iter;
          (*iter)->saveMeta();
        }
      }
    }

    const auto [modIndex, modInfo] =
        doInstall(fileName, modName, currentMod, priority, false);

    if (modInfo != nullptr) {
      modInfo->addInstalledFile(modID, fileID);
      m_DownloadManager.markInstalled(index);
      if (settings().interface().hideDownloadsAfterInstallation()) {
        m_DownloadManager.removeDownload(index, false);
      }
    }

    return modInfo;
  } catch (const std::exception& e) {
    reportError(QString(e.what()));
  }

  return nullptr;
}

ModInfo::Ptr OrganizerCore::installArchive(const QString& archivePath, int priority,
                                           bool reinstallation, ModInfo::Ptr currentMod,
                                           const QString& initModName)
{
  GuessedValue<QString> modName;
  if (!initModName.isEmpty()) {
    modName.update(initModName, GUESS_USER);
  }
  const auto [modIndex, modInfo] =
      doInstall(archivePath, modName, currentMod, priority, reinstallation);
  if (m_CurrentProfile == nullptr) {
    return nullptr;
  }

  if (modInfo != nullptr) {
    auto dlIdx = m_DownloadManager.indexByName(QFileInfo(archivePath).fileName());
    if (dlIdx != -1) {
      int modId  = m_DownloadManager.getModID(dlIdx);
      int fileId = m_DownloadManager.getFileInfo(dlIdx)->fileID;
      modInfo->addInstalledFile(modId, fileId);
    }
    m_DownloadManager.markInstalled(archivePath);
  }
  return modInfo;
}

QString OrganizerCore::resolvePath(const QString& fileName) const
{
  if (m_DirectoryStructure == nullptr) {
    return QString();
  }
  const FileEntryPtr file =
      m_DirectoryStructure->searchFile(ToWString(fileName), nullptr);
  if (file.get() != nullptr) {
    return ToQString(file->getFullPath());
  } else {
    return QString();
  }
}

QStringList OrganizerCore::listDirectories(const QString& directoryName) const
{
  QStringList result;
  DirectoryEntry* dir = m_DirectoryStructure;
  if (!directoryName.isEmpty())
    dir = dir->findSubDirectoryRecursive(ToWString(directoryName));
  if (dir != nullptr) {
    for (const auto& d : dir->getSubDirectories()) {
      result.append(ToQString(d->getName()));
    }
  }
  return result;
}

QStringList
OrganizerCore::findFiles(const QString& path,
                         const std::function<bool(const QString&)>& filter) const
{
  QStringList result;
  DirectoryEntry* dir = m_DirectoryStructure;
  if (!path.isEmpty() && path != ".")
    dir = dir->findSubDirectoryRecursive(ToWString(path));
  if (dir != nullptr) {
    std::vector<FileEntryPtr> files = dir->getFiles();
    for (FileEntryPtr& file : files) {
      QString fullPath = ToQString(file->getFullPath());
      if (filter(ToQString(file->getName()))) {
        result.append(fullPath);
      }
    }
  }
  return result;
}

QStringList OrganizerCore::getFileOrigins(const QString& fileName) const
{
  QStringList result;
  const FileEntryPtr file =
      m_DirectoryStructure->searchFile(ToWString(fileName), nullptr);

  if (file.get() != nullptr) {
    result.append(
        ToQString(m_DirectoryStructure->getOriginByID(file->getOrigin()).getName()));
    foreach (const auto& i, file->getAlternatives()) {
      result.append(
          ToQString(m_DirectoryStructure->getOriginByID(i.originID()).getName()));
    }
  }
  return result;
}

QList<MOBase::IOrganizer::FileInfo> OrganizerCore::findFileInfos(
    const QString& path,
    const std::function<bool(const MOBase::IOrganizer::FileInfo&)>& filter) const
{
  QList<IOrganizer::FileInfo> result;
  DirectoryEntry* dir = m_DirectoryStructure;
  if (!path.isEmpty() && path != ".")
    dir = dir->findSubDirectoryRecursive(ToWString(path));
  if (dir != nullptr) {
    std::vector<FileEntryPtr> files = dir->getFiles();
    for (FileEntryPtr file : files) {
      IOrganizer::FileInfo info;
      info.filePath    = ToQString(file->getFullPath());
      bool fromArchive = false;
      info.origins.append(ToQString(
          m_DirectoryStructure->getOriginByID(file->getOrigin(fromArchive)).getName()));
      info.archive = fromArchive ? ToQString(file->getArchive().name()) : "";
      for (const auto& idx : file->getAlternatives()) {
        info.origins.append(
            ToQString(m_DirectoryStructure->getOriginByID(idx.originID()).getName()));
      }

      if (filter(info)) {
        result.append(info);
      }
    }
  }
  return result;
}

DownloadManager* OrganizerCore::downloadManager()
{
  return &m_DownloadManager;
}

PluginList* OrganizerCore::pluginList()
{
  return &m_PluginList;
}

ModList* OrganizerCore::modList()
{
  return &m_ModList;
}

bool OrganizerCore::previewFileWithAlternatives(QWidget* parent, QString fileName,
                                                int selectedOrigin)
{
  fileName = QDir::fromNativeSeparators(fileName);

  // what we have is an absolute path to the file in its actual location (for the
  // primary origin) what we want is the path relative to the virtual data directory

  // we need to look in the virtual directory for the file to make sure the info is up
  // to date.

  // check if the file comes from the actual data folder instead of a mod
  QDir gameDirectory   = managedGame()->dataDirectory().absolutePath();
  QString relativePath = gameDirectory.relativeFilePath(fileName);
  QDir dirRelativePath = gameDirectory.relativeFilePath(fileName);

  // if the file is on a different drive the dirRelativePath will actually be an
  // absolute path so we make sure that is not the case
  if (!dirRelativePath.isAbsolute() && !relativePath.startsWith("..")) {
    fileName = relativePath;
  } else {
    // crude: we search for the next slash after the base mod directory to skip
    // everything up to the data-relative directory
    int offset = settings().paths().mods().size() + 1;
    offset     = fileName.indexOf("/", offset);
    fileName   = fileName.mid(offset + 1);
  }

  const FileEntryPtr file =
      directoryStructure()->searchFile(ToWString(fileName), nullptr);

  if (file.get() == nullptr) {
    reportError(tr("file not found: %1").arg(qUtf8Printable(fileName)));
    return false;
  }

  // set up preview dialog
  PreviewDialog preview(fileName, parent);

  auto addFunc = [&](int originId, std::wstring archiveName = L"") {
    FilesOrigin& origin = directoryStructure()->getOriginByID(originId);
    QString filePath =
        QDir::fromNativeSeparators(ToQString(origin.getPath())) + "/" + fileName;
    if (QFile::exists(filePath)) {
      // it's very possible the file doesn't exist, because it's inside an archive. we
      // don't support that
      QWidget* wid = m_PluginContainer->previewGenerator().genPreview(filePath);
      if (wid == nullptr) {
        reportError(tr("failed to generate preview for %1").arg(filePath));
      } else {
        preview.addVariant(ToQString(origin.getName()), wid);
      }
    } else if (archiveName != L"") {
      auto archiveFile = directoryStructure()->searchFile(archiveName);
      if (archiveFile.get() != nullptr) {
        try {
          libbsarch::bs_archive archiveLoader;
          archiveLoader.load_from_disk(archiveFile->getFullPath());
          libbsarch::memory_blob fileData =
              archiveLoader.extract_to_memory(fileName.toStdWString());
          QByteArray convertedFileData((char*)(fileData.data), fileData.size);
          QWidget* wid = m_PluginContainer->previewGenerator().genArchivePreview(
              convertedFileData, filePath);
          if (wid == nullptr) {
            reportError(tr("failed to generate preview for %1").arg(filePath));
          } else {
            preview.addVariant(ToQString(origin.getName()), wid);
          }
        } catch (std::exception& e) {
        }
      }
    }
  };

  if (selectedOrigin == -1) {
    // don't bother with the vector of origins, just add them as they come
    addFunc(file->getOrigin(), file->isFromArchive() ? file->getArchive().name() : L"");
    for (const auto& alt : file->getAlternatives()) {
      addFunc(alt.originID(), alt.isFromArchive() ? alt.archive().name() : L"");
    }
  } else {
    std::vector<int> origins;

    // start with the primary origin
    origins.push_back(file->getOrigin());

    // add other origins, push to front if it's the selected one
    for (const auto& alt : file->getAlternatives()) {
      if (alt.originID() == selectedOrigin) {
        origins.insert(origins.begin(), alt.originID());
      } else {
        origins.push_back(alt.originID());
      }
    }

    // can't be empty; either the primary origin was the selected one, or it
    // was one of the alternatives, which got inserted in front

    if (origins[0] != selectedOrigin) {
      // sanity check, this shouldn't happen unless the caller passed an
      // incorrect id

      log::warn("selected preview origin {} not found in list of alternatives",
                selectedOrigin);
    }

    for (int id : origins) {
      addFunc(id);
    }
  }

  if (preview.numVariants() > 0) {
    preview.exec();
    return true;
  } else {
    QMessageBox::information(parent, tr("Sorry"),
                             tr("Sorry, can't preview anything. This function "
                                "currently does not support extracting from bsas."));

    return false;
  }
}

bool OrganizerCore::previewFile(QWidget* parent, const QString& originName,
                                const QString& path)
{
  if (!QFile::exists(path)) {
    reportError(tr("File '%1' not found.").arg(path));
    return false;
  }

  PreviewDialog preview(path, parent);

  QWidget* wid = m_PluginContainer->previewGenerator().genPreview(path);
  if (wid == nullptr) {
    reportError(tr("Failed to generate preview for %1").arg(path));
    return false;
  }

  preview.addVariant(originName, wid);
  preview.exec();

  return true;
}

boost::signals2::connection OrganizerCore::onAboutToRun(
    const std::function<bool(const QString&, const QDir&, const QString&)>& func)
{
  return m_AboutToRun.connect(func);
}

boost::signals2::connection OrganizerCore::onFinishedRun(
    const std::function<void(const QString&, unsigned int)>& func)
{
  return m_FinishedRun.connect(func);
}

boost::signals2::connection
OrganizerCore::onUserInterfaceInitialized(std::function<void(QMainWindow*)> const& func)
{
  return m_UserInterfaceInitialized.connect(func);
}

boost::signals2::connection
OrganizerCore::onProfileCreated(std::function<void(MOBase::IProfile*)> const& func)
{
  return m_ProfileCreated.connect(func);
}

boost::signals2::connection OrganizerCore::onProfileRenamed(
    std::function<void(MOBase::IProfile*, QString const&, QString const&)> const& func)
{
  return m_ProfileRenamed.connect(func);
}

boost::signals2::connection
OrganizerCore::onProfileRemoved(std::function<void(QString const&)> const& func)
{
  return m_ProfileRemoved.connect(func);
}

boost::signals2::connection
OrganizerCore::onProfileChanged(std::function<void(IProfile*, IProfile*)> const& func)
{
  return m_ProfileChanged.connect(func);
}

boost::signals2::connection OrganizerCore::onPluginSettingChanged(
    std::function<void(QString const&, const QString& key, const QVariant&,
                       const QVariant&)> const& func)
{
  return m_PluginSettingChanged.connect(func);
}

boost::signals2::connection
OrganizerCore::onPluginEnabled(std::function<void(const IPlugin*)> const& func)
{
  return m_PluginEnabled.connect(func);
}

boost::signals2::connection
OrganizerCore::onPluginDisabled(std::function<void(const IPlugin*)> const& func)
{
  return m_PluginDisabled.connect(func);
}

boost::signals2::connection
OrganizerCore::onNextRefresh(std::function<void()> const& func,
                             RefreshCallbackGroup group, RefreshCallbackMode mode)
{
  if (m_DirectoryUpdate || mode == RefreshCallbackMode::FORCE_WAIT_FOR_REFRESH) {
    return m_OnNextRefreshCallbacks.connect(static_cast<int>(group), func);
  } else {
    func();
    return {};
  }
}

void OrganizerCore::refresh(bool saveChanges)
{
  // don't lose changes!
  if (saveChanges) {
    m_CurrentProfile->writeModlistNow(true);
  }

  updateModInfoFromDisc();
  m_CurrentProfile->refreshModStatus();

  m_ModList.notifyChange(-1);

  refreshDirectoryStructure();

  emit refreshTriggered();
}

void OrganizerCore::refreshESPList(bool force)
{
  onNextRefresh(
      [this, force] {
        if (m_LastRefreshFailed) {
          log::warn("not refreshing the plugin list after a failed directory update");
          return;
        }

        TimeThis tt("OrganizerCore::refreshESPList()");

        m_CurrentProfile->writeModlist();

        // clear list
        try {
          m_PluginList.refresh(m_CurrentProfile->name(), *m_DirectoryStructure,
                               m_CurrentProfile->getLockedOrderFileName(), force);
        } catch (const std::exception& e) {
          reportError(tr("Failed to refresh list of esps: %1").arg(e.what()));
        }
      },
      RefreshCallbackGroup::CORE, RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

void OrganizerCore::refreshBSAList()
{
  TimeThis tt("OrganizerCore::refreshBSAList()");

  auto archives = gameFeatures().gameFeature<DataArchives>();

  if (archives != nullptr) {
    m_ArchivesInit = false;

    // default archives are the ones enabled outside MO. if the list can't be
    // found (which might
    // happen if ini files are missing) use hard-coded defaults (preferrably the
    // same the game would use)
    m_DefaultArchives = archives->archives(m_CurrentProfile.get());
    if (m_DefaultArchives.length() == 0) {
      m_DefaultArchives = archives->vanillaArchives();
    }

    m_ActiveArchives.clear();

    auto iter        = enabledArchives();
    m_ActiveArchives = toStringList(iter.begin(), iter.end());
    if (m_ActiveArchives.isEmpty()) {
      m_ActiveArchives = m_DefaultArchives;
    }

    if (m_UserInterface != nullptr) {
      m_UserInterface->updateBSAList(m_DefaultArchives, m_ActiveArchives);
      m_UserInterface->archivesWriter().write();
    }

    m_ArchivesInit = true;
  }
}

void OrganizerCore::refreshLists()
{
  if ((m_CurrentProfile != nullptr) && m_DirectoryStructure->isPopulated()) {
    refreshESPList(true);
    refreshBSAList();
  }  // no point in refreshing lists if no files have been added to the directory
     // tree
}

void OrganizerCore::updateModActiveState(int index, bool active)
{
  QList<unsigned int> modsToUpdate;
  modsToUpdate.append(index);
  updateModsActiveState(modsToUpdate, active);
}

void OrganizerCore::updateModsActiveState(const QList<unsigned int>& modIndices,
                                          bool active)
{
  int enabled = 0;
  for (auto index : modIndices) {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(index);
    QDir dir(modInfo->absolutePath());
    for (const QString& esm : dir.entryList(QStringList() << "*.esm", QDir::Files)) {
      const FileEntryPtr file = m_DirectoryStructure->findFile(ToWString(esm));
      if (file.get() == nullptr) {
        log::warn("failed to activate {}", esm);
        continue;
      }

      if (active != m_PluginList.isEnabled(esm) && file->getAlternatives().empty()) {
        m_PluginList.blockSignals(true);
        m_PluginList.enableESP(esm, active);
        m_PluginList.blockSignals(false);
      }
    }

    for (const QString& esl : dir.entryList(QStringList() << "*.esl", QDir::Files)) {
      const FileEntryPtr file = m_DirectoryStructure->findFile(ToWString(esl));
      if (file.get() == nullptr) {
        log::warn("failed to activate {}", esl);
        continue;
      }

      if (active != m_PluginList.isEnabled(esl) && file->getAlternatives().empty()) {
        m_PluginList.blockSignals(true);
        m_PluginList.enableESP(esl, active);
        m_PluginList.blockSignals(false);
        ++enabled;
      }
    }
    QStringList esps = dir.entryList(QStringList() << "*.esp", QDir::Files);
    for (const QString& esp : esps) {
      const FileEntryPtr file = m_DirectoryStructure->findFile(ToWString(esp));
      if (file.get() == nullptr) {
        log::warn("failed to activate {}", esp);
        continue;
      }

      if (active != m_PluginList.isEnabled(esp) && file->getAlternatives().empty()) {
        m_PluginList.blockSignals(true);
        m_PluginList.enableESP(esp, active);
        m_PluginList.blockSignals(false);
        ++enabled;
      }
    }
  }
  if (active && (enabled > 1)) {
    MessageDialog::showMessage(
        tr("Multiple esps/esls activated, please check that they don't conflict."),
        qApp->activeWindow());
  }
  m_PluginList.refreshLoadOrder();
  // immediately save affected lists
  m_PluginListsWriter.writeImmediately(false);
}

void OrganizerCore::updateModInDirectoryStructure(unsigned int index,
                                                  ModInfo::Ptr modInfo)
{
  QMap<unsigned int, ModInfo::Ptr> allModInfo;
  allModInfo[index] = modInfo;
  updateModsInDirectoryStructure(allModInfo);
}

void OrganizerCore::updateModsInDirectoryStructure(
    QMap<unsigned int, ModInfo::Ptr> modInfo)
{
  std::vector<DirectoryRefresher::EntryInfo> entries;

  for (auto idx : modInfo.keys()) {
    // MO2 Revamped: as in a full refresh, a mod named like the game's folders or
    // Overwrite stays out of the structure
    if (m_DirectoryRefresher->isReservedModEntry(modInfo[idx]->internalName(),
                                                 modInfo[idx]->absolutePath())) {
      continue;
    }
    entries.push_back({modInfo[idx]->internalName(),
                       modInfo[idx]->absolutePath(),
                       modInfo[idx]->stealFiles(),
                       {},
                       m_CurrentProfile->getModPriority(idx)});
  }

  m_DirectoryRefresher->addMultipleModsFilesToStructure(m_DirectoryStructure, entries);

  DirectoryRefresher::cleanStructure(m_DirectoryStructure);
  // need to refresh plugin list now so we can activate esps
  refreshESPList(true);
  // activate all esps of the specified mod so the bsas get activated along with
  // it
  // not after a failed refresh: the plugin list wasn't rebuilt above, and each
  // plugin it doesn't know would be reported as an error
  if (!m_LastRefreshFailed) {
    m_PluginList.blockSignals(true);
    updateModsActiveState(modInfo.keys(), true);
    m_PluginList.blockSignals(false);
  }
  // now we need to refresh the bsa list and save it so there is no confusion
  // about what archives are available and active
  refreshBSAList();
  if (m_UserInterface != nullptr) {
    m_UserInterface->archivesWriter().writeImmediately(false);
  }

  std::vector<QString> archives = enabledArchives();
  m_DirectoryRefresher->setMods(m_CurrentProfile->getActiveMods(),
                                std::set<QString>(archives.begin(), archives.end()));

  // finally also add files from bsas to the directory structure
  for (auto idx : modInfo.keys()) {
    if (m_DirectoryRefresher->isReservedModEntry(modInfo[idx]->internalName(),
                                                 modInfo[idx]->absolutePath())) {
      continue;
    }
    m_DirectoryRefresher->addModBSAToStructure(
        m_DirectoryStructure, modInfo[idx]->internalName(),
        m_CurrentProfile->getModPriority(idx), modInfo[idx]->absolutePath(),
        modInfo[idx]->archives());
  }
}

void OrganizerCore::loggedInAction(QWidget* parent, std::function<void()> f)
{
  if (NexusInterface::instance().getAccessManager()->validated()) {
    f();
  } else if (!m_Settings.network().offlineMode()) {
    QString apiKey;
    if (GlobalSettings::nexusApiKey(apiKey)) {
      doAfterLogin([f] {
        f();
      });
      NexusInterface::instance().getAccessManager()->apiCheck(apiKey);
    } else {
      MessageDialog::showMessage(tr("You need to be logged in with Nexus"), parent);
    }
  }
}

PluginContainer& OrganizerCore::pluginContainer() const
{
  return *m_PluginContainer;
}

GameFeatures& OrganizerCore::gameFeatures() const
{
  return pluginContainer().gameFeatures();
}

IPluginGame const* OrganizerCore::managedGame() const
{
  return m_GamePlugin;
}

IOrganizer const* OrganizerCore::managedGameOrganizer() const
{
  return m_PluginContainer->requirements(m_GamePlugin).m_Organizer;
}

std::vector<QString> OrganizerCore::enabledArchives()
{
  std::vector<QString> result;
  if (settings().archiveParsing()) {
    QFile archiveFile(m_CurrentProfile->getArchivesFileName());
    if (archiveFile.open(QIODevice::ReadOnly)) {
      while (!archiveFile.atEnd()) {
        result.push_back(QString::fromUtf8(archiveFile.readLine()).trimmed());
      }
      archiveFile.close();
    }
  }
  return result;
}

void OrganizerCore::refreshDirectoryStructure()
{
  if (m_DirectoryUpdate) {
    log::debug("can't refresh, already in progress");
    return;
  }

  log::debug("refreshing structure");
  m_DirectoryUpdate = true;

  try {
    m_CurrentProfile->writeModlistNow(true);
    const auto activeModList = m_CurrentProfile->getActiveMods();
    const auto archives      = enabledArchives();

    m_DirectoryRefresher->setMods(activeModList,
                                  std::set<QString>(archives.begin(), archives.end()));
  } catch (...) {
    // no refresh starts: left set, every later refresh would be refused and
    // waitForDirectoryRefresh() (before each launch) would never return
    m_DirectoryUpdate = false;
    throw;
  }

  // runs refresh() in a thread
  QTimer::singleShot(0, m_DirectoryRefresher.get(), &DirectoryRefresher::refresh);
}

void OrganizerCore::waitForDirectoryRefresh()
{
  // a loop: another refresh can follow the one that just ended
  while (m_DirectoryUpdate) {
    log::debug("waiting until the directory refresh finishes");
    QEventLoop loop;
    connect(this, &OrganizerCore::directoryStructureReady, &loop, &QEventLoop::quit,
            Qt::ConnectionType::QueuedConnection);

    // negative when MO2 is exiting: no event runs any more, so the refresh
    // can't be seen to end and this would spin
    if (loop.exec() < 0) {
      break;
    }
  }
}

void OrganizerCore::onDirectoryRefreshFailed()
{
  // the previous structure stays; without this MO2 would believe the refresh
  // never ended, and every launch would wait for it
  log::debug("the directory refresh failed, keeping the previous structure");
  m_LastRefreshFailed = true;
  m_DirectoryUpdate   = false;
  m_RefreshPending    = false;
  emit directoryStructureReady();
}

void OrganizerCore::onDirectoryRefreshed()
{
  log::debug("directory refreshed, finishing up");
  TimeThis tt("OrganizerCore::onDirectoryRefreshed()");

  DirectoryEntry* newStructure = m_DirectoryRefresher->stealDirectoryStructure();
  Q_ASSERT(newStructure != m_DirectoryStructure);

  if (newStructure == nullptr) {
    // TODO: don't know why this happens, this slot seems to get called twice
    // with only one emit
    return;
  }

  std::swap(m_DirectoryStructure, newStructure);

  // trees plugins still hold point into the old structure, deleted below
  m_VirtualFileTreeValid->store(false);
  m_VirtualFileTreeValid = std::make_shared<std::atomic<bool>>(true);
  m_VirtualFileTree.invalidate();

  if (m_StructureDeleter.joinable()) {
    m_StructureDeleter.join();
  }

  m_StructureDeleter = MOShared::startSafeThread([=] {
    log::debug("structure deleter thread start");
    delete newStructure;
    log::debug("structure deleter thread done");
  });

  log::debug("clearing caches");
  for (int i = 0; i < m_ModList.rowCount(); ++i) {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(i);
    modInfo->clearCaches();
  }

  // needs to be done before post refresh tasks
  m_DirectoryUpdate = false;

  log::debug("running post refresh tasks");
  // run from a copy: one registered by these callbacks waits for the next
  // refresh, where clearing the list afterwards would have dropped it
  boost::signals2::signal<void()> callbacks;
  callbacks.swap(m_OnNextRefreshCallbacks);
  callbacks();

  // only now: a save queued while the last refresh had failed would write a
  // plugin list that may be another profile's. refreshLists() rebuilds it
  m_LastRefreshFailed = false;

  if (m_CurrentProfile != nullptr) {
    log::debug("refreshing lists");
    refreshLists();
  }

  emit directoryStructureReady();

  log::debug("refresh done");

  // not when something above already started one: it may not include the change
  // that set this, and its own end looks at this again
  if (m_RefreshPending && !m_DirectoryUpdate) {
    m_RefreshPending = false;
    log::debug("mods changed during the refresh, refreshing again");
    refreshDirectoryStructure();
  }
}

void OrganizerCore::clearCaches(std::vector<unsigned int> const& indices) const
{
  const auto insert = [](auto& dest, const auto& from) {
    dest.insert(from.begin(), from.end());
  };
  std::set<unsigned int> allIndices;
  for (const auto index : indices) {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(index);

    if (m_CurrentProfile->modEnabled(index)) {
      // if the mod is enabled, we need to first clear its cache so that
      // getModOverwrite(), ..., returns the newly conflicting mods (in case
      // the mod just got enabled)
      modInfo->clearCaches();
      insert(allIndices, modInfo->getModOverwrite());
      insert(allIndices, modInfo->getModOverwritten());
      insert(allIndices, modInfo->getModArchiveOverwrite());
      insert(allIndices, modInfo->getModArchiveOverwritten());
      insert(allIndices, modInfo->getModArchiveLooseOverwrite());
      insert(allIndices, modInfo->getModArchiveLooseOverwritten());
    } else {
      // if the mod is disabled, we need to first fetch the conflicting
      // mods, and then clear the cache
      insert(allIndices, modInfo->getModOverwrite());
      insert(allIndices, modInfo->getModOverwritten());
      insert(allIndices, modInfo->getModArchiveOverwrite());
      insert(allIndices, modInfo->getModArchiveOverwritten());
      insert(allIndices, modInfo->getModArchiveLooseOverwrite());
      insert(allIndices, modInfo->getModArchiveLooseOverwritten());
      modInfo->clearCaches();
    }
  }

  for (auto& index : allIndices) {
    ModInfo::getByIndex(index)->clearCaches();
  }
}

void OrganizerCore::modPrioritiesChanged(const QModelIndexList& indices)
{
  // the structure in use now is replaced when the refresh ends
  if (m_DirectoryUpdate) {
    m_RefreshPending = true;
  }

  for (unsigned int i = 0; i < currentProfile()->numMods(); ++i) {
    int priority = currentProfile()->getModPriority(i);
    if (currentProfile()->modEnabled(i)) {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(i);
      const std::wstring origin = MOBase::ToWString(modInfo->internalName());
      // not there yet when it was enabled while a refresh was running: the
      // lookup would throw and skip the rest
      if (!ownsOriginName(modInfo) || !directoryStructure()->originExists(origin)) {
        continue;
      }
      // priorities in the directory structure are one higher because data is 0
      directoryStructure()->getOriginByName(origin).setPriority(priority + 1);
    }
  }
  refreshBSAList();
  currentProfile()->writeModlist();
  directoryStructure()->getFileRegister()->sortOrigins();

  std::vector<unsigned int> vindices;

  for (auto& idx : indices) {
    vindices.push_back(idx.data(ModList::IndexRole).toInt());
  }

  clearCaches(vindices);
}

void OrganizerCore::modStatusChanged(unsigned int index)
{
  // the structure in use now is replaced when the refresh ends
  if (m_DirectoryUpdate) {
    m_RefreshPending = true;
  }

  try {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(index);
    if (m_CurrentProfile->modEnabled(index)) {
      updateModInDirectoryStructure(index, modInfo);
    } else {
      updateModActiveState(index, false);
      if (ownsOriginName(modInfo) &&
          m_DirectoryStructure->originExists(ToWString(modInfo->internalName()))) {
        FilesOrigin& origin =
            m_DirectoryStructure->getOriginByName(ToWString(modInfo->internalName()));
        origin.enable(false);
      }
      if (m_UserInterface != nullptr) {
        m_UserInterface->archivesWriter().write();
      }
    }

    for (unsigned int i = 0; i < m_CurrentProfile->numMods(); ++i) {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(i);
      int priority         = m_CurrentProfile->getModPriority(i);
      if (ownsOriginName(modInfo) &&
          m_DirectoryStructure->originExists(ToWString(modInfo->internalName()))) {
        // priorities in the directory structure are one higher because data is
        // 0
        m_DirectoryStructure->getOriginByName(ToWString(modInfo->internalName()))
            .setPriority(priority + 1);
      }
    }
    m_DirectoryStructure->getFileRegister()->sortOrigins();

    refreshLists();
    clearCaches({index});
    m_ModList.notifyModStateChanged({index});

  } catch (const std::exception& e) {
    reportError(tr("failed to update mod list: %1").arg(e.what()));
  }
}

void OrganizerCore::modStatusChanged(QList<unsigned int> index)
{
  // the structure in use now is replaced when the refresh ends
  if (m_DirectoryUpdate) {
    m_RefreshPending = true;
  }

  try {
    QMap<unsigned int, ModInfo::Ptr> modsToEnable;
    QMap<unsigned int, ModInfo::Ptr> modsToDisable;
    std::vector<unsigned int> vindices;
    for (auto idx : index) {
      if (m_CurrentProfile->modEnabled(idx)) {
        modsToEnable[idx] = ModInfo::getByIndex(idx);
      } else {
        modsToDisable[idx] = ModInfo::getByIndex(idx);
      }
      vindices.push_back(idx);
    }
    if (!modsToEnable.isEmpty()) {
      updateModsInDirectoryStructure(modsToEnable);
    }
    if (!modsToDisable.isEmpty()) {
      updateModsActiveState(modsToDisable.keys(), false);
      for (auto idx : modsToDisable.keys()) {
        if (ownsOriginName(modsToDisable[idx]) &&
            m_DirectoryStructure->originExists(ToWString(modsToDisable[idx]->internalName()))) {
          FilesOrigin& origin = m_DirectoryStructure->getOriginByName(
              ToWString(modsToDisable[idx]->internalName()));
          origin.enable(false);
        }
      }
      if (m_UserInterface != nullptr) {
        m_UserInterface->archivesWriter().write();
      }
    }

    for (unsigned int i = 0; i < m_CurrentProfile->numMods(); ++i) {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(i);
      int priority         = m_CurrentProfile->getModPriority(i);
      if (ownsOriginName(modInfo) &&
          m_DirectoryStructure->originExists(ToWString(modInfo->internalName()))) {
        // priorities in the directory structure are one higher because data is
        // 0
        m_DirectoryStructure->getOriginByName(ToWString(modInfo->internalName()))
            .setPriority(priority + 1);
      }
    }
    m_DirectoryStructure->getFileRegister()->sortOrigins();

    refreshLists();
    clearCaches(vindices);
    m_ModList.notifyModStateChanged(index);

  } catch (const std::exception& e) {
    reportError(tr("failed to update mod list: %1").arg(e.what()));
  }
}

void OrganizerCore::loginSuccessful(bool necessary)
{
  if (necessary) {
    MessageDialog::showMessage(tr("Signed in to Nexus Mods"), qApp->activeWindow());
  }
  for (QString url : m_PendingDownloads) {
    downloadRequestedNXM(url);
  }
  m_PendingDownloads.clear();
  for (auto task : m_PostLoginTasks) {
    task();
  }

  m_PostLoginTasks.clear();
  NexusInterface::instance().loginCompleted();
}

void OrganizerCore::loginFailed(const QString& message)
{
  qCritical().nospace().noquote() << "Nexus API validation failed: " << message;

  // a refused key: asking to try the same key again would only loop
  const bool refused = message.startsWith(nexusKeyRefusedMessage());
  if (refused) {
    QMessageBox::warning(qApp->activeWindow(), tr("Nexus sign-in"), message + ".");
    for (QString url : m_PendingDownloads) {
      downloadRequestedNXM(url);
    }
    m_PendingDownloads.clear();
    m_PostLoginTasks.clear();
    NexusInterface::instance().loginCompleted();
    return;
  }

  if (QMessageBox::question(qApp->activeWindow(), tr("Login failed"),
                            tr("Login failed, try again?")) == QMessageBox::Yes) {
    if (nexusApi(true)) {
      return;
    }
  }

  if (!m_PendingDownloads.isEmpty()) {
    MessageDialog::showMessage(
        tr("login failed: %1. Download will not be associated with an account")
            .arg(message),
        qApp->activeWindow());
    for (QString url : m_PendingDownloads) {
      downloadRequestedNXM(url);
    }
    m_PendingDownloads.clear();
  } else {
    MessageDialog::showMessage(tr("login failed: %1").arg(message),
                               qApp->activeWindow());
    m_PostLoginTasks.clear();
  }
  NexusInterface::instance().loginCompleted();
}

void OrganizerCore::syncOverwrite()
{
  // the dialog keeps the structure it is given: one a refresh in progress
  // replaces (and frees) right after would be read when it applies
  waitForDirectoryRefresh();

  ModInfo::Ptr modInfo = ModInfo::getOverwrite();
  SyncOverwriteDialog syncDialog(modInfo->absolutePath(), m_DirectoryStructure,
                                 qApp->activeWindow());
  if (syncDialog.exec() == QDialog::Accepted) {
    syncDialog.apply(QDir::fromNativeSeparators(m_Settings.paths().mods()));
    modInfo->diskContentModified();
    if (m_DirectoryUpdate) {
      m_RefreshPending = true;
    }
    refreshDirectoryStructure();
  }
}

QString OrganizerCore::oldMO1HookDll() const
{
  if (auto extender = gameFeatures().gameFeature<ScriptExtender>()) {
    QString hookdll =
        QDir::toNativeSeparators(managedGame()->dataDirectory().absoluteFilePath(
            extender->PluginPath() + "/hook.dll"));
    if (QFile(hookdll).exists())
      return hookdll;
  }
  return QString();
}

std::vector<unsigned int> OrganizerCore::activeProblems() const
{
  std::vector<unsigned int> problems;
  const auto& hookdll = oldMO1HookDll();
  if (!hookdll.isEmpty()) {
    // This warning will now be shown every time the problems are checked, which is a
    // bit of a "log spam". But since this is a sever error which will most likely make
    // the game crash/freeze/etc. and is very hard to diagnose,  this "log spam" will
    // make it easier for the user to notice the warning.
    log::warn("hook.dll found in game folder: {}", hookdll);
    problems.push_back(PROBLEM_MO1SCRIPTEXTENDERWORKAROUND);
  }
  return problems;
}

QString OrganizerCore::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_MO1SCRIPTEXTENDERWORKAROUND: {
    return tr(
        "MO1 \"Script Extender\" load mechanism has left hook.dll in your game folder");
  } break;
  default: {
    return tr("Description missing");
  } break;
  }
}

QString OrganizerCore::fullDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_MO1SCRIPTEXTENDERWORKAROUND: {
    return tr("<a href=\"%1\">hook.dll</a> has been found in your game folder (right "
              "click to copy the full path). "
              "This is most likely a leftover of setting the ModOrganizer 1 load "
              "mechanism to \"Script Extender\", "
              "in which case you must remove this file either by changing the load "
              "mechanism in ModOrganizer 1 or by manually removing the file. "
              "Otherwise, the game may fail to start or crash.")
        .arg(oldMO1HookDll());
    break;
  }
  default: {
    return tr("Description missing");
  } break;
  }
}

bool OrganizerCore::hasGuidedFix(unsigned int) const
{
  return false;
}

void OrganizerCore::startGuidedFix(unsigned int) const {}

bool OrganizerCore::saveCurrentLists()
{
  if (m_DirectoryUpdate) {
    log::warn("not saving lists during directory update");
    return false;
  }

  if (m_LastRefreshFailed) {
    log::warn("not saving lists after a failed directory update");
    return false;
  }

  try {
    savePluginList();
    if (m_UserInterface != nullptr) {
      m_UserInterface->archivesWriter().write();
    }
  } catch (const std::exception& e) {
    reportError(tr("failed to save load order: %1").arg(e.what()));
  }

  return true;
}

void OrganizerCore::savePluginList()
{
  onNextRefresh(
      [this]() {
        if (m_LastRefreshFailed) {
          log::warn("not saving the plugin list after a failed directory update");
          return;
        }

        m_PluginList.saveTo(m_CurrentProfile->getLockedOrderFileName());

        // MO2 Revamped: the plugins whose file time (their load order, for the
        // games using it) couldn't be set are named, once per change
        QStringList locked;
        m_PluginList.saveLoadOrder(*m_DirectoryStructure, &locked);
        if (locked != m_UndatedPlugins) {
          m_UndatedPlugins = locked;
          if (!locked.isEmpty()) {
            MessageDialog::showMessage(
                tr("These plugins are open in another program, so their load order "
                   "couldn't be set: %1. Close the program (the game, xEdit) and "
                   "refresh.")
                    .arg(locked.join(", ")),
                qApp->activeWindow());
          }
        }
      },
      RefreshCallbackGroup::CORE, RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

void OrganizerCore::saveCurrentProfile()
{
  if (m_CurrentProfile == nullptr) {
    return;
  }

  m_CurrentProfile->writeModlist();
  m_CurrentProfile->fillEmptyGamePrefs();
  m_CurrentProfile->createTweakedIniFile();
  saveCurrentLists();
  storeSettings();
}

ProcessRunner OrganizerCore::processRunner()
{
  return ProcessRunner(*this, m_UserInterface);
}

bool OrganizerCore::beforeRun(
    const QFileInfo& binary, const QDir& cwd, const QString& arguments,
    const QString& profileName, const QString& customOverwrite,
    const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries)
{
  saveCurrentProfile();

  waitForDirectoryRefresh();

  // need to make sure all data is saved before we start the application
  if (m_CurrentProfile != nullptr) {
    m_CurrentProfile->writeModlistNow(true);
  }

  if (!m_AboutToRun(binary.absoluteFilePath(), cwd, arguments)) {
    log::debug("start of \"{}\" cancelled by plugin", binary.absoluteFilePath());
    return false;
  }

  try {
    m_USVFS.updateMapping(fileMapping(profileName, customOverwrite));
    m_USVFS.updateForcedLibraries(forcedLibraries);
  } catch (const UsvfsConnectorException& e) {
    log::debug("{}", e.what());
    return false;
  } catch (const std::exception& e) {
    QWidget* w = nullptr;
    if (m_UserInterface) {
      w = m_UserInterface->mainWindow();
    }
    QMessageBox::warning(w, tr("Error"), e.what());
    return false;
  }

  return true;
}

void OrganizerCore::afterRun(const QFileInfo& binary, DWORD exitCode)
{
  // need to remove our stored load order because it may be outdated if a
  // foreign tool changed the file time. After removing that file,
  // refreshESPList will use the file time as the order
  if (managedGame()->loadOrderMechanism() ==
      IPluginGame::LoadOrderMechanism::FileTime) {
    log::debug("removing loadorder.txt");
    QFile::remove(m_CurrentProfile->getLoadOrderFileName());
  }

  // what the program wrote isn't in a refresh that is already running
  if (m_DirectoryUpdate) {
    m_RefreshPending = true;
  }
  refreshDirectoryStructure();

  refreshESPList(true);
  savePluginList();
  cycleDiagnostics();

  // These callbacks should not fiddle with directory structure and ESPs.
  m_FinishedRun(binary.absoluteFilePath(), exitCode);
}

ProcessRunner::Results OrganizerCore::waitForAllUSVFSProcesses(UILocker::Reasons reason)
{
  return processRunner().waitForAllUSVFSProcessesWithLock(reason);
}

std::vector<Mapping> OrganizerCore::fileMapping(const QString& profileName,
                                                const QString& customOverwrite)
{
  waitForDirectoryRefresh();

  IPluginGame* game = qApp->property("managed_game").value<IPluginGame*>();
  Profile profile(QDir(m_Settings.paths().profiles() + "/" + profileName), game,
                  gameFeatures());

  MappingType result;

  QStringList dataPaths;
  dataPaths.append(QDir::toNativeSeparators(game->dataDirectory().absolutePath()));

  for (auto directory : game->secondaryDataDirectories()) {
    dataPaths.append(directory.absolutePath());
  }

  bool overwriteActive = false;

  for (const auto& mod : profile.getActiveMods()) {
    if (std::get<0>(mod).compare("overwrite", Qt::CaseInsensitive) == 0) {
      continue;
    }

    unsigned int modIndex = ModInfo::getIndex(std::get<0>(mod));
    ModInfo::Ptr modPtr   = ModInfo::getByIndex(modIndex);

    bool createTarget = customOverwrite == std::get<0>(mod);

    overwriteActive |= createTarget;

    if (modPtr->isRegular()) {
      for (auto dataPath : dataPaths) {
        result.insert(result.end(), {QDir::toNativeSeparators(std::get<1>(mod)),
                                     dataPath, true, createTarget});
      }
    }
  }

  if (!overwriteActive && !customOverwrite.isEmpty()) {
    if (ModInfo::getIndex(customOverwrite) == UINT_MAX) {
      throw MyException(tr("The mod \"%1\" assigned to this program no longer exists. "
                           "Choose another one in Edit programs.")
                            .arg(customOverwrite));
    }
    throw MyException(tr("The mod \"%1\", set in Edit programs to receive new files, "
                         "is disabled in this profile. Turn it on or choose another "
                         "mod.")
                          .arg(customOverwrite));
  }

  // MO2 Revamped: the saves of the profile the program runs with, which can be
  // another one than the current profile (a profile override)
  if (profile.localSavesEnabled()) {
    auto localSaves = gameFeatures().gameFeature<LocalSavegames>();
    if (localSaves != nullptr) {
      MappingType saveMap = localSaves->mappings(profile.absolutePath() + "/saves");
      result.reserve(result.size() + saveMap.size());
      result.insert(result.end(), saveMap.begin(), saveMap.end());
    } else {
      log::warn("local save games not supported by this game plugin");
    }
  }

  // what a game writes needs somewhere to go, even if something removed it
  QDir().mkpath(m_Settings.paths().overwrite());

  for (auto dataPath : dataPaths) {
    result.insert(result.end(),
                  {QDir::toNativeSeparators(m_Settings.paths().overwrite()), dataPath,
                   true, customOverwrite.isEmpty()});
  }

  for (MOBase::IPluginFileMapper* mapper :
       m_PluginContainer->plugins<MOBase::IPluginFileMapper>()) {
    IPlugin* plugin = dynamic_cast<IPlugin*>(mapper);
    if (m_PluginContainer->isEnabled(plugin)) {
      MappingType pluginMap = mapper->mappings();
      result.reserve(result.size() + pluginMap.size());
      result.insert(result.end(), pluginMap.begin(), pluginMap.end());
    }
  }

  return result;
}

std::vector<Mapping> OrganizerCore::fileMapping(const QString& dataPath,
                                                const QString& relPath,
                                                const DirectoryEntry* base,
                                                const DirectoryEntry* directoryEntry,
                                                int createDestination)
{
  std::vector<Mapping> result;

  for (FileEntryPtr current : directoryEntry->getFiles()) {
    bool isArchive = false;
    int origin     = current->getOrigin(isArchive);
    if (isArchive || (origin == 0)) {
      continue;
    }

    QString originPath = QString::fromStdWString(base->getOriginByID(origin).getPath());
    QString fileName   = QString::fromStdWString(current->getName());
    //    QString fileName = ToQString(current->getName());
    QString source = originPath + relPath + fileName;
    QString target = dataPath + relPath + fileName;
    if (source != target) {
      result.push_back({source, target, false, false});
    }
  }

  // recurse into subdirectories
  for (const auto& d : directoryEntry->getSubDirectories()) {
    int origin = d->anyOrigin();

    QString originPath = QString::fromStdWString(base->getOriginByID(origin).getPath());
    QString dirName    = QString::fromStdWString(d->getName());
    QString source     = originPath + relPath + dirName;
    QString target     = dataPath + relPath + dirName;

    bool writeDestination = (base == directoryEntry) && (origin == createDestination);

    result.push_back({source, target, true, writeDestination});
    std::vector<Mapping> subRes =
        fileMapping(dataPath, relPath + dirName + "\\", base, d, createDestination);
    result.insert(result.end(), subRes.begin(), subRes.end());
  }
  return result;
}
