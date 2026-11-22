// MO2 Revamped: Bethesda game framework, Oblivion saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The layout is UESP's; which fields MO2 shows are MO2's (game_oblivion).
#include "oblivionsave.h"

#include "saves/fallout3save.h"  // MaxScreenshotSide
#include "saves/savestrings.h"   // MaxPlugins
#include "util/boundedreader.h"

#include <algorithm>
#include <cstring>

namespace mo2r::bethesda
{

namespace
{
  constexpr char Magic[]          = "TES4SAVEGAME";
  constexpr std::size_t MagicSize = sizeof(Magic) - 1;

  // one length byte, then the bytes; the text ends at the first NUL (the
  // game writes one in), as the old plugin read it
  std::string bstring(BoundedReader& r)
  {
    const auto length = r.read<std::uint8_t>();
    const auto bytes  = r.bytes(length);
    std::string out(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (const auto nul = out.find('\0'); nul != std::string::npos) {
      out.resize(nul);
    }
    return out;
  }

  SaveSystemTime systemTime(BoundedReader& r)
  {
    SaveSystemTime t;
    t.year         = r.read<std::uint16_t>();
    t.month        = r.read<std::uint16_t>();
    t.dayOfWeek    = r.read<std::uint16_t>();
    t.day          = r.read<std::uint16_t>();
    t.hour         = r.read<std::uint16_t>();
    t.minute       = r.read<std::uint16_t>();
    t.second       = r.read<std::uint16_t>();
    t.milliseconds = r.read<std::uint16_t>();
    return t;
  }
}  // namespace

std::optional<OblivionSaveInfo> readOblivionSaveInfo(std::span<const std::byte> bytes)
{
  BoundedReader r(bytes);
  const auto magic = r.bytes(MagicSize);
  if (!r.ok() || std::memcmp(magic.data(), Magic, MagicSize) != 0) {
    return std::nullopt;
  }
  OblivionSaveInfo info;
  info.majorVersion = r.read<std::uint8_t>();
  info.minorVersion = r.read<std::uint8_t>();
  systemTime(r);                  // the exe's last change
  r.skip(sizeof(std::uint32_t));  // header version
  r.skip(sizeof(std::uint32_t));  // header size
  info.saveNumber     = r.read<std::uint32_t>();
  info.playerName     = bstring(r);
  info.playerLevel    = r.read<std::uint16_t>();
  info.playerLocation = bstring(r);
  info.gameDays       = r.read<float>();
  info.gameTicks      = r.read<std::uint32_t>();
  info.savedAt        = systemTime(r);
  info.end            = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return info;
}

std::optional<OblivionSaveData> readOblivionSaveData(std::span<const std::byte> bytes,
                                                     const OblivionSaveInfo& info)
{
  BoundedReader r(bytes);
  r.skip(info.end);
  OblivionSaveData data;
  // the size covers the width, height and pixels; 0 is no screenshot at all
  if (r.read<std::uint32_t>() != 0) {
    data.screenshotWidth  = r.read<std::uint32_t>();
    data.screenshotHeight = r.read<std::uint32_t>();
    if (data.screenshotWidth > MaxScreenshotSide || data.screenshotHeight > MaxScreenshotSide) {
      return std::nullopt;
    }
    data.screenshot = r.bytes(std::size_t{data.screenshotWidth} * data.screenshotHeight * 3);
  }
  const auto count = r.count<std::uint8_t>(saves::MaxPlugins);
  if (!count) {
    return std::nullopt;
  }
  data.plugins.reserve(std::min(*count, r.remaining()));
  for (std::size_t i = 0; i < *count && r.ok(); ++i) {
    data.plugins.push_back(bstring(r));
  }
  if (!r.ok()) {
    return std::nullopt;
  }
  return data;
}

}  // namespace mo2r::bethesda
