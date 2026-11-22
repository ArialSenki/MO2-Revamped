// MO2 Revamped: shared bits of the uibase unit tests (not shipped).
// A CHECK macro, a pass/fail count and a non-zero exit on failure, as the
// other standalone tests in source/revamped/tests do. Build and run them with
// run.ps1 in this folder.
//
// Every test takes an optional "--out <file>": its output then goes to that
// file, which run.ps1 reads when the test runs on the private desktop.
#pragma once

#include <windows.h>

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <cstdio>
#include <cstring>
#include <string>

namespace uitest
{
inline int passed   = 0;
inline int failures = 0;

inline void redirect(int argc, char** argv)
{
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--out") == 0) {
      FILE* f = nullptr;
      freopen_s(&f, argv[i + 1], "w", stdout);
      std::setvbuf(stdout, nullptr, _IONBF, 0);
    }
  }
}

inline int finish(const char* name)
{
  std::printf("%s: %d passed, %d failed\n", name, passed, failures);
  std::fflush(stdout);
  return failures == 0 ? 0 : 1;
}

inline std::string show(const QString& s)
{
  return s.toStdString();
}

// the folder of the test exe (run.ps1's output folder)
inline QString exeDir()
{
  wchar_t path[MAX_PATH * 4] = {};
  GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  return QFileInfo(QString::fromWCharArray(path)).absolutePath();
}

// a fresh folder next to the test exe for the files a test writes
inline QString scratchDir(const QString& name)
{
  QDir dir(exeDir() + "/work/" + name);
  dir.removeRecursively();
  QDir().mkpath(dir.path());
  return dir.path();
}
}  // namespace uitest

#define CHECK(x)                                                                       \
  do {                                                                                 \
    if (x) {                                                                           \
      ++uitest::passed;                                                                \
    } else {                                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                         \
      ++uitest::failures;                                                              \
    }                                                                                  \
  } while (0)

// QString comparison that prints both sides on failure
#define CHECK_STR(a, b)                                                                \
  do {                                                                                 \
    const QString check_a_ = (a);                                                      \
    const QString check_b_ = (b);                                                      \
    if (check_a_ == check_b_) {                                                        \
      ++uitest::passed;                                                                \
    } else {                                                                           \
      std::printf("FAIL %s:%d: %s == %s\n  got \"%s\"\n  expected \"%s\"\n", __FILE__, \
                  __LINE__, #a, #b, check_a_.toUtf8().constData(),                     \
                  check_b_.toUtf8().constData());                                      \
      ++uitest::failures;                                                              \
    }                                                                                  \
  } while (0)
