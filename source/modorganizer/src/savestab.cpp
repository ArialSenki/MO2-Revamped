#include "savestab.h"
#include "fluenticons.h"
#include "menuutils.h"
#include "fluentpanels.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include "activatemodsdialog.h"
#include "eldenringsavesettings.h"
#include "organizercore.h"
#include "ui_mainwindow.h"
#include <iplugingame.h>
#include <localsavegames.h>
#include <isavegameinfowidget.h>
#include <QHeaderView>
#include <QIcon>
#include <QLocale>
#include <QTimer>

using namespace MOBase;

SavesTab::SavesTab(QWidget* window, OrganizerCore& core, Ui::MainWindow* mwui)
    : m_window(window), m_core(core), m_CurrentSaveView(nullptr),
      ui{mwui->tabWidget, mwui->savesTab, mwui->savegameList}
{
  m_SavesWatcherTimer.setSingleShot(true);
  m_SavesWatcherTimer.setInterval(500);

  ui.list->installEventFilter(this);
  ui.list->setMouseTracking(true);

  connect(&m_SavesWatcher, &QFileSystemWatcher::directoryChanged, [&] {
    m_SavesWatcherTimer.start();
  });

  connect(&m_SavesWatcherTimer, &QTimer::timeout, [&] {
    refreshSavesIfOpen();
  });

  connect(ui.list, &QWidget::customContextMenuRequested, [&](auto pos) {
    onContextMenu(pos);
  });

  connect(ui.list, &QTreeWidget::itemEntered, [&](auto* item) {
    saveSelectionChanged(item);
  });

  createFooter();
}

void SavesTab::createFooter()
{
  // like the Downloads tab: the source of the list on the left, open and
  // refresh on the right
  auto* layout = qobject_cast<QVBoxLayout*>(ui.tab->layout());
  if (layout == nullptr) {
    return;
  }

  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  auto* route = new FluentPanels::ElidedLabel(ui.tab);
  route->setObjectName(QStringLiteral("savesRouteLabel"));
  m_route = route;
  footer->addWidget(route, 1);

  auto* open = new QPushButton(menuGlyph(MenuGlyphs::Folder), tr("Open folder"), ui.tab);
  open->setObjectName(QStringLiteral("savesOpenFolder"));
  open->setToolTip(tr("Open the folder these saves are read from"));
  auto* refresh = new QPushButton(ui.tab);
  refresh->setObjectName(QStringLiteral("savesRefresh"));
  refresh->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/refresh.svg"));
  refresh->setToolTip(tr("Refresh"));
  refresh->setAccessibleName(tr("Refresh"));
  footer->addWidget(open);
  footer->addWidget(refresh);
  layout->addLayout(footer);

  connect(open, &QPushButton::clicked, this, [this] {
    if (!m_routePath.isEmpty()) {
      // a route without saves yet has no folder: Explore would open its
      // parent instead
      QDir().mkpath(m_routePath);
      shell::Explore(m_routePath);
    }
  });
  connect(refresh, &QPushButton::clicked, this, [this] {
    refreshSaveList();
  });
}

void SavesTab::updateRoute(const QDir& savesDir)
{
  if (m_route == nullptr) {
    return;
  }

  // say whose saves these are: the profile's own, the game's (shared with
  // Steam and every setup that does not isolate them) or a shared folder
  const QString path    = QDir::cleanPath(savesDir.absolutePath());
  const QString profile = QDir::cleanPath(m_core.currentProfile()->absolutePath());
  const QString game =
      QDir::cleanPath(m_core.managedGame()->savesDirectory().absolutePath());

  QString kind;
  if (path.startsWith(profile + "/", Qt::CaseInsensitive) ||
      path.compare(profile, Qt::CaseInsensitive) == 0) {
    kind = tr("This profile's own saves");
  } else if (path.compare(game, Qt::CaseInsensitive) == 0) {
    kind = tr("The game's saves, shared with Steam");
  } else {
    kind = tr("Shared saves");
  }

  m_routePath = path;
  static_cast<FluentPanels::ElidedLabel*>(m_route)->setFullText(
      kind + QStringLiteral(" \u00b7 ") + QDir::toNativeSeparators(path));
}

bool SavesTab::eventFilter(QObject* object, QEvent* e)
{
  if (object == ui.list) {
    if (e->type() == QEvent::Leave || e->type() == QEvent::WindowDeactivate) {
      hideSaveGameInfo();
    } else if (e->type() == QEvent::KeyPress) {
      QKeyEvent* keyEvent = static_cast<QKeyEvent*>(e);
      if (keyEvent->key() == Qt::Key_Delete) {
        deleteSavegame();
      }
    }
  }

  return false;
}

void SavesTab::displaySaveGameInfo(QTreeWidgetItem* newItem)
{
  // don't display the widget if the main window doesn't have focus
  //
  // this goes against the standard behaviour for tooltips, which are displayed
  // on hover regardless of focus, but this widget is so large and busy that
  // it's probably better this way
  if (!m_window->isActiveWindow()) {
    return;
  }

  if (m_CurrentSaveView == nullptr) {
    auto info = m_core.gameFeatures().gameFeature<SaveGameInfo>();

    if (info != nullptr) {
      m_CurrentSaveView = info->getSaveGameWidget(m_window);
    }

    if (m_CurrentSaveView == nullptr) {
      return;
    }
  }

  m_CurrentSaveView->setSave(*m_SaveGames[ui.list->indexOfTopLevelItem(newItem)]);

  QWindow* window = m_CurrentSaveView->window()->windowHandle();
  QRect screenRect;
  if (window == nullptr)
    screenRect = QGuiApplication::primaryScreen()->geometry();
  else
    screenRect = window->screen()->geometry();

  QPoint pos = QCursor::pos();
  if (pos.x() + m_CurrentSaveView->width() > screenRect.right()) {
    pos.rx() -= (m_CurrentSaveView->width() + 2);
  } else {
    pos.rx() += 5;
  }

  if (pos.y() + m_CurrentSaveView->height() > screenRect.bottom()) {
    pos.ry() -= (m_CurrentSaveView->height() + 10);
  } else {
    pos.ry() += 20;
  }
  m_CurrentSaveView->move(pos);

  m_CurrentSaveView->show();
  m_CurrentSaveView->setProperty("displayItem",
                                 QVariant::fromValue(static_cast<void*>(newItem)));
}

void SavesTab::saveSelectionChanged(QTreeWidgetItem* newItem)
{
  if (newItem == nullptr) {
    hideSaveGameInfo();
  } else if (m_CurrentSaveView == nullptr ||
             newItem != m_CurrentSaveView->property("displayItem").value<void*>()) {
    displaySaveGameInfo(newItem);
  }
}

void SavesTab::hideSaveGameInfo()
{
  if (m_CurrentSaveView != nullptr) {
    m_CurrentSaveView->deleteLater();
    m_CurrentSaveView = nullptr;
  }
}

void SavesTab::refreshSavesIfOpen()
{
  if (ui.mainTabs->currentWidget() == ui.tab) {
    refreshSaveList();
  }
}

QDir SavesTab::currentSavesDir() const
{
  QDir savesDir;
  auto currentProfile = m_core.currentProfile();
  if (currentProfile->localSavesEnabled()) {
    auto localSaves = m_core.gameFeatures().gameFeature<LocalSavegames>();
    if (localSaves != nullptr) {
      const auto mappings = localSaves->mappings(
          currentProfile->absolutePath() + "/saves");
      const QString gameSavePath = QDir::cleanPath(
          m_core.managedGame()->savesDirectory().absolutePath());
      for (const auto& mapping : mappings) {
        if (mapping.isDirectory &&
            QDir::cleanPath(mapping.destination) == gameSavePath) {
          savesDir.setPath(mapping.source);
          return savesDir;
        }
      }
    }
    // Elden Ring maps its route itself: without a mapping (the route was
    // switched to the Steam saves and MO2 not restarted yet, or the game
    // plugin did not load) the game uses its own save folder
    if (EldenRingSaveSettings::supports(m_core.managedGame())) {
      return m_core.managedGame()->savesDirectory();
    }
    savesDir.setPath(currentProfile->savePath());
  } else {
    auto iniFiles = m_core.managedGame()->iniFiles();
    if (iniFiles.isEmpty()) {
      return m_core.managedGame()->savesDirectory();
    }
    QString iniPath = currentProfile->absoluteIniFilePath(iniFiles[0]);
    wchar_t path[MAX_PATH];
    if (::GetPrivateProfileStringW(L"General", L"SLocalSavePath", L"", path, MAX_PATH,
                                   iniPath.toStdWString().c_str())) {
      savesDir.setPath(m_core.managedGame()->documentsDirectory().absoluteFilePath(
          QString::fromWCharArray(path)));
    } else {
      savesDir = m_core.managedGame()->savesDirectory();
    }
  }
  return savesDir;
}

void SavesTab::startMonitorSaves()
{
  startMonitorSaves(currentSavesDir());
}

void SavesTab::startMonitorSaves(const QDir& savesDir)
{
  stopMonitorSaves();
  m_SavesWatcher.addPath(savesDir.absolutePath());
  if (m_core.currentProfile()->localSavesEnabled() &&
      m_core.gameFeatures().gameFeature<LocalSavegames>() != nullptr) {
    // Save Isolation atomically replaces its route INI in the profile folder.
    m_SavesWatcher.addPath(m_core.currentProfile()->absolutePath());
  }
}

void SavesTab::stopMonitorSaves()
{
  if (m_SavesWatcher.directories().length() > 0) {
    m_SavesWatcher.removePaths(m_SavesWatcher.directories());
  }
}

void SavesTab::refreshSaveList()
{
  if (m_SaveListRefreshQueued) {
    return;
  }
  m_SaveListRefreshQueued = true;
  QTimer::singleShot(0, this, [this] {
    m_SaveListRefreshQueued = false;
    if (ui.mainTabs->currentWidget() != ui.tab) {
      return;
    }
    refreshSaveListNow();
  });
}

void SavesTab::refreshSaveListNow()
{
  TimeThis tt("MainWindow::refreshSaveList()");

  // Reuse one mapping lookup for both the watcher and save list. The save
  // mapping provider logs each lookup, so asking twice here duplicates the
  // same "save mapping prepared" entry on every refresh.
  QDir savesDir = currentSavesDir();
  startMonitorSaves(savesDir);  // re-starts monitoring
  updateRoute(savesDir);

  try {
    MOBase::log::debug("reading save games from {}", savesDir.absolutePath());
    m_SaveGames = m_core.managedGame()->listSaves(savesDir);
    std::sort(m_SaveGames.begin(), m_SaveGames.end(),
              [](auto const& lhs, auto const& rhs) {
                return lhs->getCreationTime() > rhs->getCreationTime();
              });

    ui.list->clear();

    // a third column shows when each save was written, newest first
    if (ui.list->columnCount() < 3) {
      ui.list->setColumnCount(3);
      ui.list->headerItem()->setText(2, tr("Modified"));
      ui.list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
      // the file path takes the free width, so the three columns fit the card
      // without a horizontal scroll bar
      ui.list->header()->setStretchLastSection(false);
      ui.list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    }

    const QLocale locale;
    for (auto& save : m_SaveGames) {
      auto relpath = savesDir.relativeFilePath(save->getFilepath());
      auto display = save->getName();
      auto* item   = new QTreeWidgetItem(
          ui.list, {display, relpath,
                    locale.toString(save->getCreationTime(), QLocale::ShortFormat)});
      item->setIcon(0, FluentIcons::file(save->getFilepath(), false));
      ui.list->addTopLevelItem(item);
    }
  } catch (std::exception& e) {
    // listSaves() can throw
    log::error("{}", e.what());
  }
}

void SavesTab::deleteSavegame()
{
  auto info = m_core.gameFeatures().gameFeature<SaveGameInfo>();

  QString savesMsgLabel;
  QStringList deleteFiles;

  int count = 0;
  const QDir savesDir = currentSavesDir();
  const bool eldenRing = EldenRingSaveSettings::supports(m_core.managedGame());

  for (const QModelIndex& idx : ui.list->selectionModel()->selectedRows()) {

    auto& saveGame = m_SaveGames[idx.row()];

    if (count < 10) {
      // every Elden Ring save is ER0000: its account folder tells them apart
      const QString name =
          eldenRing ? savesDir.relativeFilePath(saveGame->getFilepath())
                    : QFileInfo(saveGame->getFilepath()).completeBaseName();
      savesMsgLabel += "<li>" + name.toHtmlEscaped() + "</li>";
    }
    ++count;

    deleteFiles += saveGame->allFiles();
  }

  if (count == 0) {
    return;
  }

  if (count > 10) {
    savesMsgLabel += "<li><i>... " + tr("%1 more").arg(count - 10) + "</i></li>";
  }

  // the Steam route: these are the saves the game uses outside MO2 too
  if (eldenRing && QDir::cleanPath(savesDir.absolutePath()) ==
                       QDir::cleanPath(
                           m_core.managedGame()->savesDirectory().absolutePath())) {
    savesMsgLabel += "<br>" + tr("These are the game's own Steam saves, also used "
                                 "when Elden Ring starts outside MO2.");
  }

  if (QMessageBox::question(
          m_window, tr("Confirm"),
          tr("Are you sure you want to remove the following %n save(s)?<br>"
             "<ul>%1</ul><br>"
             "Removed saves will be sent to the Recycle Bin.",
             "", count)
              .arg(savesMsgLabel),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
    shellDelete(deleteFiles, true);  // recycle bin delete.
    refreshSaveList();
  }
}

void SavesTab::onContextMenu(const QPoint& pos)
{
  QItemSelectionModel* selection = ui.list->selectionModel();

  if (!selection->hasSelection()) {
    return;
  }

  QMenu menu;
  const auto rows = selection->selectedRows();

  // only offered when the save actually misses something
  auto info = m_core.gameFeatures().gameFeature<SaveGameInfo>();
  if (info != nullptr && rows.count() == 1) {
    auto& save = m_SaveGames[rows[0].row()];
    SaveGameInfo::MissingAssets missing = info->getMissingAssets(*save);
    if (missing.size() != 0) {
      QAction* action = menu.addAction(tr("Restore required plugins..."));
      action->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/enable.svg"));
      action->setToolTip(
          tr("Choose installed mods to activate the plugins required by this save."));
      connect(action, &QAction::triggered, this, [this, missing] {
        fixMods(missing);
      });
      menu.addSeparator();
    }
  }

  const QString file = m_SaveGames[rows[0].row()]->getFilepath();
  menu.addAction(menuGlyph(MenuGlyphs::Folder), tr("Show in folder"), [&] {
    openInExplorer();
  });
  if (rows.count() == 1) {
    addCopyMenu(&menu, {{tr("File name"), QFileInfo(file).fileName()},
                        {tr("File path"), nativePath(file)},
                        {tr("Folder path"), nativePath(QFileInfo(file).absolutePath())}});
  }

  menu.addSeparator();
  menu.addAction(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
                 rows.count() == 1 ? tr("Delete...")
                                   : tr("Delete %1 saves...").arg(rows.count()),
                 [&] {
                   deleteSavegame();
                 });

  menu.exec(ui.list->viewport()->mapToGlobal(pos));
}

void SavesTab::fixMods(SaveGameInfo::MissingAssets const& missingAssets)
{
  ActivateModsDialog dialog(missingAssets, m_window);
  if (dialog.exec() == QDialog::Accepted) {
    // activate the required mods, then enable all esps
    std::set<QString> modsToActivate = dialog.getModsToActivate();
    for (std::set<QString>::iterator iter = modsToActivate.begin();
         iter != modsToActivate.end(); ++iter) {
      if ((*iter != "<data>") && (*iter != "<overwrite>")) {
        unsigned int modIndex = ModInfo::getIndex(*iter);
        m_core.currentProfile()->setModEnabled(modIndex, true);
      }
    }

    m_core.currentProfile()->writeModlist();
    m_core.refreshLists();

    std::set<QString> espsToActivate = dialog.getESPsToActivate();
    for (std::set<QString>::iterator iter = espsToActivate.begin();
         iter != espsToActivate.end(); ++iter) {
      m_core.pluginList()->enableESP(*iter);
    }

    m_core.saveCurrentLists();
  }
}

void SavesTab::openInExplorer()
{
  auto info = m_core.gameFeatures().gameFeature<SaveGameInfo>();

  const auto sel = ui.list->selectionModel()->selectedRows();
  if (sel.empty()) {
    return;
  }

  auto& saveGame = m_SaveGames[sel[0].row()];
  shell::Explore(saveGame->getFilepath());
}
