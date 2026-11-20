// MO2 Revamped: Bethesda game framework, the games' INI files.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "inifiles.h"

#include <iprofile.h>
#include <registry.h>
#include <utility.h>

#include <QFile>
#include <QFileInfo>

#include <Windows.h>

#include <vector>

namespace mo2r::bethesda
{

QString iniValue(const QString& iniPath, const QString& section, const QString& key,
                 const QString& defaultValue, int maxLength)
{
  std::vector<wchar_t> buffer(static_cast<std::size_t>(std::max(maxLength, 1)), L'\0');
  const auto s = section.toStdWString();
  const auto k = key.toStdWString();
  const auto d = defaultValue.toStdWString();
  const auto p = iniPath.toStdWString();
  const DWORD n = ::GetPrivateProfileStringW(s.c_str(), k.c_str(), d.c_str(), buffer.data(),
                                             static_cast<DWORD>(buffer.size()), p.c_str());
  return QString::fromWCharArray(buffer.data(), static_cast<int>(n));
}

bool setIniValue(const QString& iniPath, const QString& section, const QString& key,
                 const QString& value)
{
  const auto s = section.toStdWString();
  const auto k = key.toStdWString();
  const auto v = value.toStdWString();
  const auto p = iniPath.toStdWString();
  return MOBase::WriteRegistryValue(s.c_str(), k.c_str(),
                                    value.isNull() ? nullptr : v.c_str(), p.c_str());
}

QString profileIniFolder(const MOBase::IProfile* profile, const QString& myGamesPath)
{
  return profile->localSettingsEnabled() ? profile->absolutePath()
                                         : QDir(myGamesPath).absolutePath();
}

void copyIfMissing(const QString& sourceFolder, const QDir& directory,
                   const QString& sourceName, const QString& destinationName)
{
  const QString destination = directory.absoluteFilePath(destinationName);
  if (QFileInfo::exists(destination)) {
    return;
  }
  if (!MOBase::shellCopy(sourceFolder + "/" + sourceName, destination)) {
    QFile(destination).open(QIODevice::WriteOnly);
  }
}

}  // namespace mo2r::bethesda
