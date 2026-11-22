// MO2 Revamped: Bethesda game framework, an Oblivion save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// What is shown, and when it's read, is as for the other games
// (bethesdasavegame.cpp); the fields are those MO2's game_oblivion showed.
#include "saves/bethesdasavegame.h"
#include "saves/oblivionsave.h"

#include <log.h>

#include <QDate>
#include <QFile>
#include <QImage>
#include <QTime>

#include <algorithm>

namespace mo2r::bethesda
{

namespace
{
  // the header's strings are 255 bytes at most: it always fits in this
  constexpr qint64 InfoBytes = 4 * 1024;
  // the screenshot (256x192 RGB from the game) and the plugin list; a save
  // with more is read whole, up to this
  constexpr qint64 UsualDataBytes = 1024 * 1024;
  constexpr qint64 MaxSaveBytes   = 512LL * 1024 * 1024;

  std::span<const std::byte> asBytes(const QByteArray& a)
  {
    return std::as_bytes(std::span(a.constData(), static_cast<std::size_t>(a.size())));
  }

  QByteArray readStart(QFile& file, qint64 bytes)
  {
    if (!file.seek(0)) {
      return {};
    }
    return file.read(std::clamp<qint64>(bytes, 0, MaxSaveBytes));
  }

  QString text(const std::string& s)
  {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
  }

  // the game's local time, as UTC; invalid if it's no time (then the file's
  // time is shown)
  QDateTime localTime(const SaveSystemTime& t)
  {
    const QDate date(t.year, t.month, t.day);
    const QTime time(t.hour, t.minute, t.second, t.milliseconds);
    if (!date.isValid() || !time.isValid()) {
      return {};
    }
    return QDateTime(date, time).toUTC();
  }

  SaveDetails readOblivionDetails(const QString& path)
  {
    SaveDetails details;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::warn("failed to open {}", path);
      return details;
    }
    QByteArray bytes = readStart(file, InfoBytes);
    const auto info  = readOblivionSaveInfo(asBytes(bytes));
    if (!info) {
      MOBase::log::warn("{}: not a readable save", path);
      return details;
    }
    bytes     = readStart(file, UsualDataBytes);
    auto data = readOblivionSaveData(asBytes(bytes), *info);
    if (!data && bytes.size() < file.size()) {
      bytes = readStart(file, file.size());
      data  = readOblivionSaveData(asBytes(bytes), *info);
    }
    if (!data) {
      MOBase::log::warn("{}: the save's plugin list can't be read", path);
      return details;
    }
    details.read = true;
    for (const std::string& plugin : data->plugins) {
      details.plugins.append(text(plugin));
    }
    // full size, as the old plugin showed it (packed RGB rows)
    const std::uint32_t w = data->screenshotWidth;
    const std::uint32_t h = data->screenshotHeight;
    if (w != 0 && h != 0 && data->screenshot.size() == std::size_t{w} * h * 3) {
      details.screenshot =
          QImage(reinterpret_cast<const uchar*>(data->screenshot.data()), static_cast<int>(w),
                 static_cast<int>(h), static_cast<qsizetype>(w) * 3, QImage::Format_RGB888)
              .copy();
    }
    return details;
  }
}  // namespace

std::shared_ptr<const BethesdaSaveGame> readOblivionSaveGame(const BethesdaGame* game,
                                                             const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    MOBase::log::error("failed to open {}", path);
    return nullptr;
  }
  const QByteArray bytes = readStart(file, InfoBytes);
  const auto info        = readOblivionSaveInfo(asBytes(bytes));
  if (!info) {
    MOBase::log::error("{}: wrong file format or damaged save", path);
    return nullptr;
  }
  SaveSummary summary;
  summary.playerName     = text(info->playerName);
  summary.playerLevel    = info->playerLevel;
  summary.playerLocation = text(info->playerLocation);
  summary.saveNumber     = info->saveNumber;
  // the game's own time rather than the file's, which a copy changes
  summary.savedAt = localTime(info->savedAt);
  return std::make_shared<const BethesdaSaveGame>(game, path, std::move(summary),
                                                  &readOblivionDetails, false);
}

}  // namespace mo2r::bethesda
