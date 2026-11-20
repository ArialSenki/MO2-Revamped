// MO2 Revamped: Bethesda game framework, the archive invalidation BSA.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Oblivion, Fallout 3 and New Vegas load loose files over a BSA's only if the
// BSA is older than them; an archive listed first in SArchiveList with
// bInvalidateOlderFiles=1 makes the game prefer loose files. MO2 writes a
// tiny BSA for that with one empty "dummy.dds" in the root folder. The bytes
// here are the ones MO2 has always written (game_gamebryo's dummybsa), so a
// profile switched between the old and new plugins sees the same file; the
// format is UESP's "Oblivion Mod:BSA File Format" (versions 0x67 and 0x68).
#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string_view>

namespace mo2r::bethesda
{

// the BSA hash of a file or folder name (lower case, '/' separators)
std::uint64_t bsaHash(std::string_view name);

// the dummy archive's bytes for a BSA version
QByteArray dummyBsa(std::uint32_t version);

// writes it; false if the file can't be written
bool writeDummyBsa(const QString& path, std::uint32_t version);

}  // namespace mo2r::bethesda
