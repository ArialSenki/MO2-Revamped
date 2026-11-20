// MO2 Revamped: Bethesda game framework, the machine type of an executable.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// From the PE format (Microsoft's "PE Format" documentation): the DOS header's
// e_lfanew at 0x3C points at "PE\0\0", followed by the COFF file header whose
// first field is the machine (0x14c x86, 0x8664 x64).
#pragma once

#include <QString>

#include <cstdint>
#include <span>

namespace mo2r::bethesda
{

// the machine field of a PE image held in bytes (the file's start, or all of
// it); 0 if it isn't one or the headers are past the bytes given. *ntOffset
// gets e_lfanew when the DOS header could be read, so a caller with only the
// file's start can read the headers from there.
std::uint16_t peMachine(std::span<const std::byte> bytes, std::int64_t* ntOffset = nullptr);

// the machine field of the executable at path, reading only its headers; 0 if
// it can't be read or isn't a PE image
std::uint16_t peMachineOfFile(const QString& path);

}  // namespace mo2r::bethesda
