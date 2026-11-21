// MO2 Revamped: test for the plugin header cache's change time (not shipped).
// In games ordered by file dates, MO2 and other tools keep a plugin's write
// time while rewriting it; the cache then compares the NTFS change time too
// (pluginlist.cpp, statLoose()). This checks what that relies on: a rewrite of
// the same size, with its write time put back, keeps the size and write time
// and changes the change time.
// Build and run (Developer prompt):
//   cl /nologo /std:c++20 /EHsc changetime_test.cpp && changetime_test
#include <windows.h>

#include <cstdio>
#include <string>

namespace
{
struct Stat
{
  LONGLONG write = 0, change = 0, size = 0;
  bool ok = false;
};

// as statLoose() reads it: one open with FILE_READ_ATTRIBUTES
Stat statOf(const std::wstring& path)
{
  Stat s;
  HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return s;
  }
  FILE_BASIC_INFO basic{};
  LARGE_INTEGER size{};
  if (GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) &&
      GetFileSizeEx(h, &size)) {
    s.write  = basic.LastWriteTime.QuadPart;
    s.change = basic.ChangeTime.QuadPart;
    s.size   = size.QuadPart;
    s.ok     = true;
  }
  CloseHandle(h);
  return s;
}

bool writeFile(const std::wstring& path, const char* text, const FILETIME* keepWrite)
{
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const bool ok = WriteFile(h, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
  // as Wrye Bash and MO2's own load order writer do: the date put back
  if (keepWrite != nullptr) {
    SetFileTime(h, nullptr, nullptr, keepWrite);
  }
  CloseHandle(h);
  return ok;
}

int failures = 0;

void check(bool ok, const char* what)
{
  std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}
}  // namespace

int main()
{
  wchar_t dir[MAX_PATH];
  GetTempPathW(MAX_PATH, dir);
  const std::wstring path = std::wstring(dir) + L"mo2r_changetime_test.esp";

  check(writeFile(path, "TES4 header, flags 0", nullptr), "first write");
  const Stat before = statOf(path);
  check(before.ok, "stat after the first write");

  // the change time has a resolution of 100 ns but is updated lazily by some
  // file systems; a short pause keeps the test honest
  Sleep(50);

  FILETIME write;
  write.dwLowDateTime  = static_cast<DWORD>(before.write & 0xFFFFFFFF);
  write.dwHighDateTime = static_cast<DWORD>(before.write >> 32);
  check(writeFile(path, "TES4 header, flags 1", &write), "same-size rewrite, date kept");
  const Stat after = statOf(path);
  check(after.ok, "stat after the rewrite");

  check(after.size == before.size, "the size is the same (the old cache key)");
  check(after.write == before.write, "the write time is the same (the old cache key)");
  check(after.change != before.change, "the change time differs (the new cache key)");

  // nothing changed: the change time stays, so the cache still hits
  const Stat again = statOf(path);
  check(again.change == after.change, "an unchanged file keeps its change time");

  check(statOf(path + L".missing").ok == false, "a missing file isn't ok (read again)");

  DeleteFileW(path.c_str());
  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
