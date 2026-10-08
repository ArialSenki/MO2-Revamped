#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "vendor/minhook/MinHook.h"

namespace {

HMODULE g_module = nullptr;
HANDLE g_log = INVALID_HANDLE_VALUE;
HMODULE g_seen[1024] = {};
unsigned int g_seen_count = 0;
SRWLOCK g_log_lock = SRWLOCK_INIT;

struct NativeInitializerRequest {
  std::wstring module_path;
  std::string function_name;
  HMODULE module = nullptr;
  volatile LONG started = 0;
};

std::vector<NativeInitializerRequest> g_native_initializer_requests;

struct EldenRingLocalWString {
  void* unknown;
  wchar_t* string;
  void* unknown2;
  uint64_t length;
  uint64_t capacity;
};

using ArchivePathResolver = void*(__cdecl*)(EldenRingLocalWString*, uint64_t,
                                             uint64_t, uint64_t, uint64_t,
                                             uint64_t);

ArchivePathResolver g_original_archive_path = nullptr;
volatile LONG g_logged_asset_overrides = 0;
volatile LONG g_logged_resolver_requests = 0;
volatile LONG g_logged_data_paths = 0;
volatile LONG g_logged_missing_asset_probes = 0;
volatile LONG g_logged_target_asset_events = 0;
volatile LONG g_focus_guard_enabled = 0;
volatile LONG g_focus_guard_focus_block_logged = 0;
volatile LONG g_focus_guard_show_seen = 0;
volatile LONG g_focus_cbt_block_logged = 0;
volatile LONG g_focus_cbt_install_failure_logged = 0;
volatile LONG g_focus_cbt_window_hook_logged = 0;
volatile LONG64 g_last_focus_switch_tick = 0;
volatile LONG64 g_last_taskbar_click_tick = 0;
volatile LONG g_focus_user_release_logged = 0;
volatile LONG g_black_background_enabled = 0;
volatile LONG g_black_background_applied_logged = 0;

constexpr size_t kMaximumFocusHookThreads = 256;
constexpr DWORD kRecentUserInputWindowMs = 1500;
HHOOK g_focus_thread_hooks[kMaximumFocusHookThreads] = {};
DWORD g_focus_hook_thread_ids[kMaximumFocusHookThreads] = {};
size_t g_focus_hook_thread_count = 0;
SRWLOCK g_focus_hook_lock = SRWLOCK_INIT;

using ShowWindowFunction = BOOL(WINAPI*)(HWND, int);
using CreateWindowExAFunction = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD,
                                              int, int, int, int, HWND, HMENU,
                                              HINSTANCE, LPVOID);
using CreateWindowExWFunction = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD,
                                              int, int, int, int, HWND, HMENU,
                                              HINSTANCE, LPVOID);
using SetForegroundWindowFunction = BOOL(WINAPI*)(HWND);
using SetActiveWindowFunction = HWND(WINAPI*)(HWND);
using BringWindowToTopFunction = BOOL(WINAPI*)(HWND);
using SetWindowPosFunction = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using SetWindowPlacementFunction = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);

ShowWindowFunction g_original_show_window = nullptr;
ShowWindowFunction g_original_show_window_async = nullptr;
CreateWindowExAFunction g_original_create_window_ex_a = nullptr;
CreateWindowExWFunction g_original_create_window_ex_w = nullptr;
SetForegroundWindowFunction g_original_set_foreground_window = nullptr;
SetActiveWindowFunction g_original_set_active_window = nullptr;
BringWindowToTopFunction g_original_bring_window_to_top = nullptr;
SetWindowPosFunction g_original_set_window_pos = nullptr;
SetWindowPlacementFunction g_original_set_window_placement = nullptr;

LRESULT CALLBACK focus_activation_hook(int code, WPARAM w_param,
                                       LPARAM l_param);
LRESULT CALLBACK focus_taskbar_mouse_hook(int code, WPARAM w_param,
                                          LPARAM l_param);

void write_bytes(const char* text, DWORD length) {
  if (g_log == INVALID_HANDLE_VALUE || length == 0) {
    return;
  }
  DWORD written = 0;
  WriteFile(g_log, text, length, &written, nullptr);
}

void log_line(const char* message, const wchar_t* detail = nullptr) {
  AcquireSRWLockExclusive(&g_log_lock);
  write_bytes(message, static_cast<DWORD>(lstrlenA(message)));
  if (detail != nullptr && detail[0] != L'\0') {
    write_bytes(" ", 1);
    char utf8[4096] = {};
    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, detail, -1, utf8, static_cast<int>(sizeof(utf8)),
        nullptr, nullptr);
    if (bytes > 1) {
      write_bytes(utf8, static_cast<DWORD>(bytes - 1));
    }
  }
  write_bytes("\r\n", 2);
  ReleaseSRWLockExclusive(&g_log_lock);
}

bool read_focus_guard_setting() {
  wchar_t config_path[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(
      L"ELDENRING_MO2_PROFILE_CONFIG", config_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return false;
  }
  if (GetFileAttributesW(config_path) == INVALID_FILE_ATTRIBUTES) {
    log_line("Profile minimize setting file is unavailable; normal window behavior retained.");
    return false;
  }

  int setting = GetPrivateProfileIntW(
      L"Startup", L"start_minimized", -1, config_path);
  if (setting < 0) {
    setting = GetPrivateProfileIntW(
        L"Startup", L"prevent_focus_steal", -1, config_path);
  }
  if (setting < 0) {
    // Preserve the previous profile choices when the option is renamed.
    setting = GetPrivateProfileIntW(
        L"Startup", L"start_in_background", 0, config_path);
  }
  const bool enabled = setting != 0;
  if (enabled) {
    log_line("Profile option enabled: start Elden Ring minimized.");
  }
  return enabled;
}

bool read_black_background_setting() {
  wchar_t config_path[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(
      L"ELDENRING_MO2_PROFILE_CONFIG", config_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH ||
      GetFileAttributesW(config_path) == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  const bool enabled = GetPrivateProfileIntW(
                           L"Startup", L"black_startup_background", 0,
                           config_path) != 0;
  if (enabled) {
    log_line("Profile option enabled: use a black startup window background.");
  }
  return enabled;
}

bool read_cpu0_affinity_setting(DWORD* delay_seconds) {
  if (delay_seconds == nullptr) {
    return false;
  }
  wchar_t config_path[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(
      L"ELDENRING_MO2_PROFILE_CONFIG", config_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH ||
      GetFileAttributesW(config_path) == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  if (GetPrivateProfileIntW(L"Performance", L"exclude_cpu0_after_start", 0,
                            config_path) == 0) {
    return false;
  }

  DWORD delay = GetPrivateProfileIntW(
      L"Performance", L"cpu0_delay_seconds", 30, config_path);
  if (delay != 15 && delay != 30 && delay != 60) {
    delay = 30;
  }
  *delay_seconds = delay;
  log_line("Profile option enabled: exclude logical CPU 0 after a startup delay.");
  return true;
}

void apply_process_priority_setting() {
  wchar_t config_path[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(
      L"ELDENRING_MO2_PROFILE_CONFIG", config_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH ||
      GetFileAttributesW(config_path) == INVALID_FILE_ATTRIBUTES ||
      GetPrivateProfileIntW(L"Performance", L"process_priority", 0,
                            config_path) != 1) {
    return;
  }

  const DWORD previous_priority = GetPriorityClass(GetCurrentProcess());
  if (previous_priority == ABOVE_NORMAL_PRIORITY_CLASS) {
    log_line("Process priority already is Above normal.");
    return;
  }
  if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
    log_line("Could not set Elden Ring process priority to Above normal.");
    return;
  }
  log_line("Profile option applied: Elden Ring process priority is Above normal.");
}

bool is_process_top_level_window(HWND window) {
  if (window == nullptr || !IsWindow(window)) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id != GetCurrentProcessId()) {
    return false;
  }
  return (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) == 0;
}

bool install_focus_activation_hook_for_thread(DWORD thread_id) {
  if (thread_id == 0 ||
      InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) == 0) {
    return false;
  }

  AcquireSRWLockExclusive(&g_focus_hook_lock);
  for (size_t index = 0; index < g_focus_hook_thread_count; ++index) {
    if (g_focus_hook_thread_ids[index] == thread_id) {
      ReleaseSRWLockExclusive(&g_focus_hook_lock);
      return true;
    }
  }

  if (g_focus_hook_thread_count >= kMaximumFocusHookThreads) {
    if (InterlockedExchange(&g_focus_cbt_install_failure_logged, 1) == 0) {
      log_line("Startup activation hook skipped: the game thread limit was reached.");
    }
    ReleaseSRWLockExclusive(&g_focus_hook_lock);
    return false;
  }

  HHOOK hook = SetWindowsHookExW(WH_CBT, focus_activation_hook, g_module,
                                 thread_id);
  if (hook == nullptr) {
    const DWORD error = GetLastError();
    if (InterlockedExchange(&g_focus_cbt_install_failure_logged, 1) == 0) {
      char message[192] = {};
      std::snprintf(
          message, sizeof(message),
          "Startup activation hook could not be installed for game thread %lu "
          "(Windows error %lu).",
          static_cast<unsigned long>(thread_id),
          static_cast<unsigned long>(error));
      log_line(message);
    }
    ReleaseSRWLockExclusive(&g_focus_hook_lock);
    return false;
  }

  const size_t index = g_focus_hook_thread_count++;
  g_focus_thread_hooks[index] = hook;
  g_focus_hook_thread_ids[index] = thread_id;
  ReleaseSRWLockExclusive(&g_focus_hook_lock);
  return true;
}

void install_focus_activation_hook_for_window(HWND window) {
  if (!is_process_top_level_window(window)) {
    return;
  }
  const DWORD thread_id = GetWindowThreadProcessId(window, nullptr);
  if (install_focus_activation_hook_for_thread(thread_id) &&
      InterlockedExchange(&g_focus_cbt_window_hook_logged, 1) == 0) {
    log_line("Startup activation hook installed for a game window thread.");
  }
}

struct FocusHookEnumerationContext {
  size_t installed = 0;
};

BOOL CALLBACK install_focus_hook_for_existing_window(HWND window,
                                                      LPARAM parameter) {
  if (!is_process_top_level_window(window)) {
    return TRUE;
  }
  auto* context = reinterpret_cast<FocusHookEnumerationContext*>(parameter);
  const DWORD thread_id = GetWindowThreadProcessId(window, nullptr);
  if (install_focus_activation_hook_for_thread(thread_id)) {
    ++context->installed;
  }
  return TRUE;
}

void install_focus_activation_hooks_for_existing_threads() {
  FocusHookEnumerationContext context = {};
  if (!EnumWindows(install_focus_hook_for_existing_window,
                   reinterpret_cast<LPARAM>(&context))) {
    log_line("Startup activation hook enumeration reported an error.");
  }

  if (context.installed > 0) {
    log_line("Startup activation hooks installed for existing game window threads.");
  } else {
    log_line("No existing game window thread was found; new windows will be hooked after creation.");
  }
}

struct ExistingWindowMinimizeContext {
  size_t minimized = 0;
};

BOOL CALLBACK minimize_existing_game_window(HWND window, LPARAM parameter) {
  if (!is_process_top_level_window(window) || !IsWindowVisible(window) ||
      g_original_show_window == nullptr) {
    return TRUE;
  }

  g_original_show_window(window, SW_SHOWMINNOACTIVE);
  InterlockedExchange(&g_focus_guard_show_seen, 1);
  auto* context = reinterpret_cast<ExistingWindowMinimizeContext*>(parameter);
  ++context->minimized;
  return TRUE;
}

void minimize_existing_game_windows() {
  ExistingWindowMinimizeContext context = {};
  if (!EnumWindows(minimize_existing_game_window,
                   reinterpret_cast<LPARAM>(&context))) {
    log_line("Startup minimization could not enumerate existing game windows.");
    return;
  }

  if (context.minimized > 0) {
    log_line("Startup minimization: minimized an existing game window without activating it.");
  }
}

DWORD WINAPI focus_switch_input_monitor(void*) {
  MSG message = {};
  PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  HHOOK taskbar_mouse_hook = SetWindowsHookExW(
      WH_MOUSE_LL, focus_taskbar_mouse_hook, g_module, 0);
  if (taskbar_mouse_hook != nullptr) {
    log_line("Startup taskbar click monitor installed.");
  } else {
    log_line("Startup taskbar click monitor could not be installed.");
  }

  while (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    const bool tab_down = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    const bool alt_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool windows_down =
        (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    if (tab_down && (alt_down || windows_down)) {
      InterlockedExchange64(&g_last_focus_switch_tick,
                            static_cast<LONG64>(GetTickCount64()));
    }
    Sleep(10);
  }

  if (taskbar_mouse_hook != nullptr) {
    UnhookWindowsHookEx(taskbar_mouse_hook);
  }
  return 0;
}

bool is_taskbar_window(HWND window) {
  if (window == nullptr) {
    return false;
  }

  wchar_t class_name[64] = {};
  if (GetClassNameW(window, class_name,
                    static_cast<int>(sizeof(class_name) / sizeof(class_name[0]))) ==
      0) {
    return false;
  }

  return std::wcscmp(class_name, L"Shell_TrayWnd") == 0 ||
         std::wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0;
}

bool point_is_on_taskbar(POINT point) {
  const HWND hit_window = WindowFromPoint(point);
  HWND current = hit_window;
  for (unsigned int depth = 0; current != nullptr && depth < 8; ++depth) {
    if (is_taskbar_window(current)) {
      return true;
    }
    current = GetParent(current);
  }
  return is_taskbar_window(GetAncestor(hit_window, GA_ROOT));
}

LRESULT CALLBACK focus_taskbar_mouse_hook(int code, WPARAM w_param,
                                          LPARAM l_param) {
  if (code == HC_ACTION && w_param == WM_LBUTTONDOWN &&
      InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0) {
    const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
    if (mouse != nullptr && point_is_on_taskbar(mouse->pt)) {
      InterlockedExchange64(&g_last_taskbar_click_tick,
                            static_cast<LONG64>(GetTickCount64()));
    }
  }
  return CallNextHookEx(nullptr, code, w_param, l_param);
}

bool recent_keyboard_switch_input() {
  const bool tab_down = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
  const bool switch_modifier_down =
      (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
      (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
      (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
  const LONG64 last_switch_tick =
      InterlockedCompareExchange64(&g_last_focus_switch_tick, 0, 0);
  const bool recent_switch =
      last_switch_tick > 0 &&
      GetTickCount64() - static_cast<ULONGLONG>(last_switch_tick) <=
          kRecentUserInputWindowMs;
  return (tab_down && switch_modifier_down) || recent_switch;
}

bool recent_taskbar_click_input() {
  const LONG64 last_click_tick =
      InterlockedExchange64(&g_last_taskbar_click_tick, 0);
  return last_click_tick > 0 &&
         GetTickCount64() - static_cast<ULONGLONG>(last_click_tick) <=
             kRecentUserInputWindowMs;
}

void release_focus_guard_after_user_input(const char* reason) {
  if (InterlockedExchange(&g_focus_guard_enabled, 0) != 0 &&
      InterlockedExchange(&g_focus_user_release_logged, 1) == 0) {
    log_line(reason != nullptr
                 ? reason
                 : "Startup minimization released after user activation input.");
  }
}

void apply_black_startup_background(HWND window) {
  if (InterlockedCompareExchange(&g_black_background_enabled, 0, 0) == 0 ||
      !is_process_top_level_window(window)) {
    return;
  }

  HGDIOBJ black_brush = GetStockObject(BLACK_BRUSH);
  if (black_brush == nullptr) {
    log_line("Black startup background skipped: Windows could not provide a black brush.");
    return;
  }

  SetLastError(ERROR_SUCCESS);
  const ULONG_PTR previous = SetClassLongPtrW(
      window, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(black_brush));
  if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
    log_line("Black startup background skipped: Windows could not update the window class.");
    return;
  }
  if (InterlockedExchange(&g_black_background_applied_logged, 1) == 0) {
    log_line("Black startup background applied to a game window class.");
  }
}

struct ExistingBackgroundContext {
  unsigned int windows = 0;
};

BOOL CALLBACK apply_black_background_to_existing_window(HWND window,
                                                        LPARAM parameter) {
  if (!is_process_top_level_window(window)) {
    return TRUE;
  }

  apply_black_startup_background(window);
  InvalidateRect(window, nullptr, TRUE);
  auto* context = reinterpret_cast<ExistingBackgroundContext*>(parameter);
  if (context != nullptr) {
    ++context->windows;
  }
  return TRUE;
}

void apply_black_background_to_existing_windows() {
  if (InterlockedCompareExchange(&g_black_background_enabled, 0, 0) == 0) {
    return;
  }

  ExistingBackgroundContext context = {};
  if (!EnumWindows(apply_black_background_to_existing_window,
                   reinterpret_cast<LPARAM>(&context))) {
    log_line("Black startup background could not enumerate existing game windows.");
    return;
  }

  if (context.windows > 0) {
    log_line("Black startup background applied to existing game window(s) after bridge startup.");
  } else {
    log_line("No game window existed when the black startup background hooks were installed.");
  }
}

DWORD deferred_startup_style(DWORD style) {
  if ((style & WS_CHILD) != 0 || (style & WS_VISIBLE) == 0) {
    return style;
  }

  const bool defer_first_show =
      InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0 ||
      InterlockedCompareExchange(&g_black_background_enabled, 0, 0) != 0;
  if (!defer_first_show) {
    return style;
  }

  // Some game windows are created visible, which can paint once before the
  // ShowWindow hooks get a chance to set the black class brush. Create these
  // top-level windows hidden, apply the requested startup behavior, then show.
  style &= ~WS_VISIBLE;
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0) {
    style |= WS_MINIMIZE;
  }
  return style;
}

int initial_show_command(DWORD requested_style) {
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0 ||
      (requested_style & WS_MINIMIZE) != 0) {
    return SW_SHOWMINNOACTIVE;
  }
  if ((requested_style & WS_MAXIMIZE) != 0) {
    return SW_SHOWMAXIMIZED;
  }
  return SW_SHOW;
}

void finish_initial_game_window(HWND window, DWORD requested_style) {
  if (!is_process_top_level_window(window)) {
    return;
  }

  install_focus_activation_hook_for_window(window);
  apply_black_startup_background(window);

  if ((requested_style & WS_VISIBLE) == 0 ||
      g_original_show_window == nullptr) {
    return;
  }

  const bool minimize =
      InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0;
  if (minimize) {
    const LONG first_show = InterlockedExchange(&g_focus_guard_show_seen, 1);
    g_original_show_window(window, SW_SHOWMINNOACTIVE);
    if (first_show == 0) {
      log_line("Startup minimization: deferred and displayed the first game window minimized.");
    }
    return;
  }

  g_original_show_window(window, initial_show_command(requested_style));
  if (InterlockedCompareExchange(&g_black_background_enabled, 0, 0) != 0) {
    log_line("Startup background: displayed the first game window after setting its class brush.");
  }
}

HWND WINAPI create_window_ex_a_hook(DWORD extended_style, LPCSTR class_name,
                                    LPCSTR window_name, DWORD style, int x,
                                    int y, int width, int height,
                                    HWND parent_window, HMENU menu,
                                    HINSTANCE instance, LPVOID parameter) {
  const DWORD guarded_style = deferred_startup_style(style);
  HWND window = g_original_create_window_ex_a(
      extended_style, class_name, window_name, guarded_style, x, y, width,
      height, parent_window, menu, instance, parameter);
  finish_initial_game_window(window, style);
  return window;
}

HWND WINAPI create_window_ex_w_hook(DWORD extended_style, LPCWSTR class_name,
                                    LPCWSTR window_name, DWORD style, int x,
                                    int y, int width, int height,
                                    HWND parent_window, HMENU menu,
                                    HINSTANCE instance, LPVOID parameter) {
  const DWORD guarded_style = deferred_startup_style(style);
  HWND window = g_original_create_window_ex_w(
      extended_style, class_name, window_name, guarded_style, x, y, width,
      height, parent_window, menu, instance, parameter);
  finish_initial_game_window(window, style);
  return window;
}

BOOL WINAPI show_window_with_focus_guard(HWND window, int command,
                                         ShowWindowFunction show_function,
                                         const char* source) {
  if (!is_process_top_level_window(window)) {
    return show_function(window, command);
  }
  install_focus_activation_hook_for_window(window);
  apply_black_startup_background(window);
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) == 0) {
    return show_function(window, command);
  }

  if (command == SW_HIDE) {
    return show_function(window, command);
  }
  const LONG first_show = InterlockedExchange(&g_focus_guard_show_seen, 1);
  const BOOL result = show_function(window, SW_SHOWMINNOACTIVE);
  if (first_show == 0) {
    log_line(source);
  }
  return result;
}

BOOL WINAPI show_window_hook(HWND window, int command) {
  return show_window_with_focus_guard(
      window, command, g_original_show_window,
      "Startup minimization: showed the game minimized without activating it.");
}

BOOL WINAPI show_window_async_hook(HWND window, int command) {
  return show_window_with_focus_guard(
      window, command, g_original_show_window_async,
      "Startup minimization: showed the game minimized asynchronously without activating it.");
}

void log_focus_request_blocked(const char* message) {
  if (InterlockedCompareExchange(&g_focus_guard_focus_block_logged, 1, 0) ==
      0) {
    log_line(message);
  }
}

LRESULT CALLBACK focus_activation_hook(int code, WPARAM w_param,
                                       LPARAM l_param) {
  if (code != HCBT_ACTIVATE ||
      InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) == 0) {
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }

  const HWND window = reinterpret_cast<HWND>(w_param);
  if (!is_process_top_level_window(window)) {
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }

  const auto* activation = reinterpret_cast<const CBTACTIVATESTRUCT*>(l_param);
  // Only an activation of this game window caused by the user's click or
  // window-switch selection releases the minimized-startup guard. General
  // keyboard/mouse activity elsewhere must not restore the game.
  const bool mouse_activation = activation != nullptr && activation->fMouse;
  if (mouse_activation) {
    InterlockedExchange(&g_focus_guard_show_seen, 1);
    release_focus_guard_after_user_input(
        "Startup minimization released after mouse activation.");
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }
  if (recent_keyboard_switch_input()) {
    InterlockedExchange(&g_focus_guard_show_seen, 1);
    release_focus_guard_after_user_input(
        "Startup minimization released after Alt+Tab or Win+Tab selected the game.");
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }
  if (recent_taskbar_click_input()) {
    InterlockedExchange(&g_focus_guard_show_seen, 1);
    release_focus_guard_after_user_input(
        "Startup minimization released after a taskbar click selected the game.");
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }

  if (InterlockedExchange(&g_focus_cbt_block_logged, 1) == 0) {
    log_line("Startup minimization blocked activation until user input.");
  }
  return 1;
}

BOOL WINAPI set_foreground_window_hook(HWND window) {
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0 &&
      is_process_top_level_window(window)) {
    install_focus_activation_hook_for_window(window);
    log_focus_request_blocked(
        "Startup minimization: blocked a game request to take foreground focus.");
    return FALSE;
  }
  return g_original_set_foreground_window(window);
}

HWND WINAPI set_active_window_hook(HWND window) {
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0 &&
      is_process_top_level_window(window)) {
    install_focus_activation_hook_for_window(window);
    log_focus_request_blocked(
        "Startup minimization: blocked a game request to become the active window.");
    return nullptr;
  }
  return g_original_set_active_window(window);
}

BOOL WINAPI bring_window_to_top_hook(HWND window) {
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0 &&
      is_process_top_level_window(window)) {
    install_focus_activation_hook_for_window(window);
    log_focus_request_blocked(
        "Startup minimization: blocked a game request to move its window to the top.");
    return FALSE;
  }
  return g_original_bring_window_to_top(window);
}

BOOL WINAPI set_window_pos_hook(HWND window, HWND insert_after, int x, int y,
                                int width, int height, UINT flags) {
  if (!is_process_top_level_window(window)) {
    return g_original_set_window_pos(window, insert_after, x, y, width,
                                     height, flags);
  }
  apply_black_startup_background(window);
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) != 0) {
    install_focus_activation_hook_for_window(window);
    const bool show_minimized = (flags & SWP_SHOWWINDOW) != 0;
    flags &= ~SWP_SHOWWINDOW;
    flags |= SWP_NOACTIVATE;
    // Keep game-owned top-level windows from taking focus during startup.
    flags &= ~SWP_NOZORDER;
    insert_after = HWND_BOTTOM;
    const BOOL result = g_original_set_window_pos(
        window, insert_after, x, y, width, height, flags);
    if (result && show_minimized && g_original_show_window != nullptr) {
      const LONG first_show = InterlockedExchange(&g_focus_guard_show_seen, 1);
      g_original_show_window(window, SW_SHOWMINNOACTIVE);
      if (first_show == 0) {
        log_line("Startup minimization: displayed a SetWindowPos request as minimized.");
      }
    }
    return result;
  }
  return g_original_set_window_pos(window, insert_after, x, y, width, height,
                                   flags);
}

BOOL WINAPI set_window_placement_hook(HWND window,
                                     const WINDOWPLACEMENT* placement) {
  if (!is_process_top_level_window(window) || placement == nullptr) {
    return g_original_set_window_placement(window, placement);
  }
  apply_black_startup_background(window);
  if (InterlockedCompareExchange(&g_focus_guard_enabled, 0, 0) == 0) {
    return g_original_set_window_placement(window, placement);
  }
  install_focus_activation_hook_for_window(window);
  if (placement->showCmd == SW_HIDE) {
    return g_original_set_window_placement(window, placement);
  }

  WINDOWPLACEMENT minimized = *placement;
  minimized.showCmd = SW_SHOWMINNOACTIVE;
  const LONG first_show = InterlockedExchange(&g_focus_guard_show_seen, 1);
  const BOOL result = g_original_set_window_placement(window, &minimized);
  if (first_show == 0) {
    log_line("Startup minimization: applied a minimized, non-activating window placement.");
  }
  return result;
}

bool install_startup_window_hooks() {
  const bool focus_guard_enabled = read_focus_guard_setting();
  const bool black_background_enabled = read_black_background_setting();
  InterlockedExchange(&g_focus_guard_enabled,
                      focus_guard_enabled ? 1 : 0);
  InterlockedExchange(&g_black_background_enabled,
                      black_background_enabled ? 1 : 0);
  if (focus_guard_enabled) {
    if (is_process_top_level_window(GetForegroundWindow())) {
      log_line("Startup minimization initialized after a game window was already foreground.");
    }
    install_focus_activation_hooks_for_existing_threads();
  }
  if (!focus_guard_enabled && !black_background_enabled) {
    return false;
  }

  MH_STATUS status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
    log_line("Startup window hooks skipped: MinHook initialization failed.");
    return false;
  }

  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32 == nullptr) {
    log_line("Startup window hooks skipped: user32.dll is unavailable.");
    return false;
  }
  void* show_window = reinterpret_cast<void*>(
      GetProcAddress(user32, "ShowWindow"));
  void* show_window_async = reinterpret_cast<void*>(
      GetProcAddress(user32, "ShowWindowAsync"));
  void* create_window_ex_a = reinterpret_cast<void*>(
      GetProcAddress(user32, "CreateWindowExA"));
  void* create_window_ex_w = reinterpret_cast<void*>(
      GetProcAddress(user32, "CreateWindowExW"));
  void* set_foreground_window = reinterpret_cast<void*>(
      GetProcAddress(user32, "SetForegroundWindow"));
  void* set_active_window = reinterpret_cast<void*>(
      GetProcAddress(user32, "SetActiveWindow"));
  void* bring_window_to_top = reinterpret_cast<void*>(
      GetProcAddress(user32, "BringWindowToTop"));
  void* set_window_pos = reinterpret_cast<void*>(
      GetProcAddress(user32, "SetWindowPos"));
  void* set_window_placement = reinterpret_cast<void*>(
      GetProcAddress(user32, "SetWindowPlacement"));

  constexpr size_t hook_count = 9;
  void* targets[hook_count] = {show_window, show_window_async,
                               create_window_ex_a, create_window_ex_w,
                               set_foreground_window, set_active_window,
                               bring_window_to_top, set_window_pos,
                               set_window_placement};
  void* detours[hook_count] = {
      reinterpret_cast<void*>(&show_window_hook),
      reinterpret_cast<void*>(&show_window_async_hook),
      reinterpret_cast<void*>(&create_window_ex_a_hook),
      reinterpret_cast<void*>(&create_window_ex_w_hook),
      reinterpret_cast<void*>(&set_foreground_window_hook),
      reinterpret_cast<void*>(&set_active_window_hook),
      reinterpret_cast<void*>(&bring_window_to_top_hook),
      reinterpret_cast<void*>(&set_window_pos_hook),
      reinterpret_cast<void*>(&set_window_placement_hook)};
  void** originals[hook_count] = {
      reinterpret_cast<void**>(&g_original_show_window),
      reinterpret_cast<void**>(&g_original_show_window_async),
      reinterpret_cast<void**>(&g_original_create_window_ex_a),
      reinterpret_cast<void**>(&g_original_create_window_ex_w),
      reinterpret_cast<void**>(&g_original_set_foreground_window),
      reinterpret_cast<void**>(&g_original_set_active_window),
      reinterpret_cast<void**>(&g_original_bring_window_to_top),
      reinterpret_cast<void**>(&g_original_set_window_pos),
      reinterpret_cast<void**>(&g_original_set_window_placement)};

  for (size_t index = 0; index < hook_count; ++index) {
    if (targets[index] == nullptr) {
      log_line("Startup window hooks skipped: a required User32 function is unavailable.");
      return false;
    }
  }

  size_t created = 0;
  for (; created < hook_count; ++created) {
    status = MH_CreateHook(targets[created], detours[created],
                           originals[created]);
    if (status != MH_OK) {
      break;
    }
  }
  if (created != hook_count) {
    for (size_t index = 0; index < created; ++index) {
      MH_RemoveHook(targets[index]);
    }
    log_line("Startup window hooks skipped: User32 detours could not be created.");
    return false;
  }

  size_t queued = 0;
  for (; queued < hook_count; ++queued) {
    status = MH_QueueEnableHook(targets[queued]);
    if (status != MH_OK) {
      break;
    }
  }
  if (queued != hook_count || MH_ApplyQueued() != MH_OK) {
    for (size_t index = 0; index < hook_count; ++index) {
      MH_DisableHook(targets[index]);
      MH_RemoveHook(targets[index]);
    }
    log_line("Startup window hooks skipped: queued User32 detours could not be enabled.");
    return false;
  }

  log_line("Startup window creation hooks installed for ANSI and Unicode windows.");
  apply_black_background_to_existing_windows();

  if (focus_guard_enabled) {
    HANDLE input_monitor =
        CreateThread(nullptr, 0, focus_switch_input_monitor, nullptr, 0,
                     nullptr);
    if (input_monitor != nullptr) {
      CloseHandle(input_monitor);
      log_line("Startup focus keyboard-switch monitor installed.");
    } else {
      log_line("Startup focus keyboard-switch monitor could not be started.");
    }
    // MO2 or another native DLL can let Elden Ring create its first window
    // before this bridge's worker thread runs. Catch that load-order race by
    // minimizing any already-visible game window after the focus hooks exist.
    minimize_existing_game_windows();
  }

  log_line("Startup window hooks installed for creation, visibility, activation, placement, and Z-order.");
  return true;
}

DWORD WINAPI apply_cpu0_affinity_after_delay(void* context) {
  const DWORD delay_seconds =
      static_cast<DWORD>(reinterpret_cast<uintptr_t>(context));
  Sleep(delay_seconds * 1000);

  DWORD_PTR process_mask = 0;
  DWORD_PTR system_mask = 0;
  if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask,
                              &system_mask)) {
    log_line("CPU 0 affinity adjustment skipped: Windows could not read the game process affinity.");
    return 0;
  }

  constexpr DWORD_PTR logical_cpu0 = static_cast<DWORD_PTR>(1);
  if ((system_mask & logical_cpu0) == 0) {
    log_line("CPU 0 affinity adjustment skipped: logical CPU 0 is unavailable in the system mask.");
    return 0;
  }
  if ((process_mask & logical_cpu0) == 0) {
    log_line("CPU 0 affinity adjustment skipped: logical CPU 0 is already excluded.");
    return 0;
  }

  const DWORD_PTR updated_mask = process_mask & ~logical_cpu0;
  if (updated_mask == 0) {
    log_line("CPU 0 affinity adjustment skipped: no processors would remain available to the game.");
    return 0;
  }
  if (!SetProcessAffinityMask(GetCurrentProcess(), updated_mask)) {
    log_line("CPU 0 affinity adjustment failed: Windows rejected the process affinity change.");
    return 0;
  }

  log_line("CPU 0 affinity adjustment applied to the Elden Ring process.");
  return 0;
}

void schedule_cpu0_affinity_adjustment() {
  DWORD delay_seconds = 30;
  if (!read_cpu0_affinity_setting(&delay_seconds)) {
    return;
  }

  HANDLE worker = CreateThread(
      nullptr, 0, &apply_cpu0_affinity_after_delay,
      reinterpret_cast<void*>(static_cast<uintptr_t>(delay_seconds)), 0,
      nullptr);
  if (worker == nullptr) {
    log_line("CPU 0 affinity adjustment could not be scheduled: Windows could not create a worker thread.");
    return;
  }
  CloseHandle(worker);
  log_line("CPU 0 affinity adjustment scheduled after the selected delay.");
}

void open_log() {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetTempPathW(MAX_PATH, path);
  if (length == 0 || length + 32 >= MAX_PATH) {
    return;
  }

  const wchar_t filename[] = L"EldenRingMO2Bridge.log";
  for (unsigned int i = 0; filename[i] != L'\0'; ++i) {
    path[length + i] = filename[i];
    path[length + i + 1] = L'\0';
  }

  g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool is_in_executable_section(
    const unsigned char* address, const unsigned char* image_base,
    size_t image_size, const IMAGE_SECTION_HEADER* sections,
    unsigned int section_count) {
  const uintptr_t address_value = reinterpret_cast<uintptr_t>(address);
  const uintptr_t image_value = reinterpret_cast<uintptr_t>(image_base);
  if (address_value < image_value || address_value - image_value >= image_size) {
    return false;
  }

  const size_t address_rva = address_value - image_value;
  for (unsigned int i = 0; i < section_count; ++i) {
    if ((sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
        sections[i].VirtualAddress >= image_size) {
      continue;
    }

    size_t section_size = sections[i].Misc.VirtualSize;
    if (section_size == 0) {
      section_size = sections[i].SizeOfRawData;
    }
    const size_t available_size = image_size - sections[i].VirtualAddress;
    if (section_size > available_size) {
      section_size = available_size;
    }

    if (address_rva >= sections[i].VirtualAddress &&
        address_rva - sections[i].VirtualAddress < section_size) {
      return true;
    }
  }
  return false;
}

wchar_t* mutable_string(EldenRingLocalWString* value) {
  if (value == nullptr || value->capacity == 0) {
    return nullptr;
  }
  return sizeof(wchar_t) * value->capacity >= 16
             ? value->string
             : reinterpret_cast<wchar_t*>(&value->string);
}

bool has_parent_segment(const wchar_t* path) {
  const wchar_t* segment = path;
  for (const wchar_t* cursor = path;; ++cursor) {
    if (*cursor == L'\\' || *cursor == L'/' || *cursor == L'\0') {
      if (cursor - segment == 2 && segment[0] == L'.' && segment[1] == L'.') {
        return true;
      }
      if (*cursor == L'\0') {
        return false;
      }
      segment = cursor + 1;
    }
  }
}

bool is_target_asset_path(const wchar_t* path) {
  if (path == nullptr) {
    return false;
  }

  static const wchar_t filename[] = L"01_000_fe.gfx";
  constexpr size_t filename_length =
      sizeof(filename) / sizeof(filename[0]) - 1;
  for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
    if (_wcsnicmp(cursor, filename, filename_length) == 0 &&
        (cursor[filename_length] == L'\0' ||
         cursor[filename_length] == L'/' ||
         cursor[filename_length] == L'\\')) {
      return true;
    }
  }
  return false;
}

void log_target_asset(const char* message, const wchar_t* path) {
  if (InterlockedIncrement(&g_logged_target_asset_events) <= 64) {
    log_line(message, path);
  }
}

bool virtual_asset_exists(const wchar_t* archive_path) {
  wchar_t candidate[2048] = {L'.', L'\\'};
  size_t out = 2;
  while (*archive_path == L'/' || *archive_path == L'\\') {
    ++archive_path;
  }
  for (; *archive_path != L'\0'; ++archive_path) {
    if (out + 1 >= sizeof(candidate) / sizeof(candidate[0])) {
      return false;
    }
    candidate[out++] = *archive_path == L'/' ? L'\\' : *archive_path;
  }
  candidate[out] = L'\0';
  if (out == 2 || has_parent_segment(candidate + 2)) {
    return false;
  }

  // Prefer a normal read open: USVFS may not expose virtual files to an
  // attributes-only request. Some path-hook implementations also resolve
  // relative and absolute game paths differently, so retry with the absolute
  // path derived from the game's working directory.
  HANDLE file = CreateFileW(candidate, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  bool used_absolute_path = false;
  wchar_t absolute_candidate[4096] = {};
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD absolute_length = GetFullPathNameW(
        candidate, static_cast<DWORD>(sizeof(absolute_candidate) /
                                      sizeof(absolute_candidate[0])),
        absolute_candidate, nullptr);
    if (absolute_length > 0 &&
        absolute_length < sizeof(absolute_candidate) /
                              sizeof(absolute_candidate[0])) {
      file = CreateFileW(absolute_candidate, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
      used_absolute_path = file != INVALID_HANDLE_VALUE;
      if (file == INVALID_HANDLE_VALUE &&
          is_target_asset_path(absolute_candidate)) {
        log_target_asset("MO2 absolute VFS probe failed:",
                         absolute_candidate);
      }
    } else if (is_target_asset_path(candidate)) {
      log_target_asset("Could not make target asset path absolute:",
                       candidate);
    }
  }

  const bool is_target_asset = is_target_asset_path(candidate);
  if (file == INVALID_HANDLE_VALUE) {
    if (is_target_asset ||
        InterlockedIncrement(&g_logged_missing_asset_probes) <= 128) {
      log_line("MO2 VFS probe did not find loose asset:", candidate);
    }
    return false;
  }
  CloseHandle(file);

  if (is_target_asset) {
    log_target_asset(used_absolute_path
                         ? "MO2 absolute VFS probe found loose asset:"
                         : "MO2 relative VFS probe found loose asset:",
                     used_absolute_path ? absolute_candidate : candidate);
  }

  const LONG override_number = InterlockedIncrement(&g_logged_asset_overrides);
  if (is_target_asset || override_number <= 128) {
    log_line("MO2 loose asset override:", candidate);
  }
  return true;
}

// Adapts YAFSML's Elden Ring resolver patch to probe files through MO2's VFS;
// see THIRD_PARTY_NOTICES.md for the upstream source and its MIT license.
void* __cdecl map_archive_path_hook(EldenRingLocalWString* path, uint64_t p2,
                                    uint64_t p3, uint64_t p4, uint64_t p5,
                                    uint64_t p6) {
  if (g_original_archive_path == nullptr) {
    return nullptr;
  }
  wchar_t* input = mutable_string(path);
  if (is_target_asset_path(input)) {
    log_target_asset("Target asset resolver input:", input);
  }
  if (input != nullptr &&
      InterlockedIncrement(&g_logged_resolver_requests) <= 64) {
    log_line("Archive resolver request:", input);
  }
  void* result = g_original_archive_path(path, p2, p3, p4, p5, p6);
  wchar_t* value = mutable_string(path);
  if (is_target_asset_path(value)) {
    log_target_asset("Target asset resolver output:", value);
  }
  if (value == nullptr) {
    return result;
  }
  if (path->length < 7 ||
      _wcsnicmp(value, L"data", 4) != 0 || value[4] < L'0' ||
      value[4] > L'9' || value[5] != L':' || value[6] != L'/') {
    if (is_target_asset_path(value)) {
      log_target_asset("Target asset is outside the dataN path format:", value);
    }
    return result;
  }

  if (InterlockedIncrement(&g_logged_data_paths) <= 128) {
    log_line("Elden Ring data path request:", value);
  }
  if (virtual_asset_exists(value + 6)) {
    // Elden Ring treats this prefix as a loose, game-relative path. MO2's
    // USVFS then supplies the active mod's winning file without copying it to
    // the installation directory.
    memcpy(value, L"./////", 6 * sizeof(wchar_t));
  }
  return result;
}

bool install_loose_asset_hook() {
  static const unsigned char signature[] = {
      0x48, 0x83, 0x7B, 0x20, 0x08, 0x48, 0x8D, 0x4B, 0x08,
      0x72, 0x03, 0x48, 0x8B, 0x09, 0x4C, 0x8B, 0x4B, 0x18,
      0x41, 0xB8, 0x05, 0x00, 0x00, 0x00, 0x4D, 0x3B, 0xC8};
  HMODULE image = GetModuleHandleW(nullptr);
  if (image == nullptr) {
    log_line("Loose asset hook skipped: game image is unavailable.");
    return false;
  }

  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    log_line("Loose asset hook skipped: invalid game image header.");
    return false;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      reinterpret_cast<const unsigned char*>(image) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    log_line("Loose asset hook skipped: game image is not 64-bit PE.");
    return false;
  }

  const unsigned char* image_base =
      reinterpret_cast<const unsigned char*>(image);
  const size_t image_size = nt->OptionalHeader.SizeOfImage;
  const IMAGE_SECTION_HEADER* image_sections = IMAGE_FIRST_SECTION(nt);
  if (image_size == 0 || nt->FileHeader.NumberOfSections == 0) {
    log_line("Loose asset hook skipped: game executable sections are unavailable.");
    return false;
  }

  void* resolver = nullptr;
  unsigned int matches = 0;
  for (unsigned int section_index = 0;
       section_index < nt->FileHeader.NumberOfSections; ++section_index) {
    const IMAGE_SECTION_HEADER& section = image_sections[section_index];
    if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
        section.VirtualAddress >= image_size) {
      continue;
    }

    size_t section_size = section.Misc.VirtualSize;
    if (section_size == 0) {
      section_size = section.SizeOfRawData;
    }
    const size_t available_size = image_size - section.VirtualAddress;
    if (section_size > available_size) {
      section_size = available_size;
    }
    if (section_size < sizeof(signature) + 5) {
      continue;
    }

    const unsigned char* code = image_base + section.VirtualAddress;
    for (size_t offset = 5; offset + sizeof(signature) <= section_size;
         ++offset) {
      const unsigned char* match = code + offset;
      if (match[-5] != 0xE8 ||
          memcmp(match, signature, sizeof(signature)) != 0) {
        continue;
      }

      int32_t displacement = 0;
      memcpy(&displacement, match - 4, sizeof(displacement));
      auto* target = reinterpret_cast<unsigned char*>(
          reinterpret_cast<uintptr_t>(match) +
          static_cast<intptr_t>(displacement));
      if (!is_in_executable_section(target, image_base, image_size,
                                    image_sections,
                                    nt->FileHeader.NumberOfSections)) {
        continue;
      }

      unsigned int jumps = 0;
      while (target[0] == 0xE9 && jumps++ < 8) {
        int32_t jump_displacement = 0;
        memcpy(&jump_displacement, target + 1, sizeof(jump_displacement));
        target = reinterpret_cast<unsigned char*>(
            reinterpret_cast<uintptr_t>(target) + 5 +
            static_cast<intptr_t>(jump_displacement));
        if (!is_in_executable_section(target, image_base, image_size,
                                      image_sections,
                                      nt->FileHeader.NumberOfSections)) {
          target = nullptr;
          break;
        }
      }
      if (target == nullptr || jumps > 8) {
        continue;
      }
      if (resolver != nullptr && resolver != target) {
        log_line("Loose asset hook skipped: resolver signatures disagree.");
        return false;
      }
      resolver = target;
      ++matches;
    }
  }

  if (resolver == nullptr || matches == 0) {
    log_line("Loose asset hook skipped: supported archive resolver target could not be resolved in executable sections.");
    return false;
  }

  MH_STATUS status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
    log_line("Loose asset hook skipped: MinHook initialization failed.");
    return false;
  }
  status = MH_CreateHook(resolver, reinterpret_cast<void*>(&map_archive_path_hook),
                         reinterpret_cast<void**>(&g_original_archive_path));
  if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
    log_line("Loose asset hook skipped: archive resolver detour could not be created.");
    return false;
  }
  status = MH_EnableHook(resolver);
  if (status != MH_OK && status != MH_ERROR_ENABLED) {
    log_line("Loose asset hook skipped: archive resolver detour could not be enabled.");
    return false;
  }
  log_line("Elden Ring loose asset resolver hook installed for MO2.");
  return true;
}

bool has_directory_segment(const wchar_t* path, const wchar_t* directory) {
  if (path == nullptr) {
    return false;
  }

  const size_t path_length = wcslen(path);
  const size_t directory_length = wcslen(directory);
  for (size_t i = 0; i + 1 + directory_length < path_length; ++i) {
    if (path[i] != L'\\') {
      continue;
    }
    if (_wcsnicmp(path + i + 1, directory, directory_length) == 0 &&
        path[i + 1 + directory_length] == L'\\') {
      return true;
    }
  }
  return false;
}

bool has_managed_dll_path_segment(const wchar_t* path) {
  // Support MO2 DLLs and Revamped Keep Original native paths.
  return has_directory_segment(path, L"DLLs") ||
         has_directory_segment(path, L"natives") ||
         has_directory_segment(path, L"native") ||
         has_directory_segment(path, L"external_dlls") ||
         has_directory_segment(path, L"MO2_DLLs") ||
         has_directory_segment(path, L"mods");
}

bool was_seen(HMODULE module) {
  for (unsigned int i = 0; i < g_seen_count; ++i) {
    if (g_seen[i] == module) {
      return true;
    }
  }
  if (g_seen_count < sizeof(g_seen) / sizeof(g_seen[0])) {
    g_seen[g_seen_count++] = module;
    return false;
  }
  return true;
}

std::wstring normalize_module_path(std::wstring path) {
  for (wchar_t& character : path) {
    if (character == L'/') {
      character = L'\\';
    }
  }
  return path;
}

bool read_native_initializer_requests() {
  const DWORD required = GetEnvironmentVariableW(
      L"ELDENRING_MO2_NATIVE_INITIALIZERS", nullptr, 0);
  if (required == 0) {
    return true;
  }
  if (required > 32767) {
    log_line("Revamped native initializer configuration is too large; skipped.");
    return false;
  }

  std::vector<wchar_t> buffer(required);
  const DWORD length = GetEnvironmentVariableW(
      L"ELDENRING_MO2_NATIVE_INITIALIZERS", buffer.data(), required);
  if (length == 0 || length >= required) {
    log_line("Could not read the Revamped native initializer configuration.");
    return false;
  }

  const std::wstring configuration(buffer.data(), length);
  size_t line_start = 0;
  while (line_start < configuration.size()) {
    size_t line_end = configuration.find(L'\n', line_start);
    if (line_end == std::wstring::npos) {
      line_end = configuration.size();
    }
    std::wstring line = configuration.substr(line_start, line_end - line_start);
    line_start = line_end + 1;
    if (!line.empty() && line.back() == L'\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const size_t separator = line.find(L'|');
    if (separator == std::wstring::npos || separator == 0 ||
        separator + 1 >= line.size()) {
      log_line("Skipped malformed Revamped native initializer entry.");
      continue;
    }

    NativeInitializerRequest request;
    request.module_path = normalize_module_path(line.substr(0, separator));
    const std::wstring wide_function = line.substr(separator + 1);
    bool valid_function = !wide_function.empty();
    for (size_t index = 0; index < wide_function.size(); ++index) {
      const wchar_t character = wide_function[index];
      const bool letter = (character >= L'A' && character <= L'Z') ||
                          (character >= L'a' && character <= L'z');
      const bool digit = character >= L'0' && character <= L'9';
      if (character > 0x7f ||
          (index == 0 ? !(letter || character == L'_')
                      : !(letter || digit || character == L'_'))) {
        valid_function = false;
        break;
      }
      request.function_name.push_back(static_cast<char>(character));
    }
    if (!valid_function) {
      log_line("Skipped an invalid Revamped native initializer symbol.");
      continue;
    }
    g_native_initializer_requests.push_back(request);
  }

  if (!g_native_initializer_requests.empty()) {
    log_line("Loaded explicit Revamped native initializer configuration.");
  }
  return true;
}

NativeInitializerRequest* find_native_initializer(const wchar_t* module_path) {
  if (module_path == nullptr) {
    return nullptr;
  }
  const std::wstring normalized = normalize_module_path(module_path);
  for (auto& request : g_native_initializer_requests) {
    if (_wcsicmp(request.module_path.c_str(), normalized.c_str()) == 0) {
      return &request;
    }
  }
  return nullptr;
}

using NativeInitializerFunction = bool(__cdecl*)();

DWORD WINAPI run_native_initializer(void* context) {
  auto* request = static_cast<NativeInitializerRequest*>(context);
  if (request == nullptr || request->module == nullptr) {
    return 0;
  }
  const FARPROC symbol = GetProcAddress(
      request->module, request->function_name.c_str());
  if (symbol == nullptr) {
    log_line("Revamped native initializer export was not found:",
             request->module_path.c_str());
    return 0;
  }

  const auto initializer = reinterpret_cast<NativeInitializerFunction>(symbol);
  log_line("Calling Revamped native initializer:", request->module_path.c_str());
  unsigned int attempts = 0;
  for (;;) {
    if (initializer()) {
      log_line("Revamped native initializer reported ready:",
               request->module_path.c_str());
      return 0;
    }
    if (++attempts % 20 == 0) {
      log_line("Revamped native initializer is still waiting for readiness:",
               request->module_path.c_str());
    }
    Sleep(250);
  }
}

void inspect_loaded_mods() {
  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) {
    log_line("Could not enumerate process DLLs.");
    return;
  }

  MODULEENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  if (Module32FirstW(snapshot, &entry)) {
    do {
      if (entry.hModule == g_module ||
          !has_managed_dll_path_segment(entry.szExePath) ||
          was_seen(entry.hModule)) {
        continue;
      }

      log_line("Found MO2-managed DLL:", entry.szExePath);
      NativeInitializerRequest* request =
          find_native_initializer(entry.szExePath);
      if (request != nullptr) {
        request->module = entry.hModule;
        if (InterlockedCompareExchange(&request->started, 1, 0) == 0) {
          HANDLE thread = CreateThread(
              nullptr, 0, run_native_initializer, request, 0, nullptr);
          if (thread != nullptr) {
            CloseHandle(thread);
          } else {
            InterlockedExchange(&request->started, 0);
            log_line("Could not start Revamped native initializer thread:",
                     entry.szExePath);
          }
        }
        continue;
      }

      if (GetProcAddress(entry.hModule, "modengine_ext_init") == nullptr) {
        log_line("No Mod Engine 2 initializer export:", entry.szModule);
        continue;
      }
      // The Mod Engine 2 extension API needs a connector object this bridge
      // does not implement. Only explicitly configured Revamped initializers are called.
      log_line(
          "Mod Engine 2 initializer detected but skipped: full connector is unavailable:",
          entry.szModule);
    } while (Module32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
}

DWORD WINAPI bridge_worker(void*) {
  open_log();
  log_line("MO2 native bridge started.");
  log_line("MO2 native bridge version: 0.5.0-alpha.33.");
  read_native_initializer_requests();
  install_startup_window_hooks();
  wchar_t executable_path[MAX_PATH] = {};
  const DWORD executable_path_length =
      GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
  if (executable_path_length > 0 && executable_path_length < MAX_PATH) {
    log_line("MO2 bridge executable:", executable_path);
  } else {
    log_line("MO2 bridge could not read the game executable path.");
  }
  wchar_t working_directory[MAX_PATH] = {};
  const DWORD working_directory_length =
      GetCurrentDirectoryW(MAX_PATH, working_directory);
  if (working_directory_length > 0 && working_directory_length < MAX_PATH) {
    log_line("MO2 bridge working directory:", working_directory);
  }
  apply_process_priority_setting();
  schedule_cpu0_affinity_adjustment();
  install_loose_asset_hook();

  // The bridge is queued before the mod DLLs so its startup hooks can run
  // early. Keep scanning briefly to find DLLs that MO2 loads afterward.
  for (unsigned int scan = 0; scan < 40; ++scan) {
    inspect_loaded_mods();
    Sleep(250);
  }

  log_line("MO2 native bridge scan finished.");
  return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
    DisableThreadLibraryCalls(instance);
    HANDLE thread = CreateThread(nullptr, 0, bridge_worker, nullptr, 0, nullptr);
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
