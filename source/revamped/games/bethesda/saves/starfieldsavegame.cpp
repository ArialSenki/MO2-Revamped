// MO2 Revamped: Bethesda game framework, a Starfield save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// What is shown, and when it's read, is as for the other games
// (bethesdasavegame.cpp); the fields are those MO2's game_starfield showed
// (no screenshot: the game keeps it in a file of its own).
#include "saves/bethesdasavegame.h"
#include "saves/starfieldsave.h"

#include "core/bethesdagame.h"

#include <log.h>

#include <QFile>
#include <QTimeZone>

#include <algorithm>
#include <vector>

namespace mo2r::bethesda
{

namespace
{
  constexpr qint64 MaxSaveBytes = 512LL * 1024 * 1024;
  // decoded bytes: the header takes a few hundred (its strings can be 64 KB
  // each), the plugin lists a few hundred KB with a big load order
  constexpr std::size_t InfoBytes[] = {4 * 1024, 256 * 1024};
  constexpr std::size_t ListBytes[] = {1024 * 1024, 16 * 1024 * 1024};
  // the container's header
  constexpr qint64 ContainerBytes = 64;

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

  QStringList texts(const std::vector<std::string>& list)
  {
    QStringList out;
    out.reserve(static_cast<qsizetype>(list.size()));
    for (const std::string& s : list) {
      out.append(text(s));
    }
    return out;
  }

  QDateTime fromFileTime(std::uint64_t fileTime)
  {
    constexpr std::uint64_t UnixEpoch = 116444736000000000ULL;
    if (fileTime <= UnixEpoch) {
      return {};
    }
    const auto ms  = static_cast<qint64>((fileTime - UnixEpoch) / 10000);
    QDateTime time = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
    if (!time.isValid() || time.date().year() > 9999) {
      return {};
    }
    return time;
  }

  // the first `limit` bytes of the save's data (fewer if it is shorter),
  // from as much of the file as they can take compressed; empty if the
  // container is damaged
  std::vector<std::byte> decodeStart(QFile& file, std::size_t limit)
  {
    const auto container = readStarfieldContainer(asBytes(readStart(file, ContainerBytes)));
    if (!container) {
      return {};
    }
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(container->uncompressedSize, limit));
    // zlib grows nothing by more than a few bytes per 16 KB, and each chunk
    // of 250 KiB adds its header and up to 15 bytes of padding
    const std::uint64_t input = container->dataOffset + want + want / 1000 +
                                (want / (250 * 1024) + 2) * 64 + 4096;
    const QByteArray bytes =
        readStart(file, static_cast<qint64>(std::min<std::uint64_t>(input, MaxSaveBytes)));
    std::vector<std::byte> out(want);
    const auto decoded = inflateStarfieldData(asBytes(bytes), *container, out);
    out.resize(decoded.value_or(0));
    return out;
  }

  SaveDetails readStarfieldDetails(const BethesdaGame* game, const QString& path)
  {
    SaveDetails details;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::warn("failed to open {}", path);
      return details;
    }
    // the game's own plugins have no Creation data in the lists
    const QStringList core = game->primaryPlugins();
    const StarfieldCorePlugin isCore = [&core](std::string_view name) {
      return core.contains(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())),
                           Qt::CaseInsensitive);
    };
    std::optional<StarfieldSaveData> data;
    for (const std::size_t limit : ListBytes) {
      const auto body = decodeStart(file, limit);
      if (const auto info = readStarfieldSaveInfo(body)) {
        data = readStarfieldSaveData(body, *info, isCore);
      }
      if (data || body.size() < limit) {
        break;
      }
    }
    if (!data) {
      MOBase::log::warn("{}: the save's plugin lists can't be read", path);
      return details;
    }
    details.read          = true;
    details.plugins       = texts(data->plugins);
    details.lightPlugins  = texts(data->lightPlugins);
    details.mediumPlugins = texts(data->mediumPlugins);
    return details;
  }
}  // namespace

std::shared_ptr<const BethesdaSaveGame> readStarfieldSaveGame(const BethesdaGame* game,
                                                              const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    MOBase::log::error("failed to open {}", path);
    return nullptr;
  }
  std::optional<StarfieldSaveInfo> info;
  for (const std::size_t limit : InfoBytes) {
    const auto body = decodeStart(file, limit);
    info            = readStarfieldSaveInfo(body);
    if (info || body.size() < limit) {
      break;
    }
  }
  if (!info) {
    MOBase::log::error("{}: wrong file format or damaged save", path);
    return nullptr;
  }
  SaveSummary summary;
  summary.playerName     = text(info->playerName);
  summary.playerLevel    = static_cast<unsigned short>(info->playerLevel);
  summary.playerLocation = text(info->playerLocation);
  summary.saveNumber     = info->saveNumber;
  summary.savedAt        = fromFileTime(info->fileTime);
  return std::make_shared<const BethesdaSaveGame>(
      game, path, std::move(summary),
      [game](const QString& p) {
        return readStarfieldDetails(game, p);
      },
      true, true);
}

}  // namespace mo2r::bethesda
