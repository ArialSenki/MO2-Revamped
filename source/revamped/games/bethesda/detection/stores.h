// MO2 Revamped: Bethesda game framework, where each store installs a game.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// One function per store; each returns the game folder, or an empty string if
// that store doesn't have the game. None throws, and each attempt is logged at
// debug level so a user's log shows why a game wasn't found.
#pragma once

#include <QString>
#include <QStringList>

namespace mo2r::bethesda
{

// HKLM\<key>, value "Installed Path": written by Bethesda's installers and by
// the game's launcher on its first run (Steam, retail)
QString findBethesdaInstall(const QString& key);

// HKLM\Software\GOG.com\Games\<gogId>, value "path"
QString findGogInstall(const QString& gogId);

// the Epic Games launcher's manifests (*.item, JSON): the InstallLocation of
// the first whose AppName is one of appNames
QString findEpicInstall(const QStringList& appNames);

// <library>/steamapps/common/<folder> in the Steam library that lists appId
// in Steam's libraryfolders.vdf, if that folder exists
QString findSteamInstall(const QString& appId, const QString& folder);

// steamapps/common/<folder> in the first Steam library (Steam's own folder,
// then those in steamapps/libraryfolders.vdf) where it has validFile: MO2's
// findSteamGame, which Fallout 4's plugin used, for a library that doesn't
// list the app
QString findSteamFolder(const QString& folder, const QString& validFile);

}  // namespace mo2r::bethesda
