// MO2 Revamped: Morrowind, a save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The fields, the name and the card's lines are those of MO2's
// game_morrowind (MorrowindSaveGame, MorrowindSaveGameInfoWidget); the bytes
// are read by saves/morrowindsave.
#include "morrowindsavegame.h"

#include "core/bethesdagame.h"
#include "saves/morrowindsave.h"

#include <savegamecard.h>

#include <imoinfo.h>
#include <ipluginlist.h>
#include <log.h>

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace mo2r::bethesda
{

namespace
{
  // the header with its plugin list: a few KB
  constexpr qint64 InfoBytes    = 64 * 1024;
  constexpr qint64 MaxSaveBytes = 512LL * 1024 * 1024;

  // the old widget's context, so its translations still apply
  QString tr(const char* source)
  {
    return QCoreApplication::translate("MorrowindSaveGameInfoWidget", source);
  }

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

  // the game writes its own code page; MO2 read them as Latin-1
  QString latin1(const std::string& s)
  {
    return QString::fromLatin1(s.data(), static_cast<qsizetype>(s.size()));
  }

  // the header, from the file's start or, with a long plugin list, all of it
  std::optional<MorrowindSaveInfo> readInfo(QFile& file, QByteArray& bytes)
  {
    bytes     = readStart(file, InfoBytes);
    auto info = readMorrowindSaveInfo(asBytes(bytes));
    if (!info && bytes.size() < file.size()) {
      bytes = readStart(file, file.size());
      info  = readMorrowindSaveInfo(asBytes(bytes));
    }
    return info;
  }

  // the 128x128 BGRA picture, its alpha inverted, at the old card's size
  QImage screenshot(std::span<const std::byte> pixels)
  {
    constexpr int Side = static_cast<int>(MorrowindScreenshotSide);
    if (pixels.size() != std::size_t{Side} * Side * 4) {
      return {};
    }
    QImage image(Side, Side, QImage::Format_RGBA8888);
    const auto* p = reinterpret_cast<const unsigned char*>(pixels.data());
    for (int y = 0; y < Side; ++y) {
      for (int x = 0; x < Side; ++x, p += 4) {
        image.setPixel(x, y, QColor(p[2], p[1], p[0], 255 - p[3]).rgba());
      }
    }
    return image.scaled(252, 192);
  }

  // the number in a name like "Save0003.ess", as the old plugin took it:
  // the five characters after the fourth, trailing zeros removed
  unsigned long saveNumber(const QString& path)
  {
    static const QRegularExpression trailingZeros("0+$");
    return static_cast<unsigned long>(
        QFileInfo(path).fileName().mid(4, 5).remove(trailingZeros).toInt());
  }

  SaveDetails readDetails(const QString& path, unsigned short& level)
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
    // the player's record is far into the file, after the scripts
    bytes           = readStart(file, file.size());
    const auto data = readMorrowindSaveData(asBytes(bytes), *info);
    if (!data) {
      MOBase::log::warn("{}: the save's screenshot can't be read", path);
    } else {
      details.screenshot = screenshot(data->screenshot);
      if (data->playerLevel) {
        level = *data->playerLevel;
      } else {
        MOBase::log::warn("{}: the player's level can't be read", path);
      }
    }
    details.read = true;
    for (const std::string& plugin : info->plugins) {
      details.plugins.append(latin1(plugin));
    }
    return details;
  }
}  // namespace

std::shared_ptr<const MorrowindSaveGame> MorrowindSaveGame::read(const BethesdaGame* game,
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
  summary.playerName     = latin1(info->playerName).trimmed();
  summary.playerLocation = latin1(info->playerLocation).trimmed();
  summary.saveNumber     = saveNumber(path);
  Extra extra;
  extra.saveName        = latin1(info->saveName).trimmed();
  extra.playerHealth    = info->playerHealth;
  extra.playerMaxHealth = info->playerMaxHealth;
  extra.gameDays        = info->gameDays;
  return std::make_shared<const MorrowindSaveGame>(game, path, std::move(summary),
                                                   std::move(extra),
                                                   std::make_shared<unsigned short>(0));
}

MorrowindSaveGame::MorrowindSaveGame(const BethesdaGame* game, QString path,
                                     SaveSummary summary, Extra extra,
                                     std::shared_ptr<unsigned short> level)
    : BethesdaSaveGame(game, std::move(path), std::move(summary),
                       [level](const QString& p) {
                         return readDetails(p, *level);
                       }),
      m_Extra(std::move(extra)), m_Level(std::move(level))
{}

QString MorrowindSaveGame::getName() const
{
  return QString("%1, #%2, %3")
      .arg(summary().playerName)
      .arg(summary().saveNumber)
      .arg(summary().playerLocation);
}

unsigned short MorrowindSaveGame::playerLevel() const
{
  // the level comes with the rest of the details, read once
  (void)detailsRead();
  return *m_Level;
}

MOBase::ISaveGameInfoWidget* MorrowindSaveGameInfo::getSaveGameWidget(QWidget* parent) const
{
  return new MorrowindSaveGameWidget(m_Game, parent);
}

MorrowindSaveGameWidget::MorrowindSaveGameWidget(const BethesdaGame* game, QWidget* parent)
    : MOBase::ISaveGameInfoWidget(parent), m_Game(game),
      m_Card(std::make_unique<RevampedFluent::SaveGameCard>())
{
  // the old widget's name, which stylesheets may use
  setObjectName(QStringLiteral("MorrowindSaveGameInfoWidget"));
  m_Card->setup(this);
}

MorrowindSaveGameWidget::~MorrowindSaveGameWidget() = default;

void MorrowindSaveGameWidget::setSave(MOBase::ISaveGame const& save)
{
  const auto& s     = dynamic_cast<const MorrowindSaveGame&>(save);
  const auto& extra = s.extra();
  RevampedFluent::SaveGameCard& card = *m_Card;

  card.setScreenshot(s.screenshot());
  card.setTitle(s.summary().playerName);
  const QDateTime t      = s.getCreationTime();
  const QString location = s.summary().playerLocation;
  const unsigned short level = s.playerLevel();
  card.setLines(
      {location.isEmpty() ? tr("Level %1").arg(level)
                          : tr("Level %1, %2").arg(QString::number(level), location),
       tr("Health %1 of %2")
           .arg(qRound(extra.playerHealth))
           .arg(qRound(extra.playerMaxHealth)),
       tr("%1, day %2").arg(extra.saveName, QString::number(qRound(extra.gameDays))),
       tr("Save %1, %2")
           .arg(s.summary().saveNumber)
           .arg(QLocale::system().toString(t.date(), QLocale::FormatType::ShortFormat) + " " +
                QLocale::system().toString(t.time(), QLocale::FormatType::ShortFormat))});

  card.clearSections();
  MOBase::IPluginList* pluginList = m_Game->organizer()->pluginList();
  QStringList missing;
  for (const QString& plugin : s.plugins()) {
    if (pluginList->state(plugin) != MOBase::IPluginList::STATE_ACTIVE) {
      missing.append(plugin);
    }
  }
  card.addMissing(tr("Missing plugins"), missing);
  card.finish();
}

}  // namespace mo2r::bethesda
