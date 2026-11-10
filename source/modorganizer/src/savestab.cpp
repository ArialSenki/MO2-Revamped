#include "savestab.h"
#include "fluenticons.h"
#include "menuutils.h"
#include "fluentpanels.h"
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include "activatemodsdialog.h"
#include "eldenringsavesettings.h"
#include "organizercore.h"
#include "ui_mainwindow.h"
#include <iplugingame.h>
#include <report.h>
#include <localsavegames.h>
#include <isavegameinfowidget.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
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

  connect(&m_SavesWatcher, &QFileSystemWatcher::directoryChanged,
          [&](const QString& path) {
            // the profile folder changes on every mod toggle (modlist.txt);
            // only a new save route there means the saves list changed
            if (!m_routeWatchFolder.isEmpty() &&
                QDir::cleanPath(path) == QDir::cleanPath(m_routeWatchFolder)) {
              const QDateTime stamp =
                  QFileInfo(QDir(m_routeWatchFolder)
                                .filePath(QLatin1String(EldenRingSaveSettings::ConfigFilename)))
                      .lastModified();
              if (stamp == m_routeStamp) {
                return;
              }
              m_routeStamp = stamp;
            }
            m_SavesWatcherTimer.start();
          });

  connect(&m_SavesWatcherTimer, &QTimer::timeout, [&] {
    if (m_menuOpen) {
      m_refreshAfterMenu = true;
      return;
    }
    refreshSavesIfOpen();
  });

  // an empty list says so instead of showing a blank card
  m_empty = new QLabel(ui.list->viewport());
  m_empty->setObjectName(QStringLiteral("savesEmptyLabel"));
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setWordWrap(true);
  m_empty->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* emptyLayout = new QVBoxLayout(ui.list->viewport());
  emptyLayout->setContentsMargins(16, 16, 16, 16);
  emptyLayout->addWidget(m_empty);
  m_empty->hide();

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
  m_openFolder = open;
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
  QString detail;
  if (path.startsWith(profile + "/", Qt::CaseInsensitive) ||
      path.compare(profile, Qt::CaseInsensitive) == 0) {
    kind = tr("This profile's own saves");
  } else if (path.compare(game, Qt::CaseInsensitive) == 0) {
    // only Elden Ring and Nightreign keep their saves in Steam's folder
    if (EldenRingSaveSettings::supports(m_core.managedGame())) {
      kind   = tr("Steam's saves");
      detail = tr("The game's saves, shared with Steam") + QStringLiteral("\n");
    } else {
      kind = tr("The game's saves");
    }
  } else {
    kind = tr("Shared saves");
  }

  m_routePath = path;
  // the path doesn't fit beside the buttons: the kind stays readable
  static_cast<FluentPanels::ElidedLabel*>(m_route)->setFullText(kind);
  m_route->setToolTip(detail + QDir::toNativeSeparators(path));
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

bool SavesTab::gameHasSaves() const
{
  // a plugin without a saves folder gives an empty path, which QDir reads as
  // the working directory (MO2's own folder)
  const QString path = m_core.managedGame()->savesDirectory().path();
  return !path.isEmpty() && path != QLatin1String(".");
}

void SavesTab::startMonitorSaves(const QDir& savesDir)
{
  stopMonitorSaves();
  if (!gameHasSaves()) {
    return;
  }
  m_SavesWatcher.addPath(savesDir.absolutePath());
  m_routeWatchFolder.clear();
  if (m_core.currentProfile()->localSavesEnabled() &&
      m_core.gameFeatures().gameFeature<LocalSavegames>() != nullptr &&
      EldenRingSaveSettings::supports(m_core.managedGame())) {
    // Save Isolation atomically replaces its route INI in the profile folder.
    m_routeWatchFolder = m_core.currentProfile()->absolutePath();
    m_routeStamp       = QFileInfo(QDir(m_routeWatchFolder)
                                 .filePath(QLatin1String(EldenRingSaveSettings::ConfigFilename)))
                       .lastModified();
    m_SavesWatcher.addPath(m_routeWatchFolder);
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

  if (!gameHasSaves()) {
    m_SaveGames.clear();
    ui.list->clear();
    // the label paints its own copy of the text: clear() would leave it
    if (m_route != nullptr) {
      static_cast<FluentPanels::ElidedLabel*>(m_route)->setFullText({});
    }
    m_routePath.clear();
    if (m_openFolder != nullptr) {
      m_openFolder->setEnabled(false);
    }
    m_empty->setText(tr("This game has no saves MO2 can show."));
    m_empty->show();
    return;
  }
  if (m_openFolder != nullptr) {
    m_openFolder->setEnabled(true);
  }

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
      // the name takes the free width, so the date stays in the card without a
      // horizontal scroll bar; the path column is hidden below
      ui.list->header()->setStretchLastSection(false);
      ui.list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    }

    const QLocale locale;
    for (auto& save : m_SaveGames) {
      auto relpath = savesDir.relativeFilePath(save->getFilepath());
      auto display = save->getName();
      auto* item   = new QTreeWidgetItem(
          ui.list, {display, relpath,
                    locale.toString(save->getCreationTime(), QLocale::ShortFormat)});
      item->setIcon(0, FluentIcons::file(save->getFilepath(), false));
      item->setToolTip(0, QDir::toNativeSeparators(relpath));
      ui.list->addTopLevelItem(item);
    }
    // the side panel has room for the name and the date; the file's path,
    // squeezed to "..." there, shows on the name
    ui.list->setColumnHidden(1, true);

    m_empty->setText(tr("No saves in this folder yet."));
    m_empty->setVisible(m_SaveGames.empty());
  } catch (std::exception& e) {
    // listSaves() can throw
    log::error("{}", e.what());
    m_empty->setText(tr("The saves in this folder couldn't be read."));
    m_empty->setVisible(ui.list->topLevelItemCount() == 0);
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
    // recycle bin delete; a save in use stays where it was
    shellDelete(deleteFiles, true);
    // Windows reports success when the user skips a file in use, so the files say
    if (std::any_of(deleteFiles.begin(), deleteFiles.end(), [](const QString& file) {
          return QFile::exists(file);
        })) {
      reportError(tr("Some of these saves are still there. They may be in use, or the "
                     "deletion was cancelled."));
    }
    refreshSaveList();
  }
}

void SavesTab::onContextMenu(const QPoint& pos)
{
  QItemSelectionModel* selection = ui.list->selectionModel();

  // on empty space there is no save to act on, whatever stayed selected
  if (!selection->hasSelection() || !ui.list->indexAt(pos).isValid()) {
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
      menu.setToolTipsVisible(true);
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

  // the watcher's refresh waits for the menu: it would clear the rows the
  // menu's actions work on
  m_menuOpen = true;
  menu.exec(ui.list->viewport()->mapToGlobal(pos));
  m_menuOpen = false;
  if (m_refreshAfterMenu) {
    m_refreshAfterMenu = false;
    refreshSavesIfOpen();
  }
}

void SavesTab::fixMods(SaveGameInfo::MissingAssets const& missingAssets)
{
  ActivateModsDialog dialog(missingAssets, m_window);
  if (dialog.exec() == QDialog::Accepted) {
    // activate the required mods, then enable all esps
    std::set<QString> modsToActivate = dialog.getModsToActivate();
    for (std::set<QString>::iterator iter = modsToActivate.begin();
         iter != modsToActivate.end(); ++iter) {
      // the game's folder and Overwrite are always on and aren't mods in the list
      unsigned int modIndex = ModInfo::getIndex(*iter);
      if (modIndex != UINT_MAX) {
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

    const std::set<QString> missing = dialog.getMissingPlugins();
    if (!missing.empty()) {
      QMessageBox::information(
          m_window, tr("Restore required plugins"),
          tr("No installed mod has these plugins, so they were not restored:\n\n%1")
              .arg(QStringList(missing.begin(), missing.end()).join(QLatin1Char('\n'))));
    }
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
