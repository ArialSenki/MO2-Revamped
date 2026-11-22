// MO2 Revamped: Bethesda game framework, script extender checks.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// What a game plugin's diagnose warns about: a script extender that doesn't
// match the game version, and an Address Library left behind by a game
// update. Both only speak up with proof, so a list that doesn't use either
// never gets a warning (MO2 Revamped 3895161).
#pragma once

#include <QString>

namespace mo2r::bethesda
{

class BethesdaGame;

// the first parts of a version joined: ("1.6.1170.0", 3, "_") is "1_6_1170",
// missing parts are 0; empty for an unknown version (empty or MO2's 1.0.0
// placeholder)
QString joinedVersion(const QString& version, int parts, const QString& separator);

// the extender's loader is in the game folder but not its runtime for this
// game version (runtimePrefix + version + ".dll", the name the loader looks
// for), neither there nor in an active mod's Root/ folder (Root Builder, MO2
// Revamped's Root/ mapping), so the game would start without it. The runtime
// expected goes to *expected.
bool extenderRuntimeMissing(const BethesdaGame* game, QString* expected);

// extender plugins (DLLs in the addressLibraryFolder of the virtual Data
// folder) and an Address Library for other game versions, but none for this
// one: the game was updated past it. The file expected goes to *expected.
bool addressLibraryMissing(const BethesdaGame* game, QString* expected);

}  // namespace mo2r::bethesda
