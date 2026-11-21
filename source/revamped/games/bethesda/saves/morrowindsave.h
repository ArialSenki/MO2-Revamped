// MO2 Revamped: Bethesda game framework, Morrowind saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// A Morrowind .ess is a plugin file (UESP's "Morrowind Mod:Save File
// Format"): a TES3 record whose HEDR holds the save's description (the name
// the player gave it), then a MAST and a DATA subrecord per plugin the save
// uses, then GMDT with the character's health, the cell's name, the game's
// days and the character's name. Records follow: SCRD and SCRS (the 128x128
// BGRA screenshot), the globals, scripts and regions, and the NPC_ records;
// the player's level is in the NPDT of the NPC_ named "player".
//
// Strings are fixed-size, NUL-padded Latin-1 fields (the game's own code
// page), kept here as their bytes. Every read is bounded: a damaged or
// truncated save gives nullopt, never a read past the buffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mo2r::bethesda
{

struct MorrowindSaveInfo
{
  std::string saveName;  // HEDR's description, up to its first NUL
  std::vector<std::string> plugins;
  float playerHealth    = 0;
  float playerMaxHealth = 0;
  std::string playerLocation;  // GMDT's cell name, up to its first NUL
  float gameDays = 0;
  std::string playerName;
  std::size_t end = 0;  // where GMDT ends
};

inline constexpr std::uint32_t MorrowindScreenshotSide = 128;

struct MorrowindSaveData
{
  // 128 * 128 * 4 bytes, BGRA, the alpha inverted
  std::span<const std::byte> screenshot;
  // the player's level; nullopt if the player's NPC_ record isn't found
  std::optional<std::uint16_t> playerLevel;
};

// the header up to GMDT; nullopt if the bytes aren't a Morrowind save or end
// too early
std::optional<MorrowindSaveInfo> readMorrowindSaveInfo(std::span<const std::byte> bytes);

// the screenshot and the player's level after GMDT (bytes is the same
// buffer, info its info); nullopt if the screenshot isn't there
std::optional<MorrowindSaveData> readMorrowindSaveData(std::span<const std::byte> bytes,
                                                       const MorrowindSaveInfo& info);

}  // namespace mo2r::bethesda
