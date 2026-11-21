// MO2 Revamped: Bethesda game framework, Fallout 4 saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The layout is UESP's; which fields MO2 shows are MO2's (game_fallout4).
#include "fallout4save.h"

#include "saves/fallout3save.h"  // MaxScreenshotSide
#include "saves/savestrings.h"
#include "util/boundedreader.h"

#include <cstring>

namespace mo2r::bethesda
{

namespace
{
  constexpr char Magic[]          = "FO4_SAVEGAME";
  constexpr std::size_t MagicSize = sizeof(Magic) - 1;
  constexpr std::uint32_t MaxHeaderSize = 512 * 1024;
  // the light plugin list came with form version 68
  constexpr std::uint8_t LightPluginsFormVersion = 68;
}  // namespace

std::size_t Fallout4SaveInfo::screenshotBytes() const noexcept
{
  if (screenshotWidth == 0 || screenshotHeight == 0 ||
      screenshotWidth > MaxScreenshotSide || screenshotHeight > MaxScreenshotSide) {
    return 0;
  }
  return std::size_t{screenshotWidth} * screenshotHeight * 4;
}

std::optional<Fallout4SaveInfo> readFallout4SaveInfo(std::span<const std::byte> bytes)
{
  BoundedReader r(bytes);
  const auto magic = r.bytes(MagicSize);
  if (!r.ok() || std::memcmp(magic.data(), Magic, MagicSize) != 0) {
    return std::nullopt;
  }
  if (r.read<std::uint32_t>() > MaxHeaderSize) {
    return std::nullopt;
  }

  Fallout4SaveInfo info;
  info.version        = r.read<std::uint32_t>();
  info.saveNumber     = r.read<std::uint32_t>();
  info.playerName     = saves::wstring(r);
  info.playerLevel    = r.read<std::uint32_t>();
  info.playerLocation = saves::wstring(r);
  info.playTime       = saves::wstring(r);
  info.playerRace     = saves::wstring(r);
  r.skip(sizeof(std::uint16_t));  // sex
  r.skip(2 * sizeof(float));      // experience, experience for the next level
  info.fileTime         = r.read<std::uint64_t>();
  info.screenshotWidth  = r.read<std::uint32_t>();
  info.screenshotHeight = r.read<std::uint32_t>();
  info.screenshotOffset = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return info;
}

std::optional<Fallout4SaveData> readFallout4SaveData(std::span<const std::byte> bytes,
                                                     const Fallout4SaveInfo& info)
{
  if (info.screenshotWidth > MaxScreenshotSide || info.screenshotHeight > MaxScreenshotSide) {
    return std::nullopt;
  }
  BoundedReader r(bytes);
  r.skip(info.screenshotOffset);
  Fallout4SaveData data;
  data.screenshot  = r.bytes(info.screenshotBytes());
  data.formVersion = r.read<std::uint8_t>();
  data.gameVersion = saves::wstring(r);
  r.skip(sizeof(std::uint32_t));  // the plugin lists' size
  const auto count = r.count<std::uint8_t>(saves::MaxPlugins);
  if (!count) {
    return std::nullopt;
  }
  data.plugins = saves::wstrings(r, *count);
  if (data.formVersion >= LightPluginsFormVersion) {
    const auto lightCount = r.count<std::uint16_t>(saves::MaxLightPlugins);
    if (!lightCount) {
      return std::nullopt;
    }
    data.lightPlugins = saves::wstrings(r, *lightCount);
  }
  if (!r.ok()) {
    return std::nullopt;
  }
  return data;
}

std::size_t fallout4SaveDataBound(const Fallout4SaveInfo& info, std::size_t listBytes)
{
  return info.screenshotOffset + info.screenshotBytes() + listBytes;
}

}  // namespace mo2r::bethesda
