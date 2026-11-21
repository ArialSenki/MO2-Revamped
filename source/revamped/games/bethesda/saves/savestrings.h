// MO2 Revamped: Bethesda game framework, the strings in Creation Engine saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#pragma once

#include "util/boundedreader.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mo2r::bethesda::saves
{

// a "wstring" (UESP's name: a 16-bit length, then that many bytes, no NUL);
// like the old plugins, the text ends at a NUL if the game padded one in
inline std::string wstring(BoundedReader& r)
{
  const auto length = r.read<std::uint16_t>();
  const auto bytes  = r.bytes(length);
  std::string out(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (const auto nul = out.find('\0'); nul != std::string::npos) {
    out.resize(nul);
  }
  return out;
}

// `count` wstrings; stops at the first that doesn't fit (the reader fails)
inline std::vector<std::string> wstrings(BoundedReader& r, std::size_t count)
{
  std::vector<std::string> out;
  // each takes 2 bytes at least: no reserve beyond what the data can hold
  out.reserve(std::min(count, r.remaining() / 2));
  for (std::size_t i = 0; i < count && r.ok(); ++i) {
    out.push_back(wstring(r));
  }
  return out;
}

// the plugin limits of the engine (254 full plugins and 4096 light ones);
// counts above these are damage, not load orders
inline constexpr std::size_t MaxPlugins      = 255;  // the count is one byte
inline constexpr std::size_t MaxLightPlugins = 4096;

}  // namespace mo2r::bethesda::saves
