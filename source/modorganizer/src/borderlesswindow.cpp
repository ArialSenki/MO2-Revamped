#include "borderlesswindow.h"

#include "organizercore.h"
#include "game_features.h"
#include "profile.h"
#include "settings.h"

#include <iplugingame.h>
#include <log.h>
#include <scriptextender.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <atomic>
#include <thread>

#include <Windows.h>

#include <iterator>
#include <memory>
#include <optional>
#include <vector>

using namespace MOBase;

namespace
{

constexpr auto EnabledKey = "Settings/borderless_window";
// the INI values before MO2 changed them, per INI file
constexpr auto SavedGroup = "BorderlessSaved";
// a key the INI didn't have: put back as missing
const QString Absent = QStringLiteral("\x01");

struct GameInfo
{
  // the INI with the [Display] settings
  QString ini;
  // the game turns borderless itself with bBorderless
  bool native;
  // the game's exe, whose window loses its border when not native
  QString exe;
};

std::optional<GameInfo> info(const IPluginGame* game)
{
  if (game == nullptr) {
    return std::nullopt;
  }
  const QString name = game->gameShortName();
  if (name == QLatin1String("SkyrimSE")) {
    return GameInfo{QStringLiteral("skyrimprefs.ini"), true, QStringLiteral("SkyrimSE.exe")};
  }
  if (name == QLatin1String("EnderalSE")) {
    return GameInfo{QStringLiteral("enderalprefs.ini"), true, QStringLiteral("SkyrimSE.exe")};
  }
  if (name == QLatin1String("Fallout4")) {
    return GameInfo{QStringLiteral("fallout4prefs.ini"), true, QStringLiteral("Fallout4.exe")};
  }
  if (name == QLatin1String("Skyrim")) {
    return GameInfo{QStringLiteral("skyrimprefs.ini"), false, QStringLiteral("TESV.exe")};
  }
  if (name == QLatin1String("Enderal")) {
    return GameInfo{QStringLiteral("enderalprefs.ini"), false, QStringLiteral("TESV.exe")};
  }
  if (name == QLatin1String("Fallout3")) {
    return GameInfo{QStringLiteral("falloutprefs.ini"), false, QStringLiteral("Fallout3.exe")};
  }
  if (name == QLatin1String("FalloutNV") || name == QLatin1String("TTW")) {
    return GameInfo{QStringLiteral("falloutprefs.ini"), false, QStringLiteral("FalloutNV.exe")};
  }
  if (name == QLatin1String("Oblivion") || name == QLatin1String("Nehrim")) {
    return GameInfo{QStringLiteral("oblivion.ini"), false, QStringLiteral("Oblivion.exe")};
  }
  return std::nullopt;
}

// the game's own spelling of the INI's name, for the profile's copy
QString iniName(const IPluginGame* game, const QString& wanted)
{
  for (const QString& ini : game->iniFiles()) {
    if (ini.compare(wanted, Qt::CaseInsensitive) == 0) {
      return ini;
    }
  }
  return wanted;
}

QRect primaryScreen()
{
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
    const RECT& r = monitor.rcMonitor;
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
  }
  return QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

QString readIni(const QString& file, const QString& key)
{
  std::vector<wchar_t> buffer(1024);
  GetPrivateProfileStringW(L"Display", key.toStdWString().c_str(), Absent.toStdWString().c_str(),
                           buffer.data(), static_cast<DWORD>(buffer.size()),
                           file.toStdWString().c_str());
  return QString::fromWCharArray(buffer.data());
}

bool writeIni(const QString& file, const QString& key, const QString& value)
{
  const std::wstring k = key.toStdWString();
  const std::wstring v = value.toStdWString();
  return WritePrivateProfileStringW(L"Display", k.c_str(), value == Absent ? nullptr : v.c_str(),
                                    file.toStdWString().c_str()) != FALSE;
}

// the records of the values MO2 changed, for every instance in one place:
// instances whose profiles use the INI in Documents share that file
QString savedStorePath()
{
  return QDir(GlobalSettings::directory()).filePath(QStringLiteral("MO2-Revamped-Borderless.ini"));
}

// records an earlier build kept in the instance's ModOrganizer.ini
void moveInstanceRecords(OrganizerCore& core, QSettings& store)
{
  QSettings instance(core.settings().filename(), QSettings::IniFormat);
  instance.beginGroup(SavedGroup);
  const QStringList keys = instance.childKeys();
  for (const QString& key : keys) {
    if (!store.contains(key)) {
      store.setValue(key, instance.value(key));
    }
  }
  instance.endGroup();
  if (!keys.isEmpty()) {
    instance.remove(SavedGroup);
  }
}

bool sameIniName(const QString& a, const QString& b)
{
  return QFileInfo(a).fileName().compare(QFileInfo(b).fileName(), Qt::CaseInsensitive) == 0;
}

QString savedKey(const QString& iniPath)
{
  const QByteArray path = QDir::cleanPath(iniPath).toLower().toUtf8();
  return QString::fromLatin1(
      QCryptographicHash::hash(path, QCryptographicHash::Sha1).toHex().left(16));
}

// the game itself, the program its plugin starts it with, or the script
// extender's loader: not its launcher, the editor or a tool in its folder
bool startsTheGame(OrganizerCore& core, const IPluginGame* game, const GameInfo& gameInfo,
                   const QFileInfo& binary)
{
  if (QDir::cleanPath(binary.absolutePath())
          .compare(QDir::cleanPath(game->gameDirectory().absolutePath()),
                   Qt::CaseInsensitive) != 0) {
    return false;
  }
  QStringList names{gameInfo.exe, game->binaryName()};
  if (const auto extender = core.gameFeatures().gameFeature<ScriptExtender>()) {
    names.append(extender->loaderName());
  }
  for (const QString& name : names) {
    if (!name.isEmpty() && binary.fileName().compare(name, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

// the visible top-level window with a title bar of a process named exe
struct Found
{
  std::wstring exe;
  HWND window = nullptr;
  DWORD process = 0;
  long area     = 0;
};

BOOL CALLBACK findWindow(HWND window, LPARAM param)
{
  auto* found = reinterpret_cast<Found*>(param);
  if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
    return TRUE;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) {
    return TRUE;
  }
  wchar_t path[MAX_PATH * 2]{};
  DWORD size = static_cast<DWORD>(std::size(path));
  const bool named = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
  CloseHandle(process);
  if (!named) {
    return TRUE;
  }
  const QString name = QFileInfo(QString::fromWCharArray(path, size)).fileName();
  if (name.compare(QString::fromStdWString(found->exe), Qt::CaseInsensitive) != 0) {
    return TRUE;
  }
  // not a console a plugin opened, nor a message box
  wchar_t className[64]{};
  GetClassNameW(window, className, static_cast<int>(std::size(className)));
  if (wcscmp(className, L"ConsoleWindowClass") == 0 || wcscmp(className, L"#32770") == 0) {
    return TRUE;
  }
  RECT r{};
  GetWindowRect(window, &r);
  const long area = (r.right - r.left) * (r.bottom - r.top);
  // the game's main window: the largest, not a splash
  if (r.right - r.left < 320 || r.bottom - r.top < 240 || area <= found->area) {
    return TRUE;
  }
  found->window  = window;
  found->process = pid;
  found->area    = area;
  return TRUE;
}

constexpr LONG_PTR FrameStyles =
    WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
constexpr LONG_PTR FrameExStyles =
    WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE;

enum class Strip
{
  Done,
  // it had no frame
  Nothing,
  // Windows didn't let MO2 change it (a game run as administrator)
  Refused,
};

// takes the frame off and covers the window's monitor
Strip stripBorder(HWND window)
{
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  if ((style & (WS_CAPTION | WS_THICKFRAME)) == 0) {
    return Strip::Nothing;
  }
  SetWindowLongPtrW(window, GWL_STYLE, (style & ~FrameStyles) | WS_POPUP);
  if ((GetWindowLongPtrW(window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) != 0) {
    return Strip::Refused;
  }
  SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~FrameExStyles);

  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT& r = monitor.rcMonitor;
  SetWindowPos(window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
  return Strip::Done;
}

// The watcher runs on a thread of its own with its own message loop: the
// window events Windows sends (shown, moved, resized) reach it at once.
// Through MO2's interface thread they came seconds late while it waits for
// the game, and the framed window showed first.
thread_local std::wstring watchedExe;
thread_local DWORD watchedProcess = 0;
thread_local HWINEVENTHOOK moveHook = nullptr;

void watchMoves(DWORD process);

void handleWindow(HWND window, LONG object, LONG child)
{
  if (window == nullptr || object != OBJID_WINDOW || child != CHILDID_SELF ||
      GetAncestor(window, GA_ROOT) != window || watchedExe.empty()) {
    return;
  }
  Found found;
  found.exe = watchedExe;
  findWindow(window, reinterpret_cast<LPARAM>(&found));
  if (found.window != window) {
    return;
  }
  watchMoves(found.process);
  if (stripBorder(window) == Strip::Done) {
    log::debug("borderless window: took the frame off the game's window");
  }
}

void CALLBACK onWindowEvent(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD,
                            DWORD)
{
  handleWindow(window, object, child);
}

// New Vegas shows a small framed window first, then sizes it for the game
// and puts the frame back: each move or resize of that process's windows
void watchMoves(DWORD process)
{
  if (moveHook == nullptr && process != 0) {
    watchedProcess = process;
    moveHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                               &onWindowEvent, process, 0, WINEVENT_OUTOFCONTEXT);
  }
}

bool processAlive(DWORD pid)
{
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (process == nullptr) {
    return false;
  }
  const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
  CloseHandle(process);
  return alive;
}

}  // namespace

std::optional<std::pair<QString, QString>>
BorderlessWindow::forcedLibrary(const IPluginGame* game)
{
  const auto gameInfo = info(game);
  if (!gameInfo || gameInfo->native) {
    return std::nullopt;
  }
  const QString dll = QDir(QCoreApplication::applicationDirPath())
                          .absoluteFilePath(QStringLiteral("borderless/mo2r_borderless.dll"));
  if (!QFileInfo::exists(dll)) {
    log::warn("borderless window: {} is missing; the border comes off from outside the game",
              dll);
    return std::nullopt;
  }
  return std::make_pair(gameInfo->exe, QDir::toNativeSeparators(dll));
}

bool BorderlessWindow::supported(const IPluginGame* game)
{
  return info(game).has_value();
}

bool BorderlessWindow::enabled(OrganizerCore& core)
{
  return QSettings(core.settings().filename(), QSettings::IniFormat)
      .value(EnabledKey, false)
      .toBool();
}

void BorderlessWindow::setEnabled(OrganizerCore& core, bool enabled)
{
  QSettings(core.settings().filename(), QSettings::IniFormat).setValue(EnabledKey, enabled);
  log::info("borderless window {}", enabled ? "on" : "off");
}

bool BorderlessWindow::prepareLaunch(OrganizerCore& core, const QFileInfo& binary)
{
  const IPluginGame* game = core.managedGame();
  const auto gameInfo     = info(game);
  const Profile* profile  = core.currentProfile();
  if (!gameInfo || profile == nullptr || !startsTheGame(core, game, *gameInfo, binary)) {
    return false;
  }

  const QString iniPath = profile->absoluteIniFilePath(iniName(game, gameInfo->ini));
  QSettings settings(savedStorePath(), QSettings::IniFormat);
  settings.beginGroup(SavedGroup);
  moveInstanceRecords(core, settings);
  const QString key       = savedKey(iniPath);
  const QStringList saved = settings.value(key).toStringList();

  if (!enabled(core)) {
    // turned off: every INI of this game MO2 changed gets its values back
    // (another profile's, or the one in Documents from before profile INIs
    // were turned on)
    for (const QString& other : settings.childKeys()) {
      const QStringList record = settings.value(other).toStringList();
      if (record.isEmpty() || !sameIniName(record[0], iniPath)) {
        continue;
      }
      bool restored = true;
      if (QFileInfo::exists(record[0])) {
        for (int i = 1; i < record.size(); ++i) {
          const int equals = record[i].indexOf(QLatin1Char('='));
          if (equals > 0 &&
              !writeIni(record[0], record[i].left(equals), record[i].mid(equals + 1))) {
            restored = false;
          }
        }
        if (restored) {
          log::info("borderless window off: {} has its display settings back", record[0]);
        } else {
          // kept: the next start tries again (a read-only INI, for one)
          log::warn("borderless window off: can't write {}; its display settings stay "
                    "as they are until it can be written",
                    record[0]);
        }
      }
      if (restored) {
        settings.remove(other);
      }
    }
    return false;
  }

  // a missing or empty INI is the game's first start: it writes its own, and
  // with an INI holding only these keys New Vegas opens its launcher instead
  const QFileInfo ini(iniPath);
  if (!ini.exists() || ini.size() == 0) {
    log::warn("borderless window: {} isn't there yet; start the game once first", iniPath);
    return false;
  }

  const QRect screen = primaryScreen();
  QList<QPair<QString, QString>> values = {
      {QStringLiteral("bFull Screen"), QStringLiteral("0")},
      {QStringLiteral("iSize W"), QString::number(screen.width())},
      {QStringLiteral("iSize H"), QString::number(screen.height())},
  };
  if (gameInfo->native) {
    values.append({QStringLiteral("bBorderless"), QStringLiteral("1")});
  } else {
    // the framed window opens where it ends up, so taking the frame off
    // doesn't move it
    values.append({QStringLiteral("iLocation X"), QString::number(screen.left())});
    values.append({QStringLiteral("iLocation Y"), QString::number(screen.top())});
  }

  if (!ini.permissions().testFlag(QFileDevice::WriteUser)) {
    log::warn("borderless window: {} is read-only; the game keeps its display settings",
              iniPath);
    return false;
  }

  if (saved.isEmpty()) {
    // a copy of an INI MO2 already changed (a profile that took the one in
    // Documents) has the user's values in that INI's record, not in the file;
    // only a file that holds MO2's values is such a copy
    QStringList original;
    bool holdsOurs = true;
    for (const auto& value : values) {
      holdsOurs = holdsOurs && readIni(iniPath, value.first) == value.second;
    }
    for (const QString& other : settings.childKeys()) {
      if (!holdsOurs) {
        break;
      }
      const QStringList record = settings.value(other).toStringList();
      if (!record.isEmpty() && sameIniName(record[0], iniPath)) {
        original = record;
        break;
      }
    }
    if (original.isEmpty()) {
      original.append(iniPath);
      for (const auto& value : values) {
        original.append(value.first + QLatin1Char('=') + readIni(iniPath, value.first));
      }
    } else {
      original[0] = iniPath;
    }
    settings.setValue(key, original);
  } else {
    // a record from an earlier version: the keys it doesn't have still hold
    // the user's values
    QStringList record = saved;
    bool added         = false;
    for (const auto& value : values) {
      const QString prefix = value.first + QLatin1Char('=');
      bool there           = false;
      for (const QString& entry : record) {
        there = there || entry.startsWith(prefix);
      }
      if (!there) {
        record.append(prefix + readIni(iniPath, value.first));
        added = true;
      }
    }
    if (added) {
      settings.setValue(key, record);
    }
  }
  for (const auto& value : values) {
    if (!writeIni(iniPath, value.first, value.second)) {
      log::error("borderless window: can't write {} in {}", value.first, iniPath);
      return false;
    }
  }
  log::info("borderless window: {} set to {}x{}{}", iniPath, screen.width(), screen.height(),
            gameInfo->native ? ", borderless" : ", windowed");
  return !gameInfo->native;
}

void BorderlessWindow::watchGameWindow(const IPluginGame* game)
{
  const auto gameInfo = info(game);
  if (!gameInfo || gameInfo->native) {
    return;
  }

  // one watcher: a second launch while the first waits uses it
  static std::atomic<bool> running{false};
  if (running.exchange(true)) {
    return;
  }

  const std::wstring exe = gameInfo->exe.toStdWString();
  std::thread([exe] {
    watchedExe = exe;
    HWINEVENTHOOK showHook =
        SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &onWindowEvent, 0, 0,
                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    // the window can be there before the hook: a look every half second too,
    // which also notices the frame coming back without a move
    const UINT_PTR tick = SetTimer(nullptr, 0, 500, nullptr);
    const ULONGLONG start = GetTickCount64();
    bool reported         = false;

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      if (message.message == WM_TIMER && message.wParam == tick) {
        if (watchedProcess != 0) {
          if (!processAlive(watchedProcess)) {
            break;
          }
        } else if (GetTickCount64() - start > 180000) {
          log::warn("borderless window: the game's window didn't show");
          break;
        }
        Found found;
        found.exe = watchedExe;
        EnumWindows(&findWindow, reinterpret_cast<LPARAM>(&found));
        if (found.window != nullptr) {
          watchMoves(found.process);
          const Strip result = stripBorder(found.window);
          if (result == Strip::Refused) {
            log::warn("borderless window: Windows doesn't let MO2 change the game's window "
                      "(is the game set to run as administrator?)");
            break;
          }
          if (!reported && GetWindowLongPtrW(found.window, GWL_STYLE) & WS_POPUP) {
            reported = true;
            log::info("borderless window: the game's window has no border");
          }
        }
        continue;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    KillTimer(nullptr, tick);
    if (showHook != nullptr) {
      UnhookWinEvent(showHook);
    }
    if (moveHook != nullptr) {
      UnhookWinEvent(moveHook);
      moveHook = nullptr;
    }
    watchedExe.clear();
    watchedProcess = 0;
    running        = false;
  }).detach();
}
