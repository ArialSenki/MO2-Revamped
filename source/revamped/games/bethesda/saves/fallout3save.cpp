// MO2 Revamped: Bethesda game framework, Fallout 3 and New Vegas save headers.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The field layout is UESP's; which fields MO2 shows, and the bytes it skips
// between the screenshot and the plugin list, are MO2's (game_falloutnv).
#include "fallout3save.h"

#include "util/boundedreader.h"

#include <algorithm>
#include <cstring>

namespace mo2r::bethesda
{

namespace
{
  constexpr char Magic[]               = "FO3SAVEGAME";
  constexpr std::size_t MagicSize      = sizeof(Magic) - 1;
  constexpr std::byte Separator{0x7C};  // '|'
  // the unnamed block before the first '|' (the game's language and padding)
  constexpr std::size_t MaxPreamble = 4096;
  constexpr std::size_t MaxPlugins  = 255;  // the count is one byte

  // a field followed by its '|'
  template <typename T>
  T field(BoundedReader& r)
  {
    const T value = r.read<T>();
    r.skip(1);
    return value;
  }

  // 16-bit length, '|', the bytes, '|'
  std::string string(BoundedReader& r)
  {
    const auto length = r.read<std::uint16_t>();
    r.skip(1);
    const auto bytes = r.bytes(length);
    r.skip(1);
    std::string out(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    // the game pads some names with NULs: the name ends at the first
    if (const auto nul = out.find('\0'); nul != std::string::npos) {
      out.resize(nul);
    }
    return out;
  }
}  // namespace

std::optional<Fallout3SaveInfo> readFallout3SaveInfo(std::span<const std::byte> bytes,
                                                     Fallout3Layout layout)
{
  BoundedReader r(bytes);
  const auto magic = r.bytes(MagicSize);
  if (!r.ok() || std::memcmp(magic.data(), Magic, MagicSize) != 0) {
    return std::nullopt;
  }
  r.skip(sizeof(std::uint32_t));  // header size
  r.skip(sizeof(std::uint32_t));  // version
  r.skip(1);                      // separator
  for (std::size_t i = 0; layout == Fallout3Layout::NewVegas; ++i) {
    if (i == MaxPreamble || !r.ok()) {
      return std::nullopt;
    }
    if (r.read<std::byte>() == Separator) {
      break;
    }
  }

  Fallout3SaveInfo info;
  info.screenshotWidth  = field<std::uint32_t>(r);
  info.screenshotHeight = field<std::uint32_t>(r);
  info.saveNumber       = field<std::uint32_t>(r);
  info.playerName       = string(r);
  info.playerTitle      = string(r);
  info.playerLevel      = static_cast<std::uint16_t>(field<std::int32_t>(r));
  info.playerLocation   = string(r);
  info.end              = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return info;
}

std::optional<Fallout3SaveData> readFallout3SaveData(std::span<const std::byte> bytes,
                                                     const Fallout3SaveInfo& info)
{
  const std::uint64_t w = info.screenshotWidth;
  const std::uint64_t h = info.screenshotHeight;
  if (w > MaxScreenshotSide || h > MaxScreenshotSide) {
    return std::nullopt;
  }

  BoundedReader r(bytes);
  r.skip(info.end);
  Fallout3SaveData data;
  data.playTime = string(r);
  // no picture: no pixels either (the game writes none)
  if (w != 0 && h != 0) {
    data.screenshot = r.bytes(static_cast<std::size_t>(w * h * 3));
  }
  r.skip(5);  // a separator and the plugin list's size
  const auto count = r.read<std::uint8_t>();
  r.skip(1);
  if (!r.ok()) {
    return std::nullopt;
  }
  data.plugins.reserve(std::min<std::size_t>(count, MaxPlugins));
  for (std::size_t i = 0; i < count; ++i) {
    data.plugins.push_back(string(r));
  }
  if (!r.ok()) {
    return std::nullopt;
  }
  return data;
}

std::size_t fallout3SaveDataBound(const Fallout3SaveInfo& info, std::size_t longestString)
{
  const std::uint64_t w = std::min(info.screenshotWidth, MaxScreenshotSide);
  const std::uint64_t h = std::min(info.screenshotHeight, MaxScreenshotSide);
  const std::uint64_t string = 2 + 1 + longestString + 1;
  return static_cast<std::size_t>(info.end + string + w * h * 3 + 5 + 2 +
                                  MaxPlugins * string);
}

}  // namespace mo2r::bethesda
