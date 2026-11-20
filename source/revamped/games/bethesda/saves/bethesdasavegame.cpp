// MO2 Revamped: Bethesda game framework, a save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "bethesdasavegame.h"

#include "core/bethesdagame.h"
#include "saves/fallout3save.h"

#include <igamefeatures.h>
#include <imoinfo.h>
#include <log.h>
#include <scriptextender.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

namespace mo2r::bethesda
{

namespace
{
  // the start of a .fos holds every field the list shows; its strings can be
  // up to 64 KB each, so a save with longer ones is read again further
  constexpr qint64 InfoBytes    = 4 * 1024;
  constexpr qint64 MaxInfoBytes = 256 * 1024;
  // plugin names and the play time are short; a save with longer ones is read
  // whole (up to this, far above any real save)
  constexpr std::size_t UsualStringLength = 512;
  constexpr qint64 MaxSaveBytes           = 512LL * 1024 * 1024;

  std::span<const std::byte> asBytes(const QByteArray& a)
  {
    return std::as_bytes(std::span(a.constData(), static_cast<std::size_t>(a.size())));
  }

  QString text(const std::string& s)
  {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
  }

  QByteArray readStart(QFile& file, qint64 bytes)
  {
    if (!file.seek(0)) {
      return {};
    }
    return file.read(std::min(bytes, MaxSaveBytes));
  }

  std::optional<Fallout3SaveInfo> readInfo(QFile& file, QByteArray& bytes)
  {
    bytes     = readStart(file, InfoBytes);
    auto info = readFallout3SaveInfo(asBytes(bytes));
    if (!info && bytes.size() < file.size()) {
      bytes = readStart(file, MaxInfoBytes);
      info  = readFallout3SaveInfo(asBytes(bytes));
    }
    return info;
  }

  SaveDetails readFallout3Details(const QString& path)
  {
    SaveDetails details;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      MOBase::log::warn("failed to open {}", path);
      return details;
    }
    QByteArray bytes;
    const auto info = readInfo(file, bytes);
    if (!info) {
      MOBase::log::warn("{}: not a readable save", path);
      return details;
    }
    bytes     = readStart(file, fallout3SaveDataBound(*info, UsualStringLength));
    auto data = readFallout3SaveData(asBytes(bytes), *info);
    if (!data && bytes.size() < file.size()) {
      bytes = readStart(file, file.size());
      data  = readFallout3SaveData(asBytes(bytes), *info);
    }
    if (!data) {
      MOBase::log::warn("{}: the save's plugin list can't be read", path);
      return details;
    }
    for (const std::string& plugin : data->plugins) {
      details.plugins.append(text(plugin));
    }
    if (!data->screenshot.empty()) {
      // packed RGB rows (width * 3 bytes each), copied out of the buffer
      const QImage image(reinterpret_cast<const uchar*>(data->screenshot.data()),
                         static_cast<int>(info->screenshotWidth),
                         static_cast<int>(info->screenshotHeight),
                         static_cast<qsizetype>(info->screenshotWidth) * 3,
                         QImage::Format_RGB888);
      details.screenshot = image.copy().scaledToWidth(256);
    }
    return details;
  }
}  // namespace

BethesdaSaveGame::BethesdaSaveGame(const BethesdaGame* game, QString path,
                                   SaveSummary summary, DetailsReader details,
                                   bool lightEnabled, bool mediumEnabled)
    : m_Game(game), m_Path(std::move(path)),
      m_CreationTime(QFileInfo(m_Path).lastModified()), m_Summary(std::move(summary)),
      m_Reader(std::move(details)), m_LightEnabled(lightEnabled),
      m_MediumEnabled(mediumEnabled)
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
                                                             const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    MOBase::log::error("failed to open {}", path);
    return nullptr;
  }
  QByteArray bytes;
  const auto info = readInfo(file, bytes);
  if (!info) {
    MOBase::log::error("{}: wrong file format or damaged save", path);
    return nullptr;
  }
  SaveSummary summary;
  summary.playerName     = text(info->playerName);
  summary.playerLevel    = info->playerLevel;
  summary.playerLocation = text(info->playerLocation);
  summary.saveNumber     = info->saveNumber;
  return std::make_shared<const BethesdaSaveGame>(game, path, std::move(summary),
                                                  &readFallout3Details);
}

}  // namespace mo2r::bethesda
