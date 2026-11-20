#include "gamebryosavegameinfo.h"

#include "gamebryosavegame.h"
#include "gamebryosavegameinfowidget.h"
#include "gamegamebryo.h"
#include "imodinterface.h"
#include "imoinfo.h"
#include "iplugingame.h"
#include "ipluginlist.h"

#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

GamebryoSaveGameInfo::GamebryoSaveGameInfo(GameGamebryo const* game) : m_Game(game) {}

GamebryoSaveGameInfo::~GamebryoSaveGameInfo() {}

GamebryoSaveGameInfo::MissingAssets
GamebryoSaveGameInfo::getMissingAssets(MOBase::ISaveGame const& save) const
{
  GamebryoSaveGame const& gamebryoSave = dynamic_cast<GamebryoSaveGame const&>(save);
  MOBase::IOrganizer* organizerCore    = m_Game->m_Organizer;

  // collect the list of missing plugins
  MissingAssets missingAssets;

  for (QString const& pluginName : gamebryoSave.getPlugins()) {
    switch (organizerCore->pluginList()->state(pluginName)) {
    case MOBase::IPluginList::STATE_INACTIVE:
      missingAssets[pluginName] =
          ProvidingModules{organizerCore->pluginList()->origin(pluginName)};
      break;
    case MOBase::IPluginList::STATE_MISSING:
      missingAssets[pluginName] = ProvidingModules();
      break;
    }
  }

  for (QString const& pluginName : gamebryoSave.getLightPlugins()) {
    switch (organizerCore->pluginList()->state(pluginName)) {
    case MOBase::IPluginList::STATE_INACTIVE:
      missingAssets[pluginName] =
          ProvidingModules{organizerCore->pluginList()->origin(pluginName)};
      break;
    case MOBase::IPluginList::STATE_MISSING:
      missingAssets[pluginName] = ProvidingModules();
      break;
    }
  }

  // MO2 Revamped: nothing missing, nothing to look for (each mod's folder was
  // listed anyway: thousands of listings on a right click with a big list)
  if (missingAssets.isEmpty()) {
    return missingAssets;
  }

  // MO2 Revamped: the save's names and the files on disk compared ignoring
  // case, as the game does (Skyrim.esm in a save, skyrim.esm on disk)
  QHash<QString, QString> missingByFolded;
  for (auto it = missingAssets.cbegin(); it != missingAssets.cend(); ++it) {
    missingByFolded.insert(it.key().toCaseFolded(), it.key());
  }
  const auto findMissing = [&](const QString& esp) {
    const auto key = missingByFolded.constFind(esp.toCaseFolded());
    return key == missingByFolded.cend() ? missingAssets.end()
                                         : missingAssets.find(*key);
  };

  // Find out any other mods that might contain the esp/esm
  QStringList espFilter({"*.esp", "*.esl", "*.esm"});

  QString dataDir(organizerCore->managedGame()->dataDirectory().absolutePath());

  // Search normal mods. A note: This will also find mods in data.
  for (QString const& mod : organizerCore->modList()->allModsByProfilePriority()) {
    MOBase::IModInterface* modInfo = organizerCore->modList()->getMod(mod);
    if (modInfo == nullptr) {
      continue;
    }
    QStringList esps = QDir(modInfo->absolutePath()).entryList(espFilter, QDir::Files);
    for (QString const& esp : esps) {
      MissingAssets::iterator iter = findMissing(esp);
      if (iter == missingAssets.end()) {
        continue;
      }
      if (modInfo->absolutePath() == dataDir) {
        // We have to prune esps that reside in the data directory, otherwise
        // you get all the unmanaged mods listed as potential candidates for
        // enabling
        if (modInfo->name() != organizerCore->pluginList()->origin(esp)) {
          continue;
        }
      }
      if (!iter->contains(modInfo->name())) {
        iter->push_back(modInfo->name());
      }
    }
  }

  // search in overwrite
  {
    QDir overwriteDir(organizerCore->overwritePath());
    QStringList esps = overwriteDir.entryList(espFilter, QDir::Files);
    for (const QString& esp : esps) {
      MissingAssets::iterator iter = findMissing(esp);
      if (iter != missingAssets.end()) {
        if (!iter->contains("<overwrite>")) {
          iter->push_back("<overwrite>");
        }
      }
    }
  }

  return missingAssets;
}

MOBase::ISaveGameInfoWidget*
GamebryoSaveGameInfo::getSaveGameWidget(QWidget* parent) const
{
  return new GamebryoSaveGameInfoWidget(this, parent);
}
