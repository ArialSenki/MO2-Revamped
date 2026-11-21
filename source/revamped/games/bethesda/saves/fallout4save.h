// MO2 Revamped: Bethesda game framework, Fallout 4 saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The .fos format of Fallout 4 (UESP's "Fallout 4 Mod:Save File Format"):
// "FO4_SAVEGAME", the header size, then the header (version, save number,
// the character's name, level and location, the play time, race, sex and
// experience, the real time as a FILETIME and the screenshot's size), the
// RGBA screenshot, then, uncompressed, the form version, the game's version,
// the plugin lists' size and the lists (light plugins from form version 68).
//
// Every read is bounded: a damaged or truncated save gives nullopt, never a
// read past the buffer or an allocation the file doesn't call for.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mo2r::bethesda
{

struct Fallout4SaveInfo
{
  std::uint32_t version    = 0;
  std::uint32_t saveNumber = 0;
  std::string playerName;  // UTF-8, as the game wrote it
  std::uint32_t playerLevel = 0;
  std::string playerLocation;
  std::string playTime;    // "0d.0h.12m.0 days.0 hours.12 minutes"
  std::string playerRace;  // the race's editor ID ("HumanRace")
  std::uint64_t fileTime = 0;  // when it was saved: a Windows FILETIME, UTC
  std::uint32_t screenshotWidth  = 0;
  std::uint32_t screenshotHeight = 0;
  std::size_t screenshotOffset   = 0;  // where the RGBA pixels start

  // 0 for no picture or a damaged size
  std::size_t screenshotBytes() const noexcept;
};

// the header; nullopt if the bytes aren't a Fallout 4 save or end too early
std::optional<Fallout4SaveInfo> readFallout4SaveInfo(std::span<const std::byte> bytes);

struct Fallout4SaveData
{
  std::span<const std::byte> screenshot;  // width * height * 4 bytes, RGBA
  std::uint8_t formVersion = 0;
  std::string gameVersion;  // "1.10.163.0"
  std::vector<std::string> plugins;
  std::vector<std::string> lightPlugins;
};

// the screenshot and plugin lists after the header (bytes is the file's
// start, info its header); nullopt if they don't fit in bytes or are damaged
std::optional<Fallout4SaveData> readFallout4SaveData(std::span<const std::byte> bytes,
                                                     const Fallout4SaveInfo& info);

// how many bytes from the start readFallout4SaveData() needs if the plugin
// lists and the version before them take at most listBytes
std::size_t fallout4SaveDataBound(const Fallout4SaveInfo& info, std::size_t listBytes);

}  // namespace mo2r::bethesda
