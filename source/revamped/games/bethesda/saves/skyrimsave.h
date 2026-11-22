// MO2 Revamped: Bethesda game framework, Skyrim Special Edition saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The .ess format (UESP's "Skyrim Mod:Save File Format", SE's changes):
// "TESV_SAVEGAME", the header size, then the header (version, save number,
// the character's name, level and location, the in-game date, race, sex and
// experience, the real time as a FILETIME, the screenshot's size and, from
// version 12, the body's compression), the screenshot (RGBA from version 12,
// RGB before) and the body. A compressed body starts with its uncompressed
// and compressed sizes; compression 1 is zlib, 2 is one LZ4 block.
//
// The body opens with its form version and the plugin lists, the only part
// MO2 reads: a body is decoded only as far as a caller allows (a few MB at
// most), never whole. Every read is bounded; a damaged or truncated save
// gives nullopt, never a read past the buffer or an allocation the file
// doesn't call for.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mo2r::bethesda
{

enum class SkyrimCompression : std::uint16_t
{
  None = 0,
  Zlib = 1,
  Lz4  = 2
};

struct SkyrimSaveInfo
{
  std::uint32_t version    = 0;
  std::uint32_t saveNumber = 0;
  std::string playerName;  // UTF-8, as the game wrote it
  std::uint32_t playerLevel = 0;
  std::string playerLocation;
  std::string gameDate;    // the in-game date and time, as text
  std::string playerRace;  // the race's editor ID ("NordRace")
  std::uint64_t fileTime = 0;  // when it was saved: a Windows FILETIME, UTC
  std::uint32_t screenshotWidth  = 0;
  std::uint32_t screenshotHeight = 0;
  SkyrimCompression compression  = SkyrimCompression::None;
  std::size_t screenshotOffset   = 0;  // where the pixels start

  bool alpha() const noexcept { return version >= 12; }  // RGBA, not RGB
  std::size_t screenshotBytes() const noexcept;
  std::size_t screenshotEnd() const noexcept { return screenshotOffset + screenshotBytes(); }
};

// the header; nullopt if the bytes aren't a Skyrim save or end too early
std::optional<SkyrimSaveInfo> readSkyrimSaveInfo(std::span<const std::byte> bytes);

// where the body is: its first byte, and for a compressed body both sizes
// (the compressed one is checked against nothing here: the caller has only
// the file's start)
struct SkyrimBodyLayout
{
  std::size_t offset           = 0;
  std::uint32_t compressedSize   = 0;
  std::uint32_t uncompressedSize = 0;
};

// nullopt if the screenshot's size is damage, the sizes don't fit in bytes or
// are beyond any save, or the compression is unknown
std::optional<SkyrimBodyLayout> readSkyrimBodyLayout(std::span<const std::byte> bytes,
                                                     const SkyrimSaveInfo& info);

// how many bytes from the file's start readSkyrimSaveData() needs to decode
// bodyLimit bytes of the body
std::size_t skyrimSaveDataBound(const SkyrimSaveInfo& info, const SkyrimBodyLayout& layout,
                                std::size_t bodyLimit);

struct SkyrimSaveData
{
  std::span<const std::byte> screenshot;  // points into the bytes given
  std::uint8_t formVersion = 0;
  std::vector<std::string> plugins;
  std::vector<std::string> lightPlugins;  // form version 78 and later
};

// the screenshot and the plugin lists, decoding at most bodyLimit bytes of
// the body; nullopt if they can't be read from bytes (a damaged body, or a
// limit or a buffer too short for them)
std::optional<SkyrimSaveData> readSkyrimSaveData(std::span<const std::byte> bytes,
                                                 const SkyrimSaveInfo& info,
                                                 const SkyrimBodyLayout& layout,
                                                 std::size_t bodyLimit);

// the plugin lists at the start of a decoded body (exposed for tests)
std::optional<SkyrimSaveData> readSkyrimBody(std::span<const std::byte> body);

}  // namespace mo2r::bethesda
