// MO2 Revamped: Bethesda game framework, Starfield saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// A Starfield .sfs is a container ("BCPS"): its version, where the table of
// chunk sizes is, a reserved field, where the compressed data starts and the
// data's uncompressed size. The data is a run of zlib streams (chunks of 250
// KiB uncompressed each), each starting at the next 16-byte boundary of the
// file after the one before. Decoded, it starts with "SFS_SAVEGAME", the
// header's size, its version, the save's version, the save number, the
// character's name, level and location, the play time, race, sex and
// experience and the real time as a FILETIME (UTC); 12 bytes and the form
// version later come the game's version (twice), the plugin lists' size and
// the full, light and (from save version 122) medium plugin lists. From save
// version 122, each plugin but the game's own has its Creation's name and id,
// a block of flags and an "is a Creation" byte after its name.
//
// The data is decoded only as far as a caller allows, never whole. Every read
// is bounded: a damaged or truncated save gives nullopt, never a read past the
// buffer or an allocation the file doesn't call for.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mo2r::bethesda
{

struct StarfieldContainer
{
  std::uint32_t version          = 0;
  std::uint64_t dataOffset       = 0;  // the first chunk's zlib stream
  std::uint64_t uncompressedSize = 0;
};

// the container's header; nullopt if the bytes aren't a Starfield save or
// its offsets can't be right
std::optional<StarfieldContainer> readStarfieldContainer(std::span<const std::byte> file);

// decodes the chunks from the container's data offset into `out` until it is
// full, the data ends or `file` (the file's first bytes) runs out; the bytes
// written, or nullopt if a chunk is damaged before any byte of it is decoded
std::optional<std::size_t> inflateStarfieldData(std::span<const std::byte> file,
                                                const StarfieldContainer& container,
                                                std::span<std::byte> out) noexcept;

struct StarfieldSaveInfo
{
  std::uint32_t version     = 0;
  std::uint8_t saveVersion  = 0;
  std::uint32_t saveNumber  = 0;
  std::string playerName;  // UTF-8, as the game wrote it
  std::uint32_t playerLevel = 0;
  std::string playerLocation;
  std::string playTime;
  std::string playerRace;
  std::uint64_t fileTime = 0;  // when it was saved: a Windows FILETIME, UTC
  std::size_t end        = 0;  // in the decoded data
};

// the header at the start of the decoded data
std::optional<StarfieldSaveInfo> readStarfieldSaveInfo(std::span<const std::byte> data);

struct StarfieldSaveData
{
  std::uint8_t formVersion = 0;
  std::string gameVersion;
  std::vector<std::string> plugins;
  std::vector<std::string> lightPlugins;
  std::vector<std::string> mediumPlugins;  // save version 122 and later
};

// true for the game's own plugins, which have no Creation data after their
// names (MO2 asks the game's primary plugins)
using StarfieldCorePlugin = std::function<bool(std::string_view)>;

// the plugin lists after the header in the decoded data
std::optional<StarfieldSaveData> readStarfieldSaveData(std::span<const std::byte> data,
                                                       const StarfieldSaveInfo& info,
                                                       const StarfieldCorePlugin& isCore);

// the save version the plugins have Creation data from
inline constexpr std::uint8_t StarfieldCreationDataVersion = 122;

}  // namespace mo2r::bethesda
