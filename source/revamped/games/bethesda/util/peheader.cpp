// MO2 Revamped: Bethesda game framework, the machine type of an executable.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "peheader.h"

#include "util/boundedreader.h"

#include <QFile>

namespace mo2r::bethesda
{

namespace
{
  constexpr std::uint16_t DosMagic = 0x5A4D;      // "MZ"
  constexpr std::uint32_t NtMagic  = 0x00004550;  // "PE\0\0"
  constexpr std::size_t LfanewAt   = 0x3C;
  constexpr std::size_t HeadBytes  = 4096;

  // "PE\0\0" and the machine field
  std::uint16_t machineAt(std::span<const std::byte> nt)
  {
    BoundedReader r(nt);
    const auto signature = r.read<std::uint32_t>();
    const auto machine   = r.read<std::uint16_t>();
    return r.ok() && signature == NtMagic ? machine : 0;
  }
}  // namespace

std::uint16_t peMachine(std::span<const std::byte> bytes, std::int64_t* ntOffset)
{
  BoundedReader r(bytes);
  const auto magic = r.read<std::uint16_t>();
  if (!r.ok() || magic != DosMagic || !r.skip(LfanewAt - sizeof(magic))) {
    return 0;
  }
  const auto lfanew = r.read<std::int32_t>();
  if (!r.ok() || lfanew < 0) {
    return 0;
  }
  if (ntOffset != nullptr) {
    *ntOffset = lfanew;
  }
  if (static_cast<std::size_t>(lfanew) >= bytes.size()) {
    return 0;
  }
  return machineAt(bytes.subspan(static_cast<std::size_t>(lfanew)));
}

std::uint16_t peMachineOfFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }
  const QByteArray head = file.read(HeadBytes);
  const auto bytes      = std::as_bytes(std::span(head.constData(), head.size()));
  std::int64_t ntOffset = -1;
  const auto machine    = peMachine(bytes, &ntOffset);
  if (ntOffset < 0 || ntOffset + 6 <= static_cast<std::int64_t>(head.size())) {
    return machine;
  }
  // a DOS stub longer than the first 4 KB (rare): the headers read from there
  if (!file.seek(ntOffset)) {
    return 0;
  }
  const QByteArray nt = file.read(6);
  return machineAt(std::as_bytes(std::span(nt.constData(), nt.size())));
}

}  // namespace mo2r::bethesda
