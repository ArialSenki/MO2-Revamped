// MO2 Revamped: Bethesda game framework, Oblivion saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The .ess format of Oblivion (UESP's "Oblivion Mod:Save File Format"), the
// same for Nehrim: "TES4SAVEGAME", the format's major and minor version, the
// exe's time (a SYSTEMTIME), the header's version and size, then the save
// number, the character's name (a "bstring": one length byte, then the bytes,
// a NUL among them), level and location, the game's days and ticks and the
// real time the game saved at (a SYSTEMTIME, local time). After the header:
// the screenshot's size, and if it isn't 0 its width, height and RGB pixels,
// then the plugin list (a count byte and bstrings).
//
// Every read is bounded: a damaged or truncated save gives nullopt, never a
// read past the buffer or an allocation the file doesn't hold.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mo2r::bethesda
{

// Windows' SYSTEMTIME, as the game wrote it
struct SaveSystemTime
{
  std::uint16_t year         = 0;
  std::uint16_t month        = 0;
  std::uint16_t dayOfWeek    = 0;
  std::uint16_t day          = 0;
  std::uint16_t hour         = 0;
  std::uint16_t minute       = 0;
  std::uint16_t second       = 0;
  std::uint16_t milliseconds = 0;
};

struct OblivionSaveInfo
{
  std::uint8_t majorVersion = 0;
  std::uint8_t minorVersion = 0;
  std::uint32_t saveNumber  = 0;
  std::string playerName;  // as the game wrote it (read as UTF-8, as MO2 did)
  std::uint16_t playerLevel = 0;
  std::string playerLocation;
  float gameDays          = 0;
  std::uint32_t gameTicks = 0;
  SaveSystemTime savedAt;  // the PC's local time
  std::size_t end = 0;     // where the screenshot's size is
};

struct OblivionSaveData
{
  std::uint32_t screenshotWidth  = 0;
  std::uint32_t screenshotHeight = 0;
  std::span<const std::byte> screenshot;  // width * height * 3 bytes, RGB
  std::vector<std::string> plugins;
};

// the header; nullopt if the bytes aren't an Oblivion save or end too early
std::optional<OblivionSaveInfo> readOblivionSaveInfo(std::span<const std::byte> bytes);

// the screenshot and the plugin list after the header (bytes is the same
// buffer, info its header); the screenshot points into bytes
std::optional<OblivionSaveData> readOblivionSaveData(std::span<const std::byte> bytes,
                                                     const OblivionSaveInfo& info);

}  // namespace mo2r::bethesda
