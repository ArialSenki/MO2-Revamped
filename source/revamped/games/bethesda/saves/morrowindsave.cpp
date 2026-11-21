// MO2 Revamped: Bethesda game framework, Morrowind saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The layout is UESP's; which fields MO2 shows, and the bytes it skips, are
// MO2's (game_morrowind, by Schilduin).
#include "morrowindsave.h"

#include "util/boundedreader.h"

#include <cstring>
#include <string_view>

namespace mo2r::bethesda
{

namespace
{
  using Tag = std::string_view;

  bool is(std::span<const std::byte> tag, Tag name)
  {
    return tag.size() == 4 && std::memcmp(tag.data(), name.data(), 4) == 0;
  }

  // a fixed-size field, up to its first NUL
  std::string fixed(BoundedReader& r, std::size_t size)
  {
    const auto bytes = r.bytes(size);
    std::string out(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (const auto nul = out.find('\0'); nul != std::string::npos) {
      out.resize(nul);
    }
    return out;
  }

  // a NAME-like subrecord's text: its length, then the bytes with a NUL last
  // (the NUL dropped)
  std::string zstring(BoundedReader& r)
  {
    const auto length = r.read<std::uint32_t>();
    const auto bytes  = r.bytes(length);
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       bytes.empty() ? 0 : bytes.size() - 1);
  }
}  // namespace

std::optional<MorrowindSaveInfo> readMorrowindSaveInfo(std::span<const std::byte> bytes)
{
  BoundedReader r(bytes);
  if (!is(r.bytes(4), "TES3")) {
    return std::nullopt;
  }
  r.skip(3 * sizeof(std::uint32_t));  // the record's size, a reserved field, its flags
  r.skip(4);                          // HEDR
  r.skip(sizeof(std::uint32_t));      // HEDR's size
  r.skip(sizeof(float));              // the format's version
  r.skip(sizeof(std::uint32_t));      // the file's type
  r.skip(32);                         // the author, empty in saves

  MorrowindSaveInfo info;
  info.saveName = fixed(r, 256);
  r.skip(sizeof(std::uint32_t));  // the save's record count

  // a MAST (the plugin's name) and a DATA (its size, for version checks) per
  // plugin; each takes 16 bytes at least, so the buffer bounds the loop
  auto tag = r.bytes(4);
  while (r.ok() && is(tag, "MAST")) {
    info.plugins.push_back(zstring(r));
    r.skip(4);  // DATA
    r.skip(r.read<std::uint32_t>());
    tag = r.bytes(4);
  }

  // GMDT
  r.skip(sizeof(std::uint32_t));  // its size
  info.playerHealth    = r.read<float>();
  info.playerMaxHealth = r.read<float>();
  r.skip(2 * sizeof(double));     // the old plugin's "current and max stamina"
  info.playerLocation = fixed(r, 64);
  info.gameDays       = r.read<float>();
  info.playerName     = fixed(r, 32);
  info.end            = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return info;
}

std::optional<MorrowindSaveData> readMorrowindSaveData(std::span<const std::byte> bytes,
                                                       const MorrowindSaveInfo& info)
{
  BoundedReader r(bytes);
  r.skip(info.end);
  r.skip(28);                     // SCRD: the screenshot's pixel format
  r.skip(4);                      // SCRS
  r.skip(sizeof(std::uint32_t));  // its size, always 65536
  MorrowindSaveData data;
  data.screenshot = r.bytes(std::size_t{MorrowindScreenshotSide} * MorrowindScreenshotSide * 4);
  if (!r.ok()) {
    return std::nullopt;
  }

  // the records up to the first NPC_ (globals, scripts, regions): a size,
  // two reserved fields and the data
  auto tag = r.bytes(4);
  while (r.ok() && !is(tag, "NPC_")) {
    r.skip(std::size_t{r.read<std::uint32_t>()} + 8);
    tag = r.bytes(4);
  }
  // the player's NPC_ (the first, in the game's saves): its NPDT's level
  while (r.ok() && is(tag, "NPC_")) {
    const std::size_t size = r.read<std::uint32_t>();
    r.skip(8);  // the reserved fields
    r.skip(4);  // NAME
    const std::size_t before = r.position();
    const std::string name   = zstring(r);
    const std::size_t taken  = 4 + (r.position() - before);  // NAME's tag, size, text
    if (!r.ok()) {
      break;
    }
    if (name == "player") {
      auto sub = r.bytes(4);
      while (r.ok() && !is(sub, "NPDT")) {
        r.skip(r.read<std::uint32_t>());
        sub = r.bytes(4);
      }
      r.skip(sizeof(std::uint32_t));  // NPDT's size
      const auto level = r.read<std::uint16_t>();
      if (r.ok()) {
        data.playerLevel = level;
      }
      break;
    }
    // another NPC_: the rest of it, then the next record
    if (size < taken) {
      break;
    }
    r.skip(size - taken);
    tag = r.bytes(4);
  }
  return data;
}

}  // namespace mo2r::bethesda
