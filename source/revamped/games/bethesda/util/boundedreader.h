// MO2 Revamped: Bethesda game framework, a reader over untrusted bytes.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Every save header, plugins.txt line and archive index the framework parses in
// C++ goes through this class. It never reads outside its buffer: each read
// checks the bytes left, each length read from the data is checked against a
// caller's limit, and a failed read leaves the reader failed, so a parser can
// read a whole header and check once at the end. No exceptions, no allocation
// beyond what a caller asks for.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace mo2r::bethesda
{

class BoundedReader
{
public:
  explicit BoundedReader(std::span<const std::byte> data) noexcept : m_data(data) {}

  BoundedReader(const void* data, std::size_t size) noexcept
      : m_data(static_cast<const std::byte*>(data), size)
  {}

  bool ok() const noexcept { return !m_failed; }
  std::size_t position() const noexcept { return m_pos; }
  std::size_t remaining() const noexcept { return m_failed ? 0 : m_data.size() - m_pos; }

  // little-endian trivially copyable values (all Bethesda formats are LE)
  template <typename T>
  T read() noexcept
  {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    if (take(sizeof(T))) {
      std::memcpy(&value, m_data.data() + m_pos - sizeof(T), sizeof(T));
    }
    return value;
  }

  bool skip(std::size_t count) noexcept { return take(count); }

  // a view of the next `count` bytes, empty and failed if there aren't enough
  std::span<const std::byte> bytes(std::size_t count) noexcept
  {
    if (!take(count)) {
      return {};
    }
    return m_data.subspan(m_pos - count, count);
  }

  // a string with a length prefix of type Length, at most maxLength bytes;
  // a trailing NUL (BZString) is dropped
  template <typename Length>
  std::string lengthPrefixed(std::size_t maxLength) noexcept
  {
    const auto length = static_cast<std::size_t>(read<Length>());
    if (!ok() || length > maxLength) {
      fail();
      return {};
    }
    const auto view = bytes(length);
    std::string out(reinterpret_cast<const char*>(view.data()), view.size());
    if (!out.empty() && out.back() == '\0') {
      out.pop_back();
    }
    return out;
  }

  // a count read from the data, refused above maxCount (a damaged file can't
  // make a parser loop or allocate for billions of entries)
  template <typename Count>
  std::optional<std::size_t> count(std::size_t maxCount) noexcept
  {
    const auto n = static_cast<std::size_t>(read<Count>());
    if (!ok() || n > maxCount) {
      fail();
      return std::nullopt;
    }
    return n;
  }

  void fail() noexcept { m_failed = true; }

private:
  bool take(std::size_t count) noexcept
  {
    if (m_failed || count > m_data.size() - m_pos) {
      m_failed = true;
      return false;
    }
    m_pos += count;
    return true;
  }

  std::span<const std::byte> m_data;
  std::size_t m_pos = 0;
  bool m_failed     = false;
};

}  // namespace mo2r::bethesda
