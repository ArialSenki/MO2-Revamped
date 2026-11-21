// MO2 Revamped: Bethesda game framework, Fallout 3 and New Vegas save headers.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The .fos format (UESP's "Fallout 3:Save File Format", the same for New
// Vegas): "FO3SAVEGAME", the header size, a version, then '|'-separated
// fields. New Vegas puts the game's language (a NUL-padded block ending in
// '|') before the first field; Fallout 3 doesn't. Strings are a 16-bit length, '|', the bytes and '|'. The header
// holds the screenshot's size, the save number, the character's name, title,
// level and location, the play time, the RGB screenshot and the plugin list.
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

struct Fallout3SaveInfo
{
  std::uint32_t screenshotWidth  = 0;
  std::uint32_t screenshotHeight = 0;
  std::uint32_t saveNumber       = 0;
  std::string playerName;  // UTF-8 as the game wrote it
  std::string playerTitle;
  std::uint16_t playerLevel = 0;
  std::string playerLocation;
  std::size_t end = 0;  // where the fields after the location start
};

struct Fallout3SaveData
{
  std::string playTime;
  std::span<const std::byte> screenshot;  // width * height * 3 bytes, RGB
  std::vector<std::string> plugins;
};

// screenshots above this many pixels a side are damage, not pictures
inline constexpr std::uint32_t MaxScreenshotSide = 8192;

enum class Fallout3Layout
{
  Fallout3,  // the screenshot's width right after the version
  NewVegas   // the language block first
};

// the fields up to the location; nullopt if the bytes aren't a Fallout 3/New
// Vegas save or end too early
std::optional<Fallout3SaveInfo> readFallout3SaveInfo(std::span<const std::byte> bytes,
                                                     Fallout3Layout layout);

// the play time, screenshot and plugins after the info fields (bytes is the
// same buffer, info its info); the screenshot points into bytes
std::optional<Fallout3SaveData> readFallout3SaveData(std::span<const std::byte> bytes,
                                                     const Fallout3SaveInfo& info);

// how many bytes from the start readFallout3SaveData() needs at most if no
// string after the info is longer than longestString, for a reader that has
// only the file's start (the strings' 16-bit lengths allow 64 KB each)
std::size_t fallout3SaveDataBound(const Fallout3SaveInfo& info, std::size_t longestString);

}  // namespace mo2r::bethesda
