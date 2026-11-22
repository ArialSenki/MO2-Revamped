// MO2 Revamped: Bethesda game framework, the start of an LZ4 block.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Skyrim SE compresses a save's body as one LZ4 block (the block format, no
// frame). MO2 needs only the body's first bytes (the plugin lists), so this
// decoder fills the output it is given and stops: it never writes past the
// output, never reads past the input, and never copies from before the
// output's start. It is ours rather than liblz4 so that what it reads from a
// save is checked here, in a few lines a fuzzer covers whole, and so that the
// framework needs no DLL.
#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace mo2r::bethesda
{

// decodes `in` into `out` until `out` is full or `in` ends; the bytes
// written, or nullopt if the block is damaged (a match offset of 0 or before
// the output's start). A block that ends early gives the bytes it held: the
// caller tells a short body from a full one by what it can parse.
std::optional<std::size_t> decodeLz4Prefix(std::span<const std::byte> in,
                                           std::span<std::byte> out) noexcept;

// the input bytes that hold at least the first `outputBytes` bytes of a
// block's output (LZ4 grows incompressible data by under 1/255, plus the
// sequence headers)
constexpr std::size_t lz4InputBound(std::size_t outputBytes) noexcept
{
  return outputBytes + outputBytes / 255 + 64 * 1024;
}

}  // namespace mo2r::bethesda
