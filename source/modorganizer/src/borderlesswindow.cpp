#include "borderlesswindow.h"

#include "organizercore.h"
#include "game_features.h"
#include "profile.h"
#include "settings.h"

#include <iplugingame.h>
#include <log.h>
#include <scriptextender.h>

#include <QCryptographicHash>
#include <QDir>
#include <QPointer>
#include <QSettings>
#include <QTimer>

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
  QSettings settings(core.settings().filename(), QSettings::IniFormat);
  settings.beginGroup(SavedGroup);
  const QString key       = savedKey(iniPath);
  const QStringList saved = settings.value(key).toStringList();

  if (!enabled(core)) {
    // turned off: every INI MO2 changed gets its values back (another
    // profile's, or the global one from before profile INIs were turned on)
    for (const QString& other : settings.childKeys()) {
      const QStringList record = settings.value(other).toStringList();
      if (!record.isEmpty() && QFileInfo::exists(record[0])) {
        for (int i = 1; i < record.size(); ++i) {
          const int equals = record[i].indexOf(QLatin1Char('='));
          if (equals > 0) {
            writeIni(record[0], record[i].left(equals), record[i].mid(equals + 1));
          }
        }
        log::info("borderless window off: {} has its display settings back", record[0]);
      }
      settings.remove(other);
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
  }

  if (!ini.permissions().testFlag(QFileDevice::WriteUser)) {
    log::warn("borderless window: {} is read-only; the game keeps its display settings",
              iniPath);
    return false;
  }

  if (saved.isEmpty()) {
    // a copy of an INI MO2 already changed (a profile that took the global
    // one) has the user's values in that INI's record, not in the file
    QStringList original;
    for (const QString& other : settings.childKeys()) {
      const QStringList record = settings.value(other).toStringList();
      if (!record.isEmpty() &&
          QFileInfo(record[0]).fileName().compare(ini.fileName(), Qt::CaseInsensitive) == 0) {
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

  // a few minutes for the window to show (a loader, a slow first start),
  // then checks while the game runs
  struct State
  {
    std::wstring exe;
    int waited = 0;
    DWORD process = 0;
  };
  auto state  = std::make_shared<State>();
  state->exe  = gameInfo->exe.toStdWString();
  // one watcher: a second launch while the first waits uses it
  static QPointer<QTimer> running;
  if (running) {
    return;
  }
  auto* timer = new QTimer(qApp);
  running     = timer;
  timer->setInterval(500);
  QObject::connect(timer, &QTimer::timeout, timer, [timer, state] {
    if (state->process != 0 && !processAlive(state->process)) {
      timer->deleteLater();
      return;
    }
    Found found;
    found.exe = state->exe;
    EnumWindows(&findWindow, reinterpret_cast<LPARAM>(&found));
    if (found.window == nullptr) {
      if (state->process == 0 && ++state->waited > 360) {
        log::warn("borderless window: the game's window didn't show");
        timer->deleteLater();
      }
      return;
    }
    if (state->process == 0) {
      state->process = found.process;
      timer->setInterval(2000);
    }
    switch (stripBorder(found.window)) {
    case Strip::Done:
      log::info("borderless window: took the border off the game's window");
      break;
    case Strip::Refused:
      log::warn("borderless window: Windows doesn't let MO2 change the game's window (is "
                "the game set to run as administrator?)");
      timer->deleteLater();
      break;
    case Strip::Nothing:
      break;
    }
  });
  timer->start();
}
