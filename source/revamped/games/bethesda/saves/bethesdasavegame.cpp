// MO2 Revamped: Bethesda game framework, a save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "bethesdasavegame.h"

#include "core/bethesdagame.h"
#include "saves/fallout3save.h"
#include "saves/fallout4save.h"
#include "saves/skyrimsave.h"

#include <igamefeatures.h>
#include <imoinfo.h>
#include <log.h>
#include <scriptextender.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>

namespace mo2r::bethesda
{

namespace
{
  // the start of a save holds every field the list shows; its strings can be
  // up to 64 KB each, so a save with longer ones is read again further
  constexpr qint64 InfoBytes    = 4 * 1024;
  constexpr qint64 MaxInfoBytes = 256 * 1024;
  // plugin names and the play time are short; a save with longer ones is read
  // whole (up to this, far above any real save)
  constexpr std::size_t UsualStringLength = 512;
  constexpr qint64 MaxSaveBytes           = 512LL * 1024 * 1024;
  // Skyrim SE and Fallout 4: the plugin lists are read from this much of the
  // body (or of the bytes after the screenshot) first, then from the larger
  // amount if they're longer; real lists take a few hundred KB at most
  constexpr std::size_t UsualListBytes = 1024 * 1024;
  constexpr std::size_t MaxListBytes   = 16 * 1024 * 1024;

  std::span<const std::byte> asBytes(const QByteArray& a)
  {
    return std::as_bytes(std::span(a.constData(), static_cast<std::size_t>(a.size())));
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

  QByteArray readStart(QFile& file, qint64 bytes)
  {
    if (!file.seek(0)) {
      return {};
    }
    return file.read(std::clamp<qint64>(bytes, 0, MaxSaveBytes));
  }

  qint64 asOffset(std::size_t n)
  {
    return static_cast<qint64>(std::min<std::size_t>(n, MaxSaveBytes));
  }

  // the header from the file's start, read again further if its strings are
  // long; bytes keeps what was read
  template <typename Parse>
  auto readHeader(QFile& file, QByteArray& bytes, Parse parse)
  {
    bytes     = readStart(file, InfoBytes);
    auto info = parse(asBytes(bytes));
    if (!info && bytes.size() < file.size()) {
      bytes = readStart(file, MaxInfoBytes);
      info  = parse(asBytes(bytes));
    }
    return info;
  }

  // a FILETIME (100 ns since 1601, UTC) as a time; invalid if it's no time
  // the game could have written
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

  // packed rows (width * bytes per pixel each), copied out of the buffer
  QImage screenshot(std::span<const std::byte> pixels, std::uint32_t width,
                    std::uint32_t height, bool alpha, int scaledWidth)
  {
    const std::size_t bpp = alpha ? 4 : 3;
    if (pixels.empty() || pixels.size() != std::size_t{width} * height * bpp) {
      return {};
    }
    const QImage image(reinterpret_cast<const uchar*>(pixels.data()), static_cast<int>(width),
                       static_cast<int>(height), static_cast<qsizetype>(width * bpp),
                       alpha ? QImage::Format_RGBA8888_Premultiplied
                             : QImage::Format_RGB888);
    return image.copy().scaledToWidth(scaledWidth);
  }

  template <typename Info>
  SaveSummary summaryOf(const Info& info)
  {
    SaveSummary summary;
    summary.playerName     = text(info.playerName);
    summary.playerLevel    = static_cast<unsigned short>(info.playerLevel);
    summary.playerLocation = text(info.playerLocation);
    summary.saveNumber     = info.saveNumber;
    return summary;
  }

  // --- Fallout 3, New Vegas ---

  SaveDetails readFallout3Details(const QString& path, Fallout3Layout layout)
  {
    SaveDetails details;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::warn("failed to open {}", path);
      return details;
    }
    QByteArray bytes;
    const auto info = readHeader(file, bytes, [layout](std::span<const std::byte> b) {
      return readFallout3SaveInfo(b, layout);
    });
    if (!info) {
      MOBase::log::warn("{}: not a readable save", path);
      return details;
    }
    bytes     = readStart(file, asOffset(fallout3SaveDataBound(*info, UsualStringLength)));
    auto data = readFallout3SaveData(asBytes(bytes), *info);
    if (!data && bytes.size() < file.size()) {
      bytes = readStart(file, file.size());
      data  = readFallout3SaveData(asBytes(bytes), *info);
    }
    if (!data) {
      MOBase::log::warn("{}: the save's plugin list can't be read", path);
      return details;
    }
    details.read       = true;
    details.plugins    = texts(data->plugins);
    details.screenshot = screenshot(data->screenshot, info->screenshotWidth,
                                    info->screenshotHeight, false, 256);
    return details;
  }

  // --- Skyrim Special Edition ---

  SaveDetails readSkyrimDetails(const QString& path)
  {
    SaveDetails details;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::warn("failed to open {}", path);
      return details;
    }
    QByteArray bytes;
    const auto info = readHeader(file, bytes, &readSkyrimSaveInfo);
    if (!info) {
      MOBase::log::warn("{}: not a readable save", path);
      return details;
    }
    // the body's sizes follow the screenshot
    bytes             = readStart(file, asOffset(info->screenshotEnd() + 8));
    const auto layout = readSkyrimBodyLayout(asBytes(bytes), *info);
    if (!layout) {
      MOBase::log::warn("{}: the save's body can't be found (a damaged screenshot size, or "
                        "compression {})",
                        path, static_cast<int>(info->compression));
      return details;
    }
    const std::size_t bodySize =
        info->compression == SkyrimCompression::None
            ? static_cast<std::size_t>(
                  std::max<qint64>(file.size() - asOffset(layout->offset), 0))
            : layout->uncompressedSize;
    std::optional<SkyrimSaveData> data;
    for (const std::size_t limit : {UsualListBytes, MaxListBytes}) {
      bytes = readStart(file, asOffset(skyrimSaveDataBound(*info, *layout, limit)));
      data  = readSkyrimSaveData(asBytes(bytes), *info, *layout, limit);
      if (data || limit >= bodySize) {
        break;
      }
    }
    if (!data) {
      MOBase::log::warn("{}: the save's plugin lists can't be read", path);
      return details;
    }
    details.read         = true;
    details.plugins      = texts(data->plugins);
    details.lightPlugins = texts(data->lightPlugins);
    details.screenshot   = screenshot(data->screenshot, info->screenshotWidth,
                                      info->screenshotHeight, info->alpha(), 320);
    return details;
  }

  // --- Fallout 4 ---

  SaveDetails readFallout4Details(const QString& path)
  {
    SaveDetails details;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::warn("failed to open {}", path);
      return details;
    }
    QByteArray bytes;
    const auto info = readHeader(file, bytes, &readFallout4SaveInfo);
    if (!info) {
      MOBase::log::warn("{}: not a readable save", path);
      return details;
    }
    bytes     = readStart(file, asOffset(fallout4SaveDataBound(*info, UsualListBytes)));
    auto data = readFallout4SaveData(asBytes(bytes), *info);
    if (!data && bytes.size() < file.size()) {
      bytes = readStart(file, asOffset(fallout4SaveDataBound(*info, MaxListBytes)));
      data  = readFallout4SaveData(asBytes(bytes), *info);
    }
    if (!data) {
      MOBase::log::warn("{}: the save's plugin lists can't be read", path);
      return details;
    }
    details.read         = true;
    details.plugins      = texts(data->plugins);
    details.lightPlugins = texts(data->lightPlugins);
    details.screenshot   = screenshot(data->screenshot, info->screenshotWidth,
                                      info->screenshotHeight, true, 384);
    return details;
  }

  // the header of the save at path, parsed by parse, as a save whose details
  // reader reads the rest when asked; null if the header can't be read
  // (logged)
  template <typename Parse, typename Summarize>
  std::shared_ptr<const BethesdaSaveGame>
  makeSave(const BethesdaGame* game, const QString& path, Parse parse, Summarize summarize,
           BethesdaSaveGame::DetailsReader details, bool lightEnabled)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::error("failed to open {}", path);
      return nullptr;
    }
    QByteArray bytes;
    const auto info = readHeader(file, bytes, parse);
    if (!info) {
      MOBase::log::error("{}: wrong file format or damaged save", path);
      return nullptr;
    }
    return std::make_shared<const BethesdaSaveGame>(game, path, summarize(*info),
                                                    std::move(details), lightEnabled);
  }
}  // namespace

BethesdaSaveGame::BethesdaSaveGame(const BethesdaGame* game, QString path,
                                   SaveSummary summary, DetailsReader details,
                                   bool lightEnabled, bool mediumEnabled)
    : m_Game(game), m_Path(std::move(path)),
      // the game's own time where the save has one (as the old plugins did),
      // the file's otherwise
      m_CreationTime(summary.savedAt.isValid() ? summary.savedAt
                                               : QFileInfo(m_Path).lastModified()),
      m_Summary(std::move(summary)), m_Reader(std::move(details)),
      m_LightEnabled(lightEnabled), m_MediumEnabled(mediumEnabled)
{}

QString BethesdaSaveGame::getName() const
{
  return QCoreApplication::translate("QObject", "%1, #%2, Level %3, %4")
      .arg(m_Summary.playerName)
      .arg(m_Summary.saveNumber)
      .arg(m_Summary.playerLevel)
      .arg(m_Summary.playerLocation);
}

QString BethesdaSaveGame::coSavePath() const
{
  const QFileInfo file(m_Path);
  return file.absolutePath() + "/" + file.completeBaseName() + "." +
         m_Game->coSaveExtension();
}

bool BethesdaSaveGame::hasScriptExtenderFile() const
{
  return QFileInfo(coSavePath()).exists();
}

QStringList BethesdaSaveGame::allFiles() const
{
  QStringList files{m_Path};
  // the co-save goes with the save (deleting, copying) when the game has a
  // script extender
  MOBase::IOrganizer* organizer = m_Game->organizer();
  if (organizer != nullptr &&
      organizer->gameFeatures()->gameFeature<MOBase::ScriptExtender>() != nullptr) {
    const QFileInfo coSave(coSavePath());
    if (coSave.exists()) {
      files.append(coSave.absoluteFilePath());
    }
  }
  return files;
}

const SaveDetails& BethesdaSaveGame::details() const
{
  std::call_once(m_Once, [this] {
    if (m_Reader) {
      m_Details = m_Reader(m_Path);
    }
  });
  return m_Details;
}

std::shared_ptr<const BethesdaSaveGame> readFallout3SaveGame(const BethesdaGame* game,
                                                             const QString& path,
                                                             bool newVegas)
{
  const Fallout3Layout layout = newVegas ? Fallout3Layout::NewVegas : Fallout3Layout::Fallout3;
  return makeSave(
      game, path,
      [layout](std::span<const std::byte> b) {
        return readFallout3SaveInfo(b, layout);
      },
      [](const Fallout3SaveInfo& info) {
        return summaryOf(info);
      },
      [layout](const QString& p) {
        return readFallout3Details(p, layout);
      },
      false);
}

std::shared_ptr<const BethesdaSaveGame> readSkyrimSaveGame(const BethesdaGame* game,
                                                           const QString& path)
{
  return makeSave(
      game, path, &readSkyrimSaveInfo,
      [](const SkyrimSaveInfo& info) {
        SaveSummary summary = summaryOf(info);
        summary.savedAt     = fromFileTime(info.fileTime);
        return summary;
      },
      &readSkyrimDetails, true);
}

std::shared_ptr<const BethesdaSaveGame> readFallout4SaveGame(const BethesdaGame* game,
                                                             const QString& path)
{
  return makeSave(
      game, path, &readFallout4SaveInfo,
      [](const Fallout4SaveInfo& info) {
        SaveSummary summary = summaryOf(info);
        summary.savedAt     = fromFileTime(info.fileTime);
        return summary;
      },
      &readFallout4Details, true);
}

}  // namespace mo2r::bethesda
