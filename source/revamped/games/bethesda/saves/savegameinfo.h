// MO2 Revamped: Bethesda game framework, save details for the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's SaveGameInfo feature: the plugins a save needs that aren't active and
// the mods that have them, and the card shown over a save.
#pragma once

#include <isavegameinfowidget.h>
#include <savegameinfo.h>

#include <memory>

namespace RevampedFluent
{
class SaveGameCard;
}

namespace mo2r::bethesda
{

class BethesdaGame;

class BethesdaSaveGameInfo : public MOBase::SaveGameInfo
{
public:
  explicit BethesdaSaveGameInfo(const BethesdaGame* game) : m_Game(game) {}

  MissingAssets getMissingAssets(MOBase::ISaveGame const& save) const override;
  MOBase::ISaveGameInfoWidget* getSaveGameWidget(QWidget* parent) const override;

private:
  const BethesdaGame* m_Game;
};

class BethesdaSaveGameWidget : public MOBase::ISaveGameInfoWidget
{
  Q_OBJECT

public:
  BethesdaSaveGameWidget(const BethesdaGame* game, QWidget* parent);
  ~BethesdaSaveGameWidget() override;

  void setSave(MOBase::ISaveGame const& save) override;

private:
  const BethesdaGame* m_Game;
  std::unique_ptr<RevampedFluent::SaveGameCard> m_Card;
};

}  // namespace mo2r::bethesda
