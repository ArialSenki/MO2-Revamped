#include "pluginlistcontextmenu.h"
#include "fluenticons.h"
#include "menuutils.h"

#include <report.h>
#include <utility.h>

#include "organizercore.h"
#include "pluginlistview.h"
#include <QIcon>

using namespace MOBase;

PluginListContextMenu::PluginListContextMenu(const QModelIndex& index,
                                             OrganizerCore& core, PluginListView* view)
    : QMenu(view), m_core(core),
      // the name column whichever column was clicked: the origin lookup and
      // Copy plugin name read the plugin's name from it
      m_index((index.model() == view->model() ? view->indexViewToModel(index) : index)
                  .siblingAtColumn(PluginList::COL_NAME)),
      m_view(view)
{
  // a click on empty space acts on no plugin, whatever was selected before
  QModelIndexList selectedRows;
  if (m_index.isValid()) {
    m_plugin = m_index.data().toString();
    if (view->selectionModel()->hasSelection()) {
      selectedRows = view->indexViewToModel(view->selectionModel()->selectedRows());
    } else {
      selectedRows = {m_index};
    }
  }

  // names, not rows: the list can be rebuilt while the menu is open
  PluginList* list = m_core.pluginList();
  for (const auto& idx : selectedRows) {
    if (idx.isValid() && idx.row() < list->rowCount()) {
      m_selected.append(list->getName(idx.row()));
    }
  }

  // the default command first, the selection's commands next and the whole
  // list's last, as the mod list orders them
  ModInfo::Ptr origin;
  if (m_index.isValid()) {
    unsigned int modInfoIndex = ModInfo::getIndex(list->origin(m_plugin));
    // this is to avoid showing the option on game files like skyrim.esm
    if (modInfoIndex != UINT_MAX) {
      origin = ModInfo::getByIndex(modInfoIndex);
      if (!origin->isForeign() && m_selected.size() == 1) {
        QAction* infoAction = addAction(tr("Origin information..."), [=]() {
          openOriginInformation(m_plugin);
        });
        infoAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/information.svg"));
        setDefaultMenuAction(this, infoAction);
        addSeparator();
      }
    }
  }

  if (!m_selected.isEmpty()) {
    QAction* enableSelectedAction = addAction(tr("Enable selected"), [=]() {
      m_core.pluginList()->setEnabled(indicesOf(m_selected), true);
    });
    enableSelectedAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"));
    QAction* disableSelectedAction = addAction(tr("Disable selected"), [=]() {
      m_core.pluginList()->setEnabled(indicesOf(m_selected), false);
    });
    disableSelectedAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/disable.svg"));
    addMenu(createSendToContextMenu());

    bool hasLocked   = false;
    bool hasUnlocked = false;
    // MO2 Revamped: a locked plugin turned off since can still be unlocked;
    // only locking needs it on (it has no load order otherwise)
    for (auto& idx : indicesOf(m_selected)) {
      if (m_core.pluginList()->isESPLocked(idx.row())) {
        hasLocked = true;
      } else if (m_core.pluginList()->isEnabled(idx.row())) {
        hasUnlocked = true;
      }
    }

    if (hasLocked) {
      QAction* unlockAction = addAction(tr("Unlock load order"), [=]() {
        setESPLock(m_selected, false);
      });
      unlockAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/unlock.svg"));
    }
    if (hasUnlocked) {
      QAction* lockAction = addAction(tr("Lock load order"), [=]() {
        setESPLock(m_selected, true);
      });
      lockAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/status/locked.svg"));
    }
  }

  if (m_index.isValid()) {
    addSeparator();
    if (origin) {
      QAction* openOriginAction = addAction(tr("Open origin folder"), [=]() {
        openOriginExplorer(m_selected);
      });
      openOriginAction->setIcon(menuGlyph(MenuGlyphs::Folder));
    }
    const QString plugin = m_plugin;
    if (origin) {
      addCopyMenu(this, {{tr("Plugin name"), plugin},
                         {tr("Origin name"), origin->name()},
                         {tr("Plugin path"),
                          nativePath(QDir(origin->absolutePath()).absoluteFilePath(plugin))}});
    } else {
      addCopyAction(this, tr("Copy plugin name"), plugin);
    }
  }

  addSeparator();
  QAction* enableAllAction = addAction(tr("Enable all"), [=]() {
    if (QMessageBox::question(m_view->topLevelWidget(), tr("Confirm"),
                              tr("Really enable all plugins?"),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      m_core.pluginList()->setEnabledAll(true);
    }
  });
  enableAllAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"));
  QAction* disableAllAction = addAction(tr("Disable all"), [=]() {
    if (QMessageBox::question(m_view->topLevelWidget(), tr("Confirm"),
                              tr("Really disable all plugins?"),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      m_core.pluginList()->setEnabledAll(false);
    }
  });
  disableAllAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/disable.svg"));
}

QMenu* PluginListContextMenu::createSendToContextMenu()
{
  QMenu* menu = new QMenu(this);
  menu->setTitle(tr("Send to"));
  menu->setIcon(menuGlyph(MenuGlyphs::Move));
  menu->addAction(tr("Top"), [=]() {
    m_core.pluginList()->sendToPriority(indicesOf(m_selected), 0);
  });
  menu->addAction(tr("Bottom"), [=]() {
    m_core.pluginList()->sendToPriority(indicesOf(m_selected), INT_MAX);
  });
  menu->addAction(tr("Priority..."), [=]() {
    sendPluginsToPriority(m_selected);
  });
  return menu;
}

QModelIndexList PluginListContextMenu::indicesOf(const QStringList& plugins) const
{
  return m_core.pluginList()->indicesForNames(plugins);
}

void PluginListContextMenu::sendPluginsToPriority(const QStringList& plugins)
{
  bool ok;
  int newPriority = QInputDialog::getInt(m_view->topLevelWidget(), tr("Set Priority"),
                                         tr("Set the priority of the selected plugins"),
                                         0, 0, INT_MAX, 1, &ok);
  if (!ok)
    return;

  // looked up after the dialog, which can also outlive a refresh
  m_core.pluginList()->sendToPriority(indicesOf(plugins), newPriority);
}

void PluginListContextMenu::setESPLock(const QStringList& plugins, bool locked)
{
  for (auto& idx : indicesOf(plugins)) {
    if (!locked || m_core.pluginList()->isEnabled(idx.row())) {
      m_core.pluginList()->lockESPIndex(idx.row(), locked);
    }
  }
}

void PluginListContextMenu::openOriginExplorer(const QStringList& plugins)
{
  // plugins of the same mod share one window
  QStringList folders;
  for (const auto& fileName : plugins) {
    unsigned int modIndex = ModInfo::getIndex(m_core.pluginList()->origin(fileName));
    if (modIndex == UINT_MAX) {
      continue;
    }
    ModInfo::Ptr modInfo = ModInfo::getByIndex(modIndex);
    const QString folder = modInfo->absolutePath();
    if (!folders.contains(folder, Qt::CaseInsensitive)) {
      folders.append(folder);
    }
  }

  for (const auto& folder : folders) {
    shell::Explore(folder);
  }
}

void PluginListContextMenu::openOriginInformation(const QString& fileName)
{
  try {
    unsigned int modIndex = ModInfo::getIndex(m_core.pluginList()->origin(fileName));
    if (modIndex == UINT_MAX) {
      return;
    }
    ModInfo::Ptr modInfo = ModInfo::getByIndex(modIndex);

    if (modInfo->isRegular() || modInfo->isOverwrite()) {
      emit openModInformation(modIndex);
    }
  } catch (const std::exception& e) {
    reportError(e.what());
  }
}
