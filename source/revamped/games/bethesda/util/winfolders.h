// MO2 Revamped: Bethesda game framework, registry values and user folders.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Nothing here throws: a missing or unreadable value is an empty string, so a
// broken registry entry can't stop MO2 from detecting the game.
#pragma once

#include <QString>

#include <Windows.h>
#include <ShlObj.h>

namespace mo2r::bethesda
{

// a REG_SZ value under root\path, looked up in the 32-bit view first and then
// the 64-bit one (Bethesda's and GOG's installers write the 32-bit one);
// empty if missing. REG_EXPAND_SZ values are not read, as in MO2's game
// plugins.
QString registryString(HKEY root, const QString& path, const QString& value);

// a known folder (FOLDERID_Documents, ...) with '/' separators; with
// defaultPath, where it would be if the user hadn't moved it. Empty if Windows
// doesn't know it.
QString knownFolder(const KNOWNFOLDERID& id, bool defaultPath = false);

// %LOCALAPPDATA% with '/' separators, where the games keep plugins.txt
QString localAppData();

// <Documents>/My Games/<folder>: the first of the user's Documents, the
// default Documents and the registry's "Personal" folder where it exists; if
// none does yet (the game never ran), the user's Documents one, where the game
// will create it
QString myGamesFolder(const QString& folder);

}  // namespace mo2r::bethesda
