// MO2 Revamped: Bethesda game framework, the games' INI files.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The games read their INIs with GetPrivateProfileString, so the framework
// reads them the same way; writes go through MO2's WriteRegistryValue (it
// clears a read-only flag first, as MO2 always did).
#pragma once

#include <QDir>
#include <QString>

namespace MOBase
{
class IProfile;
}

namespace mo2r::bethesda
{

// a value of an INI, defaultValue if missing; at most maxLength characters
QString iniValue(const QString& iniPath, const QString& section, const QString& key,
                 const QString& defaultValue, int maxLength = 260);

// writes a value; false on failure
bool setIniValue(const QString& iniPath, const QString& section, const QString& key,
                 const QString& value);

// removes a key; false on failure
bool removeIniValue(const QString& iniPath, const QString& section, const QString& key);

// the folder with the INIs a profile uses: its own with local settings,
// otherwise the game's My Games folder
QString profileIniFolder(const MOBase::IProfile* profile, const QString& myGamesPath);

// copies sourceFolder/sourceName to directory/destinationName unless that
// exists; if the copy fails, an empty file is made there instead (MO2's
// profile setup expects every file to exist afterwards)
void copyIfMissing(const QString& sourceFolder, const QDir& directory,
                   const QString& sourceName, const QString& destinationName);

}  // namespace mo2r::bethesda
