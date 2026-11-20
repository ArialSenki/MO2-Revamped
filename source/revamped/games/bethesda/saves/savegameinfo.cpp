// MO2 Revamped: Bethesda game framework, save details for the Saves tab.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// Which mods count as having a missing plugin is MO2's rule (game_gamebryo's
// GamebryoSaveGameInfo); the card is MO2 Revamped's.
#include "savegameinfo.h"

#include "core/bethesdagame.h"
#include "saves/bethesdasavegame.h"

#include <savegamecard.h>

#include <imodinterface.h>
#include <imodlist.h>
#include <imoinfo.h>
#include <iplugingame.h>
#include <ipluginlist.h>

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QLocale>

namespace mo2r::bethesda
{

namespace
{
  // the old plugins' context, so their translations still apply
  QString tr(const char* source)
  {
    return QCoreApplication::translate("GamebryoSaveGameInfoWidget", source);
  }
}  // namespace

BethesdaSaveGameInfo::MissingAssets
BethesdaSaveGameInfo::getMissingAssets(MOBase::ISaveGame const& save) const
{
  const auto& bethesdaSave       = dynamic_cast<const BethesdaSaveGame&>(save);
  MOBase::IOrganizer* organizer  = m_Game->organizer();
  MOBase::IPluginList* pluginList = organizer->pluginList();

  // the save's plugins that aren't active: an inactive one's own mod has it
  MissingAssets missing;
  for (const QStringList* list : {&bethesdaSave.plugins(), &bethesdaSave.lightPlugins()}) {
    for (const QString& plugin : *list) {
      switch (pluginList->state(plugin)) {
      case MOBase::IPluginList::STATE_INACTIVE:
        missing[plugin] = ProvidingModules{pluginList->origin(plugin)};
        break;
      case MOBase::IPluginList::STATE_MISSING:
        missing[plugin] = ProvidingModules();
        break;
      default:
        break;
      }
    }
  }
  if (missing.isEmpty()) {
    return missing;
  }

  // any other mod (or overwrite) with a plugin of that name, ignoring case as
  // the game does
  QHash<QString, QString> byFolded;
  for (auto it = missing.cbegin(); it != missing.cend(); ++it) {
    byFolded.insert(it.key().toCaseFolded(), it.key());
  }
  const auto providers = [&](const QString& file) -> ProvidingModules* {
    const auto key = byFolded.constFind(file.toCaseFolded());
    return key == byFolded.cend() ? nullptr : &missing[*key];
  };
  const QStringList pluginFiles{"*.esp", "*.esl", "*.esm"};
  const QString dataDir = organizer->managedGame()->dataDirectory().absolutePath();

  for (const QString& name : organizer->modList()->allModsByProfilePriority()) {
    const MOBase::IModInterface* mod = organizer->modList()->getMod(name);
    if (mod == nullptr) {
      continue;
    }
    for (const QString& file : QDir(mod->absolutePath()).entryList(pluginFiles, QDir::Files)) {
      ProvidingModules* list = providers(file);
      if (list == nullptr) {
        continue;
      }
      // the game's Data folder is a "mod" too: only its own plugins count
      // there, or every unmanaged mod would be offered
      if (mod->absolutePath() == dataDir && mod->name() != pluginList->origin(file)) {
        continue;
      }
      if (!list->contains(mod->name())) {
        list->push_back(mod->name());
      }
    }
  }

  for (const QString& file :
       QDir(organizer->overwritePath()).entryList(pluginFiles, QDir::Files)) {
    if (ProvidingModules* list = providers(file); list != nullptr) {
      if (!list->contains("<overwrite>")) {
        list->push_back("<overwrite>");
      }
    }
  }
  return missing;
}

MOBase::ISaveGameInfoWidget* BethesdaSaveGameInfo::getSaveGameWidget(QWidget* parent) const
{
  return new BethesdaSaveGameWidget(m_Game, parent);
}

BethesdaSaveGameWidget::BethesdaSaveGameWidget(const BethesdaGame* game, QWidget* parent)
    : MOBase::ISaveGameInfoWidget(parent), m_Game(game),
      m_Card(std::make_unique<RevampedFluent::SaveGameCard>())
{
  // the old widget's name, which stylesheets may use
  setObjectName(QStringLiteral("GamebryoSaveGameInfoWidget"));
  m_Card->setup(this);
}

BethesdaSaveGameWidget::~BethesdaSaveGameWidget() = default;

void BethesdaSaveGameWidget::setSave(MOBase::ISaveGame const& save)
{
  const auto& s            = dynamic_cast<const BethesdaSaveGame&>(save);
  const SaveSummary& info  = s.summary();
  RevampedFluent::SaveGameCard& card = *m_Card;

  card.setScreenshot(s.screenshot());
  card.setTitle(info.playerName);
  const QDateTime t = s.getCreationTime().toLocalTime();
  const QString when =
      QLocale::system().toString(t.date(), QLocale::FormatType::ShortFormat) + " " +
      QLocale::system().toString(t.time(), QLocale::FormatType::ShortFormat);
  card.setLines({info.playerLocation.isEmpty()
                     ? tr("Level %1").arg(info.playerLevel)
                     : tr("Level %1, %2").arg(QString::number(info.playerLevel),
                                              info.playerLocation),
                 tr("Save %1, %2").arg(info.saveNumber).arg(when)});

  card.clearSections();
  if (s.hasScriptExtenderFile()) {
    card.addNote(tr("Has script extender data"));
  }
  MOBase::IPluginList* pluginList = m_Game->organizer()->pluginList();
  const auto inactive = [pluginList](const QStringList& plugins) {
    QStringList out;
    for (const QString& plugin : plugins) {
      if (pluginList->state(plugin) != MOBase::IPluginList::STATE_ACTIVE) {
        out.append(plugin);
      }
    }
    return out;
  };
  card.addMissing(tr("Missing plugins (ESP, ESM)"), inactive(s.plugins()));
  if (s.mediumEnabled()) {
    card.addMissing(tr("Missing medium plugins (ESH)"), inactive(s.mediumPlugins()));
  }
  if (s.lightEnabled()) {
    card.addMissing(tr("Missing light plugins (ESL)"), inactive(s.lightPlugins()));
  }
  card.finish();
}

}  // namespace mo2r::bethesda
