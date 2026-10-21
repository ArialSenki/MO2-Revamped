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
      m_index(index.model() == view->model() ? view->indexViewToModel(index) : index),
      m_view(view)
{
  if (view->selectionModel()->hasSelection()) {
    m_selected = view->indexViewToModel(view->selectionModel()->selectedRows());
  } else if (index.isValid()) {
    m_selected = {index};
  }

  // the default command first, the selection's commands next and the whole
  // list's last, as the mod list orders them
  ModInfo::Ptr origin;
  if (m_index.isValid()) {
    unsigned int modInfoIndex =
        ModInfo::getIndex(m_core.pluginList()->origin(m_index.data().toString()));
    // this is to avoid showing the option on game files like skyrim.esm
    if (modInfoIndex != UINT_MAX) {
      origin = ModInfo::getByIndex(modInfoIndex);
      if (!origin->isForeign() && m_selected.size() == 1) {
        QAction* infoAction = addAction(tr("Origin information..."), [=]() {
          openOriginInformation(index);
        });
        infoAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/information.svg"));
        setDefaultMenuAction(this, infoAction);
        addSeparator();
      }
    }
  }

  if (!m_selected.isEmpty()) {
    QAction* enableSelectedAction = addAction(tr("Enable selected"), [=]() {
      m_core.pluginList()->setEnabled(m_selected, true);
    });
    enableSelectedAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"));
    QAction* disableSelectedAction = addAction(tr("Disable selected"), [=]() {
      m_core.pluginList()->setEnabled(m_selected, false);
    });
    disableSelectedAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/disable.svg"));
    addMenu(createSendToContextMenu());

    bool hasLocked   = false;
    bool hasUnlocked = false;
    for (auto& idx : m_selected) {
      if (m_core.pluginList()->isEnabled(idx.row())) {
        if (m_core.pluginList()->isESPLocked(idx.row())) {
          hasLocked = true;
        } else {
          hasUnlocked = true;
        }
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
    const QString plugin = m_index.data().toString();
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
    m_core.pluginList()->sendToPriority(m_selected, 0);
  });
  menu->addAction(tr("Bottom"), [=]() {
    m_core.pluginList()->sendToPriority(m_selected, INT_MAX);
  });
  menu->addAction(tr("Priority..."), [=]() {
    sendPluginsToPriority(m_selected);
  });
  return menu;
}

void PluginListContextMenu::sendPluginsToPriority(const QModelIndexList& indices)
{
  bool ok;
  int newPriority = QInputDialog::getInt(m_view->topLevelWidget(), tr("Set Priority"),
                                         tr("Set the priority of the selected plugins"),
                                         0, 0, INT_MAX, 1, &ok);
  if (!ok)
    return;

  m_core.pluginList()->sendToPriority(m_selected, newPriority);
}

void PluginListContextMenu::setESPLock(const QModelIndexList& indices, bool locked)
{
  for (auto& idx : indices) {
    if (m_core.pluginList()->isEnabled(idx.row())) {
      m_core.pluginList()->lockESPIndex(idx.row(), locked);
    }
  }
}

void PluginListContextMenu::openOriginExplorer(const QModelIndexList& indices)
{
  for (auto& idx : indices) {
    QString fileName      = idx.data().toString();
    unsigned int modIndex = ModInfo::getIndex(m_core.pluginList()->origin(fileName));
    if (modIndex == UINT_MAX) {
      continue;
    }
    ModInfo::Ptr modInfo = ModInfo::getByIndex(modIndex);
    shell::Explore(modInfo->absolutePath());
  }
}

void PluginListContextMenu::openOriginInformation(const QModelIndex& index)
{
  try {
    QString fileName      = index.data().toString();
    unsigned int modIndex = ModInfo::getIndex(m_core.pluginList()->origin(fileName));
    ModInfo::Ptr modInfo  = ModInfo::getByIndex(modIndex);

    if (modInfo->isRegular() || modInfo->isOverwrite()) {
      emit openModInformation(modIndex);
    }
  } catch (const std::exception& e) {
    reportError(e.what());
  }
}
