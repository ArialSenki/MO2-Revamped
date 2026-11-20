// MO2 Revamped: Bethesda game framework, a save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The fields the list shows (name, number, level, location) are read when the
// save is listed; the screenshot and plugins, which cost more, the first time
// they are asked for (the save's card, its context menu).
#pragma once

#include <isavegame.h>

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace mo2r::bethesda
{

class BethesdaGame;

struct SaveSummary
{
  QString playerName;
  unsigned short playerLevel = 0;
  QString playerLocation;
  unsigned long saveNumber = 0;
};

struct SaveDetails
{
  QStringList plugins;
  QStringList lightPlugins;
  QStringList mediumPlugins;
  QImage screenshot;
};

class BethesdaSaveGame : public MOBase::ISaveGame
{
public:
  using DetailsReader = std::function<SaveDetails(const QString& path)>;

  BethesdaSaveGame(const BethesdaGame* game, QString path, SaveSummary summary,
                   DetailsReader details, bool lightEnabled = false,
                   bool mediumEnabled = false);

  QString getFilepath() const override { return m_Path; }
  QDateTime getCreationTime() const override { return m_CreationTime; }
  QString getName() const override;
  QString getSaveGroupIdentifier() const override { return m_Summary.playerName; }
  QStringList allFiles() const override;

  const SaveSummary& summary() const { return m_Summary; }
  // the script extender's co-save next to this save
  QString coSavePath() const;
  bool hasScriptExtenderFile() const;

  const QStringList& plugins() const { return details().plugins; }
  const QStringList& lightPlugins() const { return details().lightPlugins; }
  const QStringList& mediumPlugins() const { return details().mediumPlugins; }
  const QImage& screenshot() const { return details().screenshot; }
  bool lightEnabled() const { return m_LightEnabled; }
  bool mediumEnabled() const { return m_MediumEnabled; }

private:
  const SaveDetails& details() const;

  const BethesdaGame* m_Game;
  QString m_Path;
  QDateTime m_CreationTime;
  SaveSummary m_Summary;
  DetailsReader m_Reader;
  bool m_LightEnabled;
  bool m_MediumEnabled;
  mutable std::once_flag m_Once;
  mutable SaveDetails m_Details;
};

// a Fallout 3 / New Vegas save, null if its header can't be read (logged)
std::shared_ptr<const BethesdaSaveGame> readFallout3SaveGame(const BethesdaGame* game,
                                                             const QString& path);

}  // namespace mo2r::bethesda
