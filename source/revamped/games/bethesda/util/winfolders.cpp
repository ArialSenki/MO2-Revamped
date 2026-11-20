// MO2 Revamped: Bethesda game framework, registry values and user folders.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The folder lookup order is MO2's (game_gamebryo's determineMyGamesPath).
#include "winfolders.h"

#include <QDir>
#include <QFileInfo>

#include <KnownFolders.h>
#include <ShlObj.h>

#include <vector>

namespace mo2r::bethesda
{

namespace
{
  QString readValue(HKEY key, const QString& value)
  {
    const auto name = value.toStdWString();
    DWORD size      = 0;
    // REG_SZ only, as MO2's plugins read it (RRF_NOEXPAND turns a
    // REG_EXPAND_SZ value away)
    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_NOEXPAND;
    if (::RegGetValueW(key, nullptr, name.c_str(), flags, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size < sizeof(wchar_t)) {
      return {};
    }
    // a value can change between the calls: a few tries with the new size
    for (int attempt = 0; attempt < 3; ++attempt) {
      std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
      DWORD bytes  = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
      const auto r = ::RegGetValueW(key, nullptr, name.c_str(), flags, nullptr,
                                    buffer.data(), &bytes);
      if (r == ERROR_SUCCESS) {
        return QString::fromWCharArray(buffer.data());
      }
      if (r != ERROR_MORE_DATA) {
        return {};
      }
      size = bytes;
    }
    return {};
  }

  // the registry's own idea of a shell folder (User Shell Folders), expanded
  QString shellFolderFromRegistry(const QString& name)
  {
    const QString base = registryString(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
        name);
    if (base.isEmpty()) {
      return {};
    }
    const auto source = base.toStdWString();
    const DWORD n     = ::ExpandEnvironmentStringsW(source.c_str(), nullptr, 0);
    if (n == 0) {
      return base;
    }
    std::wstring expanded(n, L'\0');
    if (::ExpandEnvironmentStringsW(source.c_str(), expanded.data(), n) == 0) {
      return base;
    }
    return QString::fromWCharArray(expanded.c_str());
  }
}  // namespace

QString registryString(HKEY root, const QString& path, const QString& value)
{
  const auto subKey = path.toStdWString();
  for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE | view, &key) !=
        ERROR_SUCCESS) {
      continue;
    }
    // the first view that has the key decides, as in MO2's plugins
    const QString result = readValue(key, value);
    ::RegCloseKey(key);
    return result;
  }
  return {};
}

QString knownFolder(const KNOWNFOLDERID& id, bool defaultPath)
{
  PWSTR path = nullptr;
  QString result;
  if (::SHGetKnownFolderPath(id, defaultPath ? KF_FLAG_DEFAULT_PATH : 0, nullptr,
                             &path) == S_OK) {
    result = QDir::fromNativeSeparators(QString::fromWCharArray(path));
  }
  if (path != nullptr) {
    ::CoTaskMemFree(path);
  }
  return result;
}

QString localAppData()
{
  QString result = knownFolder(FOLDERID_LocalAppData);
  if (result.isEmpty()) {
    result = shellFolderFromRegistry("Local AppData");
  }
  return result;
}

QString myGamesFolder(const QString& folder)
{
  const QString suffix = "/My Games/" + folder;
  const QString documents = knownFolder(FOLDERID_Documents);
  for (const QString& base :
       {documents, knownFolder(FOLDERID_Documents, true), shellFolderFromRegistry("Personal")}) {
    if (!base.isEmpty() && QFileInfo::exists(base + suffix)) {
      return base + suffix;
    }
  }
  // the game never started yet: where it will make the folder, not an empty
  // path (that would be MO2's own folder, and INIs and saves would go there)
  return documents.isEmpty() ? QString() : documents + suffix;
}

}  // namespace mo2r::bethesda
