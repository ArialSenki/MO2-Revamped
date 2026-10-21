#include "modlistcontextmenu.h"
#include "fluenticons.h"
#include "menuutils.h"

#include <report.h>

#include "modlist.h"
#include "modlistview.h"
#include "modlistviewactions.h"
#include "downloadmanager.h"
#include "nexusinterface.h"
#include "organizercore.h"
#include "profile.h"
#include "settings.h"

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

void ModListGlobalContextMenu::addInsertActions(QMenu* menu, ModListView* view,
                                                ModInfo::Ptr info,
                                                const QModelIndex& index)
{
  // the mod are not created/installed at the same position depending
  // on the clicked mod and the sort order
  QString installText = tr("Install mod above...");
  QString createText  = tr("Create empty mod above");
  if (info->isSeparator()) {
    installText = tr("Install mod inside...");
    createText  = tr("Create empty mod inside");
  } else if (view->sortOrder() == Qt::DescendingOrder) {
    installText = tr("Install mod below...");
    createText  = tr("Create empty mod below");
  }

  QAction* installAction = menu->addAction(installText, [=]() {
    view->actions().installMod("", index);
  });
  installAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/install.svg"));
  QAction* createAction = menu->addAction(createText, [=]() {
    view->actions().createEmptyMod(index);
  });
  createAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/create-mod.svg"));
  QAction* separatorAction = menu->addAction(tr("Create separator above"), [=]() {
    view->actions().createSeparator(index);
  });
  separatorAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/separator.svg"));
}

void ModListGlobalContextMenu::populate(OrganizerCore& core, ModListView* view,
                                        const QModelIndex& index)
{
  clear();

  auto modIndex = index.data(ModList::IndexRole);
  if (modIndex.isValid() && view->sortColumn() == ModList::COL_PRIORITY) {
    auto info = ModInfo::getByIndex(modIndex.toInt());
    if (!info->isBackup()) {
      addMenuSection(this, tr("Add to list"));
      addInsertActions(this, view, info, index);
    }
  } else {
    addMenuSection(this, tr("Add to list"));
    QAction* installAction = addAction(tr("Install mod..."), [=]() {
      view->actions().installMod();
    });
    installAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/install.svg"));
    QAction* createAction = addAction(tr("Create empty mod"), [=]() {
      view->actions().createEmptyMod();
    });
    createAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/create-mod.svg"));
    QAction* separatorAction = addAction(tr("Create separator"), [=]() {
      view->actions().createSeparator();
    });
    separatorAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/separator.svg"));
  }

  if (view->hasCollapsibleSeparators()) {
    addMenuSection(this, tr("List display"));
    QAction* collapseAction = addAction(tr("Collapse all"), view, &QTreeView::collapseAll);
    collapseAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/collapse.svg"));
    QAction* expandAction = addAction(tr("Expand all"), view, &QTreeView::expandAll);
    expandAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/expand.svg"));
  }

  addMenuSection(this, tr("Bulk actions"));

  QString enableTxt = tr("Enable all"), disableTxt = tr("Disable all");

  if (view->isFilterActive()) {
    enableTxt  = tr("Enable all matching mods");
    disableTxt = tr("Disable all matching mods");
  }

  QAction* enableAction = addAction(enableTxt, [=] {
    view->actions().setAllMatchingModsEnabled(true);
  });
  enableAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"));
  QAction* disableAction = addAction(disableTxt, [=] {
    view->actions().setAllMatchingModsEnabled(false);
  });
  disableAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/disable.svg"));

  addMenuSection(this, tr("Maintenance"));
  QAction* updatesAction = addAction(tr("Check for updates"), [=]() {
    view->actions().checkModsForUpdates();
  });
  updatesAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/update-check.svg"));
  QAction* categoriesAction = addAction(tr("Auto assign categories"), [=]() {
    view->actions().assignCategories();
  });
  categoriesAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/categories.svg"));
  QAction* refreshAction = addAction(tr("Refresh"), &core, &OrganizerCore::refresh);
  refreshAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/refresh.svg"));
  QAction* exportAction = addAction(tr("Export to CSV..."), [=]() {
    view->actions().exportModListCSV();
  });
  exportAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/export.svg"));

  if (m_ProfileBackupCreation || m_ProfileBackupRestoration ||
      m_ProfileBackupRemoval) {
    addMenuSection(this, tr("Mod list backups"));
    if (m_ProfileBackupCreation) {
      QAction* createBackupAction = addAction(tr("Create mod list backup"));
      createBackupAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/backup.svg"));
      createBackupAction->setToolTip(
          tr("Save a backup of this profile's current mod list."));
      connect(createBackupAction, &QAction::triggered, this,
              [this] { m_ProfileBackupCreation(); });
    }
    if (m_ProfileBackupRestoration) {
      QAction* restoreBackupAction =
          addAction(tr("Restore mod list backup..."));
      restoreBackupAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/restore.svg"));
      restoreBackupAction->setToolTip(
          tr("Choose a saved backup for this profile's mod list."));
      connect(restoreBackupAction, &QAction::triggered, this,
              [this] { m_ProfileBackupRestoration(); });
    }
    if (m_ProfileBackupRemoval) {
      QAction* deleteBackupAction = addAction(tr("Delete mod list backup..."));
      deleteBackupAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"));
      deleteBackupAction->setToolTip(
          tr("Choose and permanently delete a saved mod list backup."));
      connect(deleteBackupAction, &QAction::triggered, this,
              [this] { m_ProfileBackupRemoval(); });
    }
  }

  if (m_LoadOrderBackupCreation || m_LoadOrderBackupRestoration ||
      m_LoadOrderBackupRemoval) {
    addMenuSection(this, tr("Plugin order backups"));
    if (m_LoadOrderBackupCreation) {
      QAction* createBackupAction = addAction(tr("Create plugin order backup"));
      createBackupAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/backup.svg"));
      createBackupAction->setToolTip(
          tr("Save this profile's current plugin load order."));
      connect(createBackupAction, &QAction::triggered, this,
              [this] { m_LoadOrderBackupCreation(); });
    }
    if (m_LoadOrderBackupRestoration) {
      QAction* restoreBackupAction =
          addAction(tr("Restore plugin order backup..."));
      restoreBackupAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/restore.svg"));
      restoreBackupAction->setToolTip(
          tr("Choose a saved plugin load order for this profile."));
      connect(restoreBackupAction, &QAction::triggered, this,
              [this] { m_LoadOrderBackupRestoration(); });
    }
    if (m_LoadOrderBackupRemoval) {
      QAction* deleteBackupAction =
          addAction(tr("Delete plugin order backup..."));
      deleteBackupAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"));
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
          targetMenu->setIcon(FluentIcons::icon(":/MO/gui/check"));
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

  // Windows 11 order: the default command first, the everyday ones next, the
  // folders and the clipboard after them and the destructive command last
  if (!info->isForeign() && !info->isOverwrite()) {
    QAction* infoAction = addAction(menuGlyph(MenuGlyphs::Information),
                                    tr("Information..."), [=]() {
                                      view->actions().displayModInformation(
                                          m_index.data(ModList::IndexRole).toInt());
                                    });
    setDefaultMenuAction(this, infoAction);
  }

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

  auto viewIndex = view->indexModelToView(m_index);
  if (view->model()->hasChildren(viewIndex)) {
    bool expanded = view->isExpanded(viewIndex);
    addSeparator();
    addAction(menuGlyph(MenuGlyphs::Collapse), tr("Collapse all"), view,
              &QTreeView::collapseAll);
    addAction(tr("Collapse others"), [=]() {
      m_view->collapseAll();
      m_view->setExpanded(viewIndex, expanded);
    });
    addAction(menuGlyph(MenuGlyphs::Expand), tr("Expand all"), view,
              &QTreeView::expandAll);
  }

  // install or create next to this mod: only by priority, where a position
  // in the list means something
  if (view->sortColumn() == ModList::COL_PRIORITY && !info->isBackup() &&
      !info->isOverwrite()) {
    addSeparator();
    auto* insertMenu = addMenu(menuGlyph(MenuGlyphs::Add), tr("Add to list"));
    ModListGlobalContextMenu::addInsertActions(insertMenu, view, info, index);
  }

  // the remove command of each type goes last, after the list commands
  if (m_remove != nullptr) {
    addSeparator();
    addAction(m_remove);
  }
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

  QMenu* menu = new QMenu(this);
  menu->setTitle(tr("Send to"));
  menu->setIcon(menuGlyph(MenuGlyphs::Move));
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
  // plain submenus: the check boxes inside keep the menu open while several
  // categories are picked, the choice applies when the submenu closes
  ModListChangeCategoryMenu* categoriesMenu =
      new ModListChangeCategoryMenu(m_categories, mod, this);
  categoriesMenu->setTitle(tr("Categories"));
  categoriesMenu->setIcon(menuGlyph(MenuGlyphs::Tag));
  connect(categoriesMenu, &QMenu::aboutToHide, [=]() {
    m_actions.setCategories(m_selected, m_index, categoriesMenu->categories());
  });
  addMenu(categoriesMenu);

  // A primary category can only be changed when the mod has alternatives.
  // Avoid adding an empty menu for uncategorized mods or a no-op menu for one category.
  if (mod->getCategories().size() > 1) {
    ModListPrimaryCategoryMenu* primaryCategoryMenu =
        new ModListPrimaryCategoryMenu(m_categories, mod, this);
    primaryCategoryMenu->setTitle(tr("Primary category"));
    primaryCategoryMenu->setIcon(menuGlyph(MenuGlyphs::Star));
    connect(primaryCategoryMenu, &QMenu::aboutToHide, [=]() {
      int category = primaryCategoryMenu->primaryCategory();
      if (category != -1) {
        m_actions.setPrimaryCategory(m_selected, category);
      }
    });
    addMenu(primaryCategoryMenu);
  }
}

void ModListContextMenu::addEnableActions()
{
  // only the commands that change something for the selection
  bool anyActive = false, anyInactive = false;
  for (auto& idx : m_selected) {
    auto index = idx.data(ModList::IndexRole);
    if (!index.isValid()) {
      continue;
    }
    // Overwrite and DLCs are always on, separators and backups never: they
    // don't decide which command is offered
    const auto info = ModInfo::getByIndex(index.toInt());
    if (info->alwaysEnabled() || !info->canBeEnabled()) {
      continue;
    }
    if (m_core.currentProfile()->modEnabled(index.toInt())) {
      anyActive = true;
    } else {
      anyInactive = true;
    }
  }

  const bool several = m_selected.size() > 1;
  if (anyInactive) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"),
              several ? tr("Enable selected") : tr("Enable"), [=]() {
                m_core.modList()->setActive(m_selected, true);
              });
  }
  if (anyActive) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/disable.svg"),
              several ? tr("Disable selected") : tr("Disable"), [=]() {
                m_core.modList()->setActive(m_selected, false);
              });
  }
}

void ModListContextMenu::addVisitActions(ModInfo::Ptr mod)
{
  if (mod->nexusId() > 0) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/visit.svg"), tr("Visit on Nexus"),
              [=]() {
                m_actions.visitOnNexus(m_selected);
              });
  }

  const auto url = mod->parseCustomURL();
  if (url.isValid()) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/visit.svg"),
              tr("Visit on %1").arg(url.host()), [=]() {
                m_actions.visitWebPage(m_selected);
              });
  }
}

void ModListContextMenu::addColorActions(ModInfo::Ptr mod)
{
  addAction(menuGlyph(MenuGlyphs::Color), tr("Select color..."), [=]() {
    m_actions.setColor(m_selected, m_index);
  });
  if (mod->color().isValid()) {
    addAction(tr("Reset color"), [=]() {
      m_actions.resetColor(m_selected);
    });
  }
}

QString ModListContextMenu::downloadPath(ModInfo::Ptr mod) const
{
  // the same lookup Reinstall uses: an absolute path that still exists, else
  // the file name in the downloads folder
  const QString file = mod->installationFile();
  if (file.isEmpty()) {
    return {};
  }
  const QFileInfo info(file);
  if (info.isAbsolute() && info.exists()) {
    return file;
  }
  return m_core.downloadManager()->getOutputDirectory() + "/" +
         (info.isAbsolute() ? info.fileName() : file);
}

void ModListContextMenu::addFolderActions(ModInfo::Ptr mod)
{
  const QString download = downloadPath(mod);
  QMenu* folders = addOpenFolderMenu(this, {{tr("Mod folder"), mod->absolutePath()},
                                            {tr("Downloaded archive"), download}});
  if (m_selected.size() > 1) {
    // every selected mod's folder, as Open in Explorer did
    folders->addSeparator();
    folders->addAction(tr("Folders of the %1 selected mods").arg(m_selected.size()),
                       [this] {
                         m_actions.openExplorer(m_selected);
                       });
  }

  std::vector<std::pair<QString, QString>> copies{
      {tr("Name"), mod->name()}, {tr("Folder path"), nativePath(mod->absolutePath())}};
  if (mod->nexusId() > 0) {
    copies.push_back({tr("Nexus link"), NexusInterface::instance().getModURL(
                                            mod->nexusId(), mod->gameName())});
  }
  if (!download.isEmpty()) {
    copies.push_back({tr("Archive name"), QFileInfo(download).fileName()});
  }
  addCopyMenu(this, copies);
}

void ModListContextMenu::addOverwriteActions(ModInfo::Ptr mod)
{
  setToolTipsVisible(true);

  QAction* manageAction = addAction(menuGlyph(MenuGlyphs::Document),
                                    tr("Review and manage files..."), [=]() {
                                      m_actions.displayModInformation(
                                          mod, ModInfo::getIndex(mod->name()));
                                    });
  manageAction->setToolTip(
      tr("Browse files in Overwrite, create folders, rename items, or move files "
         "into regular mods."));
  setDefaultMenuAction(this, manageAction);

  if (QDir(mod->absolutePath()).count() > 2) {
    addMenuSection(this, tr("Organize files"));
    QAction* syncAction = addAction(menuGlyph(MenuGlyphs::Sync), tr("Sync to mods..."),
                                    [=]() {
                                      m_core.syncOverwrite();
                                    });
    syncAction->setToolTip(
        tr("Review and assign files from Overwrite to regular mods."));

    QAction* createModAction =
        addAction(FluentIcons::icon(":/MO/gui/contextmenu/create-mod.svg"),
                  tr("Create mod from Overwrite..."), [=]() {
                    m_actions.createModFromOverwrite();
                  });
    createModAction->setToolTip(
        tr("Move all files from Overwrite into a new regular mod."));

    QAction* moveAction = addAction(menuGlyph(MenuGlyphs::Merge),
                                    tr("Move files to an existing mod..."), [=]() {
                                      m_actions.moveOverwriteContentToExistingMod();
                                    });
    moveAction->setToolTip(
        tr("Choose a regular mod and move all files from Overwrite into it."));

    m_remove = new QAction(menuGlyph(MenuGlyphs::Clear), tr("Clear Overwrite..."), this);
    connect(m_remove, &QAction::triggered, this, [=]() {
      m_actions.clearOverwrite();
    });
    m_remove->setToolTip(
        tr("Permanently delete every file and folder currently in Overwrite."));
  }

  addSeparator();
  // Overwrite's own folder, not those of other selected mods
  addAction(menuGlyph(MenuGlyphs::Folder), tr("Open folder"), [=]() {
    shell::Explore(mod->absolutePath());
  });
  addCopyAction(this, tr("Copy folder path"), nativePath(mod->absolutePath()));
}

QStringList ModListContextMenu::modsUnderSelectedSeparators() const
{
  // a separator holds the mods after it in priority order, up to the next
  // separator
  QStringList names;
  Profile* profile = m_core.currentProfile();
  if (profile == nullptr) {
    return names;
  }

  const auto& byPriority = profile->getAllIndexesByPriority();
  for (const auto& idx : m_selected) {
    const auto index = idx.data(ModList::IndexRole);
    if (!index.isValid() || !ModInfo::getByIndex(index.toInt())->isSeparator()) {
      continue;
    }
    const int priority = profile->getModPriority(index.toInt());
    for (auto it = byPriority.upper_bound(priority); it != byPriority.end(); ++it) {
      const auto info = ModInfo::getByIndex(it->second);
      if (info->isSeparator() || info->isOverwrite()) {
        break;
      }
      if (!info->isForeign() && !info->isBackup() && !names.contains(info->name())) {
        names.append(info->name());
      }
    }
  }
  return names;
}

void ModListContextMenu::addSeparatorActions(ModInfo::Ptr mod)
{
  // the mods in the separator, switched together
  const QStringList inside = modsUnderSelectedSeparators();
  bool anyActive = false, anyInactive = false;
  for (const auto& name : inside) {
    const unsigned int index = ModInfo::getIndex(name);
    if (index != UINT_MAX && m_core.currentProfile()->modEnabled(index)) {
      anyActive = true;
    } else {
      anyInactive = true;
    }
  }
  if (anyInactive) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"),
              tr("Enable its mods"), [this, inside]() {
                m_core.modList()->setActive(inside, true);
              });
  }
  if (anyActive) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/disable.svg"),
              tr("Disable its mods"), [this, inside]() {
                m_core.modList()->setActive(inside, false);
              });
  }

  addSeparator();
  addAction(FluentIcons::icon(":/MO/gui/contextmenu/rename.svg"), tr("Rename"),
            [=]() {
              m_actions.renameMod(m_index);
            });
  addColorActions(mod);
  addCategoryContextMenus(mod);
  if (m_view->sortColumn() == ModList::COL_PRIORITY) {
    addSendToContextMenu();
  }

  addSeparator();
  addCopyAction(this, tr("Copy name"), mod->name());

  m_remove = new QAction(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
                         m_selected.size() > 1 ? tr("Remove separators...")
                                               : tr("Remove separator..."),
                         this);
  // only the separators of a mixed selection: the label names separators
  QModelIndexList separators;
  for (const auto& index : m_selected) {
    const auto selected = ModInfo::getByIndex(index.data(ModList::IndexRole).toInt());
    if (selected && selected->isSeparator()) {
      separators.append(index);
    }
  }
  connect(m_remove, &QAction::triggered, this, [=]() {
    m_actions.removeMods(separators);
  });
}

void ModListContextMenu::addForeignActions(ModInfo::Ptr mod)
{
  if (m_view->sortColumn() == ModList::COL_PRIORITY) {
    addSendToContextMenu();
  }
  addSeparator();
  addCopyAction(this, tr("Copy name"), mod->name());
}

void ModListContextMenu::addBackupActions(ModInfo::Ptr mod)
{
  auto flags = mod->getFlags();
  addVisitActions(mod);
  addSeparator();
  addAction(FluentIcons::icon(":/MO/gui/contextmenu/reinstall.svg"), tr("Restore backup"),
            [=]() {
              m_actions.restoreBackup(m_index);
            });
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_INVALID) != flags.end()) {
    addAction(menuGlyph(MenuGlyphs::Warning), tr("Ignore missing data"), [=]() {
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
  addFolderActions(mod);

  m_remove = new QAction(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
                         tr("Remove backup..."), this);
  connect(m_remove, &QAction::triggered, this, [=]() {
    m_actions.removeMods(m_selected);
  });
}

void ModListContextMenu::addRegularActions(ModInfo::Ptr mod)
{
  auto flags = mod->getFlags();

  addVisitActions(mod);
  addSeparator();

  addEnableActions();
  if (m_view->sortColumn() == ModList::COL_PRIORITY) {
    addSendToContextMenu();
  }
  addCategoryContextMenus(mod);

  addSeparator();

  addAction(FluentIcons::icon(":/MO/gui/contextmenu/rename.svg"), tr("Rename"), [=]() {
    m_actions.renameMod(m_index);
  });
  addAction(FluentIcons::icon(":/MO/gui/contextmenu/reinstall.svg"), tr("Reinstall"),
            [=]() {
              m_actions.reinstallMod(m_index);
            });
  addAction(FluentIcons::icon(":/MO/gui/contextmenu/backup.svg"), tr("Create backup"),
            [=]() {
              m_actions.createBackup(m_index);
            });
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_HIDDEN_FILES) !=
      flags.end()) {
    addAction(menuGlyph(MenuGlyphs::Show), tr("Restore hidden files"), [=]() {
      m_actions.restoreHiddenFiles(m_selected);
    });
  }
  addColorActions(mod);

  // updates and the Nexus account commands
  addSeparator();
  if (mod->nexusId() > 0) {
    addAction(FluentIcons::icon(":/MO/gui/contextmenu/updates.svg"),
              tr("Check for updates"), [=]() {
                m_actions.checkModsForUpdates(m_selected);
              });
  }
  if (mod->updateIgnored()) {
    addAction(tr("Un-ignore update"), [=]() {
      m_actions.setIgnoreUpdate(m_selected, false);
    });
  } else if (mod->updateAvailable() || mod->downgradeAvailable()) {
    addAction(tr("Ignore update"), [=]() {
      m_actions.setIgnoreUpdate(m_selected, true);
    });
  }
  if (mod->downgradeAvailable()) {
    addAction(tr("Change versioning scheme"), [=]() {
      m_actions.changeVersioningScheme(m_index);
    });
  }

  if (mod->nexusId() > 0) {
    addNexusMenu(mod);
  }

  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_INVALID) != flags.end()) {
    addAction(menuGlyph(MenuGlyphs::Warning), tr("Ignore missing data"), [=]() {
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
  addFolderActions(mod);

  m_remove = new QAction(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
                         m_selected.size() > 1 ? tr("Remove mods...") : tr("Remove..."),
                         this);
  connect(m_remove, &QAction::triggered, this, [=]() {
    m_actions.removeMods(m_selected);
  });
}

void ModListContextMenu::addNexusMenu(ModInfo::Ptr mod)
{
  // endorsement, tracking and the Nexus category in one submenu
  auto* nexus = new QMenu(tr("Nexus"), this);
  nexus->setIcon(menuGlyph(MenuGlyphs::Web));

  if (Settings::instance().nexus().endorsementIntegration()) {
    switch (mod->endorsedState()) {
    case EndorsedState::ENDORSED_TRUE: {
      nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/endorse.svg"),
                       tr("Un-endorse"), [=]() {
                         m_actions.setEndorsed(m_selected, false);
                       });
    } break;
    case EndorsedState::ENDORSED_FALSE: {
      nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/endorse.svg"),
                       tr("Endorse"), [=]() {
                         m_actions.setEndorsed(m_selected, true);
                       });
      nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/wont-endorse.svg"),
                       tr("Won't endorse"), [=]() {
                         m_actions.willNotEndorsed(m_selected);
                       });
    } break;
    case EndorsedState::ENDORSED_NEVER: {
      nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/endorse.svg"),
                       tr("Endorse"), [=]() {
                         m_actions.setEndorsed(m_selected, true);
                       });
    } break;
    default: {
      nexus->addAction(tr("Endorsement state unknown"))->setEnabled(false);
    } break;
    }
  }

  if (Settings::instance().nexus().trackedIntegration()) {
    switch (mod->trackedState()) {
    case TrackedState::TRACKED_FALSE: {
      nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/tracking.svg"),
                       tr("Start tracking"), [=]() {
                         m_actions.setTracked(m_selected, true);
                       });
    } break;
    case TrackedState::TRACKED_TRUE: {
      nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/tracking.svg"),
                       tr("Stop tracking"), [=]() {
                         m_actions.setTracked(m_selected, false);
                       });
    } break;
    default: {
      nexus->addAction(tr("Tracked state unknown"))->setEnabled(false);
    } break;
    }
  }

  if (mod->getNexusCategory() > 0 || !mod->installationFile().isEmpty()) {
    nexus->addAction(FluentIcons::icon(":/MO/gui/contextmenu/remap-category.svg"),
                     tr("Use the Nexus category"), [=]() {
                       m_actions.remapCategory(m_selected);
                     });
  }

  if (nexus->isEmpty()) {
    delete nexus;
    return;
  }
  addMenu(nexus);
}
