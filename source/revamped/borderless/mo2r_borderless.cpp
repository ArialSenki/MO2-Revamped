// MO2 Revamped: borderless window from inside the game.
//
// Loaded by MO2 (usvfs forced load) into Fallout 3, New Vegas, Oblivion and
// the original Skyrim when the Run menu's "Borderless window" is on. These
// games only have a framed window. Changing it from MO2's process waits for
// the game to process Windows' messages, which it doesn't do for seconds
// while it loads (the framed window showed first) or while it shuts down
// (its window stayed black over the whole screen). From inside, on the
// game's own thread, nothing waits:
//
// - the game's main window loses its frame the moment it is created, and
//   keeps the size and place of its monitor (the game's later SetWindowLong
//   and SetWindowPos calls are adjusted as they happen);
// - when the game stops drawing to shut down, the window is cloaked, so the
//   desktop shows while the game frees its resources. New Vegas doesn't post
//   WM_QUIT nor destroy its window: it leaves WinMain and frees everything
//   behind a black window that covers the screen. A thread of this DLL posts
//   WM_NULL to the window every 50 ms; when the game hasn't taken one for a
//   moment and the screen is black, the window is cloaked (DWM hides it
//   without the game's thread, which no longer answers), and shown again if
//   the game takes messages again (a long hitch, not the exit).
//
// It uses only documented window functions: an in-context WinEvent hook for
// the window's creation, a subclass procedure for its style and position,
// and a WH_GETMESSAGE hook on the window's thread for WM_QUIT. No code is
// patched. A log goes to %TEMP%\MO2 Revamped borderless.log.

#include <windows.h>

#include <dwmapi.h>

#include <cstdarg>
#include <cstdio>

namespace
{

HMODULE g_module           = nullptr;
HWINEVENTHOOK g_createHook = nullptr;
HHOOK g_messageHook        = nullptr;
HWND g_main                = nullptr;
WNDPROC g_original         = nullptr;
bool g_unicode             = false;
bool g_hidden              = false;
volatile LONG g_cloaked    = 0;
// the last time the game's thread took a message (GetTickCount)
volatile LONG g_lastPump   = 0;
// when the DLL took over the window (GetTickCount)
LONG g_takenOver           = 0;

constexpr LONG FrameStyles =
    WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
constexpr LONG FrameExStyles =
    WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE;

void log(const char* format, ...)
{
  wchar_t folder[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, folder) == 0) {
    return;
  }
  wchar_t path[MAX_PATH]{};
  _snwprintf_s(path, _TRUNCATE, L"%sMO2 Revamped borderless.log", folder);
  FILE* file = nullptr;
  if (_wfopen_s(&file, path, L"a") != 0 || file == nullptr) {
    return;
  }
  SYSTEMTIME now;
  GetLocalTime(&now);
  fprintf(file, "%02d:%02d:%02d.%03d [%lu] ", now.wHour, now.wMinute, now.wSecond,
          now.wMilliseconds, GetCurrentProcessId());
  va_list args;
  va_start(args, format);
  vfprintf(file, format, args);
  va_end(args);
  fputc('\n', file);
  fclose(file);
}

RECT monitorRect(HWND window)
{
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
    return monitor.rcMonitor;
  }
  return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

LRESULT callOriginal(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  return g_unicode ? CallWindowProcW(g_original, window, message, wparam, lparam)
                   : CallWindowProcA(g_original, window, message, wparam, lparam);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message) {
  case WM_STYLECHANGING: {
    auto* styles = reinterpret_cast<STYLESTRUCT*>(lparam);
    if (wparam == static_cast<WPARAM>(GWL_STYLE)) {
      styles->styleNew = (styles->styleNew & ~FrameStyles) | WS_POPUP;
    } else if (wparam == static_cast<WPARAM>(GWL_EXSTYLE)) {
      styles->styleNew &= ~FrameExStyles;
    }
    break;
  }
  case WM_WINDOWPOSCHANGING: {
    auto* position = reinterpret_cast<WINDOWPOS*>(lparam);
    // minimized (Alt+Tab) or hidden at exit: left alone
    if (IsIconic(window) || g_hidden || position->x <= -32000) {
      break;
    }
    const RECT r = monitorRect(window);
    if (!(position->flags & SWP_NOMOVE)) {
      position->x = r.left;
      position->y = r.top;
    }
    if (!(position->flags & SWP_NOSIZE)) {
      position->cx = r.right - r.left;
      position->cy = r.bottom - r.top;
    }
    break;
  }
  case WM_CLOSE:
    log("WM_CLOSE");
    break;
  case WM_DESTROY:
    log("WM_DESTROY");
    break;
  }
  return callOriginal(window, message, wparam, lparam);
}

// the game leaving its message loop: the window goes before the game
// spends a second or two freeing its resources behind a black window
LRESULT CALLBACK messageHook(int code, WPARAM wparam, LPARAM lparam)
{
  if (code == HC_ACTION) {
    InterlockedExchange(&g_lastPump, static_cast<LONG>(GetTickCount()));
  }
  if (code == HC_ACTION && wparam == PM_REMOVE) {
    const MSG* message = reinterpret_cast<const MSG*>(lparam);
    if (message->message == WM_QUIT && g_main != nullptr && !g_hidden) {
      g_hidden = true;
      ShowWindow(g_main, SW_HIDE);
      log("WM_QUIT: the game is closing; its window is hidden");
    }
  }
  return CallNextHookEx(g_messageHook, code, wparam, lparam);
}

void cloak(HWND window, bool on)
{
  const BOOL value = on ? TRUE : FALSE;
  DwmSetWindowAttribute(window, DWMWA_CLOAK, &value, sizeof(value));
}

// nine points of the monitor, all black: the game isn't drawing
bool screenBlack(HWND window)
{
  const RECT r = monitorRect(window);
  HDC screen   = GetDC(nullptr);
  if (screen == nullptr) {
    return false;
  }
  bool black = true;
  for (int i = 1; i <= 3 && black; ++i) {
    for (int j = 1; j <= 3 && black; ++j) {
      const COLORREF c = GetPixel(screen, r.left + (r.right - r.left) * i / 4,
                                  r.top + (r.bottom - r.top) * j / 4);
      black = c != CLR_INVALID && GetRValue(c) < 4 && GetGValue(c) < 4 && GetBValue(c) < 4;
    }
  }
  ReleaseDC(nullptr, screen);
  return black;
}

DWORD WINAPI exitWatch(LPVOID)
{
  for (;;) {
    Sleep(50);
    HWND window = g_main;
    if (window == nullptr || !IsWindow(window)) {
      return 0;
    }
    // posted, never sent: a game that no longer takes messages can't hold
    // this thread
    PostMessageW(window, WM_NULL, 0, 0);
    const LONG idle = static_cast<LONG>(GetTickCount()) - g_lastPump;
    // not while the game starts: it takes no messages for seconds after
    // making its window, in front of a black window, and Fallout 3 stops
    // again for a moment when it loads its main menu (seen 24 s in)
    if (static_cast<LONG>(GetTickCount()) - g_takenOver < 45000) {
      continue;
    }
    if (!g_cloaked) {
      if (idle > 300 && screenBlack(window)) {
        cloak(window, true);
        InterlockedExchange(&g_cloaked, 1);
        log("the game stopped drawing (%ld ms without messages, black screen): window cloaked",
            idle);
      }
    } else if (idle < 100) {
      cloak(window, false);
      InterlockedExchange(&g_cloaked, 0);
      log("the game takes messages again: window shown");
    }
  }
}

bool isMainWindow(HWND window)
{
  if (GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER) != nullptr) {
    return false;
  }
  const LONG style = GetWindowLongW(window, GWL_STYLE);
  if ((style & WS_CHILD) || (style & WS_CAPTION) != WS_CAPTION) {
    return false;
  }
  wchar_t className[64]{};
  GetClassNameW(window, className, 64);
  return wcscmp(className, L"#32770") != 0 && wcscmp(className, L"ConsoleWindowClass") != 0 &&
         wcscmp(className, L"IME") != 0 && wcscmp(className, L"MSCTFIME UI") != 0;
}

void takeOver(HWND window)
{
  g_main    = window;
  g_unicode = IsWindowUnicode(window) != FALSE;
  g_original = reinterpret_cast<WNDPROC>(
      g_unicode ? SetWindowLongPtrW(window, GWLP_WNDPROC,
                                    reinterpret_cast<LONG_PTR>(&windowProc))
                : SetWindowLongPtrA(window, GWLP_WNDPROC,
                                    reinterpret_cast<LONG_PTR>(&windowProc)));
  if (g_original == nullptr) {
    log("could not subclass the game's window (%lu)", GetLastError());
    g_main = nullptr;
    return;
  }

  // the style goes through WM_STYLECHANGING, the place through
  // WM_WINDOWPOSCHANGING
  SetWindowLongW(window, GWL_STYLE, GetWindowLongW(window, GWL_STYLE));
  SetWindowLongW(window, GWL_EXSTYLE, GetWindowLongW(window, GWL_EXSTYLE));
  const RECT r = monitorRect(window);
  SetWindowPos(window, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);

  g_messageHook =
      SetWindowsHookExW(WH_GETMESSAGE, &messageHook, nullptr, GetCurrentThreadId());
  InterlockedExchange(&g_lastPump, static_cast<LONG>(GetTickCount()));
  g_takenOver = static_cast<LONG>(GetTickCount());
  if (g_messageHook != nullptr) {
    HANDLE watch = CreateThread(nullptr, 0, &exitWatch, nullptr, 0, nullptr);
    if (watch != nullptr) {
      CloseHandle(watch);
    }
  }
  log("took over the game's window: no frame, %ldx%ld at %ld,%ld; exit hook %s",
      r.right - r.left, r.bottom - r.top, r.left, r.top, g_messageHook ? "on" : "off");
}

// in context: on the thread that created the window, as it was created
void CALLBACK onCreate(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD)
{
  if (g_main != nullptr || window == nullptr || object != OBJID_WINDOW ||
      child != CHILDID_SELF || !isMainWindow(window)) {
    return;
  }
  takeOver(window);
  if (g_main != nullptr && g_createHook != nullptr) {
    UnhookWinEvent(g_createHook);
    g_createHook = nullptr;
  }
}

// a thread of its own holds the hook for the whole process: the thread
// that loaded this DLL may end
DWORD WINAPI hookThread(LPVOID)
{
  g_createHook =
      SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, g_module, &onCreate,
                      GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
  log("loaded; creation hook %s", g_createHook ? "on" : "off");
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    DispatchMessageW(&message);
  }
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, &hookThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
