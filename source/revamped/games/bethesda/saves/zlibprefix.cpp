// MO2 Revamped: Bethesda game framework, the start of a zlib stream.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "zlibprefix.h"

#include <zlib.h>

#include <algorithm>

namespace mo2r::bethesda
{

std::optional<std::size_t> inflatePrefix(std::span<const std::byte> in,
                                         std::span<std::byte> out) noexcept
{
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) {
    return std::nullopt;
  }
  // zlib counts in 32 bits: feed and drain at most 1 GB at a time
  constexpr std::size_t Step = std::size_t{1} << 30;
  std::size_t inPos          = 0;
  std::size_t outPos         = 0;
  while (outPos < out.size()) {
    const auto inStep  = std::min(in.size() - inPos, Step);
    const auto outStep = std::min(out.size() - outPos, Step);
    stream.next_in  = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data() + inPos));
    stream.avail_in = static_cast<uInt>(inStep);
    stream.next_out = reinterpret_cast<Bytef*>(out.data() + outPos);
    stream.avail_out = static_cast<uInt>(outStep);
    const int result = inflate(&stream, Z_SYNC_FLUSH);
    inPos += inStep - stream.avail_in;
    outPos += outStep - stream.avail_out;
    if (result == Z_STREAM_END || result == Z_BUF_ERROR) {
      break;  // the end, or no progress possible (the input ran out)
    }
    if (result != Z_OK) {
      inflateEnd(&stream);
      return std::nullopt;  // Z_DATA_ERROR, Z_MEM_ERROR, Z_NEED_DICT
    }
    if (inPos == in.size() && stream.avail_out != 0) {
      break;  // all the input taken, and nothing more to give
    }
  }
  inflateEnd(&stream);
  return outPos;
}

}  // namespace mo2r::bethesda
