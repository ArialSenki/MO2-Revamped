#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "loglist.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"
#include <log.h>
#include <report.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>

using namespace MOBase;

thread_local LPTOP_LEVEL_EXCEPTION_FILTER g_prevExceptionFilter = nullptr;
thread_local std::terminate_handler g_prevTerminateHandler      = nullptr;

namespace
{

constexpr DWORD kStatusHeapCorruption     = 0xC0000374;
constexpr DWORD kStatusStackBufferOverrun = 0xC0000409;
constexpr size_t kDiagnosticPathCapacity  = 1024;
constexpr int kFreshProcessRestartExitCode = 0x4D4F3252;

wchar_t g_memoryDiagnosticPath[kDiagnosticPathCapacity] = {};
volatile LONG g_memoryDiagnosticWriteLock                 = 0;
volatile LONG g_memoryDiagnosticHandlersInstalled         = 0;
volatile LONG g_memoryFirstChanceCount                     = 0;

void appendText(char* buffer, size_t capacity, size_t& length,
                const char* text) noexcept
{
  while (*text != '\0' && length < capacity) {
    buffer[length++] = *text++;
  }
}

void appendUnsigned(char* buffer, size_t capacity, size_t& length,
                    unsigned long long value) noexcept
{
  char digits[24];
  size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0 && count < sizeof(digits));

  while (count > 0 && length < capacity) {
    buffer[length++] = digits[--count];
  }
}

void appendHex(char* buffer, size_t capacity, size_t& length,
               unsigned long long value) noexcept
{
  static constexpr char digits[] = "0123456789ABCDEF";
  for (int shift = 60; shift >= 0 && length < capacity; shift -= 4) {
    buffer[length++] = digits[(value >> shift) & 0x0F];
  }
}

void appendHexField(char* buffer, size_t capacity, size_t& length,
                    const char* name, unsigned long long value) noexcept
{
  appendText(buffer, capacity, length, name);
  appendText(buffer, capacity, length, "=0x");
  appendHex(buffer, capacity, length, value);
  appendText(buffer, capacity, length, "\r\n");
}

void appendDecimalField(char* buffer, size_t capacity, size_t& length,
                        const char* name, unsigned long long value) noexcept
{
  appendText(buffer, capacity, length, name);
  appendText(buffer, capacity, length, "=");
  appendUnsigned(buffer, capacity, length, value);
  appendText(buffer, capacity, length, "\r\n");
}

void appendModuleDetails(char* buffer, size_t capacity, size_t& length,
                         uintptr_t address) noexcept
{
  HMODULE module = nullptr;
  if (address == 0 ||
      !GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(address), &module)) {
    return;
  }

  wchar_t modulePath[kDiagnosticPathCapacity] = {};
  const DWORD pathLength =
      GetModuleFileNameW(module, modulePath, static_cast<DWORD>(std::size(modulePath)));
  if (pathLength == 0 || pathLength >= std::size(modulePath)) {
    return;
  }

  char utf8Path[kDiagnosticPathCapacity * 4] = {};
  const int utf8Length = WideCharToMultiByte(
      CP_UTF8, 0, modulePath, static_cast<int>(pathLength), utf8Path,
      static_cast<int>(sizeof(utf8Path) - 1), nullptr, nullptr);
  if (utf8Length <= 0) {
    return;
  }

  appendText(buffer, capacity, length, "exception_module=");
  for (int i = 0; i < utf8Length && length < capacity; ++i) {
    buffer[length++] = utf8Path[i];
  }
  appendText(buffer, capacity, length, "\r\n");
  appendHexField(buffer, capacity, length, "module_base",
                 reinterpret_cast<uintptr_t>(module));
  appendHexField(buffer, capacity, length, "module_offset",
                 address - reinterpret_cast<uintptr_t>(module));
}

void prepareMemoryDiagnosticPath() noexcept
{
  wchar_t executablePath[kDiagnosticPathCapacity] = {};
  const DWORD pathLength = GetModuleFileNameW(
      nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
  if (pathLength == 0 || pathLength >= std::size(executablePath)) {
    return;
  }

  size_t directoryLength = pathLength;
  while (directoryLength > 0 && executablePath[directoryLength - 1] != L'\\' &&
         executablePath[directoryLength - 1] != L'/') {
    --directoryLength;
  }
  if (directoryLength == 0 || directoryLength + 31 >= std::size(executablePath)) {
    return;
  }

  executablePath[directoryLength] = L'\0';
  const wchar_t logDirectory[] = L"logs";
  const size_t logDirectoryLength = std::size(logDirectory) - 1;
  CopyMemory(executablePath + directoryLength, logDirectory,
             sizeof(logDirectory));
  CreateDirectoryW(executablePath, nullptr);

  const wchar_t logFile[] = L"\\memory_diagnostics.log";
  const size_t currentLength = directoryLength + logDirectoryLength;
  if (currentLength + std::size(logFile) >= std::size(g_memoryDiagnosticPath)) {
    return;
  }

  CopyMemory(g_memoryDiagnosticPath, executablePath,
             currentLength * sizeof(wchar_t));
  CopyMemory(g_memoryDiagnosticPath + currentLength, logFile, sizeof(logFile));
}

HANDLE openMemoryDiagnosticLog() noexcept
{
  if (g_memoryDiagnosticPath[0] != L'\0') {
    HANDLE file = CreateFileW(g_memoryDiagnosticPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      return file;
    }
  }

  wchar_t temporaryPath[kDiagnosticPathCapacity] = {};
  const DWORD pathLength = GetTempPathW(
      static_cast<DWORD>(std::size(temporaryPath)), temporaryPath);
  const wchar_t fileName[] = L"ModOrganizer-memory_diagnostics.log";
  if (pathLength == 0 || pathLength + std::size(fileName) >= std::size(temporaryPath)) {
    return INVALID_HANDLE_VALUE;
  }
  CopyMemory(temporaryPath + pathLength, fileName, sizeof(fileName));
  return CreateFileW(temporaryPath, FILE_APPEND_DATA,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void writeMemoryDiagnostic(_EXCEPTION_POINTERS* exceptionInfo,
                           const char* stage) noexcept
{
  if (InterlockedCompareExchange(&g_memoryDiagnosticWriteLock, 1, 0) != 0) {
    return;
  }

  char line[4096] = {};
  size_t length   = 0;
  SYSTEMTIME time = {};
  GetSystemTime(&time);
  appendText(line, sizeof(line), length, "utc=");
  appendUnsigned(line, sizeof(line), length, time.wYear);
  appendText(line, sizeof(line), length, "-");
  if (time.wMonth < 10) appendText(line, sizeof(line), length, "0");
  appendUnsigned(line, sizeof(line), length, time.wMonth);
  appendText(line, sizeof(line), length, "-");
  if (time.wDay < 10) appendText(line, sizeof(line), length, "0");
  appendUnsigned(line, sizeof(line), length, time.wDay);
  appendText(line, sizeof(line), length, "T");
  if (time.wHour < 10) appendText(line, sizeof(line), length, "0");
  appendUnsigned(line, sizeof(line), length, time.wHour);
  appendText(line, sizeof(line), length, ":");
  if (time.wMinute < 10) appendText(line, sizeof(line), length, "0");
  appendUnsigned(line, sizeof(line), length, time.wMinute);
  appendText(line, sizeof(line), length, ":");
  if (time.wSecond < 10) appendText(line, sizeof(line), length, "0");
  appendUnsigned(line, sizeof(line), length, time.wSecond);
  appendText(line, sizeof(line), length, ".");
  if (time.wMilliseconds < 100) appendText(line, sizeof(line), length, "0");
  if (time.wMilliseconds < 10) appendText(line, sizeof(line), length, "0");
  appendUnsigned(line, sizeof(line), length, time.wMilliseconds);
  appendText(line, sizeof(line), length, "Z\r\nstage=");
  appendText(line, sizeof(line), length, stage);
  appendText(line, sizeof(line), length, "\r\n");
  appendDecimalField(line, sizeof(line), length, "thread_id", GetCurrentThreadId());

  const EXCEPTION_RECORD* record =
      exceptionInfo != nullptr ? exceptionInfo->ExceptionRecord : nullptr;
  const CONTEXT* context =
      exceptionInfo != nullptr ? exceptionInfo->ContextRecord : nullptr;
  if (record != nullptr) {
    appendHexField(line, sizeof(line), length, "exception_code",
                   record->ExceptionCode);
    appendHexField(line, sizeof(line), length, "exception_flags",
                   record->ExceptionFlags);
    appendHexField(line, sizeof(line), length, "exception_address",
                   reinterpret_cast<uintptr_t>(record->ExceptionAddress));
    appendModuleDetails(line, sizeof(line), length,
                        reinterpret_cast<uintptr_t>(record->ExceptionAddress));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2) {
      appendHexField(line, sizeof(line), length, "access_type",
                     record->ExceptionInformation[0]);
      appendHexField(line, sizeof(line), length, "access_address",
                     record->ExceptionInformation[1]);
    }
  }

  if (context != nullptr) {
#if defined(_M_X64)
    appendHexField(line, sizeof(line), length, "instruction_pointer", context->Rip);
    appendHexField(line, sizeof(line), length, "stack_pointer", context->Rsp);
    appendHexField(line, sizeof(line), length, "frame_pointer", context->Rbp);
#elif defined(_M_IX86)
    appendHexField(line, sizeof(line), length, "instruction_pointer", context->Eip);
    appendHexField(line, sizeof(line), length, "stack_pointer", context->Esp);
    appendHexField(line, sizeof(line), length, "frame_pointer", context->Ebp);
#endif
  }
  appendText(line, sizeof(line), length, "\r\n");

  const HANDLE file = openMemoryDiagnosticLog();
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
  }

  InterlockedExchange(&g_memoryDiagnosticWriteLock, 0);
}

bool launchFreshProcessAfterRestart()
{
  std::vector<wchar_t> modulePath(32768, L'\0');
  const DWORD modulePathLength = GetModuleFileNameW(
      nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
  if (modulePathLength == 0 || modulePathLength >= modulePath.size()) {
    return false;
  }

  const std::wstring executable(modulePath.data(), modulePathLength);
  std::wstring commandLine = L"\"" + executable + L"\"";
  std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
  mutableCommandLine.push_back(L'\0');

  const auto separator = executable.find_last_of(L"\\/");
  const std::wstring workingDirectory =
      separator == std::wstring::npos ? std::wstring{}
                                      : executable.substr(0, separator);

  STARTUPINFOW startupInfo{};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo{};
  if (!CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr,
                      nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                      workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                      &startupInfo, &processInfo)) {
    return false;
  }

  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return true;
}

LONG CALLBACK onFirstChanceMemoryException(_EXCEPTION_POINTERS* exceptionInfo)
{
  if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr) {
    const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    if (code == kStatusHeapCorruption || code == kStatusStackBufferOverrun ||
        code == EXCEPTION_ACCESS_VIOLATION) {
      const LONG count = InterlockedIncrement(&g_memoryFirstChanceCount);
      if (count <= 32) {
        writeMemoryDiagnostic(exceptionInfo, "first_chance");
      }
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

int run(int argc, char* argv[]);

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);

  if (r == kFreshProcessRestartExitCode) {
    if (launchFreshProcessAfterRestart()) {
      return 0;
    }

    std::cerr << "Mod Organizer could not reopen after changing instances. Win32 error: "
              << GetLastError() << '\n';
    return 1;
  }

  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  MOShared::SetThisThreadName("main");
  setExceptionHandlers();

  cl::CommandLine cl;
  if (auto r = cl.process(GetCommandLineW())) {
    return *r;
  }

  initLogging();

  // must be after logging
  TimeThis tt("main() multiprocess");

  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  MOApplication app(argc, argv);

  // check if the command line wants to run something right now
  if (auto r = cl.runPostApplication(app)) {
    return *r;
  }

  // check if there's another process running
  MOMultiProcess multiProcess(cl.multiple());

  if (multiProcess.ephemeral()) {
    // this is not the primary process

    if (cl.forwardToPrimary(multiProcess)) {
      // but there's something on the command line that could be forwarded to
      // it, so just exit
      return 0;
    }

    QMessageBox::information(
        nullptr, QObject::tr("Mod Organizer"),
        QObject::tr("An instance of Mod Organizer is already running"));

    return 1;
  }

  // check if the command line wants to run something right now
  if (auto r = cl.runPostMultiProcess(multiProcess)) {
    return *r;
  }

  tt.stop();

  // stuff that's done only once, even if MO restarts in the loop below
  app.firstTimeSetup(multiProcess);

  // force the "Select instance" dialog on startup, only for first loop or when
  // the current instance cannot be used
  bool pick = cl.pick();

  // MO runs in a loop because it can be restarted in several ways, such as
  // when switching instances or changing some settings
  for (;;) {
    try {
      auto& m = InstanceManager::singleton();

      if (cl.instance()) {
        m.overrideInstance(*cl.instance());
      }

      if (cl.profile()) {
        m.overrideProfile(*cl.profile());
      }

      // set up plugins, OrganizerCore, etc.
      {
        const auto r = app.setup(multiProcess, pick);
        pick         = false;

        if (r == RestartExitCode || r == ReselectExitCode) {
          // resets things when MO is "restarted"
          app.resetForRestart();

          // don't reprocess command line
          cl.clear();

          if (r == ReselectExitCode) {
            pick = true;
          }

          continue;
        } else if (r != 0) {
          // something failed, quit
          return r;
        }
      }

      // check if the command line wants to run something right now
      if (auto r = cl.runPostOrganizer(app.core())) {
        return *r;
      }

      // run the main window
      const auto r = app.run(multiProcess);

      if (r == RestartExitCode) {
        // Qt can leave the existing event loop in a quit state after a restart.
        // Start a clean process after this one releases its instance lock.
        return kFreshProcessRestartExitCode;
      }

      return r;
    } catch (const std::exception& e) {
      reportError(e.what());
      return 1;
    }
  }
}

LONG WINAPI onUnhandledException(_EXCEPTION_POINTERS* ptrs)
{
  writeMemoryDiagnostic(ptrs, "unhandled");

  const auto path = OrganizerCore::getGlobalCoreDumpPath();
  const auto type = OrganizerCore::getGlobalCoreDumpType();

  const auto r = env::coredump(path.empty() ? nullptr : path.c_str(), type, ptrs);

  if (r) {
    log::error("ModOrganizer has crashed, core dump created.");
  } else {
    log::error("ModOrganizer has crashed, core dump failed");
  }

  // g_prevExceptionFilter somehow sometimes point to this function, making this
  // recurse and create hundreds of core dump, not sure why
  if (g_prevExceptionFilter && ptrs && g_prevExceptionFilter != onUnhandledException)
    return g_prevExceptionFilter(ptrs);
  else
    return EXCEPTION_CONTINUE_SEARCH;
}

void onTerminate() noexcept
{
  __try {
    // force an exception to get a valid stack trace for this thread
    *(int*)0 = 42;
  } __except (onUnhandledException(GetExceptionInformation()),
              EXCEPTION_EXECUTE_HANDLER) {
  }

  if (g_prevTerminateHandler) {
    g_prevTerminateHandler();
  } else {
    std::abort();
  }
}

void setExceptionHandlers()
{
  if (InterlockedCompareExchange(&g_memoryDiagnosticHandlersInstalled, 1, 0) == 0) {
    prepareMemoryDiagnosticPath();
    AddVectoredExceptionHandler(1, onFirstChanceMemoryException);
  }

  if (g_prevExceptionFilter) {
    // already called
    return;
  }

  g_prevExceptionFilter  = SetUnhandledExceptionFilter(onUnhandledException);
  g_prevTerminateHandler = std::set_terminate(onTerminate);
}
