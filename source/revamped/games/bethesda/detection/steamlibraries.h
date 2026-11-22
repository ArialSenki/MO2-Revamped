// MO2 Revamped: Bethesda game framework, Steam library detection.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Reads Steam's libraryfolders.vdf (KeyValues text) to find which library has
// an app installed. Written from Valve's KeyValues format: quoted keys and
// values, nested { } blocks, // comments, backslash escapes in strings.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mo2r::bethesda
{

struct SteamLibrary
{
  std::string path;               // as written in the file, escapes resolved
  std::vector<std::string> apps;  // app IDs listed under "apps"
};

// the libraries in a libraryfolders.vdf text; empty if it can't be parsed.
// Never throws, and stops at a nesting depth of 16.
std::vector<SteamLibrary> parseSteamLibraries(std::string_view vdf);

// the library path that lists appId, if any
std::optional<std::string> steamLibraryForApp(const std::vector<SteamLibrary>& libraries,
                                              std::string_view appId);

}  // namespace mo2r::bethesda
