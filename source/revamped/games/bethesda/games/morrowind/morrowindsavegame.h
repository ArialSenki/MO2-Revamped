// MO2 Revamped: Morrowind, a save in the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// A Morrowind save has no number and the level isn't in its header: the
// number comes from the file's name, as MO2's game_morrowind took it, and the
// level is read with the screenshot, the first time either is asked for. The
// card shows the save's own name, the character's health and the game's day.
#pragma once

#include "saves/bethesdasavegame.h"
#include "saves/savegameinfo.h"

#include <isavegameinfowidget.h>

#include <memory>

namespace RevampedFluent
{
class SaveGameCard;
}

namespace mo2r::bethesda
{

class BethesdaGame;

class MorrowindSaveGame : public BethesdaSaveGame
{
public:
  struct Extra
  {
    QString saveName;  // the name the player gave the save
    float playerHealth    = 0;
    float playerMaxHealth = 0;
    float gameDays        = 0;
  };

  // null if the header can't be read (logged)
  static std::shared_ptr<const MorrowindSaveGame> read(const BethesdaGame* game,
                                                       const QString& path);

  MorrowindSaveGame(const BethesdaGame* game, QString path, SaveSummary summary, Extra extra,
                    std::shared_ptr<unsigned short> level);

  // "name, #number, location": no level, it isn't read when listing
  QString getName() const override;

  const Extra& extra() const { return m_Extra; }
  // read with the screenshot; 0 if the player's record isn't found
  unsigned short playerLevel() const;

private:
  Extra m_Extra;
  std::shared_ptr<unsigned short> m_Level;
};

class MorrowindSaveGameInfo : public BethesdaSaveGameInfo
{
public:
  explicit MorrowindSaveGameInfo(const BethesdaGame* game)
      : BethesdaSaveGameInfo(game), m_Game(game)
  {}

  MOBase::ISaveGameInfoWidget* getSaveGameWidget(QWidget* parent) const override;

private:
  const BethesdaGame* m_Game;
};

class MorrowindSaveGameWidget : public MOBase::ISaveGameInfoWidget
{
  Q_OBJECT

public:
  MorrowindSaveGameWidget(const BethesdaGame* game, QWidget* parent);
  ~MorrowindSaveGameWidget() override;

  void setSave(MOBase::ISaveGame const& save) override;

private:
  const BethesdaGame* m_Game;
  std::unique_ptr<RevampedFluent::SaveGameCard> m_Card;
};

}  // namespace mo2r::bethesda
