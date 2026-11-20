#include "skyrimSEunmanagedmods.h"

SkyrimSEUnmangedMods::SkyrimSEUnmangedMods(const GameGamebryo* game)
    : GamebryoUnmangedMods(game)
{}

SkyrimSEUnmangedMods::~SkyrimSEUnmangedMods() {}

QStringList SkyrimSEUnmangedMods::mods(bool onlyOfficial) const
{
  QStringList result;

  QStringList pluginList   = game()->primaryPlugins();
  QStringList otherPlugins = game()->DLCPlugins();
  otherPlugins.append(game()->CCPlugins());
  for (QString plugin : otherPlugins) {
    pluginList.removeAll(plugin);
  }
  for (const QString& fileName : dataPlugins()) {
    if (!pluginList.contains(fileName, Qt::CaseInsensitive)) {
      // MO2 Revamped: "only official" is the DLC and Creation Club ones
      // (pluginList never has them here, so nothing was listed)
      if (!onlyOfficial || otherPlugins.contains(fileName, Qt::CaseInsensitive)) {
        result.append(fileName.chopped(4));  // trims the extension off
      }
    }
  }

  return result;
}
