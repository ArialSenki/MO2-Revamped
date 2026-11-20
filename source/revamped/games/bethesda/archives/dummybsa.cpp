// MO2 Revamped: Bethesda game framework, the archive invalidation BSA.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "dummybsa.h"

#include <QFile>

namespace mo2r::bethesda
{

namespace
{
  void put32(QByteArray& out, std::uint32_t v)
  {
    for (int i = 0; i < 4; ++i) {
      out.append(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
  }

  void put64(QByteArray& out, std::uint64_t v)
  {
    put32(out, static_cast<std::uint32_t>(v));
    put32(out, static_cast<std::uint32_t>(v >> 32));
  }

  std::uint32_t hashRun(std::string_view s)
  {
    std::uint32_t h = 0;
    for (const char c : s) {
      h = h * 0x1003F + static_cast<unsigned char>(c);
    }
    return h;
  }

  constexpr std::string_view FileName = "dummy.dds";
  constexpr std::uint32_t HeaderSize  = 0x24;
}  // namespace

std::uint64_t bsaHash(std::string_view name)
{
  std::string lower(name);
  for (char& c : lower) {
    c = c == '\\' ? '/' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  const auto dot = lower.rfind('.');
  const std::string_view root =
      std::string_view(lower).substr(0, dot == std::string::npos ? lower.size() : dot);
  const std::string_view ext =
      dot == std::string::npos ? std::string_view() : std::string_view(lower).substr(dot);

  std::uint64_t hash = 0;
  const auto n       = root.size();
  if (n > 0) {
    const auto u = [&](std::size_t i) {
      return static_cast<std::uint32_t>(static_cast<unsigned char>(root[i]));
    };
    hash = u(n - 1) | (n > 2 ? u(n - 2) << 8 : 0) | static_cast<std::uint32_t>(n << 16) |
           (u(0) << 24);
  }
  if (!ext.empty()) {
    if (ext == ".kf") {
      hash |= 0x80;
    } else if (ext == ".nif") {
      hash |= 0x8000;
    } else if (ext == ".dds") {
      hash |= 0x8080;
    } else if (ext == ".wav") {
      hash |= 0x80000000;
    }
    // the characters between the first and the last two, then the extension
    const std::uint32_t middle = n > 3 ? hashRun(root.substr(1, n - 3)) : 0;
    const std::uint32_t high   = middle + hashRun(ext);
    hash |= static_cast<std::uint64_t>(high) << 32;
  }
  return hash;
}

QByteArray dummyBsa(std::uint32_t version)
{
  // one folder with an empty name holding one empty file; offsets as MO2
  // writes them
  const auto fileNamesLength = static_cast<std::uint32_t>(FileName.size() + 1);
  QByteArray out;
  out.append("BSA", 4);  // with its NUL
  put32(out, version);
  put32(out, HeaderSize);       // folder records follow the header
  put32(out, 0x01 | 0x02);      // archive flags: has folder names, has file names
  put32(out, 1);                // folders
  put32(out, 1);                // files
  put32(out, 1);                // folder names' length: the empty name's NUL
  put32(out, fileNamesLength);  // file names' length
  put32(out, 2);                // content flags: textures

  // the folder record
  put64(out, bsaHash(""));
  put32(out, 1);  // its files
  put32(out, 0x34 + fileNamesLength);

  // the folder's file block: its name, then the file record
  out.append('\0');
  put64(out, bsaHash(FileName));
  put32(out, 0);  // the file's size
  put32(out, static_cast<std::uint32_t>(0x44 + FileName.size() + 1 + 4));

  // the file names, then the file's data
  out.append(FileName.data(), static_cast<qsizetype>(FileName.size()));
  out.append('\0');
  put32(out, 0);
  return out;
}

bool writeDummyBsa(const QString& path, std::uint32_t version)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  const QByteArray bytes = dummyBsa(version);
  return file.write(bytes) == bytes.size();
}

}  // namespace mo2r::bethesda
