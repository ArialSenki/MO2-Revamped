#include "modlistcontextmenu.h"

#include <report.h>

#include "modlist.h"
#include "modlistview.h"
#include "modlistviewactions.h"
#include "organizercore.h"

#include <QIcon>
#include <QSize>

#include <utility>

using namespace MOBase;

ModListGlobalContextMenu::ModListGlobalContextMenu(OrganizerCore& core,
                                                   ModListView* view, QWidget* parent)
    : ModListGlobalContextMenu(core, view, QModelIndex(), parent)
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(
    OrganizerCore& core, ModListView* view, QWidget* parent,
    std::function<void()> profileBackupRemoval)
    : ModListGlobalContextMenu(core, view, QModelIndex(), parent,
                               profileBackupRemoval)
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(
    OrganizerCore& core, ModListView* view, QWidget* parent,
    std::function<void()> profileBackupCreation,
    std::function<void()> profileBackupRestoration,
    std::function<void()> profileBackupRemoval)
    : ModListGlobalContextMenu(core, view, QModelIndex(), parent,
                               profileBackupCreation,
                               profileBackupRestoration,
                               profileBackupRemoval, {}, {}, {})
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(
    OrganizerCore& core, ModListView* view, QWidget* parent,
    std::function<void()> profileBackupCreation,
    std::function<void()> profileBackupRestoration,
    std::function<void()> profileBackupRemoval,
    std::function<void()> loadOrderBackupCreation,
    std::function<void()> loadOrderBackupRestoration,
    std::function<void()> loadOrderBackupRemoval)
    : ModListGlobalContextMenu(core, view, QModelIndex(), parent,
                               profileBackupCreation,
                               profileBackupRestoration,
                               profileBackupRemoval,
                               loadOrderBackupCreation,
                               loadOrderBackupRestoration,
                               loadOrderBackupRemoval)
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(OrganizerCore& core,
                                                   ModListView* view,
                                                   const QModelIndex& index,
                                                   QWidget* parent)
    : ModListGlobalContextMenu(core, view, index, parent, {})
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(
    OrganizerCore& core, ModListView* view, const QModelIndex& index,
                                                   QWidget* parent,
                                                   std::function<void()> profileBackupRemoval)
    : ModListGlobalContextMenu(core, view, index, parent, {}, {},
                               profileBackupRemoval, {}, {}, {})
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(
    OrganizerCore& core, ModListView* view, const QModelIndex& index,
    QWidget* parent, std::function<void()> profileBackupCreation,
    std::function<void()> profileBackupRestoration,
    std::function<void()> profileBackupRemoval)
    : ModListGlobalContextMenu(core, view, index, parent,
                               profileBackupCreation,
                               profileBackupRestoration,
                               profileBackupRemoval, {}, {}, {})
{}

ModListGlobalContextMenu::ModListGlobalContextMenu(
    OrganizerCore& core, ModListView* view, const QModelIndex& index,
    QWidget* parent, std::function<void()> profileBackupCreation,
    std::function<void()> profileBackupRestoration,
    std::function<void()> profileBackupRemoval,
    std::function<void()> loadOrderBackupCreation,
    std::function<void()> loadOrderBackupRestoration,
    std::function<void()> loadOrderBackupRemoval)
    : QMenu(parent),
      m_ProfileBackupCreation(std::move(profileBackupCreation)),
      m_ProfileBackupRestoration(std::move(profileBackupRestoration)),
      m_ProfileBackupRemoval(std::move(profileBackupRemoval)),
      m_LoadOrderBackupCreation(std::move(loadOrderBackupCreation)),
      m_LoadOrderBackupRestoration(std::move(loadOrderBackupRestoration)),
      m_LoadOrderBackupRemoval(std::move(loadOrderBackupRemoval))
{
  connect(this, &QMenu::aboutToShow, [=, &core] {
    populate(core, view, index);
  });
}

void ModListGlobalContextMenu::populate(OrganizerCore& core, ModListView* view,
                                        const QModelIndex& index)
{
  clear();

  auto modIndex = index.data(ModList::IndexRole);
  if (modIndex.isValid() && view->sortColumn() == ModList::COL_PRIORITY) {
    auto info = ModInfo::getByIndex(modIndex.toInt());
    if (!info->isBackup()) {
      addSection(tr("Add to list"));

      // the mod are not created/installed at the same position depending
      // on the clicked mod and the sort order
      QString installText = tr("Install mod above... ");
      QString createText  = tr("Create empty mod above");
      if (info->isSeparator()) {
        installText = tr("Install mod inside... ");
        createText  = tr("Create empty mod inside");
      } else if (view->sortOrder() == Qt::DescendingOrder) {
        installText = tr("Install mod below... ");
        createText  = tr("Create empty mod below");
      }

      QAction* installAction = addAction(installText, [=]() {
        view->actions().installMod("", index);
      });
      installAction->setIcon(QIcon(":/MO/gui/mainwindow/install.svg"));
      QAction* createAction = addAction(createText, [=]() {
        view->actions().createEmptyMod(index);
      });
      createAction->setIcon(QIcon(":/MO/gui/contextmenu/create-mod.svg"));
      QAction* separatorAction = addAction(tr("Create separator above"), [=]() {
        view->actions().createSeparator(index);
      });
      separatorAction->setIcon(QIcon(":/MO/gui/contextmenu/separator.svg"));
    }
  } else {
    addSection(tr("Add to list"));
    QAction* installAction = addAction(tr("Install mod..."), [=]() {
      view->actions().installMod();
    });
    installAction->setIcon(QIcon(":/MO/gui/mainwindow/install.svg"));
    QAction* createAction = addAction(tr("Create empty mod"), [=]() {
      view->actions().createEmptyMod();
    });
    createAction->setIcon(QIcon(":/MO/gui/contextmenu/create-mod.svg"));
    QAction* separatorAction = addAction(tr("Create separator"), [=]() {
      view->actions().createSeparator();
    });
    separatorAction->setIcon(QIcon(":/MO/gui/contextmenu/separator.svg"));
  }

  if (view->hasCollapsibleSeparators()) {
    addSection(tr("List display"));
    QAction* collapseAction = addAction(tr("Collapse all"), view, &QTreeView::collapseAll);
    collapseAction->setIcon(QIcon(":/MO/gui/contextmenu/collapse.svg"));
    QAction* expandAction = addAction(tr("Expand all"), view, &QTreeView::expandAll);
    expandAction->setIcon(QIcon(":/MO/gui/contextmenu/expand.svg"));
  }

  addSection(tr("Bulk actions"));

  QString enableTxt = tr("Enable all"), disableTxt = tr("Disable all");

  if (view->isFilterActive()) {
    enableTxt  = tr("Enable all matching mods");
    disableTxt = tr("Disable all matching mods");
  }

  QAction* enableAction = addAction(enableTxt, [=] {
    view->actions().setAllMatchingModsEnabled(true);
  });
  enableAction->setIcon(QIcon(":/MO/gui/contextmenu/enable.svg"));
  QAction* disableAction = addAction(disableTxt, [=] {
    view->actions().setAllMatchingModsEnabled(false);
  });
  disableAction->setIcon(QIcon(":/MO/gui/contextmenu/disable.svg"));

  addSection(tr("Maintenance"));
  QAction* updatesAction = addAction(tr("Check for updates"), [=]() {
    view->actions().checkModsForUpdates();
  });
  updatesAction->setIcon(QIcon(":/MO/gui/contextmenu/update-check.svg"));
  QAction* categoriesAction = addAction(tr("Auto assign categories"), [=]() {
    view->actions().assignCategories();
  });
  categoriesAction->setIcon(QIcon(":/MO/gui/contextmenu/categories.svg"));
  QAction* refreshAction = addAction(tr("Refresh"), &core, &OrganizerCore::refresh);
  refreshAction->setIcon(QIcon(":/MO/gui/mainwindow/refresh.svg"));
  QAction* exportAction = addAction(tr("Export to csv..."), [=]() {
    view->actions().exportModListCSV();
  });
  exportAction->setIcon(QIcon(":/MO/gui/contextmenu/export.svg"));

  if (m_ProfileBackupCreation || m_ProfileBackupRestoration ||
      m_ProfileBackupRemoval) {
    addSection(tr("Mod list backups"));
    if (m_ProfileBackupCreation) {
      QAction* createBackupAction = addAction(tr("Create mod list backup"));
      createBackupAction->setIcon(QIcon(":/MO/gui/contextmenu/backup.svg"));
      createBackupAction->setToolTip(
          tr("Save a backup of this profile's current mod list."));
      connect(createBackupAction, &QAction::triggered, this,
              [this] { m_ProfileBackupCreation(); });
    }
    if (m_ProfileBackupRestoration) {
      QAction* restoreBackupAction =
          addAction(tr("Restore mod list backup..."));
      restoreBackupAction->setIcon(QIcon(":/MO/gui/mainwindow/restore.svg"));
      restoreBackupAction->setToolTip(
          tr("Choose a saved backup for this profile's mod list."));
      connect(restoreBackupAction, &QAction::triggered, this,
              [this] { m_ProfileBackupRestoration(); });
    }
    if (m_ProfileBackupRemoval) {
      QAction* deleteBackupAction = addAction(tr("Delete mod list backup..."));
      deleteBackupAction->setIcon(QIcon(":/MO/gui/contextmenu/remove.svg"));
      deleteBackupAction->setToolTip(
          tr("Choose and permanently delete a saved mod list backup."));
      connect(deleteBackupAction, &QAction::triggered, this,
              [this] { m_ProfileBackupRemoval(); });
    }
  }

  if (m_LoadOrderBackupCreation || m_LoadOrderBackupRestoration ||
      m_LoadOrderBackupRemoval) {
    addSection(tr("Plugin order backups"));
    if (m_LoadOrderBackupCreation) {
      QAction* createBackupAction = addAction(tr("Create plugin order backup"));
      createBackupAction->setIcon(QIcon(":/MO/gui/contextmenu/backup.svg"));
      createBackupAction->setToolTip(
          tr("Save this profile's current plugin load order."));
      connect(createBackupAction, &QAction::triggered, this,
              [this] { m_LoadOrderBackupCreation(); });
    }
    if (m_LoadOrderBackupRestoration) {
      QAction* restoreBackupAction =
          addAction(tr("Restore plugin order backup..."));
      restoreBackupAction->setIcon(QIcon(":/MO/gui/mainwindow/restore.svg"));
      restoreBackupAction->setToolTip(
          tr("Choose a saved plugin load order for this profile."));
      connect(restoreBackupAction, &QAction::triggered, this,
              [this] { m_LoadOrderBackupRestoration(); });
    }
    if (m_LoadOrderBackupRemoval) {
      QAction* deleteBackupAction =
          addAction(tr("Delete plugin order backup..."));
      deleteBackupAction->setIcon(QIcon(":/MO/gui/contextmenu/remove.svg"));
      deleteBackupAction->setToolTip(
          tr("Choose and permanently delete a saved plugin load order."));
      connect(deleteBackupAction, &QAction::triggered, this,
              [this] { m_LoadOrderBackupRemoval(); });
    }
  }
}

ModListChangeCategoryMenu::ModListChangeCategoryMenu(CategoryFactory* categories,
                                                     ModInfo::Ptr mod, QMenu* parent)
    : QMenu(tr("Change Categories"), parent)
{
  populate(this, categories, mod);
}

std::vector<std::pair<int, bool>> ModListChangeCategoryMenu::categories() const
{
  return categories(this);
}

std::vector<std::pair<int, bool>>
ModListChangeCategoryMenu::categories(const QMenu* menu) const
{
  std::vector<std::pair<int, bool>> cats;
  for (QAction* action : menu->actions()) {
    if (action->menu() != nullptr) {
      auto pcats = categories(action->menu());
      cats.insert(cats.end(), pcats.begin(), pcats.end());
    } else {
      QWidgetAction* widgetAction = qobject_cast<QWidgetAction*>(action);
      if (widgetAction != nullptr) {
        QCheckBox* checkbox = qobject_cast<QCheckBox*>(widgetAction->defaultWidget());
        cats.emplace_back(widgetAction->data().toInt(), checkbox->isChecked());
      }
    }
  }
  return cats;
}

bool ModListChangeCategoryMenu::populate(QMenu* menu, CategoryFactory* factory,
                                         ModInfo::Ptr mod, int targetId)
{
  const std::set<int>& categories = mod->getCategories();

  bool childEnabled = false;

  for (unsigned int i = 1; i < factory->numCategories(); ++i) {
    if (factory->getParentID(i) == targetId) {
      QMenu* targetMenu = menu;
      if (factory->hasChildren(i)) {
        targetMenu = menu->addMenu(factory->getCategoryName(i).replace('&', "&&"));
      }

      int id = factory->getCategoryID(i);
      QScopedPointer<QCheckBox> checkBox(new QCheckBox(targetMenu));
      bool enabled = categories.find(id) != categories.end();
      checkBox->setText(factory->getCategoryName(i).replace('&', "&&"));
      if (enabled) {
        childEnabled = true;
      }
      checkBox->setChecked(enabled ? Qt::Checked : Qt::Unchecked);

      QScopedPointer<QWidgetAction> checkableAction(new QWidgetAction(targetMenu));
      checkableAction->setDefaultWidget(checkBox.take());
      checkableAction->setData(id);
      targetMenu->addAction(checkableAction.take());

      if (factory->hasChildren(i)) {
        if (populate(targetMenu, factory, mod, factory->getCategoryID(i)) || enabled) {
          targetMenu->setIcon(QIcon(":/MO/gui/resources/check.png"));
        }
      }
    }
  }
  return childEnabled;
}

ModListPrimaryCategoryMenu::ModListPrimaryCategoryMenu(CategoryFactory* categories,
                                                       ModInfo::Ptr mod, QMenu* parent)
    : QMenu(tr("Primary Category"), parent)
{
  connect(this, &QMenu::aboutToShow, [=]() {
    populate(categories, mod);
  });
}

void ModListPrimaryCategoryMenu::populate(const CategoryFactory* factory,
                                          ModInfo::Ptr mod)
{
  clear();
  const std::set<int>& categories = mod->getCategories();
  for (int categoryID : categories) {
    int catIdx            = factory->getCategoryIndex(categoryID);
    QWidgetAction* action = new QWidgetAction(this);
    try {
      QRadioButton* categoryBox =
          new QRadioButton(factory->getCategoryName(catIdx).replace('&', "&&"), this);
      categoryBox->setChecked(categoryID == mod->primaryCategory());
      action->setDefaultWidget(categoryBox);
      action->setData(categoryID);
    } catch (const std::exception& e) {
      log::error("failed to create category checkbox: {}", e.what());
    }

    action->setData(categoryID);
    addAction(action);
  }
}

int ModListPrimaryCategoryMenu::primaryCategory() const
{
  for (QAction* action : actions()) {
    QWidgetAction* widgetAction = qobject_cast<QWidgetAction*>(action);
    if (widgetAction) {
      QRadioButton* button = qobject_cast<QRadioButton*>(widgetAction->defaultWidget());
      if (button && button->isChecked()) {
        return widgetAction->data().toInt();
      }
    }
  }
  return -1;
}

ModListContextMenu::ModListContextMenu(const QModelIndex& index, OrganizerCore& core,
                                       CategoryFactory* categories, ModListView* view)
    : QMenu(view), m_core(core), m_categories(categories),
      m_index(index.model() == view->model() ? view->indexViewToModel(index) : index),
      m_view(view), m_actions(view->actions())
{
  if (view->selectionModel()->hasSelection()) {
    m_selected = view->indexViewToModel(view->selectionModel()->selectedRows());
  } else {
    m_selected = {index};
  }

  ModInfo::Ptr info = ModInfo::getByIndex(index.data(ModList::IndexRole).toInt());

  auto viewIndex = view->indexModelToView(m_index);
  if (view->model()->hasChildren(viewIndex)) {
    bool expanded = view->isExpanded(viewIndex);
    addSeparator();
    addAction(tr("Collapse all"), view, &QTreeView::collapseAll);
    addAction(tr("Collapse others"), [=]() {
      m_view->collapseAll();
      m_view->setExpanded(viewIndex, expanded);
    });
    addAction(tr("Expand all"), view, &QTreeView::expandAll);
  }

  addSeparator();

  // Add type-specific items
  if (info->isOverwrite()) {
    addOverwriteActions(info);
  } else if (info->isBackup()) {
    addBackupActions(info);
  } else if (info->isSeparator()) {
    addSeparatorActions(info);
  } else if (info->isForeign()) {
    addForeignActions(info);
  } else {
    addRegularActions(info);
  }

  // add information for all except foreign
  if (!info->isForeign() && !info->isOverwrite()) {
    QAction* infoAction = addAction(tr("Information..."), [=]() {
      view->actions().displayModInformation(m_index.data(ModList::IndexRole).toInt());
    });
    infoAction->setIcon(QIcon(":/MO/gui/contextmenu/information.svg"));
    setDefaultAction(infoAction);
  }
}

void ModListContextMenu::addMenuAsPushButton(QMenu* menu)
{
  QPushButton* pushBtn = new QPushButton(menu->title());
  pushBtn->setIcon(menu->icon());
  pushBtn->setIconSize(QSize(16, 16));
  pushBtn->setMenu(menu);
  QWidgetAction* action = new QWidgetAction(this);
  action->setDefaultWidget(pushBtn);
  addAction(action);
}

void ModListContextMenu::addSendToContextMenu()
{
  static const std::vector overwritten_flags{
      ModInfo::EConflictFlag::FLAG_CONFLICT_MIXED,
      ModInfo::EConflictFlag::FLAG_CONFLICT_OVERWRITTEN,
      ModInfo::EConflictFlag::FLAG_CONFLICT_REDUNDANT};

  static const std::vector overwrite_flags{
      ModInfo::EConflictFlag::FLAG_CONFLICT_MIXED,
      ModInfo::EConflictFlag::FLAG_CONFLICT_OVERWRITE};

  bool overwrite = false, overwritten = false;
  for (auto& idx : m_selected) {
    auto index = idx.data(ModList::IndexRole);
    if (index.isValid()) {
      auto info  = ModInfo::getByIndex(index.toInt());
      auto flags = info->getConflictFlags();
      if (std::find_first_of(flags.begin(), flags.end(), overwritten_flags.begin(),
                             overwritten_flags.end()) != flags.end()) {
        overwritten = true;
      }
      if (std::find_first_of(flags.begin(), flags.end(), overwrite_flags.begin(),
                             overwrite_flags.end()) != flags.end()) {
        overwrite = true;
      }
    }
  }

  QMenu* menu = new QMenu(m_view);
  menu->setTitle(tr("Send to... "));
  menu->addAction(tr("Lowest priority"), [this] {
    m_actions.sendModsToTop(m_selected);
  });
  menu->addAction(tr("Highest priority"), [this] {
    m_actions.sendModsToBottom(m_selected);
  });
  menu->addAction(tr("Priority..."), [this] {
    m_actions.sendModsToPriority(m_selected);
  });
  menu->addAction(tr("Separator..."), [this] {
    m_actions.sendModsToSeparator(m_selected);
  });
  if (overwrite) {
    menu->addAction(tr("First conflict"), [this] {
      m_actions.sendModsToFirstConflict(m_selected);
    });
  }
  if (overwritten) {
    menu->addAction(tr("Last conflict"), [this] {
      m_actions.sendModsToLastConflict(m_selected);
    });
  }
  addMenu(menu);
}

void ModListContextMenu::addCategoryContextMenus(ModInfo::Ptr mod)
{
  ModListChangeCategoryMenu* categoriesMenu =
      new ModListChangeCategoryMenu(m_categories, mod, this);
  categoriesMenu->setIcon(QIcon(":/MO/gui/contextmenu/categories.svg"));
  connect(categoriesMenu, &QMenu::aboutToHide, [=]() {
    m_actions.setCategories(m_selected, m_index, categoriesMenu->categories());
  });
  addMenuAsPushButton(categoriesMenu);

  // A primary category can only be changed when the mod has alternatives.
  // Avoid adding an empty menu for uncategorized mods or a no-op menu for one category.
  if (mod->getCategories().size() > 1) {
    ModListPrimaryCategoryMenu* primaryCategoryMenu =
        new ModListPrimaryCategoryMenu(m_categories, mod, this);
    primaryCategoryMenu->setIcon(QIcon(":/MO/gui/contextmenu/primary-category.svg"));
    connect(primaryCategoryMenu, &QMenu::aboutToHide, [=]() {
      int category = primaryCategoryMenu->primaryCategory();
      if (category != -1) {
        m_actions.setPrimaryCategory(m_selected, category);
      }
    });
    addMenuAsPushButton(primaryCategoryMenu);
  }
}

void ModListContextMenu::addOverwriteActions(ModInfo::Ptr mod)
{
  setToolTipsVisible(true);

  QAction* manageAction = addAction(tr("Review and manage files..."), [=]() {
    m_actions.displayModInformation(mod, ModInfo::getIndex(mod->name()));
  });
  manageAction->setToolTip(
      tr("Browse files in Overwrite, create folders, rename items, or move files "
         "into regular mods."));
  setDefaultAction(manageAction);

  if (QDir(mod->absolutePath()).count() > 2) {
    addSection(tr("Organize files"));
    QAction* syncAction = addAction(tr("Sync to Mods..."), [=]() {
      m_core.syncOverwrite();
    });
    syncAction->setToolTip(
        tr("Review and assign files from Overwrite to regular mods."));

    QAction* createModAction = addAction(tr("Create mod from Overwrite..."), [=]() {
      m_actions.createModFromOverwrite();
    });
    createModAction->setToolTip(
        tr("Move all files from Overwrite into a new regular mod."));

    QAction* moveAction = addAction(tr("Move files to an existing mod..."), [=]() {
      m_actions.moveOverwriteContentToExistingMod();
    });
    moveAction->setToolTip(
        tr("Choose a regular mod and move all files from Overwrite into it."));

    addSection(tr("Cleanup"));
    QAction* clearAction = addAction(tr("Clear Overwrite..."), [=]() {
      m_actions.clearOverwrite();
    });
    clearAction->setToolTip(
        tr("Permanently delete every file and folder currently in Overwrite."));
  }

  addSeparator();
  addAction(tr("Open Overwrite folder in Explorer"), [=]() {
    m_actions.openExplorer(m_selected);
  });
}

void ModListContextMenu::addSeparatorActions(ModInfo::Ptr mod)
{
  addCategoryContextMenus(mod);
  addSeparator();

  addAction(tr("Rename Separator..."), [=]() {
    m_actions.renameMod(m_index);
  });
  addAction(tr("Remove Separator..."), [=]() {
    m_actions.removeMods(m_selected);
  });
  addSeparator();

  if (m_view->sortColumn() == ModList::COL_PRIORITY) {
    addSendToContextMenu();
    addSeparator();
  }
  addAction(tr("Select Color..."), [=]() {
    m_actions.setColor(m_selected, m_index);
  });

  if (mod->color().isValid()) {
    addAction(tr("Reset Color"), [=]() {
      m_actions.resetColor(m_selected);
    });
  }

  addSeparator();
}

void ModListContextMenu::addForeignActions(ModInfo::Ptr mod)
{
  if (m_view->sortColumn() == ModList::COL_PRIORITY) {
    addSendToContextMenu();
  }
}

void ModListContextMenu::addBackupActions(ModInfo::Ptr mod)
{
  auto flags = mod->getFlags();
  addAction(tr("Restore Backup"), [=]() {
    m_actions.restoreBackup(m_index);
  });
  addAction(tr("Remove Backup..."), [=]() {
    m_actions.removeMods(m_selected);
  });
  addSeparator();
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_INVALID) != flags.end()) {
    addAction(tr("Ignore missing data"), [=]() {
      m_actions.ignoreMissingData(m_selected);
    });
  }
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_ALTERNATE_GAME) !=
      flags.end()) {
    addAction(tr("Mark as converted/working"), [=]() {
      m_actions.markConverted(m_selected);
    });
  }
  addSeparator();
  if (mod->nexusId() > 0) {
    addAction(tr("Visit on Nexus"), [=]() {
      m_actions.visitOnNexus(m_selected);
    });
  }

  const auto url = mod->parseCustomURL();
  if (url.isValid()) {
    addAction(tr("Visit on %1").arg(url.host()), [=]() {
      m_actions.visitWebPage(m_selected);
    });
  }

  addAction(tr("Open in Explorer"), [=]() {
    m_actions.openExplorer(m_selected);
  });
}

void ModListContextMenu::addRegularActions(ModInfo::Ptr mod)
{
  auto flags = mod->getFlags();

  addCategoryContextMenus(mod);
  addSeparator();

  if (mod->downgradeAvailable()) {
    addAction(tr("Change versioning scheme"), [=]() {
      m_actions.changeVersioningScheme(m_index);
    });
  }

  if (mod->nexusId() > 0) {
    QAction* forceCheckAction = addAction(tr("Force-check updates"), [=]() {
      m_actions.checkModsForUpdates(m_selected);
    });
    forceCheckAction->setIcon(QIcon(":/MO/gui/contextmenu/updates.svg"));
  }
  if (mod->updateIgnored()) {
    addAction(tr("Un-ignore update"), [=]() {
      m_actions.setIgnoreUpdate(m_selected, false);
    });
  } else {
    if (mod->updateAvailable() || mod->downgradeAvailable()) {
      addAction(tr("Ignore update"), [=]() {
        m_actions.setIgnoreUpdate(m_selected, true);
      });
    }
  }
  addSeparator();

  QAction* enableAction = addAction(tr("Enable selected"), [=]() {
    m_core.modList()->setActive(m_selected, true);
  });
  enableAction->setIcon(QIcon(":/MO/gui/contextmenu/enable.svg"));
  QAction* disableAction = addAction(tr("Disable selected"), [=]() {
    m_core.modList()->setActive(m_selected, false);
  });
  disableAction->setIcon(QIcon(":/MO/gui/contextmenu/disable.svg"));

  addSeparator();

  if (m_view->sortColumn() == ModList::COL_PRIORITY) {
    addSendToContextMenu();
    addSeparator();
  }

  QAction* renameAction = addAction(tr("Rename Mod..."), [=]() {
    m_actions.renameMod(m_index);
  });
  renameAction->setIcon(QIcon(":/MO/gui/contextmenu/rename.svg"));
  QAction* reinstallAction = addAction(tr("Reinstall Mod"), [=]() {
    m_actions.reinstallMod(m_index);
  });
  reinstallAction->setIcon(QIcon(":/MO/gui/contextmenu/reinstall.svg"));
  QAction* removeAction = addAction(tr("Remove Mod..."), [=]() {
    m_actions.removeMods(m_selected);
  });
  removeAction->setIcon(QIcon(":/MO/gui/contextmenu/remove.svg"));
  QAction* backupAction = addAction(tr("Create Backup"), [=]() {
    m_actions.createBackup(m_index);
  });
  backupAction->setIcon(QIcon(":/MO/gui/contextmenu/backup.svg"));

  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_HIDDEN_FILES) !=
      flags.end()) {
    addAction(tr("Restore hidden files"), [=]() {
      m_actions.restoreHiddenFiles(m_selected);
    });
  }

  addSeparator();

  if (m_index.column() == ModList::COL_NOTES) {
    addAction(tr("Select Color..."), [=]() {
      m_actions.setColor(m_selected, m_index);
    });
    if (mod->color().isValid()) {
      addAction(tr("Reset Color"), [=]() {
        m_actions.resetColor(m_selected);
      });
    }
    addSeparator();
  }

  if (mod->nexusId() > 0 && Settings::instance().nexus().endorsementIntegration()) {
    switch (mod->endorsedState()) {
    case EndorsedState::ENDORSED_TRUE: {
      QAction* unendorseAction = addAction(tr("Un-Endorse"), [=]() {
        m_actions.setEndorsed(m_selected, false);
      });
      unendorseAction->setIcon(QIcon(":/MO/gui/contextmenu/endorse.svg"));
    } break;
    case EndorsedState::ENDORSED_FALSE: {
      QAction* endorseAction = addAction(tr("Endorse"), [=]() {
        m_actions.setEndorsed(m_selected, true);
      });
      endorseAction->setIcon(QIcon(":/MO/gui/contextmenu/endorse.svg"));
      QAction* wontEndorseAction = addAction(tr("Won't endorse"), [=]() {
        m_actions.willNotEndorsed(m_selected);
      });
      wontEndorseAction->setIcon(QIcon(":/MO/gui/contextmenu/wont-endorse.svg"));
    } break;
    case EndorsedState::ENDORSED_NEVER: {
      QAction* endorseAction = addAction(tr("Endorse"), [=]() {
        m_actions.setEndorsed(m_selected, true);
      });
      endorseAction->setIcon(QIcon(":/MO/gui/contextmenu/endorse.svg"));
    } break;
    default: {
      QAction* action = new QAction(tr("Endorsement state unknown"), this);
      action->setEnabled(false);
      addAction(action);
    } break;
    }
  }

  if (mod->nexusId() > 0 &&
      (mod->getNexusCategory() > 0 || !mod->installationFile().isEmpty()) &&
      !mod->isSeparator()) {
    QAction* remapCategoryAction =
        addAction(tr("Remap Category (From Nexus)"), [=]() {
          m_actions.remapCategory(m_selected);
        });
    remapCategoryAction->setIcon(QIcon(":/MO/gui/contextmenu/remap-category.svg"));
  }

  if (mod->nexusId() > 0 && Settings::instance().nexus().trackedIntegration()) {
    switch (mod->trackedState()) {
    case TrackedState::TRACKED_FALSE: {
      QAction* startTrackingAction = addAction(tr("Start tracking"), [=]() {
        m_actions.setTracked(m_selected, true);
      });
      startTrackingAction->setIcon(QIcon(":/MO/gui/contextmenu/tracking.svg"));
    } break;
    case TrackedState::TRACKED_TRUE: {
      QAction* stopTrackingAction = addAction(tr("Stop tracking"), [=]() {
        m_actions.setTracked(m_selected, false);
      });
      stopTrackingAction->setIcon(QIcon(":/MO/gui/contextmenu/tracking.svg"));
    } break;
    default: {
      QAction* action = new QAction(tr("Tracked state unknown"), this);
      action->setEnabled(false);
      addAction(action);
    } break;
    }
  }

  addSeparator();

  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_INVALID) != flags.end()) {
    addAction(tr("Ignore missing data"), [=]() {
      m_actions.ignoreMissingData(m_selected);
    });
  }

  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_ALTERNATE_GAME) !=
      flags.end()) {
    addAction(tr("Mark as converted/working"), [=]() {
      m_actions.markConverted(m_selected);
    });
  }

  addSeparator();

  if (mod->nexusId() > 0) {
    QAction* visitNexusAction = addAction(tr("Visit on Nexus"), [=]() {
      m_actions.visitOnNexus(m_selected);
    });
    visitNexusAction->setIcon(QIcon(":/MO/gui/contextmenu/visit.svg"));
  }

  const auto url = mod->parseCustomURL();
  if (url.isValid()) {
    QAction* visitCustomUrlAction =
        addAction(tr("Visit on %1").arg(url.host()), [=]() {
          m_actions.visitWebPage(m_selected);
        });
    visitCustomUrlAction->setIcon(QIcon(":/MO/gui/contextmenu/visit.svg"));
  }

  QAction* openExplorerAction = addAction(tr("Open in Explorer"), [=]() {
    m_actions.openExplorer(m_selected);
  });
  openExplorerAction->setIcon(QIcon(":/MO/gui/contextmenu/explorer.svg"));
}
