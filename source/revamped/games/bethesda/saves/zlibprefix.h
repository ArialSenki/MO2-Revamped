// MO2 Revamped: Bethesda game framework, the start of a zlib stream.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Skyrim SE can compress a save's body with zlib instead of LZ4 (its INI's
// uiCompression). Like decodeLz4Prefix(), this fills the output it is given
// and stops, with zlib's own bounds (the static zlib the old plugins link).
#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace mo2r::bethesda
{

// inflates `in` into `out` until `out` is full or the stream (or `in`) ends;
// the bytes written, or nullopt if the stream is damaged
std::optional<std::size_t> inflatePrefix(std::span<const std::byte> in,
                                         std::span<std::byte> out) noexcept;

}  // namespace mo2r::bethesda
