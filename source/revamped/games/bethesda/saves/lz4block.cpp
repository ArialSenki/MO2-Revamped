// MO2 Revamped: Bethesda game framework, the start of an LZ4 block.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The block format is LZ4's (lz4_Block_format.md); the code is ours.
#include "lz4block.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace mo2r::bethesda
{

namespace
{
  // a length's extra bytes: each adds its value, 255 says another follows;
  // false if the input ends first or the length can't be held
  bool extendLength(std::span<const std::byte> in, std::size_t& pos, std::size_t& length)
  {
    for (;;) {
      if (pos >= in.size()) {
        return false;
      }
      const auto b = std::to_integer<std::size_t>(in[pos++]);
      if (length > SIZE_MAX - b) {
        return false;
      }
      length += b;
      if (b != 255) {
        return true;
      }
    }
  }
}  // namespace

std::optional<std::size_t> decodeLz4Prefix(std::span<const std::byte> in,
                                           std::span<std::byte> out) noexcept
{
  std::size_t ip = 0;
  std::size_t op = 0;
  while (op < out.size() && ip < in.size()) {
    const auto token = std::to_integer<unsigned>(in[ip++]);

    std::size_t literals = token >> 4;
    if (literals == 15 && !extendLength(in, ip, literals)) {
      return op;  // the input ends in a length: what came before stands
    }
    const std::size_t available = std::min(literals, in.size() - ip);
    const std::size_t copied    = std::min(available, out.size() - op);
    std::memcpy(out.data() + op, in.data() + ip, copied);
    op += copied;
    ip += available;
    if (copied < literals || ip >= in.size()) {
      // the output is full, the input ended in the literals, or this was the
      // last sequence (it has no match)
      return op;
    }

    if (in.size() - ip < 2) {
      return op;
    }
    const std::size_t offset = std::to_integer<std::size_t>(in[ip]) |
                               (std::to_integer<std::size_t>(in[ip + 1]) << 8);
    ip += 2;
    if (offset == 0 || offset > op) {
      return std::nullopt;  // a match before the output: damage
    }
    std::size_t match = (token & 0x0F);
    if (match == 15 && !extendLength(in, ip, match)) {
      return op;
    }
    match += 4;  // the minimum match
    const std::size_t count = std::min(match, out.size() - op);
    // byte by byte: a match may overlap the bytes it writes (offset < length)
    for (std::size_t i = 0; i < count; ++i, ++op) {
      out[op] = out[op - offset];
    }
  }
  return op;
}

}  // namespace mo2r::bethesda
