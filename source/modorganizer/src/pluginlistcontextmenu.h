#ifndef PLUGINLISTCONTEXTMENU_H
#define PLUGINLISTCONTEXTMENU_H

#include <QMenu>
#include <QModelIndex>
#include <QTreeView>

#include "modinfo.h"

class PluginListView;
class OrganizerCore;

class PluginListContextMenu : public QMenu
{
  Q_OBJECT

public:
  // creates a new context menu, the given index is the one for the click; when
  // it is invalid (empty space), only the whole list's commands are shown
  //
  PluginListContextMenu(const QModelIndex& index, OrganizerCore& core,
                        PluginListView* modListView);

signals:

  // emitted to open a mod information
  //
  void openModInformation(unsigned int modIndex);

public:
  // create the "Send to... " context menu
  //
  QMenu* createSendToContextMenu();
  void sendPluginsToPriority(const QStringList& plugins);

  // set ESP lock on the given plugins
  //
  void setESPLock(const QStringList& plugins, bool locked);

  // open explorer or mod information for the origin of the plugins
  //
  void openOriginExplorer(const QStringList& plugins);
  void openOriginInformation(const QString& plugin);

  // the current indices of the given plugins; the list can be rebuilt while
  // the menu is open, so actions look the plugins up by name when they run
  //
  QModelIndexList indicesOf(const QStringList& plugins) const;

  OrganizerCore& m_core;
  QModelIndex m_index;
  // the clicked plugin and the plugins the selection actions apply to
  QString m_plugin;
  QStringList m_selected;
  PluginListView* m_view;
};

#endif
