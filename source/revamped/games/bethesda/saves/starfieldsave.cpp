// MO2 Revamped: Bethesda game framework, Starfield saves.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The layout, and where each chunk starts, are as MO2's game_starfield (by
// Silarn) reads them.
#include "starfieldsave.h"

#include "saves/savestrings.h"
#include "util/boundedreader.h"

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cstring>

namespace mo2r::bethesda
{

namespace
{
  constexpr char ContainerMagic[] = "BCPS";
  constexpr char SaveMagic[]      = "SFS_SAVEGAME";
  // the container's header: magic, version, three offsets and the size
  constexpr std::uint64_t ContainerHeaderSize = 4 + 4 + 8 + 8 + 8 + 8;
  // decoded saves are a few hundred MB at most; sizes beyond this are damage
  constexpr std::uint64_t MaxDataSize = 4ULL * 1024 * 1024 * 1024;
  // the medium plugin list's count is 32-bit; the engine has 256 slots
  constexpr std::size_t MaxMediumPlugins = 4096;
  // each chunk starts on a 16-byte boundary of the file
  constexpr std::uint64_t ChunkAlignment = 16;

  template <std::size_t N>
  bool hasMagic(BoundedReader& r, const char (&magic)[N])
  {
    const auto bytes = r.bytes(N - 1);
    return r.ok() && std::memcmp(bytes.data(), magic, N - 1) == 0;
  }

  // a list's count, then its plugins; with Creation data after each plugin
  // that isn't the game's own
  template <typename Count>
  std::optional<std::vector<std::string>> pluginList(BoundedReader& r, std::size_t maxCount,
                                                     bool creationData,
                                                     const StarfieldCorePlugin& isCore)
  {
    const auto count = r.count<Count>(maxCount);
    if (!count) {
      return std::nullopt;
    }
    std::vector<std::string> plugins;
    plugins.reserve(std::min(*count, r.remaining() / 2));
    for (std::size_t i = 0; i < *count && r.ok(); ++i) {
      plugins.push_back(saves::wstring(r));
      if (creationData && !(isCore && isCore(plugins.back()))) {
        saves::wstring(r);  // the Creation's name
        saves::wstring(r);  // its id
        r.skip(r.read<std::uint16_t>());  // its flags
        r.skip(1);                        // is a Creation
      }
    }
    if (!r.ok()) {
      return std::nullopt;
    }
    return plugins;
  }
}  // namespace

std::optional<StarfieldContainer> readStarfieldContainer(std::span<const std::byte> file)
{
  BoundedReader r(file);
  if (!hasMagic(r, ContainerMagic)) {
    return std::nullopt;
  }
  StarfieldContainer c;
  c.version = r.read<std::uint32_t>();
  r.skip(sizeof(std::uint64_t));  // where the chunk size table is
  r.skip(sizeof(std::uint64_t));  // reserved
  c.dataOffset       = r.read<std::uint64_t>();
  c.uncompressedSize = r.read<std::uint64_t>();
  if (!r.ok() || c.dataOffset < ContainerHeaderSize || c.uncompressedSize > MaxDataSize) {
    return std::nullopt;
  }
  return c;
}

std::optional<std::size_t> inflateStarfieldData(std::span<const std::byte> file,
                                                const StarfieldContainer& container,
                                                std::span<std::byte> out) noexcept
{
  std::uint64_t pos  = container.dataOffset;
  std::size_t outPos = 0;
  while (outPos < out.size() && pos < file.size()) {
    z_stream stream{};
    // a zlib or gzip header, as the old plugin accepted
    if (inflateInit2(&stream, 15 + 32) != Z_OK) {
      break;
    }
    const auto in    = file.subspan(static_cast<std::size_t>(pos));
    const auto inLen = std::min<std::size_t>(in.size(), UINT_MAX);
    stream.next_in   = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data()));
    stream.avail_in  = static_cast<uInt>(inLen);
    bool ended       = false;
    bool damaged     = false;
    while (outPos < out.size()) {
      const auto outLen = std::min<std::size_t>(out.size() - outPos, UINT_MAX);
      stream.next_out   = reinterpret_cast<Bytef*>(out.data() + outPos);
      stream.avail_out  = static_cast<uInt>(outLen);
      const int result  = inflate(&stream, Z_NO_FLUSH);
      outPos += outLen - stream.avail_out;
      if (result == Z_STREAM_END) {
        ended = true;
        break;
      }
      if (result == Z_BUF_ERROR || (result == Z_OK && stream.avail_in == 0)) {
        break;  // the input given ran out
      }
      if (result != Z_OK) {
        damaged = true;  // Z_DATA_ERROR, Z_MEM_ERROR, Z_NEED_DICT
        break;
      }
    }
    const std::uint64_t consumed = stream.total_in;
    inflateEnd(&stream);
    if (damaged) {
      if (outPos == 0) {
        return std::nullopt;
      }
      break;
    }
    if (!ended) {
      break;  // out is full, or the file's bytes ran out
    }
    // the next chunk, on the next 16-byte boundary
    const std::uint64_t next = pos + consumed;
    pos = (next + ChunkAlignment - 1) / ChunkAlignment * ChunkAlignment;
  }
  return outPos;
}

std::optional<StarfieldSaveInfo> readStarfieldSaveInfo(std::span<const std::byte> data)
{
  BoundedReader r(data);
  if (!hasMagic(r, SaveMagic)) {
    return std::nullopt;
  }
  r.skip(sizeof(std::uint32_t));  // the header's size
  StarfieldSaveInfo info;
  info.version        = r.read<std::uint32_t>();
  info.saveVersion    = r.read<std::uint8_t>();
  info.saveNumber     = r.read<std::uint32_t>();
  info.playerName     = saves::wstring(r);
  info.playerLevel    = r.read<std::uint32_t>();
  info.playerLocation = saves::wstring(r);
  info.playTime       = saves::wstring(r);
  info.playerRace     = saves::wstring(r);
  r.skip(sizeof(std::uint16_t));  // sex
  r.skip(2 * sizeof(float));      // experience, experience for the next level
  info.fileTime = r.read<std::uint64_t>();
  info.end      = r.position();
  if (!r.ok()) {
    return std::nullopt;
  }
  return info;
}

std::optional<StarfieldSaveData> readStarfieldSaveData(std::span<const std::byte> data,
                                                       const StarfieldSaveInfo& info,
                                                       const StarfieldCorePlugin& isCore)
{
  BoundedReader r(data);
  r.skip(info.end);
  r.skip(12);  // unknown, as the old plugin skipped them
  StarfieldSaveData out;
  out.formVersion = r.read<std::uint8_t>();
  out.gameVersion = saves::wstring(r);
  saves::wstring(r);              // the game's version again
  r.skip(sizeof(std::uint32_t));  // the plugin lists' size
  if (!r.ok()) {
    return std::nullopt;
  }
  const bool creationData = info.saveVersion >= StarfieldCreationDataVersion;
  auto plugins = pluginList<std::uint8_t>(r, saves::MaxPlugins, creationData, isCore);
  if (!plugins) {
    return std::nullopt;
  }
  auto light = pluginList<std::uint16_t>(r, saves::MaxLightPlugins, creationData, isCore);
  if (!light) {
    return std::nullopt;
  }
  out.plugins      = std::move(*plugins);
  out.lightPlugins = std::move(*light);
  if (creationData) {
    auto medium = pluginList<std::uint32_t>(r, MaxMediumPlugins, creationData, isCore);
    if (!medium) {
      return std::nullopt;
    }
    out.mediumPlugins = std::move(*medium);
  }
  return out;
}

}  // namespace mo2r::bethesda
