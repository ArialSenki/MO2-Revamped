// MO2 Revamped: Bethesda game framework, Skyrim Special Edition saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The layout is UESP's; which fields MO2 shows, and the plugin lists' place
// in the body, are MO2's (game_skyrimse).
#include "skyrimsave.h"

#include "saves/fallout3save.h"  // MaxScreenshotSide
#include "saves/lz4block.h"
#include "saves/savestrings.h"
#include "saves/zlibprefix.h"
#include "util/boundedreader.h"

#include <algorithm>
#include <cstring>

namespace mo2r::bethesda
{

namespace
{
  constexpr char Magic[]          = "TESV_SAVEGAME";
  constexpr std::size_t MagicSize = sizeof(Magic) - 1;
  // the header's own size: a few hundred bytes with its strings at their
  // longest (4 x 64 KB); more is damage
  constexpr std::uint32_t MaxHeaderSize = 512 * 1024;
  // bodies are tens of MB with big load orders; sizes beyond this are damage
  constexpr std::uint32_t MaxBodySize = 1024u * 1024 * 1024;
  // the light plugin list came with form version 78
  constexpr std::uint8_t LightPluginsFormVersion = 78;
}  // namespace

std::size_t SkyrimSaveInfo::screenshotBytes() const noexcept
{
  if (screenshotWidth == 0 || screenshotHeight == 0 ||
      screenshotWidth > MaxScreenshotSide || screenshotHeight > MaxScreenshotSide) {
    return 0;
  }
  return std::size_t{screenshotWidth} * screenshotHeight * (alpha() ? 4 : 3);
}

std::optional<SkyrimSaveInfo> readSkyrimSaveInfo(std::span<const std::byte> bytes)
{
  BoundedReader r(bytes);
  const auto magic = r.bytes(MagicSize);
  if (!r.ok() || std::memcmp(magic.data(), Magic, MagicSize) != 0) {
    return std::nullopt;
  }
  const auto headerSize = r.read<std::uint32_t>();
  if (headerSize > MaxHeaderSize) {
    return std::nullopt;
  }

  SkyrimSaveInfo info;
  info.version        = r.read<std::uint32_t>();
  info.saveNumber     = r.read<std::uint32_t>();
  info.playerName     = saves::wstring(r);
  info.playerLevel    = r.read<std::uint32_t>();
  info.playerLocation = saves::wstring(r);
  info.gameDate       = saves::wstring(r);
  info.playerRace     = saves::wstring(r);
  r.skip(sizeof(std::uint16_t));  // sex
  r.skip(2 * sizeof(float));      // experience, experience for the next level
  info.fileTime         = r.read<std::uint64_t>();
  info.screenshotWidth  = r.read<std::uint32_t>();
  info.screenshotHeight = r.read<std::uint32_t>();
  // Skyrim (2011) saves, version 9, have no compression field and RGB shots
  if (info.version >= 12) {
    info.compression = static_cast<SkyrimCompression>(r.read<std::uint16_t>());
  }
  info.screenshotOffset = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return info;
}

std::optional<SkyrimBodyLayout> readSkyrimBodyLayout(std::span<const std::byte> bytes,
                                                     const SkyrimSaveInfo& info)
{
  if (info.screenshotWidth > MaxScreenshotSide || info.screenshotHeight > MaxScreenshotSide) {
    return std::nullopt;
  }
  BoundedReader r(bytes);
  r.skip(info.screenshotEnd());
  SkyrimBodyLayout layout;
  switch (info.compression) {
  case SkyrimCompression::None:
    break;
  case SkyrimCompression::Zlib:
  case SkyrimCompression::Lz4:
    layout.uncompressedSize = r.read<std::uint32_t>();
    layout.compressedSize   = r.read<std::uint32_t>();
    if (layout.uncompressedSize > MaxBodySize || layout.compressedSize > MaxBodySize) {
      return std::nullopt;
    }
    break;
  default:
    return std::nullopt;
  }
  layout.offset = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return layout;
}

std::size_t skyrimSaveDataBound(const SkyrimSaveInfo& info, const SkyrimBodyLayout& layout,
                                std::size_t bodyLimit)
{
  switch (info.compression) {
  case SkyrimCompression::Zlib:
    // zlib can't grow data by more than a few bytes per 16 KB block
    return layout.offset +
           std::min<std::size_t>(layout.compressedSize, bodyLimit + bodyLimit / 1000 + 4096);
  case SkyrimCompression::Lz4:
    return layout.offset + std::min<std::size_t>(layout.compressedSize, lz4InputBound(bodyLimit));
  case SkyrimCompression::None:
  default:
    return layout.offset + bodyLimit;
  }
}

std::optional<SkyrimSaveData> readSkyrimBody(std::span<const std::byte> body)
{
  BoundedReader r(body);
  SkyrimSaveData data;
  data.formVersion = r.read<std::uint8_t>();
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

std::optional<SkyrimSaveData> readSkyrimSaveData(std::span<const std::byte> bytes,
                                                 const SkyrimSaveInfo& info,
                                                 const SkyrimBodyLayout& layout,
                                                 std::size_t bodyLimit)
{
  if (layout.offset > bytes.size() || info.screenshotEnd() > layout.offset) {
    return std::nullopt;
  }
  const auto screenshot = bytes.subspan(info.screenshotOffset, info.screenshotBytes());
  const auto stored     = bytes.subspan(layout.offset);

  std::optional<SkyrimSaveData> data;
  if (info.compression == SkyrimCompression::None) {
    data = readSkyrimBody(stored.first(std::min(stored.size(), bodyLimit)));
  } else {
    // only the start of the body, in a buffer no larger than the limit
    const auto input = stored.first(std::min<std::size_t>(stored.size(), layout.compressedSize));
    std::vector<std::byte> body(std::min<std::size_t>(layout.uncompressedSize, bodyLimit));
    const auto decoded = info.compression == SkyrimCompression::Lz4
                             ? decodeLz4Prefix(input, body)
                             : inflatePrefix(input, body);
    if (!decoded) {
      return std::nullopt;
    }
    data = readSkyrimBody(std::span(body).first(*decoded));
  }
  if (data) {
    data->screenshot = screenshot;
  }
  return data;
}

}  // namespace mo2r::bethesda
