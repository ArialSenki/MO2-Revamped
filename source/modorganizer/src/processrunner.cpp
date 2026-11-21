#include "processrunner.h"
#include "nolvus.h"
#include <memory>
#include <optional>
#include "env.h"
#include "envmodule.h"
#include "instancemanager.h"
#include "iuserinterface.h"
#include "organizercore.h"
#include "profile.h"
#include "scriptextendersetup.h"
#include "borderlesswindow.h"
#include "wabbajackinstance.h"
#include <iplugingame.h>
#include <scriptextender.h>
#include <log.h>
#include <report.h>
#include <scopeguard.h>
#include <QElapsedTimer>
#include <QMessageBox>

using namespace MOBase;

void adjustForVirtualized(const IPluginGame* game, spawn::SpawnParameters& sp,
                          const Settings& settings)
{
  const QString modsPath = settings.paths().mods();

  // Check if this a request with either an executable or a working directory
  // under our mods folder then will start the process in a virtualized
  // "environment" with the appropriate paths fixed:
  // (i.e. mods\FNIS\path\exe => game\data\path\exe)
  QString cwdPath         = sp.currentDirectory.absolutePath();
  QString trailedModsPath = modsPath;
  if (!trailedModsPath.endsWith('/')) {
    trailedModsPath = trailedModsPath + '/';
  }
  bool virtualizedCwd = cwdPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  QString binPath     = sp.binary.absoluteFilePath();
  bool virtualizedBin = binPath.startsWith(trailedModsPath, Qt::CaseInsensitive);
  if (virtualizedCwd || virtualizedBin) {
    if (virtualizedCwd) {
      int cwdOffset       = cwdPath.indexOf('/', trailedModsPath.length());
      QString adjustedCwd = cwdPath.mid(cwdOffset, -1);
      cwdPath             = game->dataDirectory().absolutePath();
      if (cwdOffset >= 0)
        cwdPath += adjustedCwd;
    }

    if (virtualizedBin) {
      int binOffset       = binPath.indexOf('/', trailedModsPath.length());
      QString adjustedBin = binPath.mid(binOffset, -1);
      binPath             = game->dataDirectory().absolutePath();
      if (binOffset >= 0)
        binPath += adjustedBin;
    }

    QString cmdline = QString("launch \"%1\" \"%2\" %3")
                          .arg(QDir::toNativeSeparators(cwdPath),
                               QDir::toNativeSeparators(binPath), sp.arguments);

    sp.binary    = QFileInfo(QCoreApplication::applicationFilePath());
    sp.arguments = cmdline;
    sp.currentDirectory.setPath(QCoreApplication::applicationDirPath());
  }
}

std::optional<ProcessRunner::Results> singleWait(HANDLE handle, DWORD pid)
{
  if (handle == INVALID_HANDLE_VALUE) {
    return ProcessRunner::Error;
  }

  const auto res = WaitForSingleObject(handle, 50);

  switch (res) {
  case WAIT_OBJECT_0: {
    log::debug("process {} completed", pid);
    return ProcessRunner::Completed;
  }

  case WAIT_TIMEOUT: {
    // still running
    return {};
  }

  case WAIT_FAILED:  // fall-through
  default: {
    // error
    const auto e = ::GetLastError();
    log::error("failed waiting for {}, {}", pid, formatSystemMessage(e));
    return ProcessRunner::Error;
  }
  }
}

enum class Interest
{
  None = 0,
  Weak,
  Strong
};

QString toString(Interest i)
{
  switch (i) {
  case Interest::Weak:
    return "weak";

  case Interest::Strong:
    return "strong";

  case Interest::None:  // fall-through
  default:
    return "no";
  }
}

struct InterestingProcess
{
  env::Process p;
  Interest interest = Interest::None;
  env::HandlePtr handle;
};

// MO2 Revamped: programs that are not waited for at all (Steam and the
// blacklisted ones a game starts, like Steam's overlay), so MO2 doesn't stay
// locked once only they remain; their children still are
struct IgnoredProcesses
{
  // Steam's own programs, never waited for
  QStringList steam;

  // blacklisted programs
  QStringList blacklisted;

  // the started program's pid when it's a script extender's loader: what it
  // starts (the game) is waited for even when blacklisted
  DWORD loaderPid = 0;
};

bool isIgnored(const env::Process& p, const IgnoredProcesses& ignored)
{
  if (ignored.steam.contains(p.name(), Qt::CaseInsensitive)) {
    return true;
  }

  if (!ignored.blacklisted.contains(p.name(), Qt::CaseInsensitive)) {
    return false;
  }

  // the parent id stays set after the loader has exited
  return (ignored.loaderPid == 0 || p.ppid() != ignored.loaderPid);
}

InterestingProcess findRandomProcess(const env::Process& root,
                                     const IgnoredProcesses& ignored)
{
  for (auto&& c : root.children()) {
    if (!isIgnored(c, ignored)) {
      env::HandlePtr h = c.openHandleForWait();
      if (h) {
        return {c, Interest::Weak, std::move(h)};
      }
    }

    auto r = findRandomProcess(c, ignored);
    if (r.handle) {
      return r;
    }
  }

  return {};
}

// returns a process that's in the hidden list, or the top-level process if
// they're all hidden; returns an invalid process if the list is empty
//
InterestingProcess findInterestingProcessInTrees(const env::Process& root,
                                                 const IgnoredProcesses& ignored)
{
  // Certain process names we wish to "hide" for aesthetic reason:
  static const std::vector<QString> hiddenList = {
      QFileInfo(QCoreApplication::applicationFilePath()).fileName(), "conhost.exe"};

  if (root.children().empty()) {
    return {};
  }

  auto isHidden = [&](auto&& p) {
    for (auto& h : hiddenList) {
      if (p.name().contains(h, Qt::CaseInsensitive)) {
        return true;
      }
    }

    return false;
  };

  for (auto&& p : root.children()) {
    if (!isHidden(p) && !isIgnored(p, ignored)) {
      env::HandlePtr h = p.openHandleForWait();
      if (h) {
        return {p, Interest::Strong, std::move(h)};
      }
    }

    auto r = findInterestingProcessInTrees(p, ignored);
    if (r.interest == Interest::Strong) {
      return r;
    }
  }

  // everything is hidden, just pick the first one that can be used
  return findRandomProcess(root, ignored);
}

void dump(const env::Process& p, int indent)
{
  log::debug("{}{}, pid={}, ppid={}", std::string(indent * 4, ' '), p.name(), p.pid(),
             p.ppid());

  for (auto&& c : p.children()) {
    dump(c, indent + 1);
  }
}

void dump(const env::Process& root)
{
  log::debug("process tree:");

  for (auto&& p : root.children()) {
    dump(p, 1);
  }
}

// gets the most interesting process in the list
//
InterestingProcess getInterestingProcess(HANDLE job, const IgnoredProcesses& ignored)
{
  env::Process root = env::getProcessTree(job);
  if (root.children().empty()) {
    log::debug("nothing to wait for");
    return {};
  }

  dump(root);

  auto interest = findInterestingProcessInTrees(root, ignored);
  if (!interest.handle) {
    // this can happen if none of the processes can be opened
    log::debug("no interesting process to wait for");
    return {};
  }

  return interest;
}

const std::chrono::milliseconds Infinite(-1);

// waits for completion, times out after `wait` if not Infinite
//
std::optional<ProcessRunner::Results> timedWait(HANDLE handle, DWORD pid,
                                                UILocker::Session* ls,
                                                std::chrono::milliseconds wait,
                                                std::atomic<bool>& interrupt)
{
  using namespace std::chrono;

  high_resolution_clock::time_point start;
  if (wait != Infinite) {
    start = high_resolution_clock::now();
  }

  while (!interrupt) {
    // wait for a very short while, allows for processing events below
    const auto r = singleWait(handle, pid);

    if (r) {
      // the process has either completed or an error was returned
      return *r;
    }

    // the process is still running

    // check the lock widget; the session can be null when running shortcuts
    // with locking disabled, in which case the user cannot force unlock
    if (ls) {
      switch (ls->result()) {
      case UILocker::StillLocked: {
        break;
      }

      case UILocker::ForceUnlocked: {
        log::debug("waiting for {} force unlocked by user", pid);
        return ProcessRunner::ForceUnlocked;
      }

      case UILocker::Cancelled: {
        log::debug("waiting for {} cancelled by user", pid);
        return ProcessRunner::Cancelled;
      }

      case UILocker::NoResult:  // fall-through
      default: {
        // shouldn't happen
        log::debug("unexpected result {} while waiting for {}",
                   static_cast<int>(ls->result()), pid);

        return ProcessRunner::Error;
      }
      }
    }

    if (wait != Infinite) {
      // check if enough time has elapsed
      const auto now = high_resolution_clock::now();
      if (duration_cast<milliseconds>(now - start) >= wait) {
        // if so, return an empty result
        return {};
      }
    }
  }

  log::debug("waiting for {} interrupted", pid);
  return ProcessRunner::ForceUnlocked;
}

ProcessRunner::Results waitForProcessesThreadImpl(HANDLE job, UILocker::Session* ls,
                                                  std::atomic<bool>& interrupt,
                                                  const IgnoredProcesses& ignored)
{
  using namespace std::chrono;

  DWORD currentPID = 0;

  // if the interesting process that was found is weak (such as ModOrganizer.exe
  // when starting a program from within the Data directory), start with a short
  // wait and check for more interesting children
  const milliseconds defaultWait(50);
  auto wait = defaultWait;

  while (!interrupt) {
    auto ip = getInterestingProcess(job, ignored);
    if (!ip.handle) {
      // nothing to wait on
      return ProcessRunner::Completed;
    }

    // update the lock widget; the session can be null when running shortcuts
    // with locking disabled
    if (ls) {
      ls->setInfo(ip.p.pid(), ip.p.name());
    }

    if (ip.p.pid() != currentPID) {
      // log any change in the process being waited for
      currentPID = ip.p.pid();

      log::debug("waiting for completion on {} ({}), {} interest", ip.p.name(),
                 ip.p.pid(), toString(ip.interest));
    }

    if (ip.interest == Interest::Strong) {
      // don't bother with short wait, this is a good process to wait for
      wait = Infinite;
    }

    const auto r = timedWait(ip.handle.get(), ip.p.pid(), ls, wait, interrupt);
    if (r) {
      if (*r == ProcessRunner::Results::Completed) {
        // process completed, check another one, reset the wait time to find
        // interesting processes
        wait = defaultWait;
      } else if (*r != ProcessRunner::Results::Running) {
        // something's wrong, or the user unlocked the ui
        return *r;
      }
    }

    // exponentially increase the wait time between checks for interesting
    // processes
    wait = std::min(wait * 2, milliseconds(2000));
  }

  log::debug("waiting for processes interrupted");
  return ProcessRunner::ForceUnlocked;
}

void waitForProcessesThread(ProcessRunner::Results& result, HANDLE job,
                            UILocker::Session* ls, std::atomic<bool>& interrupt,
                            IgnoredProcesses ignored)
{
  result = waitForProcessesThreadImpl(job, ls, interrupt, ignored);

  // the session can be null when running shortcuts with locking disabled
  if (ls) {
    ls->unlock();
  }
}

// `ignored`: file names of programs not to wait for, see isIgnored()
ProcessRunner::Results waitForProcesses(const std::vector<HANDLE>& initialProcesses,
                                        UILocker::Session* ls,
                                        const IgnoredProcesses& ignored = {})
{
  if (initialProcesses.empty()) {
    // nothing to wait for
    return ProcessRunner::Completed;
  }

  // using a job so any child process started by any of those processes can also
  // be captured and monitored
  env::HandlePtr job(CreateJobObjectW(nullptr, nullptr));
  if (!job) {
    const auto e = GetLastError();

    log::error("failed to create job to wait for processes, {}",
               formatSystemMessage(e));

    return ProcessRunner::Error;
  }

  bool oneWorked = false;

  for (auto&& h : initialProcesses) {
    if (::AssignProcessToJobObject(job.get(), h)) {
      oneWorked = true;
    } else {
      const auto e = GetLastError();

      // this happens when closing MO while multiple processes are running,
      // so the logging is disabled until it gets fixed

      // log::error(
      //  "can't assign process to job to wait for processes, {}",
      //  formatSystemMessage(e));

      // keep going
    }
  }

  HANDLE monitor = INVALID_HANDLE_VALUE;

  if (oneWorked) {
    monitor = job.get();
  } else {
    // none of the handles could be added to the job, just monitor the first one
    monitor = initialProcesses[0];
  }

  auto results = ProcessRunner::Running;
  std::atomic<bool> interrupt(false);

  auto* t = QThread::create(waitForProcessesThread, std::ref(results), monitor, ls,
                            std::ref(interrupt), ignored);

  // MO2 Revamped: queued to this thread's loop. Called on the worker, a quit()
  // that came before exec() started was lost (exec() resets it): a program
  // that ended at once left the UI waiting for good
  QEventLoop events;
  QObject::connect(t, &QThread::finished, &events, &QEventLoop::quit,
                   Qt::QueuedConnection);

  t->start();
  events.exec();

  if (t->isRunning()) {
    interrupt = true;
    t->wait();
  }

  delete t;

  return results;
}

ProcessRunner::Results waitForProcess(HANDLE initialProcess, LPDWORD exitCode,
                                      UILocker::Session* ls,
                                      const IgnoredProcesses& ignored = {})
{
  std::vector<HANDLE> processes = {initialProcess};

  const auto r = waitForProcesses(processes, ls, ignored);

  // as long as it's not running anymore, try to get the exit code
  if (exitCode && r != ProcessRunner::Running) {
    if (!::GetExitCodeProcess(initialProcess, exitCode)) {
      const auto e = ::GetLastError();
      log::warn("failed to get exit code of process, {}", formatSystemMessage(e));
    }
  }

  return r;
}

ProcessRunner::ProcessRunner(OrganizerCore& core, IUserInterface* ui)
    : m_core(core), m_ui(ui), m_lockReason(UILocker::NoReason), m_waitFlags(NoFlags),
      m_handle(INVALID_HANDLE_VALUE), m_exitCode(-1)
{
  // all processes started in ProcessRunner are hooked by default
  setHooked(true);
}

ProcessRunner& ProcessRunner::setBinary(const QFileInfo& binary)
{
  m_sp.binary = binary;
  return *this;
}

ProcessRunner& ProcessRunner::setArguments(const QString& arguments)
{
  m_sp.arguments = arguments;
  return *this;
}

ProcessRunner& ProcessRunner::setCurrentDirectory(const QDir& directory)
{
  m_sp.currentDirectory = directory;
  return *this;
}

ProcessRunner& ProcessRunner::setSteamID(const QString& steamID)
{
  m_sp.steamAppID = steamID;
  return *this;
}

ProcessRunner& ProcessRunner::setCustomOverwrite(const QString& customOverwrite)
{
  m_customOverwrite = customOverwrite;
  return *this;
}

ProcessRunner& ProcessRunner::setForcedLibraries(const ForcedLibraries& forcedLibraries)
{
  m_forcedLibraries = forcedLibraries;
  return *this;
}

ProcessRunner& ProcessRunner::setProfileName(const QString& profileName)
{
  m_profileName = profileName;
  return *this;
}

ProcessRunner& ProcessRunner::setWaitForCompletion(WaitFlags flags,
                                                   UILocker::Reasons reason)
{
  m_waitFlags  = flags;
  m_lockReason = reason;

  if (m_waitFlags.testFlag(WaitForRefresh) && !m_waitFlags.testFlag(TriggerRefresh)) {
    log::warn("process runner: WaitForRefresh without TriggerRefresh "
              "makes no sense, will be ignored");
  }

  return *this;
}

ProcessRunner& ProcessRunner::setHooked(bool b)
{
  m_sp.hooked = b;
  return *this;
}

ProcessRunner& ProcessRunner::setFromFile(QWidget* parent, const QFileInfo& targetInfo)
{
  if (!parent && m_ui) {
    parent = m_ui->mainWindow();
  }

  // if the file is a .exe, start it directly; if it's anything else, ask the
  // shell to start it

  const auto fec = spawn::getFileExecutionContext(parent, targetInfo);

  switch (fec.type) {
  case spawn::FileExecutionTypes::Executable: {
    setBinary(fec.binary);
    setArguments(fec.arguments);
    setCurrentDirectory(targetInfo.absoluteDir());
    break;
  }

  case spawn::FileExecutionTypes::Other:  // fall-through
  default: {
    m_shellOpen = targetInfo;
    setHooked(false);
    break;
  }
  }

  return *this;
}

ProcessRunner::ForcedLibraries ProcessRunner::forcedLibrariesFor(const QString& title,
                                                                 const Profile* fixedProfile)
{
  if (fixedProfile) {
    if (!fixedProfile->forcedLibrariesEnabled(title)) {
      return {};
    }

    m_core.waitForDirectoryRefresh();
    return fixedProfile->determineForcedLibraries(title);
  }

  const auto* profile = m_core.currentProfile();
  if (!profile || !profile->forcedLibrariesEnabled(title)) {
    return {};
  }

  // game plugins resolve their forced loads against a finished refresh
  m_core.waitForDirectoryRefresh();

  // read again after the wait: the profile can be switched while it runs
  profile = m_core.currentProfile();
  if (!profile || !profile->forcedLibrariesEnabled(title)) {
    return {};
  }

  return profile->determineForcedLibraries(title);
}

ProcessRunner& ProcessRunner::setFromExecutable(const Executable& exeEntry)
{
  // a copy: forcedLibrariesFor() runs events, and the executables list the
  // entry comes from can change meanwhile
  const Executable exe = exeEntry;

  if (!m_core.currentProfile()) {
    throw MyException(QObject::tr("No profile set"));
  }

  // a shortcut runs right after startup, while the first refresh may still
  // be going
  const ForcedLibraries forcedLibraries = forcedLibrariesFor(exe.title());

  const auto* profile = m_core.currentProfile();
  if (!profile) {
    throw MyException(QObject::tr("No profile set"));
  }

  const QString customOverwrite =
      profile->setting("custom_overwrites", exe.title()).toString();

  QString currentDirectory = exe.workingDirectory();
  if (currentDirectory.isEmpty()) {
    currentDirectory = exe.binaryInfo().absolutePath();
  }

  setBinary(exe.binaryInfo());
  setArguments(exe.arguments());
  setCurrentDirectory(currentDirectory);
  setSteamID(exe.steamAppID());
  setCustomOverwrite(customOverwrite);
  setForcedLibraries(forcedLibraries);

  return *this;
}

ProcessRunner& ProcessRunner::setFromShortcut(const MOShortcut& shortcut)
{
  const auto currentInstance = InstanceManager::singleton().currentInstance();

  if (currentInstance) {
    if (shortcut.hasInstance() && !shortcut.isForInstance(*currentInstance)) {
      MOBase::reportError(
          QObject::tr(
              "This shortcut is for instance '%1' but Mod Organizer is currently "
              "running for '%2'. Exit Mod Organizer before running the shortcut or "
              "change the active instance.")
              .arg(shortcut.instanceDisplayName())
              .arg(currentInstance->displayName()));

      throw std::exception();
    }
  }

  const auto* exes = m_core.executablesList();
  const auto exe   = exes->find(shortcut.executableName());

  if (exe != exes->end()) {
    setFromExecutable(*exe);
  } else {
    MOBase::reportError(
        QObject::tr("The program '%1' is no longer in instance '%2'. It may have been "
                    "renamed or removed in Edit programs; create the shortcut again.")
            .arg(shortcut.executableName())
            .arg(currentInstance->displayName()));

    throw std::exception();
  }

  return *this;
}

ProcessRunner& ProcessRunner::setFromFileOrExecutable(
    const QString& executable, const QStringList& args, const QString& cwd,
    const QString& profileOverride, const QString& forcedCustomOverwrite,
    bool ignoreCustomOverwrite)
{
  const auto* current = m_core.currentProfile();
  if (!current) {
    throw MyException(QObject::tr("No profile set"));
  }

  // MO2 Revamped: with a profile override, the mappings come from that profile
  // (see OrganizerCore::fileMapping()), so the program's mod for new files and
  // its forced libraries come from it too
  std::unique_ptr<Profile> overrideProfile;
  if (!profileOverride.isEmpty() &&
      profileOverride.compare(current->name(), Qt::CaseInsensitive) != 0) {
    // a profile that can't be read leaves the current one's settings; run()
    // reports the missing profile as before
    try {
      // only read: the full constructor repairs and migrates a profile that
      // isn't the current one (saves folders, archive invalidation in its
      // INI), and makes one that doesn't exist
      overrideProfile = std::make_unique<Profile>(
          QDir(m_core.settings().paths().profiles() + "/" + profileOverride),
          m_core.managedGame(), m_core.gameFeatures(), Profile::ReadOnly{});
    } catch (const std::exception& e) {
      log::warn("profile '{}' can't be read: {}", profileOverride, e.what());
      overrideProfile.reset();
    }
  }

  const Profile* profile = overrideProfile ? overrideProfile.get() : current;

  setBinary(QFileInfo(executable));
  // joined as they come: plugins pass whole command lines as one item
  // ("deploy -reportProgress ..."); the command line's own arguments are
  // quoted by its caller
  setArguments(args.join(" "));
  setCurrentDirectory(cwd);
  setProfileName(profileOverride);

  if (executable.contains('\\') || executable.contains('/')) {
    // file path

    if (m_sp.binary.isRelative()) {
      // relative path, should be relative to game directory
      setBinary(QFileInfo(
          m_core.managedGame()->gameDirectory().absoluteFilePath(executable)));
    }

    if (cwd == "") {
      setCurrentDirectory(m_sp.binary.absolutePath());
    }

    try {
      // a copy: forcedLibrariesFor() runs events
      const Executable exe = m_core.executablesList()->getByBinary(m_sp.binary);

      setSteamID(exe.steamAppID());
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());
      setForcedLibraries(forcedLibrariesFor(exe.title(), overrideProfile.get()));
    } catch (const std::runtime_error&) {
      // nop
    }
  } else {
    // only a file name, search executables list
    try {
      // a copy: forcedLibrariesFor() runs events
      const Executable exe = m_core.executablesList()->get(executable);

      setSteamID(exe.steamAppID());
      setCustomOverwrite(profile->setting("custom_overwrites", exe.title()).toString());
      setForcedLibraries(forcedLibrariesFor(exe.title(), overrideProfile.get()));

      if (args.isEmpty()) {
        setArguments(exe.arguments());
      }

      setBinary(exe.binaryInfo());

      if (cwd == "") {
        setCurrentDirectory(exe.workingDirectory());
      }
    } catch (const std::runtime_error&) {
      log::warn("\"{}\" not set up as executable", executable);
    }
  }

  if (ignoreCustomOverwrite) {
    setCustomOverwrite("");
  } else if (!forcedCustomOverwrite.isEmpty()) {
    setCustomOverwrite(forcedCustomOverwrite);
  }

  return *this;
}

bool ProcessRunner::shouldRunShell() const
{
  return !m_shellOpen.filePath().isEmpty();
}

ProcessRunner::Results ProcessRunner::run()
{
  // check if setHooked() was called after setFromFile(); this needs to
  // modify the settings to run the associated executable instead of using
  // shell::Open()

  if (shouldRunShell() && m_sp.hooked) {
    // this is a non-executable file, but it should be hooked; the associated
    // executable needs to be retrieved and run instead
    auto assoc = env::getAssociation(m_shellOpen);
    if (!assoc.executable.filePath().isEmpty()) {
      setBinary(assoc.executable);
      setArguments(assoc.formattedCommandLine);
      setCurrentDirectory(assoc.executable.absoluteDir());
      m_shellOpen = {};
    } else {
      // if it fails, just use the regular shell open
      log::error("failed to get the associated executable, running unhooked");
      m_sp.hooked = false;
    }
  } else if (!shouldRunShell() && !m_sp.hooked) {
    // this is an executable that should not be hooked; just run it through
    // the shell
    m_shellOpen = m_sp.binary;
  }

  std::optional<Results> r;

  if (shouldRunShell()) {
    r = runShell();
  } else {
    r = runBinary();
  }

  if (r) {
    // early result: something went wrong and the process cannot be waited for
    return *r;
  }

  return postRun();
}

std::optional<ProcessRunner::Results> ProcessRunner::runShell()
{
  const auto file = m_shellOpen.absoluteFilePath();

  log::debug("executing from shell: '{}'", file);

  auto r = shell::Open(file);
  if (!r.success()) {
    return Error;
  }

  m_handle.reset(r.stealProcessHandle());

  // not all files will return a valid handle even if opening them was
  // successful, such as inproc handlers (like the photo viewer); in this
  // case it's impossible to determine the status, so just say it's still
  // running
  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    log::debug("shell didn't report an error, but no handle is available");
    return Running;
  }

  return {};
}

std::optional<ProcessRunner::Results> ProcessRunner::runBinary()
{
  if (m_profileName.isEmpty()) {
    // get the current profile name if it wasn't overridden
    const auto* profile = m_core.currentProfile();
    if (!profile) {
      throw MyException(QObject::tr("No profile set"));
    }

    m_profileName = profile->name();
  }

  // parent widget used for any dialog popped up while checking for things
  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);

  const auto* game = m_core.managedGame();
  auto& settings   = m_core.settings();

  // MO2 Revamped: the files of the mods' Root folders the VFS can't serve
  // (rootlaunch.h): a script extender's loader replaced by the game exe and a
  // forced load, proxy DLLs through the import table, or the copy fallback
  const RootLaunch::Plan rootPlan =
      m_core.planRootLaunch(m_profileName, m_sp.binary, m_sp.arguments);
  if (!rootPlan.runBinary.isEmpty()) {
    log::info("root launch: starting {} instead of {}",
              QDir::toNativeSeparators(rootPlan.runBinary),
              QDir::toNativeSeparators(m_sp.binary.absoluteFilePath()));
    m_sp.binary           = QFileInfo(rootPlan.runBinary);
    m_sp.currentDirectory = rootPlan.runDirectory.isEmpty()
                                ? m_sp.binary.absoluteDir()
                                : QDir(rootPlan.runDirectory);
  }
  if (!rootPlan.forcedLoadLibrary.isEmpty()) {
    m_forcedLibraries.append(
        ExecutableForcedLoadSetting(rootPlan.forcedLoadProcess, rootPlan.forcedLoadLibrary)
            .withEnabled(true));
  }

  // MO2 Revamped: a DLL copied into the game folder for this run is loaded from
  // there by the game's own imports. A forced load of the same DLL finds the
  // mod's file through the VFS and loads it a second time under another path:
  // two Script Extenders and two Native Mod Loaders in BG3 (their DLLs copied
  // because bg3_dx11.exe names bg3.exe, and the plugin forcing every DLL in
  // bin). Such a forced load is left out; the copy is what the game loads
  if (!rootPlan.copies.empty() && !m_forcedLibraries.isEmpty()) {
    const QDir gameDir = game->gameDirectory();
    const QDir exeDir  = m_sp.binary.absoluteDir();
    QSet<QString> copiedPaths;
    QSet<QString> copiedSources;
    for (const auto& copy : rootPlan.copies) {
      copiedPaths.insert(QDir::cleanPath(gameDir.absoluteFilePath(copy.relativePath)).toLower());
      copiedSources.insert(QDir::cleanPath(QDir::fromNativeSeparators(copy.source)).toLower());
    }
    for (auto it = m_forcedLibraries.begin(); it != m_forcedLibraries.end();) {
      const QString path = QDir::cleanPath(
          exeDir.absoluteFilePath(QDir::fromNativeSeparators(it->library())));
      const QString key = path.toLower();
      if (copiedPaths.contains(key) || copiedSources.contains(key)) {
        log::info("root launch: {} isn't force loaded into {}, the game loads its copy "
                  "in the game folder",
                  it->library(), it->process());
        it = m_forcedLibraries.erase(it);
      } else {
        ++it;
      }
    }
  }

  // these questions come before the plugins prepare the launch: a plugin that
  // hides saves for it gives up when the game doesn't appear in a few seconds

  // MO2 Revamped: an instance made from a Wabbajack list, with the game
  // updated since (its script extender refuses the new version)
  if (!Wabbajack::confirmGameVersion(parent, m_core, m_sp.binary)) {
    return Error;
  }

  // MO2 Revamped: the loader of a mod's Root folder with no runtime DLL for
  // the game's version (FOSE on Steam's Fallout 3 1.7.0.4): asked here, instead
  // of a game without its extender and plugins
  if (!rootPlan.extenderMismatchDlls.isEmpty()) {
    if (const auto extender = m_core.gameFeatures().gameFeature<ScriptExtender>()) {
      if (!ScriptExtenderSetup::confirmRootLaunch(parent, *extender, *game,
                                                  rootPlan.extenderMismatchDlls)) {
        return Error;
      }
    }
  }

  // MO2 Revamped: the script extender's loader with a DLL for another game
  // version (the game updated) doesn't start the game; asked before Steam starts
  if (const auto extender = m_core.gameFeatures().gameFeature<ScriptExtender>()) {
    if (ScriptExtenderSetup::isLoader(m_sp.binary.absoluteFilePath(), *extender) &&
        !ScriptExtenderSetup::confirmLaunch(parent, *extender, *game)) {
      return Error;
    }
  }

  // MO2 Revamped: a Nolvus instance: what Nolvus needs that doesn't look
  // right, said before the program runs (it can still run)
  if (!Nolvus::confirmLaunch(parent, settings.filename(),
                             QDir(settings.paths().profiles()).filePath(m_profileName),
                             m_sp.binary.absoluteFilePath())) {
    return Error;
  }

  // MO2 Revamped: the display settings of the Run menu's borderless window
  const bool watchWindow = BorderlessWindow::prepareLaunch(m_core, m_sp.binary);

  // start steam if needed
  if (!checkSteam(parent, m_sp, game->gameDirectory(), m_sp.steamAppID, settings)) {
    return Error;
  }

  // warn if the executable is on the blacklist
  const QString blacklistBefore = settings.executablesBlacklist();
  if (!checkBlacklist(parent, m_sp, settings)) {
    return Error;
  }

  // MO2 Revamped: "Change the blacklist" only wrote the setting; usvfs kept the
  // old list for this start
  if (settings.executablesBlacklist() != blacklistBefore) {
    m_core.setLogLevel(settings.diagnostics().logLevel());
  }

  // MO2 Revamped: for the games MO2 takes the frame off, its DLL does it
  // from inside the game, on the game's thread, as the window is created
  if (watchWindow) {
    if (const auto library = BorderlessWindow::forcedLibrary(game)) {
      m_forcedLibraries.append(
          ExecutableForcedLoadSetting(library->first, library->second).withEnabled(true));
    }
  }

  // saves profile, sets up usvfs, notifies plugins, etc.; can return false if
  // a plugin doesn't want the program to run (such as when checkFNIS fails to
  // run FNIS and the user clicks cancel)
  QElapsedTimer startTimer;
  startTimer.start();
  if (!m_core.beforeRun(m_sp.binary, m_sp.currentDirectory, m_sp.arguments,
                        m_profileName, m_customOverwrite, m_forcedLibraries)) {
    return Error;
  }
  // MO2 Revamped: where a slow start goes, in the user's log
  log::info("start: profile, VFS and plugins took {}ms", startTimer.restart());

  // MO2 Revamped: the Root files copied for this run (taken out once the last
  // program in the VFS ends), and the import table patch spawn() applies
  auto cantStart = [&](const QString& file) {
    log::error("root launch: {} couldn't be copied into the game folder, {} isn't "
               "started",
               file, m_sp.binary.fileName());
    QMessageBox::warning(
        parent, QObject::tr("Can't start %1").arg(m_sp.binary.fileName()),
        QObject::tr("%1 couldn't be copied from the mod's Root folder into the game "
                    "folder, so it can't start. The reason is in MO2's log.")
            .arg(file));
  };

  if (!rootPlan.copies.empty()) {
    const auto copied = m_core.deployRootCopies(rootPlan.copies);
    log::info("start: {} Root files copied in {}ms", rootPlan.copies.size(),
              startTimer.restart());

    // another instance's copy in the game folder: the program would run with
    // that instance's file; not started, and told which and whose
    if (copied.foreign > 0) {
      const QString file     = QDir::toNativeSeparators(copied.foreignFiles.first());
      const QString instance = QDir::toNativeSeparators(
          QFileInfo(QFileInfo(copied.foreignStates.first()).absolutePath()).absolutePath());
      log::error("root launch: {} in the game folder is the copy of the instance in {}, "
                 "{} isn't started",
                 file, instance, m_sp.binary.fileName());
      m_core.restoreRootCopies();
      QMessageBox::warning(
          parent, QObject::tr("Can't start %1").arg(m_sp.binary.fileName()),
          QObject::tr("%1 in the game folder is a copy that the MO2 instance in %2 put "
                      "there for its own run, so %3 isn't started with it. Start that "
                      "instance once to restore its files, or remove the file.")
              .arg(file, instance, m_sp.binary.fileName()));
      return Error;
    }

    // a needed copy that failed stops the start, and what was copied comes out
    // again (nothing of this run is running yet). The game folder that can't be
    // written to at all is the exception: its notice says the game starts
    // without them
    if (!copied.notWritable && (copied.failed > 0 || !copied.error.isEmpty())) {
      m_core.restoreRootCopies();
      cantStart(copied.failedFiles.isEmpty() ? m_sp.binary.fileName()
                                             : QFileInfo(copied.failedFiles.first()).fileName());
      return Error;
    }
  }

  // the program to start is one of those copies (an extender's loader): if it
  // didn't arrive, the start is stopped with the reason rather than "file not
  // found"
  if (!rootPlan.runBinary.isEmpty() && !QFileInfo::exists(m_sp.binary.absoluteFilePath())) {
    m_core.restoreRootCopies();
    cantStart(m_sp.binary.fileName());
    return Error;
  }

  // from here until the program is up, the copies count as in use; after it,
  // its handle does (the restore timer starts then)
  m_core.beginRootRun();
  ON_BLOCK_EXIT([&] {
    m_core.endRootRun(m_handle.get());
  });

  // if the executable is inside the mods folder another instance of
  // ModOrganizer.exe is spawned instead to launch it
  const QString plannedBinary = m_sp.binary.absoluteFilePath();
  adjustForVirtualized(game, m_sp, settings);

  // only the exe the plan read is patched, not MO2's own helper
  if (rootPlan.patches()) {
    if (m_sp.binary.absoluteFilePath().compare(plannedBinary, Qt::CaseInsensitive) == 0) {
      m_sp.importRedirects   = rootPlan.redirects;
      m_sp.importPreloads    = rootPlan.preloads;
      m_sp.importPatchFailed = [this, files = rootPlan.patchFallback] {
        m_core.deployRootCopies(files);
      };
    } else {
      log::warn("root launch: {} starts through MO2's helper, its imports can't be "
                "patched; using the copy fallback",
                QDir::toNativeSeparators(plannedBinary));
      m_core.deployRootCopies(rootPlan.patchFallback);
    }
  }

  // MO2 Revamped: the older games don't declare DPI awareness; with Windows'
  // scaling above 100% Windows would stretch their full-screen window, drawn at
  // the screen's real size, a second time. The compatibility layer the
  // Properties > Compatibility "high DPI" setting uses is passed to the game
  // (and the extender's loader, which hands its environment on) for this start
  std::optional<QByteArray> compatLayer;
  if (watchWindow) {
    compatLayer = qgetenv("__COMPAT_LAYER");
    QByteArray layer = *compatLayer;
    if (!layer.contains("HighDpiAware")) {
      layer = (layer.trimmed() + " HighDpiAware").trimmed();
    }
    qputenv("__COMPAT_LAYER", layer);
  }

  // run the binary
  m_handle.reset(startBinary(parent, m_sp));

  if (compatLayer) {
    if (compatLayer->isEmpty()) {
      qunsetenv("__COMPAT_LAYER");
    } else {
      qputenv("__COMPAT_LAYER", *compatLayer);
    }
  }

  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    return Error;
  }

  if (watchWindow) {
    BorderlessWindow::watchGameWindow(game);
  }

  return {};
}

bool ProcessRunner::shouldRefresh(Results r) const
{
  // afterRun() is only called with the Refresh flag; it refreshes the
  // directory structure and notifies plugins
  //
  // refreshing is not always required and can actually cause problems:
  //
  //  1) running shortcuts doesn't need refreshing because MO closes right
  //     after
  //
  //  2) the mod info dialog is not set up to deal with refreshes, so that
  //     it will crash because the old DirectoryEntry's are still being used
  //     in the list
  if (!m_waitFlags.testFlag(TriggerRefresh)) {
    log::debug("process runner: not refreshing because the flag isn't set");
    return false;
  }

  switch (r) {
  case Completed: {
    log::debug("process runner: refreshing because the process completed");
    return true;
  }

  case ForceUnlocked: {
    log::debug("process runner: refreshing because the ui was force unlocked");
    return true;
  }

  case Error:  // fall-through
  case Cancelled:
  case Running:
  default: {
    return false;
  }
  }
}

ProcessRunner::Results ProcessRunner::postRun()
{
  const bool mustWait = (m_waitFlags & ForceWait);

  if (!m_sp.hooked && !mustWait) {
    // the process wasn't hooked and there's no force wait, don't lock
    return Running;
  }

  if (mustWait && m_lockReason == UILocker::NoReason) {
    // never lock the ui without an escape hatch for the user
    log::debug("the ForceWait flag is set but the lock reason wasn't, "
               "defaulting to LockUI");

    m_lockReason = UILocker::LockUI;
  }

  const bool lockEnabled = m_core.settings().interface().lockGUI();

  if (mustWait) {
    if (!lockEnabled) {
      // at least tell the user what's going on
      log::debug("locking is disabled, but the output of the application is required; "
                 "overriding this setting and locking the ui");
    }
  } else {
    // no force wait

    if (m_lockReason == UILocker::NoReason) {
      // no locking requested
      return Running;
    }

    if (!lockEnabled) {
      // disabling locking is like clicking on unlock immediately
      log::debug("process runner: not waiting for process because "
                 "locking is disabled");

      return ForceUnlocked;
    }
  }

  // MO2 Revamped: Steam and the blacklisted programs the game starts (Steam's
  // overlay, a browser) aren't waited for; the program MO2 started always is.
  // Read here, the wait runs on another thread
  IgnoredProcesses ignored;
  ignored.steam = {"Steam.exe",           "steamwebhelper.exe",
                   "SteamService.exe",    "GameOverlayUI.exe",
                   "GameOverlayUI64.exe", "steamerrorreporter.exe",
                   "steamerrorreporter64.exe"};
  for (const auto& exec : m_core.settings().executablesBlacklist().split(";")) {
    if (!exec.trimmed().isEmpty()) {
      ignored.blacklisted.append(exec.trimmed());
    }
  }

  // MO2 Revamped: the started program and the managed game's own binary are
  // waited for even when blacklisted; a loader (skse64_loader.exe) exits once
  // the game is up, and ignoring the game then unlocked MO2
  const QString started = m_sp.binary.fileName();
  ignored.steam.removeIf([&](const QString& s) {
    return s.compare(started, Qt::CaseInsensitive) == 0;
  });

  QStringList waitedFor = {started};
  if (const auto* game = m_core.managedGame()) {
    waitedFor.append(game->binaryName());
  }
  ignored.blacklisted.removeIf([&](const QString& s) {
    return waitedFor.contains(s, Qt::CaseInsensitive);
  });

  // MO2 Revamped: what a script extender's loader starts is the game
  if (const auto extender = m_core.gameFeatures().gameFeature<ScriptExtender>()) {
    if (m_handle.get() != INVALID_HANDLE_VALUE &&
        ScriptExtenderSetup::isLoader(m_sp.binary.absoluteFilePath(), *extender)) {
      ignored.loaderPid = ::GetProcessId(m_handle.get());
    }
  }

  auto r = Error;

  if (mustWait && m_lockReason == UILocker::PreventExit && !lockEnabled) {
    // this happens when running shortcuts and locking is disabled
    //
    // MO must stay alive until all processes are dead or child processes
    // may not get hooked properly, but the user has disabled locking the ui
    //
    // this is a bit of an edge case, but that means the user wants to run
    // shortcuts without seeing the lock dialog, so allow them to do that
    //
    // MO will be running in the background with no visual feedback, but that's
    // how it is
    r = waitForProcess(m_handle.get(), &m_exitCode, nullptr, ignored);
  } else {
    withLock([&](auto& ls) {
      r = waitForProcess(m_handle.get(), &m_exitCode, &ls, ignored);
    });
  }

  if (shouldRefresh(r)) {
    m_core.afterRun(m_sp.binary, m_exitCode);

    // MO2 Revamped: the refresh afterRun() starts can end while afterRun() is
    // still running, when a plugin it notifies asks something (MO2's FNIS check
    // does). A quit queued for a loop made before afterRun() then reached it
    // before it ran, and the wait went on until some later refresh. This waits
    // only while a refresh is going, including one that follows it
    if (m_waitFlags.testFlag(WaitForRefresh)) {
      log::debug("process runner: waiting until refresh finishes");
      m_core.waitForDirectoryRefresh();
      log::debug("process runner: refresh is done");
    }
  }

  return r;
}

ProcessRunner::Results ProcessRunner::attachToProcess(HANDLE h)
{
  m_handle.reset(h);
  return postRun();
}

DWORD ProcessRunner::exitCode() const
{
  return m_exitCode;
}

env::HandlePtr ProcessRunner::stealProcessHandle()
{
  auto h = m_handle.release();
  m_handle.reset(INVALID_HANDLE_VALUE);
  return env::HandlePtr(h);
}

ProcessRunner::Results
ProcessRunner::waitForAllUSVFSProcessesWithLock(UILocker::Reasons reason)
{
  m_lockReason = reason;

  if (!m_core.settings().interface().lockGUI()) {
    // disabling locking is like clicking on unlock immediately
    return ForceUnlocked;
  }

  auto r = Error;

  for (;;) {
    withLock([&](auto& ls) {
      const auto processes = getRunningUSVFSProcesses();

      // the handles are opened for this round only; the loop opens them
      // again for the processes still running
      ON_BLOCK_EXIT([&] {
        for (HANDLE h : processes) {
          ::CloseHandle(h);
        }
      });

      if (processes.empty()) {
        r = Completed;
        return;
      }

      r = waitForProcesses(processes, &ls);

      if (r != Completed) {
        // error, cancelled, or unlocked
        return;
      }

      // this process is completed, check for others
      r = Running;
    });

    if (r != Running) {
      break;
    }
  }

  return r;
}

void ProcessRunner::withLock(std::function<void(UILocker::Session&)> f)
{
  auto ls = UILocker::instance().lock(m_lockReason);
  f(*ls);
}
