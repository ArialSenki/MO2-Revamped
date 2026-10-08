/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutdialog.h"
#include "browserdialog.h"
#include "categories.h"
#include "categoriesdialog.h"
#include "datatab.h"
#include "downloadlist.h"
#include "downloadlistview.h"
#include "downloadstab.h"
#include "editexecutablesdialog.h"
#include "envshortcut.h"
#include "eventfilter.h"
#include "executableinfo.h"
#include "executableslist.h"
#include "filedialogmemory.h"
#include "filterlist.h"
#include "guessedvalue.h"
#include "imodinterface.h"
#include "installationmanager.h"
#include "instancemanager.h"
#include "instancemanagerdialog.h"
#include "iplugindiagnose.h"
#include "iplugingame.h"
#include "isavegame.h"
#include "isavegameinfowidget.h"
#include "listdialog.h"
#include "localsavegames.h"
#include "messagedialog.h"
#include "modlist.h"
#include "modlistcontextmenu.h"
#include "modlistviewactions.h"
#include "motddialog.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "organizercore.h"
#include "overwriteinfodialog.h"
#include "pluginlist.h"
#include "previewdialog.h"
#include "previewgenerator.h"
#include "problemsdialog.h"
#include "profile.h"
#include "profilesdialog.h"
#include "report.h"
#include "savegameinfo.h"
#include "savestab.h"
#include "selectiondialog.h"
#include "serverinfo.h"
#include "settingsdialog.h"
#include "shared/appconfig.h"
#include "startupdiagnostics.h"
#include "spawn.h"
#include "statusbar.h"
#include "tutorialmanager.h"
#include "versioninfo.h"
#include <bsainvalidation.h>
#include <dataarchives.h>
#include <safewritefile.h>
#include <scopeguard.h>
#include <taskprogressmanager.h>
#include <usvfs.h>
#include <utility.h>
#include <vector>

#include "directoryrefresher.h"
#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFIleIconProvider>
#include <QFileDialog>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QFuture>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QIODevice>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValueRef>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QModelIndex>
#include <QNetworkProxyFactory>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPoint>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QRadioButton>
#include <QRect>
#include <QResizeEvent>
#include <QRectF>
#include <QScopedPointer>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVariantList>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QWebEngineProfile>
#include <QWhatsThis>
#include <QWidgetAction>

#include <QDebug>
#include <QtGlobal>

#ifndef Q_MOC_RUN
#include <boost/algorithm/string.hpp>
#include <boost/assign.hpp>
#include <boost/bind/bind.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/thread.hpp>
#endif

#include <shlobj.h>

#include <exception>
#include <algorithm>
#include <functional>
#include <limits.h>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "gameplugins.h"

#ifdef TEST_MODELS
#include "modeltest.h"
#endif  // TEST_MODELS

#pragma warning(disable : 4428)

using namespace MOBase;
using namespace MOShared;

const QSize SmallToolbarSize(24, 24);
const QSize MediumToolbarSize(32, 32);
const QSize LargeToolbarSize(42, 36);

QString UnmanagedModName()
{
  return QObject::tr("<Unmanaged>");
}

bool runLoot(QWidget* parent, OrganizerCore& core, bool didUpdateMasterList);

namespace {

struct ProfileBackupSelection
{
  bool accepted = false;
  bool modList = false;
  bool pluginOrder = false;
};

bool showProfileBackupPrompt(QWidget* parent, const QString& windowTitle,
                             const QString& headingText,
                             const QString& descriptionText,
                             const QString& primaryButtonText,
                             const QString& cancelButtonText = QString())
{
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("profileBackupPromptDialog"));
  dialog.setWindowTitle(windowTitle);
  if (parent != nullptr) {
    dialog.setWindowIcon(parent->windowIcon());
  }
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog.setMinimumWidth(460);
  dialog.resize(540, cancelButtonText.isEmpty() ? 170 : 200);

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(18, 14, 18, 12);
  layout->setSpacing(5);

  auto* heading = new QLabel(headingText, &dialog);
  heading->setObjectName(QStringLiteral("profileBackupPromptTitle"));
  QFont headingFont = heading->font();
  if (headingFont.pointSize() > 0) {
    headingFont.setPointSize(headingFont.pointSize() + 2);
  }
  headingFont.setBold(true);
  heading->setFont(headingFont);
  heading->setWordWrap(true);
  layout->addWidget(heading);

  auto* card = new QFrame(&dialog);
  card->setObjectName(QStringLiteral("profileBackupPromptCard"));
  card->setFrameShape(QFrame::StyledPanel);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(10, 5, 10, 5);
  cardLayout->setSpacing(0);

  auto* description = new QLabel(descriptionText, card);
  description->setObjectName(QStringLiteral("profileBackupPromptDescription"));
  description->setWordWrap(true);
  cardLayout->addWidget(description);
  layout->addWidget(card);

  auto* divider = new QFrame(&dialog);
  divider->setObjectName(QStringLiteral("profileBackupPromptDivider"));
  divider->setFrameShape(QFrame::HLine);
  divider->setFrameShadow(QFrame::Plain);
  layout->addWidget(divider);

  auto* buttons = new QDialogButtonBox(&dialog);
  buttons->setObjectName(QStringLiteral("profileBackupPromptButtons"));
  auto* primaryButton =
      buttons->addButton(primaryButtonText, QDialogButtonBox::AcceptRole);
  primaryButton->setAutoDefault(false);
  if (cancelButtonText.isEmpty()) {
    primaryButton->setDefault(true);
  } else {
    auto* cancelButton =
        buttons->addButton(cancelButtonText, QDialogButtonBox::RejectRole);
    cancelButton->setDefault(true);
  }
  layout->addWidget(buttons);

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  return dialog.exec() == QDialog::Accepted;
}

bool profileHasPluginOrderData(OrganizerCore& organizer)
{
  const auto* pluginList = organizer.pluginList();
  return pluginList != nullptr && !pluginList->pluginNames().isEmpty();
}

ProfileBackupSelection chooseProfileBackupContents(QWidget* parent,
                                                    bool includeModList,
                                                    bool includePluginOrder,
                                                    bool pluginOrderAvailable)
{
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("profileBackupDialog"));
  dialog.setWindowTitle(QObject::tr("Create profile backup"));
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog.setMinimumWidth(500);
  dialog.resize(540, 320);

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(22, 18, 22, 16);
  layout->setSpacing(7);

  auto* heading = new QLabel(QObject::tr("Back up this profile"), &dialog);
  heading->setObjectName(QStringLiteral("profileBackupTitle"));
  QFont headingFont = heading->font();
  if (headingFont.pointSize() > 0) {
    headingFont.setPointSize(headingFont.pointSize() + 2);
  }
  headingFont.setBold(true);
  heading->setFont(headingFont);
  layout->addWidget(heading);

  auto* description = new QLabel(
      QObject::tr("Choose which parts of the active profile to save. "
                  "Each selected part gets a timestamped backup."),
      &dialog);
  description->setObjectName(QStringLiteral("profileBackupDescription"));
  description->setWordWrap(true);
  layout->addWidget(description);

  auto* contentsHeading = new QLabel(QObject::tr("Backup contents"), &dialog);
  contentsHeading->setObjectName(QStringLiteral("backupContentsHeading"));
  QFont contentsHeadingFont = contentsHeading->font();
  contentsHeadingFont.setBold(true);
  contentsHeading->setFont(contentsHeadingFont);
  layout->addWidget(contentsHeading);

  auto* modListCard = new QFrame(&dialog);
  modListCard->setObjectName(QStringLiteral("backupModListCard"));
  modListCard->setFrameShape(QFrame::StyledPanel);
  auto* modListCardLayout = new QVBoxLayout(modListCard);
  modListCardLayout->setContentsMargins(10, 5, 10, 5);
  modListCardLayout->setSpacing(1);

  auto* modList = new QCheckBox(QObject::tr("Mod list"), modListCard);
  modList->setObjectName(QStringLiteral("backupModList"));
  modList->setChecked(includeModList);
  auto* modListDetails = new QLabel(
      QObject::tr("All installed mods, their enabled state, and priority."),
      modListCard);
  modListDetails->setObjectName(QStringLiteral("backupModListDetails"));
  modListDetails->setIndent(22);
  modListDetails->setWordWrap(true);
  modListCardLayout->addWidget(modList);
  modListCardLayout->addWidget(modListDetails);
  layout->addWidget(modListCard);

  auto* pluginOrderCard = new QFrame(&dialog);
  pluginOrderCard->setObjectName(QStringLiteral("backupPluginOrderCard"));
  pluginOrderCard->setFrameShape(QFrame::StyledPanel);
  auto* pluginOrderCardLayout = new QVBoxLayout(pluginOrderCard);
  pluginOrderCardLayout->setContentsMargins(10, 5, 10, 5);
  pluginOrderCardLayout->setSpacing(1);

  auto* pluginOrder = new QCheckBox(QObject::tr("Plugin order"), pluginOrderCard);
  pluginOrder->setObjectName(QStringLiteral("backupPluginOrder"));
  pluginOrder->setChecked(includePluginOrder && pluginOrderAvailable);
  auto* pluginOrderDetails = new QLabel(
      pluginOrderAvailable
          ? QObject::tr("The plugin list, load order, and locked order.")
          : QObject::tr("No ESP, ESM, or ESL plugins are present in this profile. "
                        "Mod priority is included in the mod list backup."),
      pluginOrderCard);
  pluginOrderDetails->setObjectName(QStringLiteral("backupPluginOrderDetails"));
  pluginOrderDetails->setIndent(22);
  pluginOrderDetails->setWordWrap(true);
  pluginOrder->setEnabled(pluginOrderAvailable);
  pluginOrderCardLayout->addWidget(pluginOrder);
  pluginOrderCardLayout->addWidget(pluginOrderDetails);
  layout->addWidget(pluginOrderCard);

  auto* buttons = new QDialogButtonBox(&dialog);
  buttons->addButton(QDialogButtonBox::Cancel);
  auto* createButton =
      buttons->addButton(QObject::tr("Create backup"), QDialogButtonBox::AcceptRole);
  createButton->setAutoDefault(false);
  if (auto* cancelButton = buttons->button(QDialogButtonBox::Cancel)) {
    cancelButton->setDefault(true);
  }
  createButton->setEnabled(modList->isChecked() ||
                           (pluginOrderAvailable && pluginOrder->isChecked()));
  layout->addSpacing(2);
  layout->addWidget(buttons);

  const auto updateCreateButton = [createButton, modList, pluginOrder,
                                   pluginOrderAvailable] {
    createButton->setEnabled(modList->isChecked() ||
                             (pluginOrderAvailable && pluginOrder->isChecked()));
  };
  QObject::connect(modList, &QCheckBox::toggled, &dialog, updateCreateButton);
  QObject::connect(pluginOrder, &QCheckBox::toggled, &dialog,
                   updateCreateButton);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);

  ProfileBackupSelection selection;
  selection.accepted = dialog.exec() == QDialog::Accepted;
  selection.modList = selection.accepted && modList->isChecked();
  selection.pluginOrder = selection.accepted && pluginOrderAvailable &&
                          pluginOrder->isChecked();
  return selection;
}

void resizeBackupSelectionDialog(SelectionDialog& dialog)
{
  if (auto* scrollArea =
          dialog.findChild<QScrollArea*>(QStringLiteral("scrollArea"))) {
    scrollArea->setMinimumHeight(0);
  }

  constexpr int maximumVisibleChoices = 4;
  constexpr int baseHeight = 200;
  constexpr int rowHeight = 76;
  constexpr int maximumHeight = 460;
  const int visibleChoices =
      std::min(dialog.numChoices(), maximumVisibleChoices);

  dialog.setMinimumSize(520, 250);
  dialog.resize(600, std::min(maximumHeight,
                              baseHeight + visibleChoices * rowHeight));
}

}  // namespace

void setFilterShortcuts(QWidget* widget, QLineEdit* edit)
{
  auto activate = [=] {
    edit->setFocus();
    edit->selectAll();
  };

  auto reset = [=] {
    edit->clear();
    widget->setFocus();
  };

  auto hookActivate = [activate](auto* w) {
    auto* s = new QShortcut(QKeySequence::Find, w);
    s->setAutoRepeat(false);
    s->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(s, &QShortcut::activated, activate);
  };

  auto hookReset = [reset](auto* w) {
    auto* s = new QShortcut(QKeySequence(Qt::Key_Escape), w);
    s->setAutoRepeat(false);
    s->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(s, &QShortcut::activated, reset);
  };

  hookActivate(widget);
  hookReset(widget);

  hookActivate(edit);
  hookReset(edit);
}

MainWindow::MainWindow(Settings& settings, OrganizerCore& organizerCore,
                       PluginContainer& pluginContainer, QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_WasVisible(false),
      m_FirstPaint(true), m_ToolMenuDirty(true), m_linksSeparator(nullptr),
      m_Tutorial(this, "MainWindow"),
      m_OldProfileIndex(-1), m_OldExecutableIndex(-1),
      m_CategoryFactory(CategoryFactory::instance()), m_OrganizerCore(organizerCore),
      m_PluginContainer(pluginContainer),
      m_ArchiveListWriter(std::bind(&MainWindow::saveArchiveList, this)),
      m_LinkToolbar(nullptr), m_LinkDesktop(nullptr), m_LinkStartMenu(nullptr),
      m_NumberOfProblems(0), m_ProblemsCheckRequired(false)
{
  setStartupDiagnosticPhase("mainwindow.construct.begin");
  writeStartupDiagnosticEvent("mainwindow.construct.begin");

  // Keep the slow native menu fade disabled while allowing lighter menu and
  // combo motion effects.
  QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
  QApplication::setEffectEnabled(Qt::UI_AnimateMenu, true);
  QApplication::setEffectEnabled(Qt::UI_AnimateCombo, true);
  QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
  QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);

  setStartupDiagnosticPhase("mainwindow.webengine.configure");
  QWebEngineProfile::defaultProfile()->setPersistentCookiesPolicy(
      QWebEngineProfile::NoPersistentCookies);
  QWebEngineProfile::defaultProfile()->setHttpCacheMaximumSize(52428800);
  QWebEngineProfile::defaultProfile()->setCachePath(settings.paths().cache());
  QWebEngineProfile::defaultProfile()->setPersistentStoragePath(
      settings.paths().cache());

  // qt resets the thread name somewhere within the QWebEngineProfile calls
  // above
  MOShared::SetThisThreadName("main");

  setStartupDiagnosticPhase("mainwindow.setup_ui");
  ui->setupUi(this);
  writeStartupDiagnosticEvent("mainwindow.setup_ui.complete");
  // Revamped keeps the legacy menu commands on the toolbar buttons. Do not
  // restore or show the upstream top menubar for new or existing profiles.
  ui->menuBar->hide();

  setStartupDiagnosticPhase("mainwindow.build_custom_ui");
  m_FilterOptionsDialog = new QDialog(this);
  m_FilterOptionsDialog->setObjectName(QStringLiteral("filterOptionsDialog"));
  m_FilterOptionsDialog->setWindowTitle(tr("Filter options"));
  m_FilterOptionsDialog->setMinimumSize(560, 500);
  m_FilterOptionsDialog->resize(680, 680);

  auto* filterDialogLayout = new QVBoxLayout(m_FilterOptionsDialog);
  filterDialogLayout->setContentsMargins(20, 18, 20, 16);
  filterDialogLayout->setSpacing(12);

  m_FilterDialogTitle = new QLabel(tr("Filter options"), m_FilterOptionsDialog);
  m_FilterDialogTitle->setObjectName(QStringLiteral("filterDialogTitle"));
  auto filterTitleFont = m_FilterDialogTitle->font();
  filterTitleFont.setBold(true);
  filterTitleFont.setPointSize(filterTitleFont.pointSize() + 2);
  m_FilterDialogTitle->setFont(filterTitleFont);
  filterDialogLayout->addWidget(m_FilterDialogTitle);

  m_FilterDialogDescription = new QLabel(m_FilterOptionsDialog);
  m_FilterDialogDescription->setObjectName(
      QStringLiteral("filterDialogDescription"));
  m_FilterDialogDescription->setWordWrap(true);
  filterDialogLayout->addWidget(m_FilterDialogDescription);

  const int filterPaneIndex = ui->categoriesSplitter->indexOf(ui->categoriesGroup);
  if (filterPaneIndex >= 0) {
    auto* placeholder = new QWidget;
    placeholder->setObjectName(QStringLiteral("filterOptionsPlaceholder"));
    placeholder->hide();
    ui->categoriesSplitter->replaceWidget(filterPaneIndex, placeholder);
  }

  ui->categoriesGroup->setTitle(QString());
  ui->categoriesGroup->setMaximumWidth(QWIDGETSIZE_MAX);
  ui->categoriesGroup->setParent(m_FilterOptionsDialog);
  filterDialogLayout->addWidget(ui->categoriesGroup, 1);

  auto* filterDialogButtons =
      new QDialogButtonBox(QDialogButtonBox::Close, m_FilterOptionsDialog);
  filterDialogButtons->setObjectName(QStringLiteral("filterDialogButtons"));
  filterDialogLayout->addWidget(filterDialogButtons);
  connect(filterDialogButtons, &QDialogButtonBox::rejected,
          m_FilterOptionsDialog, &QDialog::reject);
  connect(m_FilterOptionsDialog, &QDialog::finished, this, [this] {
    if (ui->displayCategoriesBtn->isChecked()) {
      ui->displayCategoriesBtn->setChecked(false);
    }
  });

  ui->displayCategoriesBtn->setText(QString());
  ui->displayCategoriesBtn->setIcon(
      QIcon(QStringLiteral(":/MO/gui/mainwindow/filter.svg")));
  ui->displayCategoriesBtn->setIconSize(QSize(16, 16));
  ui->displayCategoriesBtn->setMaximumWidth(30);
  ui->displayCategoriesBtn->setMinimumWidth(28);
  ui->displayCategoriesBtn->setToolTip(tr("Filter options"));
  ui->logDock->setMinimumHeight(175);
  ui->logList->setMinimumHeight(140);
  ui->startButton->setMinimumHeight(44);
  ui->startButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  ui->startButton->setPopupMode(QToolButton::MenuButtonPopup);
  ui->startButton->setMenu(ui->menuRun);
  // Center the icon and label across the full split Run button. Its menu area
  // is excluded from the label area, so account for half of that width here.
  ui->startButton->setStyleSheet(
      "QToolButton#startButton { padding-left: 43px; }"
      "QToolButton#startButton::menu-button { width: 34px; }");
  configureEldenRingHelp();
  languageChange(settings.interface().language());
  ui->statusBar->setup(ui, settings);

  {
    auto& ni = NexusInterface::instance();

    // there are two ways to get here:
    //  1) the user just started MO, and
    //  2) the user has changed some setting that required a restart
    //
    // "restarting" MO doesn't actually re-execute the binary, it just basically
    // executes most of main() again, so a bunch of things are actually not
    // reset
    //
    // one of these things is the api status, which will have fired its events
    // long before the execution gets here because stuff is still cached and no
    // real request to nexus is actually done
    //
    // therefore, when the user starts MO normally, the user account and stats
    // will be empty (which is fine) and populated later on when the api key
    // check has finished
    //
    // in the rare case where the user restarts MO through the settings, this
    // will correctly pick up the previous values
    updateWindowTitle(ni.getAPIUserAccount());
    ui->statusBar->setAPI(ni.getAPIStats(), ni.getAPIUserAccount());
  }

  m_CategoryFactory.loadCategories();

  ui->logList->setCore(m_OrganizerCore);

  setupToolbar();
  toggleMO2EndorseState();

  TaskProgressManager::instance().tryCreateTaskbar();

  setStartupDiagnosticPhase("mainwindow.setup_mod_lists");
  setupModList();
  ui->espList->setup(m_OrganizerCore, this, ui);
  ui->bsaList->setLocalMoveOnly(true);
  ui->bsaList->setHeaderHidden(true);

  const bool pluginListAdjusted =
      settings.geometry().restoreState(ui->espList->header());

  // data tab
  setStartupDiagnosticPhase("mainwindow.create_tabs");
  setStartupDiagnosticPhase("mainwindow.create_tabs.data_tab.construct");
  m_DataTab.reset(new DataTab(m_OrganizerCore, m_PluginContainer, this, ui));
  setStartupDiagnosticPhase("mainwindow.create_tabs.data_tab.construct.complete");
  setStartupDiagnosticPhase("mainwindow.create_tabs.data_tab.restore_state");
  m_DataTab->restoreState(settings);
  setStartupDiagnosticPhase(
      "mainwindow.create_tabs.data_tab.restore_state.complete");

  connect(m_DataTab.get(), &DataTab::executablesChanged, [&] {
    refreshExecutablesList();
  });

  connect(m_DataTab.get(), &DataTab::originModified, [&](int id) {
    originModified(id);
  });

  connect(m_DataTab.get(), &DataTab::displayModInformation,
          [&](auto&& m, auto&& i, auto&& tab) {
            displayModInformation(m, i, tab);
  });

  // downloads tab
  setStartupDiagnosticPhase("mainwindow.create_tabs.downloads_tab.construct");
  m_DownloadsTab.reset(new DownloadsTab(m_OrganizerCore, ui));
  setStartupDiagnosticPhase(
      "mainwindow.create_tabs.downloads_tab.construct.complete");

  // saves tab
  m_SavesTab.reset(new SavesTab(this, m_OrganizerCore, ui));

  // Hide stuff we do not need:
  auto& features = m_OrganizerCore.gameFeatures();
  if (!features.gameFeature<GamePlugins>()) {
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->espTab));
  }
  if (!features.gameFeature<DataArchives>()) {
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->bsaTab));
  }

  settings.geometry().restoreState(ui->downloadView->header());
  qobject_cast<DownloadListHeader*>(ui->downloadView->header())
      ->ensureReadableSections();
  settings.geometry().restoreState(ui->savegameList->header());

  ui->splitter->setStretchFactor(0, 3);
  ui->splitter->setStretchFactor(1, 2);

  resizeLists(pluginListAdjusted);

  m_LinkToolbar = new QAction(QIcon(":/MO/gui/mainwindow/shortcut-add.svg"),
                              tr("Toolbar and Menu"), this);
  m_LinkDesktop = new QAction(QIcon(":/MO/gui/mainwindow/shortcut-add.svg"),
                              tr("Desktop"), this);
  m_LinkStartMenu = new QAction(QIcon(":/MO/gui/mainwindow/shortcut-add.svg"),
                                tr("Start Menu"), this);
  connect(m_LinkToolbar, &QAction::triggered, this, &MainWindow::linkToolbar);
  connect(m_LinkDesktop, &QAction::triggered, this, &MainWindow::linkDesktop);
  connect(m_LinkStartMenu, &QAction::triggered, this, &MainWindow::linkMenu);
  connect(ui->menuRun, &QMenu::aboutToShow, this,
          &MainWindow::updateShortcutActionIcons);

  ui->listOptionsBtn->setMenu(createModListOptionsMenu());

  ui->openFolderMenu->setMenu(openFolderMenu());

  // don't allow mouse wheel to switch grouping, too many people accidentally
  // turn on grouping and then don't understand what happened
  EventFilter* noWheel = new EventFilter(this, [](QObject*, QEvent* event) -> bool {
    return event->type() == QEvent::Wheel;
  });

  ui->groupCombo->installEventFilter(noWheel);
  ui->profileBox->installEventFilter(noWheel);

  updateSortButton();

  connect(&m_PluginContainer, SIGNAL(diagnosisUpdate()), this,
          SLOT(scheduleCheckForProblems()));

  connect(&m_OrganizerCore, &OrganizerCore::directoryStructureReady, this,
          &MainWindow::onDirectoryStructureChanged);
  connect(m_OrganizerCore.directoryRefresher(),
          SIGNAL(progress(const DirectoryRefreshProgress*)), this,
          SLOT(refresherProgress(const DirectoryRefreshProgress*)));
  connect(m_OrganizerCore.directoryRefresher(), SIGNAL(error(QString)), this,
          SLOT(showError(QString)));

  connect(&m_OrganizerCore.settings(), SIGNAL(languageChanged(QString)), this,
          SLOT(languageChange(QString)));
  connect(&m_OrganizerCore.settings(), SIGNAL(styleChanged(QString)), this,
          SIGNAL(styleChanged(QString)));

  connect(m_OrganizerCore.updater(), SIGNAL(restart()), this, SLOT(close()));
  connect(m_OrganizerCore.updater(), SIGNAL(updateAvailable()), this,
          SLOT(updateAvailable()));
  connect(m_OrganizerCore.updater(), SIGNAL(motdAvailable(QString)), this,
          SLOT(motdReceived(QString)));
  connect(&m_OrganizerCore, &OrganizerCore::refreshTriggered, this, [this]() {
    updateSortButton();
  });

  connect(&NexusInterface::instance(), SIGNAL(requestNXMDownload(QString)),
          &m_OrganizerCore, SLOT(downloadRequestedNXM(QString)));
  connect(&NexusInterface::instance(),
          SIGNAL(nxmDownloadURLsAvailable(QString, int, int, QVariant, QVariant, int)),
          this, SLOT(nxmDownloadURLs(QString, int, int, QVariant, QVariant, int)));
  connect(&NexusInterface::instance(), SIGNAL(needLogin()), &m_OrganizerCore,
          SLOT(nexusApi()));

  connect(NexusInterface::instance().getAccessManager(),
          &NXMAccessManager::credentialsReceived, this, &MainWindow::updateWindowTitle);
  connect(&NexusInterface::instance(), &NexusInterface::requestsChanged, ui->statusBar,
          &StatusBar::setAPI);

  connect(&TutorialManager::instance(), SIGNAL(windowTutorialFinished(QString)), this,
          SLOT(windowTutorialFinished(QString)));
  connect(ui->tabWidget, SIGNAL(currentChanged(int)), &TutorialManager::instance(),
          SIGNAL(tabChanged(int)));
  connect(ui->toolBar, SIGNAL(customContextMenuRequested(QPoint)), this,
          SLOT(toolBar_customContextMenuRequested(QPoint)));
  connect(ui->menuToolbars, &QMenu::aboutToShow, [&] {
    updateToolbarMenu();
  });
  connect(ui->menuView, &QMenu::aboutToShow, [&] {
    updateViewMenu();
  });
  connect(ui->actionTool->menu(), &QMenu::aboutToShow, [&] {
    updateToolMenu();
  });
  connect(&m_PluginContainer, &PluginContainer::pluginEnabled, this,
          [this](IPlugin* plugin) {
            m_ToolMenuDirty = true;
            if (m_PluginContainer.implementInterface<IPluginModPage>(plugin)) {
              updateModPageMenu();
            }
          });
  connect(&m_PluginContainer, &PluginContainer::pluginDisabled, this,
          [this](IPlugin* plugin) {
            m_ToolMenuDirty = true;
            if (m_PluginContainer.implementInterface<IPluginModPage>(plugin)) {
              updateModPageMenu();
            }
          });
  connect(&m_PluginContainer, &PluginContainer::pluginRegistered, this,
          &MainWindow::onPluginRegistrationChanged);
  connect(&m_PluginContainer, &PluginContainer::pluginUnregistered, this,
          &MainWindow::onPluginRegistrationChanged);

  connect(&m_OrganizerCore, &OrganizerCore::modInstalled, this,
          &MainWindow::modInstalled);

  connect(&m_CategoryFactory, SIGNAL(nexusCategoryRefresh(CategoriesDialog*)), this,
          SLOT(refreshNexusCategories(CategoriesDialog*)));
  connect(&m_CategoryFactory, SIGNAL(categoriesSaved()), this, SLOT(categoriesSaved()));

  m_CheckBSATimer.setSingleShot(true);
  connect(&m_CheckBSATimer, SIGNAL(timeout()), this, SLOT(checkBSAList()));

  setFilterShortcuts(ui->modList, ui->modFilterEdit);
  setFilterShortcuts(ui->espList, ui->espFilterEdit);
  setFilterShortcuts(ui->downloadView, ui->downloadFilterEdit);

  m_UpdateProblemsTimer.setSingleShot(true);
  connect(&m_UpdateProblemsTimer, &QTimer::timeout, this,
          &MainWindow::checkForProblemsAsync);
  connect(this, &MainWindow::checkForProblemsDone, this,
          &MainWindow::updateProblemsButton, Qt::ConnectionType::QueuedConnection);

  m_SaveMetaTimer.setSingleShot(false);
  connect(&m_SaveMetaTimer, SIGNAL(timeout()), this, SLOT(saveModMetas()));
  m_SaveMetaTimer.start(5000);

  FileDialogMemory::restore(settings);

  fixCategories();

  m_StartTime = QTime::currentTime();

  m_Tutorial.expose("modList", m_OrganizerCore.modList());
  m_Tutorial.expose("espList", m_OrganizerCore.pluginList());

  m_OrganizerCore.setUserInterface(this);
  connect(m_OrganizerCore.modList(), &ModList::showMessage, [=](auto&& message) {
    showMessage(message);
  });
  connect(m_OrganizerCore.modList(), &ModList::modRenamed,
          [=](auto&& oldName, auto&& newName) {
            modRenamed(oldName, newName);
          });
  connect(m_OrganizerCore.modList(), &ModList::modUninstalled, [=](auto&& name) {
    modRemoved(name);
  });
  connect(m_OrganizerCore.modList(), &ModList::fileMoved, [=](auto&&... args) {
    fileMoved(args...);
  });
  connect(m_OrganizerCore.installationManager(), &InstallationManager::modReplaced,
          [=](auto&& name) {
            modRemoved(name);
          });
  connect(m_OrganizerCore.downloadManager(), &DownloadManager::showMessage,
          [=](auto&& message) {
            showMessage(message);
          });
  for (const QString& fileName : m_PluginContainer.pluginFileNames()) {
    installTranslator(QFileInfo(fileName).baseName());
  }

  updateModPageMenu();

  // refresh profiles so the current profile can be activated
  refreshProfiles(false);

  ui->profileBox->setCurrentText(m_OrganizerCore.currentProfile()->name());

  if (settings.archiveParsing()) {
    ui->dataTabShowFromArchives->setCheckState(Qt::Checked);
    ui->dataTabShowFromArchives->setEnabled(true);
  } else {
    ui->dataTabShowFromArchives->setCheckState(Qt::Unchecked);
    ui->dataTabShowFromArchives->setEnabled(false);
  }

  QApplication::instance()->installEventFilter(this);

  scheduleCheckForProblems();
  refreshExecutablesList();
  updatePinnedExecutables();
  resetActionIcons();
  processUpdates();

  ui->modList->updateModCount();
  ui->espList->updatePluginCount();
  ui->statusBar->updateNormalMessage(m_OrganizerCore);
  setStartupDiagnosticPhase("mainwindow.construct.complete");
  writeStartupDiagnosticEvent("mainwindow.construct.complete");
}

void MainWindow::setupModList()
{
  ui->modList->setup(m_OrganizerCore, m_CategoryFactory, this, ui);

  connect(&ui->modList->actions(), &ModListViewActions::overwriteCleared, [=]() {
    scheduleCheckForProblems();
  });
  connect(&ui->modList->actions(), &ModListViewActions::originModified, this,
          &MainWindow::originModified);
  connect(&ui->modList->actions(), &ModListViewActions::modInfoDisplayed, this,
          &MainWindow::modInfoDisplayed);

  connect(m_OrganizerCore.modList(), &ModList::modPrioritiesChanged, [&]() {
    m_ArchiveListWriter.write();
  });
}

void MainWindow::resetActionIcons()
{
  // this is a bit of a hack
  //
  // the .qss files have historically set qproperty-icon by id and these ids
  // correspond to the QActions created in the .ui file
  //
  // the problem is that QActions do not support having their icon property
  // set from a .qss because they're not widgets (they don't inherit from
  // QWidget), and styling only works on widget
  //
  // a QAction _does_ have an associated icon, it just can't be set from a .qss
  // file
  //
  // so here, a dummy QToolButton widget is created for each QAction and is
  // given the same name as the action, which makes it pick up the icon
  // specified in the .qss file
  //
  // that icon is then given to the widget used by the QAction (if it's some
  // sort of button, which typically happens on the toolbar) _and_ to the
  // QAction itself, which is used in the menu bar

  // clearing the notification, will be set below if the stylesheet has set
  // anything for it
  m_originalNotificationIcon = {};

  // QActions created from the .ui file are children of the main window
  for (QAction* action : findChildren<QAction*>()) {
    // creating a dummy button
    auto dummy = std::make_unique<QToolButton>();

    // reusing the action name
    dummy->setObjectName(action->objectName());

    // styling the button, this has to be done manually because the button is
    // never added anywhere
    style()->polish(dummy.get());

    // the button's icon may be null if it wasn't specified in the .qss file,
    // which can happen if the stylesheet just doesn't override icons, or for
    // other actions like the pinned custom executables
    const auto icon = dummy->icon();
    if (icon.isNull()) {
      continue;
    }

    // button associated with the action on the toolbar
    QWidget* actionWidget = ui->toolBar->widgetForAction(action);

    if (auto* actionButton = dynamic_cast<QAbstractButton*>(actionWidget)) {
      actionButton->setIcon(icon);
    }

    // the action's icon is used by the menu bar
    action->setIcon(icon);

    if (action == ui->actionNotifications) {
      // if the stylesheet has set a notification icon, remember it here so it
      // can be used in updateProblemsButton()
      m_originalNotificationIcon = icon;
    }
  }

  // update the button for the potentially new icon
  updateProblemsButton();
}

MainWindow::~MainWindow()
{
  try {
    cleanup();

    m_OrganizerCore.setUserInterface(nullptr);

    if (m_IntegratedBrowser) {
      m_IntegratedBrowser->close();
      m_IntegratedBrowser.reset();
    }

    delete ui;
  } catch (std::exception& e) {
    QMessageBox::critical(
        nullptr, tr("Crash on exit"),
        tr("MO crashed while exiting.  Some settings may not be saved.\n\nError: %1")
            .arg(e.what()),
        QMessageBox::Ok);
  }
}

void MainWindow::updateWindowTitle(const APIUserAccount& user)
{
  //"\xe2\x80\x93" is an "em dash", a longer "-"
  QString title = QString("%1 \xe2\x80\x93 Mod Organizer 2: Revamped v%2")
                      .arg(m_OrganizerCore.managedGame()->displayGameName(),
                           m_OrganizerCore.getVersion().displayString(3));

  if (!user.name().isEmpty()) {
    const QString premium = (user.type() == APIUserAccountTypes::Premium ? "*" : "");
    title.append(QString(" (%1%2)").arg(user.name(), premium));
  }

  this->setWindowTitle(title);
}

void MainWindow::resizeLists(bool pluginListCustom)
{
  // ensure the columns aren't so small you can't see them any more
  for (int i = 0; i < ui->modList->header()->count(); ++i) {
    if (ui->modList->header()->sectionSize(i) < 10) {
      ui->modList->header()->resizeSection(i, 10);
    }
  }

  if (!pluginListCustom) {
    // resize plugin list to fit content
    for (int i = 0; i < ui->espList->header()->count(); ++i) {
      ui->espList->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    ui->espList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  }
}

void MainWindow::allowListResize()
{
  // allow resize on mod list
  auto* modListHeader = ui->modList->header();
  for (int i = 0; i < ui->modList->header()->count(); ++i) {
    modListHeader->setSectionResizeMode(i, QHeaderView::Interactive);
  }
  // Keep a balanced minimum width for the visible status columns while letting
  // the mod name use the remaining space as the window changes size.
  modListHeader->setStretchLastSection(false);
  modListHeader->setSectionResizeMode(ModList::COL_NAME, QHeaderView::Stretch);
  const auto ensureMinimumWidth = [modListHeader](int column, int minimumWidth) {
    if (modListHeader->sectionSize(column) < minimumWidth) {
      modListHeader->resizeSection(column, minimumWidth);
    }
  };
  ensureMinimumWidth(ModList::COL_CONFLICTFLAGS, 150);
  ensureMinimumWidth(ModList::COL_FLAGS, 130);
  ensureMinimumWidth(ModList::COL_CATEGORY, 160);
  ensureMinimumWidth(ModList::COL_VERSION, 160);
  ensureMinimumWidth(ModList::COL_PRIORITY, 150);

  // allow resize on plugin list
  for (int i = 0; i < ui->espList->header()->count(); ++i) {
    ui->espList->header()->setSectionResizeMode(i, QHeaderView::Interactive);
  }
  ui->espList->header()->setStretchLastSection(true);
}

void MainWindow::updateStyle(const QString&)
{
  resetActionIcons();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
  m_Tutorial.resize(event->size());
  QMainWindow::resizeEvent(event);
}

void MainWindow::setupToolbar()
{
  setupActionMenu(ui->actionModPage);
  setupActionMenu(ui->actionTool);
  setupActionMenu(ui->actionHelp);
  setupActionMenu(ui->actionEndorseMO);

  // Keep the other right-side toolbar actions on the same visual baseline as
  // Notifications. Equal top and bottom padding would leave their contents a
  // little higher; this shifts icon and text down while preserving total height.
  const auto alignWithNotifications = [this](QAction* action) {
    if (auto* button = qobject_cast<QToolButton*>(
            ui->toolBar->widgetForAction(action))) {
      button->setStyleSheet(
          QStringLiteral("QToolButton { padding-top: 7px; padding-bottom: 1px; "
                         "margin-top: 3px; margin-bottom: -3px; }"));
    }
  };
  alignWithNotifications(ui->actionEndorseMO);
  alignWithNotifications(ui->actionUpdate);
  alignWithNotifications(ui->actionHelp);

  createHelpMenu();
  createEndorseMenu();

  // find last separator, add a spacer just before it so the icons are
  // right-aligned
  m_linksSeparator = nullptr;
  for (auto* a : ui->toolBar->actions()) {
    if (a->isSeparator()) {
      m_linksSeparator = a;
    }
  }

  if (m_linksSeparator) {
    auto* spacer = new QWidget(ui->toolBar);
    spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    ui->toolBar->insertWidget(m_linksSeparator, spacer);

  } else {
    log::warn("no separator found on the toolbar, icons won't be right-aligned");
  }

  if (!InstanceManager::singleton().allowedToChangeInstance()) {
    ui->actionChange_Game->setVisible(false);
  }
}

void MainWindow::setupActionMenu(QAction* a)
{
  a->setMenu(new QMenu(this));

  auto* w = ui->toolBar->widgetForAction(a);
  if (auto* tb = dynamic_cast<QToolButton*>(w))
    tb->setPopupMode(QToolButton::InstantPopup);
}

void MainWindow::updatePinnedExecutables()
{
  for (auto* a : ui->toolBar->actions()) {
    if (a->objectName().startsWith("custom__")) {
      ui->toolBar->removeAction(a);
      a->deleteLater();
    }
  }

  ui->menuRun->clear();

  bool hasLinks = false;

  for (const auto& exe : *m_OrganizerCore.executablesList()) {
    if (!exe.hide() && exe.isShownOnToolbar()) {
      hasLinks = true;

      QAction* exeAction =
          new QAction(iconForExecutable(exe.binaryInfo().filePath()), exe.title());

      exeAction->setObjectName(QString("custom__") + exe.title());
      exeAction->setStatusTip(exe.binaryInfo().filePath());

      if (!connect(exeAction, SIGNAL(triggered()), this, SLOT(startExeAction()))) {
        log::debug("failed to connect trigger?");
      }

      if (m_linksSeparator) {
        ui->toolBar->insertAction(m_linksSeparator, exeAction);
      } else {
        // separator wasn't found, add it to the end
        ui->toolBar->addAction(exeAction);
      }

      ui->menuRun->addAction(exeAction);
    }
  }

  if (hasLinks) {
    ui->menuRun->addSeparator();
  }
  ui->menuRun->addAction(m_LinkToolbar);
  ui->menuRun->addAction(m_LinkDesktop);
  ui->menuRun->addAction(m_LinkStartMenu);

  // The arrow remains useful for shortcut options even when no executable is
  // pinned to the toolbar.
  ui->menuRun->menuAction()->setVisible(hasLinks || getSelectedExecutable());
  updateShortcutActionIcons();
}

void MainWindow::updateToolbarMenu()
{
  ui->actionToolBarMainToggle->setChecked(ui->toolBar->isVisible());
  ui->actionStatusBarToggle->setChecked(ui->statusBar->isVisible());

  ui->actionToolBarSmallIcons->setChecked(ui->toolBar->iconSize() == SmallToolbarSize);
  ui->actionToolBarMediumIcons->setChecked(ui->toolBar->iconSize() ==
                                           MediumToolbarSize);
  ui->actionToolBarLargeIcons->setChecked(ui->toolBar->iconSize() == LargeToolbarSize);

  ui->actionToolBarIconsOnly->setChecked(ui->toolBar->toolButtonStyle() ==
                                         Qt::ToolButtonIconOnly);
  ui->actionToolBarTextOnly->setChecked(ui->toolBar->toolButtonStyle() ==
                                        Qt::ToolButtonTextOnly);
  ui->actionToolBarIconsAndText->setChecked(ui->toolBar->toolButtonStyle() ==
                                            Qt::ToolButtonTextUnderIcon);
}

void MainWindow::updateViewMenu()
{
  ui->actionViewLog->setChecked(ui->logDock->isVisible());
}

QMenu* MainWindow::createPopupMenu()
{
  auto* m = new QMenu;

  // add all the actions from the toolbars menu
  for (auto* a : ui->menuToolbars->actions()) {
    m->addAction(a);
  }

  m->addSeparator();

  // other actions
  m->addAction(ui->actionViewLog);

  // make sure the actions are updated
  updateToolbarMenu();
  updateViewMenu();

  return m;
}

void MainWindow::on_actionToolBarMainToggle_triggered()
{
  ui->toolBar->setVisible(!ui->toolBar->isVisible());
}

void MainWindow::on_actionStatusBarToggle_triggered()
{
  ui->statusBar->setVisible(!ui->statusBar->isVisible());
}

void MainWindow::on_actionToolBarSmallIcons_triggered()
{
  setToolbarSize(SmallToolbarSize);
}

void MainWindow::on_actionToolBarMediumIcons_triggered()
{
  setToolbarSize(MediumToolbarSize);
}

void MainWindow::on_actionToolBarLargeIcons_triggered()
{
  setToolbarSize(LargeToolbarSize);
}

void MainWindow::on_actionToolBarIconsOnly_triggered()
{
  setToolbarButtonStyle(Qt::ToolButtonIconOnly);
}

void MainWindow::on_actionToolBarTextOnly_triggered()
{
  setToolbarButtonStyle(Qt::ToolButtonTextOnly);
}

void MainWindow::on_actionToolBarIconsAndText_triggered()
{
  setToolbarButtonStyle(Qt::ToolButtonTextUnderIcon);
}

void MainWindow::on_actionViewLog_triggered()
{
  ui->logDock->setVisible(!ui->logDock->isVisible());
}

void MainWindow::setToolbarSize(const QSize& s)
{
  for (auto* tb : findChildren<QToolBar*>()) {
    tb->setIconSize(s);
  }

  // Rebuild the notification badge at the new toolbar icon size so it keeps
  // the same alignment and visual weight as the neighboring actions.
  updateProblemsButton();
}

void MainWindow::setToolbarButtonStyle(Qt::ToolButtonStyle s)
{
  for (auto* tb : findChildren<QToolBar*>()) {
    tb->setToolButtonStyle(s);
  }
}

void MainWindow::on_centralWidget_customContextMenuRequested(const QPoint& pos)
{
  // this allows for getting the context menu even if both the menubar and all
  // the toolbars are hidden; an alternative is the Alt key handled in
  // keyPressEvent() below

  // the custom context menu event bubbles up to here if widgets don't actually
  // process this, which would show the menu when right-clicking button, labels,
  // etc.
  //
  // only show the context menu when right-clicking on the central widget
  // itself, which is basically just the outer edges of the main window
  auto* w = childAt(pos);
  if (w != ui->centralWidget) {
    return;
  }

  createPopupMenu()->exec(ui->centralWidget->mapToGlobal(pos));
}

void MainWindow::scheduleCheckForProblems()
{
  if (!m_UpdateProblemsTimer.isActive()) {
    m_UpdateProblemsTimer.start(500);
  }
}

void MainWindow::updateProblemsButton()
{
  // if the current stylesheet doesn't provide an icon, this is used instead
  const char* DefaultIconName = ":/MO/gui/mainwindow/notifications.svg";

  const std::size_t numProblems = m_NumberOfProblems;

  // original icon without a count painted on it
  const QIcon original = m_originalNotificationIcon.isNull()
                             ? QIcon(DefaultIconName)
                             : m_originalNotificationIcon;

  if (numProblems > 0) {
    ui->actionNotifications->setToolTip(tr("There are notifications to read"));
  } else {
    ui->actionNotifications->setToolTip(tr("There are no notifications"));
  }

  // Keep the toolbar icon canvas identical with and without a badge. Swapping
  // between the theme icon and a composed pixmap can change the button's size
  // hint and make Notifications jump vertically when the last item is cleared.
  const QSize iconCanvasSize = ui->toolBar->iconSize();
  const int iconExtent = qMax(1, qMin(iconCanvasSize.width(),
                                      iconCanvasSize.height()));
  const qreal iconScale = static_cast<qreal>(iconExtent) / 64.0;
  const int iconOffsetX = (iconCanvasSize.width() - iconExtent) / 2;
  const int iconOffsetY = (iconCanvasSize.height() - iconExtent) / 2;
  const int bellSize = qMax(1, qRound(54.0 * iconScale));

  QPixmap merged(iconCanvasSize);
  merged.fill(Qt::transparent);
  const QPixmap bell = original.pixmap(QSize(bellSize, bellSize)).scaled(
      bellSize, bellSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  {
    QPainter painter(&merged);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.drawPixmap(iconOffsetX + qRound(5.0 * iconScale),
                       iconOffsetY + qRound(5.0 * iconScale), bell);

    if (numProblems > 0) {
      const QString badgeText =
          QString::number(static_cast<qulonglong>(numProblems));
      const qreal badgeWidth =
          qMax<qreal>(22.0, 12.0 + 6.0 * badgeText.size()) * iconScale;
      const qreal badgeHeight = 22.0 * iconScale;
      const qreal badgeRightInset = 4.0 * iconScale;
      const qreal badgeBottom = iconOffsetY + iconExtent * 0.80;
      const QRectF badgeRect(
          iconOffsetX + iconExtent - badgeRightInset - badgeWidth,
          badgeBottom - badgeHeight, badgeWidth, badgeHeight);

      painter.setPen(QPen(QColor("#FFFFFF"), 1.5 * iconScale));
      painter.setBrush(QColor("#F2C84B"));
      painter.drawEllipse(badgeRect);

      QFont badgeFont = painter.font();
      badgeFont.setBold(true);
      badgeFont.setPixelSize(
          qMax(5, qRound((badgeText.size() > 2 ? 12.0 : 14.0) * iconScale)));
      painter.setFont(badgeFont);
      painter.setPen(QColor("#342B16"));
      painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
    }
  }

  const QIcon final(merged);

  ui->actionNotifications->setEnabled(numProblems > 0);

  // setting the icon on the action (shown on the menu)
  ui->actionNotifications->setIcon(final);

  // setting the icon on the toolbar button
  if (auto* actionWidget = ui->toolBar->widgetForAction(ui->actionNotifications)) {
    if (auto* button = dynamic_cast<QAbstractButton*>(actionWidget)) {
      button->setIcon(final);
    }
  }

  // updating the status bar, may be null very early when MO is starting
  if (ui->statusBar) {
    ui->statusBar->setNotifications(numProblems > 0);
  }
}

bool MainWindow::errorReported(QString& logFile)
{
  QDir dir(qApp->property("dataPath").toString() + "/" +
           QString::fromStdWString(AppConfig::logPath()));
  QFileInfoList files =
      dir.entryInfoList(QStringList("ModOrganizer_??_??_??_??_??.log"), QDir::Files,
                        QDir::Name | QDir::Reversed);

  if (files.count() > 0) {
    logFile = files.at(0).absoluteFilePath();
    QFile file(logFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      char buffer[1024];
      int line = 0;
      while (!file.atEnd()) {
        file.readLine(buffer, 1024);
        if (strncmp(buffer, "ERROR", 5) == 0) {
          return true;
        }

        // prevent this function from taking forever
        if (line++ >= 50000) {
          break;
        }
      }
    }
  }

  return false;
}

QFuture<void> MainWindow::checkForProblemsAsync()
{
  return QtConcurrent::run([this]() {
    checkForProblemsImpl();
  });
}

void MainWindow::checkForProblemsImpl()
{
  m_ProblemsCheckRequired = true;

  std::scoped_lock lk(m_CheckForProblemsMutex);

  // another thread might already have checked while this one was waiting on the lock
  if (m_ProblemsCheckRequired) {
    m_ProblemsCheckRequired = false;
    TimeThis tt("MainWindow::checkForProblemsImpl()");
    size_t numProblems = 0;
    for (QObject* pluginObj : m_PluginContainer.plugins<QObject>()) {
      IPlugin* plugin = qobject_cast<IPlugin*>(pluginObj);
      if (plugin == nullptr || m_PluginContainer.isEnabled(plugin)) {
        IPluginDiagnose* diagnose = qobject_cast<IPluginDiagnose*>(pluginObj);
        if (diagnose != nullptr)
          numProblems += diagnose->activeProblems().size();
      }
    }
    m_NumberOfProblems = numProblems;
    emit checkForProblemsDone();
  }
}

void MainWindow::about()
{
  for (IPluginTool* tool : m_PluginContainer.plugins<IPluginTool>()) {
    if (tool == nullptr || !m_PluginContainer.isEnabled(tool) ||
        tool->displayName().compare(QStringLiteral("About MO2 Revamped"),
                                    Qt::CaseInsensitive) != 0) {
      continue;
    }

    tool->setParentWidget(this);
    try {
      tool->display();
      return;
    } catch (const std::exception& e) {
      reportError(tr("Plugin \"%1\" failed: %2")
                      .arg(tool->localizedName())
                      .arg(e.what()));
    } catch (...) {
      reportError(tr("Plugin \"%1\" failed").arg(tool->localizedName()));
    }
    return;
  }

  const auto* game = m_OrganizerCore.managedGame();
  const bool eldenRingEdition =
      game && game->gameShortName().compare("eldenring", Qt::CaseInsensitive) == 0;
  AboutDialog(m_OrganizerCore.getVersion().displayString(3), this,
              eldenRingEdition)
      .exec();
}

void MainWindow::createEndorseMenu()
{
  auto* menu = ui->actionEndorseMO->menu();
  if (!menu) {
    // shouldn't happen
    return;
  }

  menu->clear();

  QAction* endorseAction = new QAction(tr("Endorse"), menu);
  connect(endorseAction, SIGNAL(triggered()), this, SLOT(actionEndorseMO()));
  menu->addAction(endorseAction);

  QAction* wontEndorseAction = new QAction(tr("Won't Endorse"), menu);
  connect(wontEndorseAction, SIGNAL(triggered()), this, SLOT(actionWontEndorseMO()));
  menu->addAction(wontEndorseAction);
}

void MainWindow::createHelpMenu()
{
  //: Translation strings for tutorial names
  static std::map<QString, const char*> translate = {
      {"First Steps", QT_TR_NOOP("Getting Started")},
      {"Conflict Resolution", QT_TR_NOOP("Understanding Mod Conflicts")},
      {"Overview", QT_TR_NOOP("Interface Overview")}};

  auto* menu = ui->actionHelp->menu();
  if (!menu) {
    // this happens on startup because languageChanged() (which calls this) is
    // called before the menus are actually created
    return;
  }

  menu->clear();

  const auto* game = m_OrganizerCore.managedGame();
  const bool isEldenRing =
      game && game->gameShortName().compare("eldenring", Qt::CaseInsensitive) == 0;

  menu->addSection(tr("Learn"));

  if (isEldenRing) {
    QAction* guideAction = new QAction(tr("Elden Ring Guide"), menu);
    guideAction->setToolTip(
        tr("Learn about profiles, archive layouts, native DLLs, and save profiles."));
    connect(guideAction, &QAction::triggered, this,
            [this] { showEldenRingGuide(); });
    menu->addAction(guideAction);
  }

  typedef std::vector<std::pair<int, QAction*>> ActionList;
  ActionList tutorials;
  QDirIterator dirIter(QApplication::applicationDirPath() + "/tutorials",
                       QStringList("*.js"), QDir::Files);
  while (dirIter.hasNext()) {
    dirIter.next();
    const QString fileName = dirIter.fileName();

    // The generic tours include Bethesda-specific plugin and record-conflict
    // guidance, while the Elden Ring tour is no longer offered from Help.
    if (isEldenRing || fileName == "tutorial_eldenring_overview.js") {
      continue;
    }

    QFile file(dirIter.filePath());
    if (!file.open(QIODevice::ReadOnly)) {
      log::error("Failed to open {}", fileName);
      continue;
    }
    const QString firstLine = QString::fromUtf8(file.readLine());
    if (!firstLine.startsWith("//TL")) {
      continue;
    }

    const QStringList params = firstLine.mid(4).trimmed().split('#');
    if (params.size() != 2) {
      log::error("invalid header line for tutorial {}, expected 2 parameters",
                 fileName);
      continue;
    }

    const auto translatedTitle = translate.find(params.at(0));
    if (translatedTitle == translate.end()) {
      continue;
    }

    QAction* tutorialAction =
        new QAction(tr(translatedTitle->second), menu);
    tutorialAction->setData(fileName);
    tutorials.push_back(
        std::make_pair(params.at(1).toInt(), tutorialAction));
  }

  std::sort(tutorials.begin(), tutorials.end(),
            [](const ActionList::value_type& lhs,
               const ActionList::value_type& rhs) {
              return lhs.first < rhs.first;
            });

  if (!tutorials.empty()) {
    QMenu* tutorialMenu = new QMenu(tr("Guided Tours"), menu);
    for (const auto& tutorial : tutorials) {
      connect(tutorial.second, SIGNAL(triggered()), this,
              SLOT(tutorialTriggered()));
      tutorialMenu->addAction(tutorial.second);
    }
    menu->addMenu(tutorialMenu);
  }

  menu->addSection(tr("Resources"));

  QAction* repositoryAction = new QAction(tr("MO2 Revamped on GitHub"), menu);
  repositoryAction->setToolTip(
      tr("Open the project repository, source notes, and third-party notices."));
  connect(repositoryAction, SIGNAL(triggered()), this,
          SLOT(revampedRepositoryTriggered()));
  menu->addAction(repositoryAction);

  if (game && !game->getSupportURL().isEmpty()) {
    QAction* gameSupportAction = new QAction(tr("Game Support Wiki"), menu);
    gameSupportAction->setToolTip(
        tr("Open the game-specific modding reference provided by its MO2 plugin."));
    connect(gameSupportAction, SIGNAL(triggered()), this, SLOT(gameSupportTriggered()));
    menu->addAction(gameSupportAction);
  }

  menu->addSection(tr("Support"));

  QAction* issueAction = new QAction(tr("Report a Problem"), menu);
  issueAction->setToolTip(
      tr("Open the MO2 Revamped issue tracker on GitHub."));
  connect(issueAction, SIGNAL(triggered()), this, SLOT(issueTriggered()));
  menu->addAction(issueAction);

  menu->addSection(tr("About"));
  QAction* revampedAboutAction =
      menu->addAction(tr("About MO2 Revamped"), this, SLOT(about()));
  revampedAboutAction->setObjectName(QStringLiteral("mo2RevampedAboutAction"));
  revampedAboutAction->setToolTip(
      tr("Show MO2 Revamped information, components, and credits."));
}

bool MainWindow::addProfile()
{
  QComboBox* profileBox = findChild<QComboBox*>("profileBox");
  bool okClicked        = false;

  QString name = QInputDialog::getText(this, tr("Name"),
                                       tr("Please enter a name for the new profile"),
                                       QLineEdit::Normal, QString(), &okClicked);
  if (okClicked && (name.size() > 0)) {
    try {
      profileBox->addItem(name);
      profileBox->setCurrentIndex(profileBox->count() - 1);
      return true;
    } catch (const std::exception& e) {
      reportError(tr("failed to create profile: %1").arg(e.what()));
      return false;
    }
  } else {
    return false;
  }
}

void MainWindow::hookUpWindowTutorials()
{
  QDirIterator dirIter(QApplication::applicationDirPath() + "/tutorials",
                       QStringList("*.js"), QDir::Files);
  while (dirIter.hasNext()) {
    dirIter.next();
    QString fileName = dirIter.fileName();
    QFile file(dirIter.filePath());
    if (!file.open(QIODevice::ReadOnly)) {
      log::error("Failed to open {}", fileName);
      continue;
    }
    QString firstLine = QString::fromUtf8(file.readLine());
    if (firstLine.startsWith("//WIN")) {
      QString windowName = firstLine.mid(6).trimmed();
      if (!m_OrganizerCore.settings().interface().isTutorialCompleted(windowName)) {
        TutorialManager::instance().activateTutorial(windowName, fileName);
      }
    }
  }
}

bool MainWindow::shouldStartTutorial() const
{
  if (GlobalSettings::hideTutorialQuestion()) {
    return false;
  }

  QMessageBox dlg(
      QMessageBox::Question, tr("Show tutorial?"),
      tr("You are starting Mod Organizer for the first time. "
         "Do you want to show a tutorial of its basic features? If you choose "
         "no you can always start the tutorial from the \"Help\" menu."),
      QMessageBox::Yes | QMessageBox::No);

  dlg.setCheckBox(new QCheckBox(tr("Never ask to show tutorials")));

  const auto r = dlg.exec();

  if (dlg.checkBox()->isChecked()) {
    GlobalSettings::setHideTutorialQuestion(true);
  }

  return (r == QMessageBox::Yes);
}

void MainWindow::showEvent(QShowEvent* event)
{
  QMainWindow::showEvent(event);

  if (!m_WasVisible) {
    ui->modList->refreshFilters();
    readSettings();

    // this needs to be connected here instead of in the constructor because the
    // actual changing of the stylesheet is done by MOApplication, which
    // connects its signal in runApplication() (in main.cpp), and that happens
    // _after_ the MainWindow is constructed, but _before_ it is shown
    //
    // by connecting the event here, changing the style setting will first be
    // handled by MOApplication, and then in updateStyle(), at which point the
    // stylesheet has already been set correctly
    connect(this, SIGNAL(styleChanged(QString)), this, SLOT(updateStyle(QString)));

    // only the first time the window becomes visible
    m_Tutorial.registerControl();

    hookUpWindowTutorials();

    if (m_OrganizerCore.settings().firstStart()) {
      QString firstStepsTutorial = ToQString(AppConfig::firstStepsTutorial());
      if (TutorialManager::instance().hasTutorial(firstStepsTutorial)) {
        if (shouldStartTutorial()) {
          TutorialManager::instance().activateTutorial("MainWindow",
                                                       firstStepsTutorial);
        }
      } else {
        log::error("{} missing", firstStepsTutorial);
        QPoint pos = ui->toolBar->mapToGlobal(QPoint());
        pos.rx() += ui->toolBar->width() / 2;
        pos.ry() += ui->toolBar->height();
        QWhatsThis::showText(pos,
                             QObject::tr("Please use \"Help\" from the toolbar to get "
                                         "usage instructions to all elements"));
      }

      if (!m_OrganizerCore.managedGame()->getSupportURL().isEmpty()) {
        QMessageBox::information(this, tr("Game Support Wiki"),
                                 tr("Do you know how to mod this game? Do you need to "
                                    "learn? There's a game support wiki available! "
                                    "Click OK to open the wiki. In the future, you can "
                                    "access this link from the \"Help\" menu."),
                                 QMessageBox::Ok);
        gameSupportTriggered();
      }

      QMessageBox newCatDialog;
      newCatDialog.setWindowTitle(tr("Category Setup"));
      newCatDialog.setText(
          tr("Please choose how to handle the default category setup.\n\n"
             "If you've already connected to Nexus, you can automatically import Nexus "
             "categories for this game (if applicable). Otherwise, use the old Mod "
             "Organizer default category structure, or leave the categories blank (for "
             "manual setup)."));
      QPushButton importBtn(tr("&Import Nexus Categories"));
      QPushButton defaultBtn(tr("Use &Old Category Defaults"));
      QPushButton cancelBtn(tr("Do &Nothing"));
      if (NexusInterface::instance().getAccessManager()->validated()) {
        newCatDialog.addButton(&importBtn, QMessageBox::ButtonRole::AcceptRole);
      }
      newCatDialog.addButton(&defaultBtn, QMessageBox::ButtonRole::AcceptRole);
      newCatDialog.addButton(&cancelBtn, QMessageBox::ButtonRole::RejectRole);
      newCatDialog.exec();
      if (newCatDialog.clickedButton() == &importBtn) {
        importCategories(false);
      } else if (newCatDialog.clickedButton() == &cancelBtn) {
        m_CategoryFactory.reset();
      } else if (newCatDialog.clickedButton() == &defaultBtn) {
        m_CategoryFactory.loadCategories();
      }
      m_CategoryFactory.saveCategories();

      m_OrganizerCore.settings().setFirstStart(false);
    } else {
      auto& settings = m_OrganizerCore.settings();
      if (m_LastVersion < QVersionNumber(2, 5) &&
          !GlobalSettings::hideCategoryReminder()) {
        QMessageBox migrateCatDialog;
        migrateCatDialog.setWindowTitle("Category Migration");
        migrateCatDialog.setText(
            tr("This is your first time running version 2.5 or higher with an old MO2 "
               "instance. The category system now relies on an updated system to map "
               "Nexus categories.\n\n"
               "In order to assign Nexus categories automatically, you will need to "
               "import the Nexus categories for the currently managed game and map "
               "them to your preferred category structure.\n\n"
               "You can either manually open the category editor, via the Settings "
               "dialog or the category filter sidebar, and set up the mappings as you "
               "see fit, or you can automatically import and map the categories as "
               "defined on Nexus.\n\n"
               "As a final option, you can disable Nexus category mapping altogether, "
               "which can be changed at any time in the Settings dialog."));
        QPushButton importBtn(tr("&Import Nexus Categories"));
        QPushButton openSettingsBtn(tr("&Open Categories Dialog"));
        QPushButton disableBtn(tr("&Disable Nexus Mappings"));
        QPushButton closeBtn(tr("&Close"));
        QCheckBox dontShow(tr("&Don't show this again"));
        if (NexusInterface::instance().getAccessManager()->validated()) {
          migrateCatDialog.addButton(&importBtn, QMessageBox::ButtonRole::AcceptRole);
        }
        migrateCatDialog.addButton(&openSettingsBtn,
                                   QMessageBox::ButtonRole::ActionRole);
        migrateCatDialog.addButton(&disableBtn,
                                   QMessageBox::ButtonRole::DestructiveRole);
        migrateCatDialog.addButton(&closeBtn, QMessageBox::ButtonRole::RejectRole);
        migrateCatDialog.setCheckBox(&dontShow);
        migrateCatDialog.exec();
        if (migrateCatDialog.clickedButton() == &importBtn) {
          importCategories(dontShow.isChecked());
        } else if (migrateCatDialog.clickedButton() == &openSettingsBtn) {
          this->ui->filtersEdit->click();
        } else if (migrateCatDialog.clickedButton() == &disableBtn) {
          Settings::instance().nexus().setCategoryMappings(false);
        }
        if (dontShow.isChecked()) {
          GlobalSettings::setHideCategoryReminder(true);
        }
      }
    }

    m_OrganizerCore.settings().widgets().restoreIndex(ui->groupCombo);

    m_OrganizerCore.settings().nexus().registerAsNXMHandler(false);
    m_WasVisible = true;
    updateProblemsButton();

    // notify plugins that the MO2 is ready
    m_PluginContainer.startPlugins(this);

    // forces a log list refresh to display startup logs
    //
    // since the log list is not visible until this point, the automatic
    // resize of columns seems to break the log list (since Qt 5.15.1 or
    // 5.15.2), an make the list empty on startup (in debug the list is not
    // empty because some logs are added after the log list becomes visible)
    //
    // the reset() forces a re-computation of the column size, thus properly
    // the logs that are already in the log model
    //
    ui->logList->reset();
    ui->logList->scrollToBottom();
  }
}

void MainWindow::paintEvent(QPaintEvent* event)
{
  if (m_FirstPaint) {
    allowListResize();
    m_FirstPaint = false;
  }

  QMainWindow::paintEvent(event);
}

void MainWindow::onBeforeClose()
{
  storeSettings();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  if (isVisible()) {
    // this is messy
    //
    // the main problem this is solving is when closing MO, then getting the
    // lock overlay because processes are still running, then pressing the X
    // again
    //
    // in this case, closeEvent() is _not_ called for the second event and the
    // window is immediately hidden
    //
    // this always saves the settings here; in the event where a lock overlay
    // is then shown, it might save settings multiple times, but it's harmless
    onBeforeClose();
  }

  // this happens for two reasons:
  //  1) the user requested to close the window, such as clicking the X
  //  2) close() is called in runApplication() after application.exec()
  //     returns, which happens when qApp->exit() is called
  //
  // the window must never actually close for 1), because settings haven't been
  // saved yet: the state of many widgets is saved to the ini, which relies on
  // the window still being onscreen (or else everything is considered hidden)
  //
  // for 2), the settings have been saved and the window can just close

  if (ModOrganizerCanCloseNow()) {
    // the user has confirmed if necessary and all settings have been saved,
    // just close it
    QMainWindow::closeEvent(event);
    return;
  }

  if (UILocker::instance().locked()) {
    // don't bother asking the user to confirm if the ui is already locked
    event->ignore();
    ExitModOrganizer(Exit::Force);
    return;
  }

  if (ModOrganizerExiting()) {
    // ignore repeated attempts
    event->ignore();
    return;
  }

  // never close the window because settings might need to be changed
  event->ignore();

  // start the process of exiting, which may require confirmation by calling
  // canExit(), among other things
  ExitModOrganizer();
}

bool MainWindow::canExit()
{
  if (m_OrganizerCore.downloadManager()->downloadsInProgressNoPause()) {
    if (QMessageBox::question(
            this, tr("Downloads in progress"),
            tr("There are still downloads in progress, do you really want to quit?"),
            QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Cancel) {
      return false;
    } else {
      m_OrganizerCore.downloadManager()->pauseAll();
    }
  }

  const auto r = m_OrganizerCore.waitForAllUSVFSProcesses();
  if (r == ProcessRunner::Cancelled) {
    return false;
  }

  setCursor(Qt::WaitCursor);
  return true;
}

void MainWindow::cleanup()
{
  QWebEngineProfile::defaultProfile()->clearAllVisitedLinks();

  if (m_IntegratedBrowser) {
    m_IntegratedBrowser->close();
    m_IntegratedBrowser = {};
  }

  m_SaveMetaTimer.stop();
  m_MetaSave.waitForFinished();
}

bool MainWindow::eventFilter(QObject* object, QEvent* event)
{
  if (event->type() == QEvent::StatusTip && object != this) {
    QMainWindow::event(event);
    return true;
  }

  return false;
}

void MainWindow::registerPluginTool(IPluginTool* tool, QString name, QMenu* menu)
{
  if (!menu) {
    menu = ui->actionTool->menu();
  }

  if (name.isEmpty())
    name = tool->displayName();

  QAction* action = new QAction(tool->icon(), name, menu);
  action->setToolTip(tool->tooltip());
  tool->setParentWidget(this);
  connect(
      action, &QAction::triggered, this,
      [this, tool]() {
        try {
          tool->display();
        } catch (const std::exception& e) {
          reportError(
              tr("Plugin \"%1\" failed: %2").arg(tool->localizedName()).arg(e.what()));
        } catch (...) {
          reportError(tr("Plugin \"%1\" failed").arg(tool->localizedName()));
        }
      },
      Qt::QueuedConnection);

  menu->addAction(action);
}

void MainWindow::updateToolMenu()
{
  if (!m_ToolMenuDirty) {
    return;
  }

  // Clear the menu:
  ui->actionTool->menu()->clear();

  std::vector<IPluginTool*> toolPlugins = m_PluginContainer.plugins<IPluginTool>();

  // Sort the plugins by display name
  std::sort(std::begin(toolPlugins), std::end(toolPlugins),
            [](IPluginTool* left, IPluginTool* right) {
              return left->displayName().toLower() < right->displayName().toLower();
            });

  // Remove disabled plugins:
  toolPlugins.erase(std::remove_if(std::begin(toolPlugins), std::end(toolPlugins),
                                   [&](auto* tool) {
                                     if (!m_PluginContainer.isEnabled(tool) ||
                                         tool->displayName().compare(
                                             QStringLiteral("About MO2 Revamped"),
                                             Qt::CaseInsensitive) == 0) {
                                       return true;
                                     }

                                     const auto* game =
                                         m_OrganizerCore.managedGame();
                                     if (!game ||
                                         game->gameShortName().compare(
                                             "eldenring", Qt::CaseInsensitive) != 0) {
                                       return false;
                                     }

                                     const QString name =
                                         tool->displayName().toCaseFolded();
                                     return name.contains("bsa packer") ||
                                            name.contains("ini editor");
                                   }),
                    toolPlugins.end());

  // Group the plugins into submenus
  QMap<QString, QList<QPair<QString, IPluginTool*>>> submenuMap;
  for (auto toolPlugin : toolPlugins) {
    QStringList toolName = toolPlugin->displayName().split("/");
    QString submenu      = toolName[0];
    toolName.pop_front();
    submenuMap[submenu].append(
        QPair<QString, IPluginTool*>(toolName.join("/"), toolPlugin));
  }

  // Start registering plugins
  for (auto submenuKey : submenuMap.keys()) {
    if (submenuMap[submenuKey].length() > 1) {
      QMenu* submenu = new QMenu(submenuKey, this);
      for (auto info : submenuMap[submenuKey]) {
        registerPluginTool(info.second, info.first, submenu);
      }
      ui->actionTool->menu()->addMenu(submenu);
    } else {
      registerPluginTool(submenuMap[submenuKey].front().second);
    }
  }

  m_ToolMenuDirty = false;
}

void MainWindow::registerModPage(IPluginModPage* modPage)
{
  QAction* action = new QAction(modPage->icon(), modPage->displayName(), this);
  connect(
      action, &QAction::triggered, this,
      [this, modPage]() {
        if (modPage->useIntegratedBrowser()) {

          if (!m_IntegratedBrowser) {
            m_IntegratedBrowser.reset(new BrowserDialog);

            connect(m_IntegratedBrowser.get(),
                    SIGNAL(requestDownload(QUrl, QNetworkReply*)), &m_OrganizerCore,
                    SLOT(requestDownload(QUrl, QNetworkReply*)));
          }

          m_IntegratedBrowser->setWindowTitle(modPage->displayName());
          m_IntegratedBrowser->openUrl(modPage->pageURL());
        } else {
          shell::Open(QUrl(modPage->pageURL()));
        }
      },
      Qt::QueuedConnection);

  ui->actionModPage->menu()->addAction(action);
}

bool MainWindow::registerNexusPage(const QString& gameName)
{
  // Get the plugin
  IPluginGame* plugin = m_OrganizerCore.getGame(gameName);
  if (plugin == nullptr)
    return false;

  // Get the gameURL
  QString gameURL = NexusInterface::instance().getGameURL(gameName);
  if (gameURL.isEmpty())
    return false;

  // Create an action
  QAction* action = new QAction(plugin->gameIcon(),
                                QObject::tr("Visit %1 on Nexus").arg(gameName), this);

  // Bind the action
  connect(
      action, &QAction::triggered, this,
      [this, gameURL]() {
        shell::Open(QUrl(gameURL));
      },
      Qt::QueuedConnection);

  // Add the action
  ui->actionModPage->menu()->addAction(action);

  return true;
}

void MainWindow::updateModPageMenu()
{
  // Clear the menu:
  ui->actionModPage->menu()->clear();

  // Determine the loaded mod page plugins
  std::vector<IPluginModPage*> modPagePlugins =
      m_PluginContainer.plugins<IPluginModPage>();

  // Sort the plugins by display name
  std::sort(std::begin(modPagePlugins), std::end(modPagePlugins),
            [](IPluginModPage* left, IPluginModPage* right) {
              return left->displayName().toLower() < right->displayName().toLower();
            });

  // Remove disabled plugins
  modPagePlugins.erase(std::remove_if(std::begin(modPagePlugins),
                                      std::end(modPagePlugins),
                                      [&](auto* tool) {
                                        return !m_PluginContainer.isEnabled(tool);
                                      }),
                       modPagePlugins.end());

  for (auto* modPagePlugin : modPagePlugins) {
    registerModPage(modPagePlugin);
  }

  QStringList registeredSources;

  // Add the primary game
  QString gameShortName = m_OrganizerCore.managedGame()->gameShortName();
  if (registerNexusPage(gameShortName))
    registeredSources << gameShortName;

  // Add the primary sources
  for (auto gameName : m_OrganizerCore.managedGame()->primarySources()) {
    if (!registeredSources.contains(gameName) && registerNexusPage(gameName))
      registeredSources << gameName;
  }

  // Add a separator if needed
  if (registeredSources.length() > 0)
    ui->actionModPage->menu()->addSeparator();

  // Add the secondary games (sorted)
  QStringList secondaryGames = m_OrganizerCore.managedGame()->validShortNames();
  secondaryGames.sort(Qt::CaseInsensitive);
  for (auto gameName : secondaryGames) {
    if (!registeredSources.contains(gameName) && registerNexusPage(gameName))
      registeredSources << gameName;
  }

  // No mod page plugin and the menu was visible
  bool keepOriginalAction =
      modPagePlugins.size() == 0 && registeredSources.length() <= 1;
  if (keepOriginalAction) {
    ui->toolBar->insertAction(ui->actionAdd_Profile, ui->actionNexus);
  } else {
    ui->toolBar->removeAction(ui->actionNexus);
  }
  ui->actionModPage->setVisible(!keepOriginalAction);
}

void MainWindow::startExeAction()
{
  QAction* action = qobject_cast<QAction*>(sender());

  if (action == nullptr) {
    log::error("not an action?");
    return;
  }

  const auto& list = *m_OrganizerCore.executablesList();

  const auto title = action->text();
  auto itor        = list.find(title);

  if (itor == list.end()) {
    log::warn("startExeAction(): executable '{}' not found", title);
    return;
  }

  action->setEnabled(false);
  Guard g([&] {
    action->setEnabled(true);
  });

  m_OrganizerCore.processRunner()
      .setFromExecutable(*itor)
      .setWaitForCompletion(ProcessRunner::TriggerRefresh)
      .run();
}

void MainWindow::activateSelectedProfile()
{
  m_OrganizerCore.setCurrentProfile(ui->profileBox->currentText());

  m_SavesTab->refreshSaveList();
  m_OrganizerCore.refresh();
  ui->modList->updateModCount();
  ui->espList->updatePluginCount();
  ui->statusBar->updateNormalMessage(m_OrganizerCore);
}

void MainWindow::on_profileBox_currentIndexChanged(int index)
{
  if (!ui->profileBox->isEnabled()) {
    return;
  }

  int previousIndex = m_OldProfileIndex;
  m_OldProfileIndex = index;

  // select has changed, save stuff
  if ((previousIndex != -1) && (m_OrganizerCore.currentProfile() != nullptr) &&
      m_OrganizerCore.currentProfile()->exists()) {
    m_OrganizerCore.saveCurrentLists();
  }

  // Avoid doing any refresh if currentProfile is already set but previous
  // index was -1 as it means that this is happening during initialization so
  // everything has already been set.
  if (previousIndex == -1 && m_OrganizerCore.currentProfile() != nullptr &&
      m_OrganizerCore.currentProfile()->exists() &&
      ui->profileBox->currentText() == m_OrganizerCore.currentProfile()->name()) {
    return;
  }

  // ensure the new index is valid
  if (index < 0 || index >= ui->profileBox->count()) {
    log::debug("invalid profile index, using last profile");
    ui->profileBox->setCurrentIndex(ui->profileBox->count() - 1);
  }

  // handle <manage> item
  if (ui->profileBox->currentIndex() == 0) {
    // remember the profile name that was selected before, previousIndex can't
    // be used again because adding/deleting profiles will change the order
    // in the list
    const QString previousName = ui->profileBox->itemText(previousIndex);

    // show the dialog
    ProfilesDialog dlg(previousName, m_OrganizerCore, this);
    dlg.exec();

    // check if the user clicked 'select' to select another profile
    std::optional<QString> newSelection = dlg.selectedProfile();

    // refresh the profile box; this loops until there is at least one profile
    // available, which shouldn't really happen because the dialog won't allow
    // it
    //
    // the `false` to refreshProfiles() is so it doesn't try to select the
    // profile in the list because 1) it's done just below, and 2) it might be
    // wrong profile if there's something in newSelection
    while (!refreshProfiles(false)) {
      ProfilesDialog dlg(previousName, m_OrganizerCore, this);
      dlg.exec();
      newSelection = dlg.selectedProfile();
    }

    // note that setCurrentText() is recursive, it will re-execute this function
    if (newSelection) {
      ui->profileBox->setCurrentText(*newSelection);
    } else {
      ui->profileBox->setCurrentText(previousName);
    }

    // nothing else to do because setCurrentText() is recursive and will
    // have re-executed on_profileBox_currentIndexChanged() again, doing all
    // the stuff below for the new selection
    return;
  }

  activateSelectedProfile();

  auto saveGames = m_OrganizerCore.gameFeatures().gameFeature<LocalSavegames>();
  if (saveGames != nullptr) {
    if (saveGames->prepareProfile(m_OrganizerCore.currentProfile())) {
      m_SavesTab->refreshSaveList();
    }
  }

  auto invalidation = m_OrganizerCore.gameFeatures().gameFeature<BSAInvalidation>();
  if (invalidation != nullptr) {
    if (invalidation->prepareProfile(m_OrganizerCore.currentProfile())) {
      QTimer::singleShot(5, [this] {
        m_OrganizerCore.refresh();
      });
    }
  }
}

bool MainWindow::refreshProfiles(bool selectProfile, QString newProfile)
{
  QComboBox* profileBox = findChild<QComboBox*>("profileBox");

  QString currentProfileName = profileBox->currentText();

  profileBox->blockSignals(true);
  profileBox->clear();
  profileBox->addItem(QObject::tr("<Manage...>"));

  QDir profilesDir(Settings::instance().paths().profiles());
  profilesDir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

  QDirIterator profileIter(profilesDir);

  while (profileIter.hasNext()) {
    profileIter.next();
    try {
      profileBox->addItem(profileIter.fileName());
    } catch (const std::runtime_error& error) {
      reportError(QObject::tr("failed to parse profile %1: %2")
                      .arg(profileIter.fileName())
                      .arg(error.what()));
    }
  }

  // now select one of the profiles, preferably the one that was selected before
  profileBox->blockSignals(false);

  if (selectProfile) {
    if (profileBox->count() > 1) {
      if (newProfile.isEmpty()) {
        profileBox->setCurrentText(currentProfileName);
      } else {
        profileBox->setCurrentText(newProfile);
      }
      if (profileBox->currentIndex() == 0) {
        profileBox->setCurrentIndex(1);
      }
    }
  }
  return profileBox->count() > 1;
}

void MainWindow::refreshExecutablesList()
{
  QAbstractItemModel* model = ui->executablesListBox->model();
  const auto* managedGame = m_OrganizerCore.managedGame();
  const QString interfaceStyle =
      Settings::instance().interface().styleName().value_or(QString{});
  const bool darkStyle =
      interfaceStyle.contains(QStringLiteral("dark"), Qt::CaseInsensitive);

  const auto roundedGameIcon = [darkStyle](const QIcon& sourceIcon) {
    constexpr int iconPixels = 96;
    constexpr qreal cornerRadius = 12.0;

    QPixmap sourcePixmap = sourceIcon.pixmap(QSize(iconPixels, iconPixels));
    if (sourcePixmap.isNull()) {
      return sourceIcon;
    }

    sourcePixmap = sourcePixmap.scaled(
        QSize(iconPixels, iconPixels), Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);
    const QRect sourceRect((sourcePixmap.width() - iconPixels) / 2,
                           (sourcePixmap.height() - iconPixels) / 2,
                           iconPixels, iconPixels);

    QPixmap roundedPixmap(iconPixels, iconPixels);
    roundedPixmap.fill(Qt::transparent);

    QPainter painter(&roundedPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath roundedShape;
    roundedShape.addRoundedRect(QRectF(1.0, 1.0, iconPixels - 2.0,
                                       iconPixels - 2.0),
                                cornerRadius, cornerRadius);
    painter.setClipPath(roundedShape);
    painter.drawPixmap(QRect(0, 0, iconPixels, iconPixels), sourcePixmap,
                       sourceRect);
    painter.setClipping(false);
    if (!darkStyle) {
      painter.setPen(QPen(QColor("#D2E2EE"), 2.0));
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(roundedShape);
    }
    painter.end();

    roundedPixmap.setDevicePixelRatio(3.0);
    return QIcon(roundedPixmap);
  };

  auto add = [&](const QString& title, const QFileInfo& binary) {
    QIcon icon;
    if (managedGame) {
      const auto executables = managedGame->executables();
      const auto gameExecutable = std::find_if(
          executables.begin(), executables.end(), [&](const auto& executable) {
            return title.compare(executable.title(), Qt::CaseInsensitive) == 0;
          });
      if (gameExecutable != executables.end()) {
        icon = roundedGameIcon(managedGame->gameIcon());
      }
    }

    if (title.compare(QStringLiteral("Explore Virtual Folder"),
                      Qt::CaseInsensitive) == 0) {
      icon = QIcon(":/MO/gui/mainwindow/explore-virtual-folder.svg");
    }

    if (icon.isNull() && !binary.fileName().isEmpty()) {
      icon = iconForExecutable(binary.filePath());
    }

    ui->executablesListBox->addItem(icon, title);

    const auto i = ui->executablesListBox->count() - 1;

    model->setData(model->index(i, 0),
                   QSize(0, ui->executablesListBox->iconSize().height() + 4),
                   Qt::SizeHintRole);
  };

  ui->executablesListBox->setEnabled(false);
  ui->executablesListBox->clear();

  add(tr("<Edit...>"), {});

  for (const auto& exe : *m_OrganizerCore.executablesList()) {
    if (!exe.hide()) {
      add(exe.title(), exe.binaryInfo());
    }
  }

  if (ui->executablesListBox->count() == 1) {
    // all executables are hidden, add an empty one to at least be able to
    // switch to edit
    add(tr("(no executables)"), QFileInfo(":badfile"));
  }

  ui->executablesListBox->setCurrentIndex(1);
  // Keep the executable popup readable and within the screen even when many
  // programs are configured. Remaining entries stay accessible by scrolling.
  ui->executablesListBox->setMaxVisibleItems(8);
  ui->executablesListBox->view()->setMinimumHeight(0);
  ui->executablesListBox->view()->setMaximumHeight(320);
  ui->executablesListBox->view()->setVerticalScrollBarPolicy(
      Qt::ScrollBarAsNeeded);
  ui->executablesListBox->setEnabled(true);
}

static bool BySortValue(const std::pair<UINT32, QTreeWidgetItem*>& LHS,
                        const std::pair<UINT32, QTreeWidgetItem*>& RHS)
{
  return LHS.first < RHS.first;
}

template <typename InputIterator>
static QStringList toStringList(InputIterator current, InputIterator end)
{
  QStringList result;
  for (; current != end; ++current) {
    result.append(*current);
  }
  return result;
}

void MainWindow::updateBSAList(const QStringList& defaultArchives,
                               const QStringList& activeArchives)
{
  m_DefaultArchives = defaultArchives;
  ui->bsaList->clear();
  ui->bsaList->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  std::vector<std::pair<UINT32, QTreeWidgetItem*>> items;

  auto invalidation = m_OrganizerCore.gameFeatures().gameFeature<BSAInvalidation>();
  std::vector<FileEntryPtr> files = m_OrganizerCore.directoryStructure()->getFiles();

  QStringList plugins =
      m_OrganizerCore.findFiles("", [](const QString& fileName) -> bool {
        return fileName.endsWith(".esp", Qt::CaseInsensitive) ||
               fileName.endsWith(".esm", Qt::CaseInsensitive) ||
               fileName.endsWith(".esl", Qt::CaseInsensitive);
      });

  auto hasAssociatedPlugin = [&](const QString& bsaName) -> bool {
    for (const QString& pluginName : plugins) {
      QFileInfo pluginInfo(pluginName);
      if (bsaName.startsWith(QFileInfo(pluginName).completeBaseName(),
                             Qt::CaseInsensitive) &&
          (m_OrganizerCore.pluginList()->state(pluginInfo.fileName()) ==
           IPluginList::STATE_ACTIVE)) {
        return true;
      }
    }
    return false;
  };

  for (FileEntryPtr current : files) {
    QFileInfo fileInfo(ToQString(current->getName().c_str()));

    if (fileInfo.suffix().toLower() == "bsa" || fileInfo.suffix().toLower() == "ba2") {
      int index = activeArchives.indexOf(fileInfo.fileName());
      if (index == -1) {
        index = 0xFFFF;
      } else {
        index += 2;
      }

      if ((invalidation != nullptr) &&
          invalidation->isInvalidationBSA(fileInfo.fileName())) {
        index = 1;
      }

      int originId = current->getOrigin();
      FilesOrigin& origin =
          m_OrganizerCore.directoryStructure()->getOriginByID(originId);

      QTreeWidgetItem* newItem = new QTreeWidgetItem(
          QStringList() << fileInfo.fileName() << ToQString(origin.getName()));
      newItem->setData(0, Qt::UserRole, index);
      newItem->setData(1, Qt::UserRole, originId);
      newItem->setFlags(newItem->flags() &
                        ~(Qt::ItemIsDropEnabled | Qt::ItemIsUserCheckable));
      newItem->setCheckState(0, (index != -1) ? Qt::Checked : Qt::Unchecked);
      newItem->setData(0, Qt::UserRole, false);
      if (m_OrganizerCore.settings().game().forceEnableCoreFiles() &&
          defaultArchives.contains(fileInfo.fileName())) {
        newItem->setCheckState(0, Qt::Checked);
        newItem->setDisabled(true);
        newItem->setData(0, Qt::UserRole, true);
      } else if (fileInfo.fileName().compare("update.bsa", Qt::CaseInsensitive) == 0) {
        newItem->setCheckState(0, Qt::Checked);
        newItem->setDisabled(true);
      } else if (hasAssociatedPlugin(fileInfo.fileName())) {
        newItem->setCheckState(0, Qt::Checked);
        newItem->setDisabled(true);
      } else {
        newItem->setCheckState(0, Qt::Unchecked);
        newItem->setDisabled(true);
      }
      if (index < 0)
        index = 0;

      UINT32 sortValue = ((origin.getPriority() & 0xFFFF) << 16) | (index & 0xFFFF);
      items.push_back(std::make_pair(sortValue, newItem));
    }
  }
  std::sort(items.begin(), items.end(), BySortValue);

  for (auto iter = items.begin(); iter != items.end(); ++iter) {
    int originID = iter->second->data(1, Qt::UserRole).toInt();

    const FilesOrigin& origin =
        m_OrganizerCore.directoryStructure()->getOriginByID(originID);

    QString modName;
    const unsigned int modIndex = ModInfo::getIndex(ToQString(origin.getName()));

    if (modIndex == UINT_MAX) {
      modName = UnmanagedModName();
    } else {
      ModInfo::Ptr modInfo = ModInfo::getByIndex(modIndex);
      modName              = modInfo->name();
    }

    QList<QTreeWidgetItem*> items =
        ui->bsaList->findItems(modName, Qt::MatchFixedString);
    QTreeWidgetItem* subItem = nullptr;
    if (items.length() > 0) {
      subItem = items.at(0);
    } else {
      subItem = new QTreeWidgetItem(QStringList(modName));
      subItem->setFlags(subItem->flags() & ~Qt::ItemIsDragEnabled);
      ui->bsaList->addTopLevelItem(subItem);
    }
    subItem->addChild(iter->second);
    subItem->setExpanded(true);
  }
  checkBSAList();
}

void MainWindow::checkBSAList()
{
  auto archives = m_OrganizerCore.gameFeatures().gameFeature<DataArchives>();

  if (archives != nullptr) {
    ui->bsaList->blockSignals(true);
    ON_BLOCK_EXIT([&]() {
      ui->bsaList->blockSignals(false);
    });

    QStringList defaultArchives = archives->archives(m_OrganizerCore.currentProfile());

    bool warning = false;

    for (int i = 0; i < ui->bsaList->topLevelItemCount(); ++i) {
      bool modWarning         = false;
      QTreeWidgetItem* tlItem = ui->bsaList->topLevelItem(i);
      for (int j = 0; j < tlItem->childCount(); ++j) {
        QTreeWidgetItem* item = tlItem->child(j);
        QString filename      = item->text(0);
        item->setIcon(0, QIcon());
        item->setToolTip(0, QString());

        if (item->checkState(0) == Qt::Unchecked) {
          if (defaultArchives.contains(filename)) {
            item->setIcon(0, QIcon(":/MO/gui/mainwindow/status/warning.svg"));
            item->setToolTip(
                0, tr("This bsa is enabled in the ini file so it may be required!"));
            modWarning = true;
          }
        }
      }
      if (modWarning) {
        ui->bsaList->expandItem(ui->bsaList->topLevelItem(i));
        warning = true;
      }
    }
    if (warning) {
      ui->tabWidget->setTabIcon(1, QIcon(":/MO/gui/mainwindow/status/warning.svg"));
    } else {
      ui->tabWidget->setTabIcon(1, QIcon());
    }
  }
}

void MainWindow::saveModMetas()
{
  if (m_MetaSave.isFinished()) {
    m_MetaSave = QtConcurrent::run([this]() {
      for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
        ModInfo::Ptr modInfo = ModInfo::getByIndex(i);
        modInfo->saveMeta();
      }
    });
  }
}

void MainWindow::fixCategories()
{
  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    ModInfo::Ptr modInfo     = ModInfo::getByIndex(i);
    std::set<int> categories = modInfo->getCategories();
    for (std::set<int>::iterator iter = categories.begin(); iter != categories.end();
         ++iter) {
      if (!m_CategoryFactory.categoryExists(*iter)) {
        modInfo->setCategory(*iter, false);
      }
    }
  }
}

void MainWindow::setupNetworkProxy(bool activate)
{
  QNetworkProxyFactory::setUseSystemConfiguration(activate);
}

void MainWindow::activateProxy(bool activate)
{
  QProgressDialog busyDialog(tr("Activating Network Proxy"), QString(), 0, 0,
                             parentWidget());
  busyDialog.setWindowFlags(busyDialog.windowFlags() &
                            ~Qt::WindowContextHelpButtonHint);
  busyDialog.setWindowModality(Qt::WindowModal);
  busyDialog.show();

  QFutureWatcher<void> futureWatcher;
  QEventLoop loop;
  connect(&futureWatcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit,
          Qt::QueuedConnection);

  futureWatcher.setFuture(QtConcurrent::run(MainWindow::setupNetworkProxy, activate));

  // wait for setupNetworkProxy while keeping ui responsive
  loop.exec();

  busyDialog.hide();
}

void MainWindow::readSettings()
{
  const auto& s = m_OrganizerCore.settings();

  if (!s.geometry().restoreGeometry(this)) {
    resize(1300, 800);
  }

  s.geometry().restoreState(this);
  s.geometry().restoreDocks(this);
  s.geometry().restoreToolbars(this);
  s.geometry().restoreState(ui->splitter);
  s.geometry().restoreGeometry(m_FilterOptionsDialog);
  ui->menuBar->hide();
  s.geometry().restoreVisibility(ui->statusBar);

  FilterWidget::setOptions(s.interface().filterOptions());

  {
    // special case in case someone puts 0 in the INI
    auto v = s.widgets().index(ui->executablesListBox);
    if (!v || v == 0) {
      v = 1;
    }

    ui->executablesListBox->setCurrentIndex(*v);
  }

  s.widgets().restoreIndex(ui->tabWidget);

  const auto* managedGame = m_OrganizerCore.managedGame();
  if (managedGame &&
      managedGame->gameShortName().compare("eldenring", Qt::CaseInsensitive) ==
          0) {
    ui->tabWidget->setCurrentWidget(ui->dataTab);
  }

  ui->modList->restoreState(s);

  ui->categoriesGroup->show();
  {
    const QSignalBlocker blocker(ui->displayCategoriesBtn);
    ui->displayCategoriesBtn->setChecked(false);
  }

  if (s.network().useProxy()) {
    activateProxy(true);
  }
}

void MainWindow::processUpdates()
{
  auto& settings      = m_OrganizerCore.settings();
  const auto earliest = QVersionNumber::fromString("2.1.2").normalized();

  const auto lastVersion    = settings.version().value_or(earliest);
  const auto currentVersion = m_OrganizerCore.getVersion().asQVersionNumber();

  m_LastVersion = lastVersion;

  settings.processUpdates(currentVersion, lastVersion);

  if (!settings.firstStart()) {
    if (lastVersion < QVersionNumber(2, 1, 6)) {
      ui->modList->header()->setSectionHidden(ModList::COL_NOTES, true);
    }

    if (lastVersion < QVersionNumber(2, 2, 1)) {
      // hide new columns by default
      for (int i = DownloadList::COL_MODNAME; i < DownloadList::COL_COUNT; ++i) {
        ui->downloadView->header()->hideSection(i);
      }
    }

    if (lastVersion < QVersionNumber(2, 3)) {
      for (int i = 1; i < ui->dataTree->header()->count(); ++i)
        ui->dataTree->setColumnWidth(i, 150);
    }
  }

  if (currentVersion < lastVersion) {
    const auto text =
        tr("Notice: Your current MO version (%1) is lower than the previously used one "
           "(%2). "
           "The GUI may not downgrade gracefully, so you may experience oddities. "
           "However, there should be no serious issues.")
            .arg(currentVersion.toString())
            .arg(lastVersion.toString());

    log::warn("{}", text);
  }
}

void MainWindow::storeSettings()
{
  auto& s = m_OrganizerCore.settings();

  s.geometry().saveState(this);
  s.geometry().saveGeometry(this);
  s.geometry().saveDocks(this);

  s.geometry().saveVisibility(ui->statusBar);
  s.geometry().saveToolbars(this);
  s.geometry().saveState(ui->splitter);
  s.geometry().saveGeometry(m_FilterOptionsDialog);
  s.geometry().saveMainWindowMonitor(this);

  s.geometry().saveState(ui->espList->header());
  s.geometry().saveState(ui->downloadView->header());
  s.geometry().saveState(ui->savegameList->header());

  s.widgets().saveIndex(ui->executablesListBox);
  s.widgets().saveIndex(ui->tabWidget);

  m_DataTab->saveState(s);
  ui->modList->saveState(s);

  s.interface().setFilterOptions(FilterWidget::options());
}

QMainWindow* MainWindow::mainWindow()
{
  return this;
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
  QWidget* currentWidget = ui->tabWidget->widget(index);
  if (currentWidget == ui->espTab) {
    m_OrganizerCore.refreshESPList();
  } else if (currentWidget == ui->bsaTab) {
    m_OrganizerCore.refreshBSAList();
  } else if (currentWidget == ui->dataTab) {
    m_DataTab->activated();
  } else if (currentWidget == ui->savesTab) {
    m_SavesTab->refreshSaveList();
  }
}

void MainWindow::on_startButton_clicked()
{
  const Executable* selectedExecutable = getSelectedExecutable();
  if (!selectedExecutable) {
    return;
  }

  ui->startButton->setEnabled(false);
  Guard g([&] {
    ui->startButton->setEnabled(true);
  });

  m_OrganizerCore.processRunner()
      .setFromExecutable(*selectedExecutable)
      .setWaitForCompletion(ProcessRunner::TriggerRefresh)
      .run();
}

bool MainWindow::modifyExecutablesDialog(int selection)
{
  bool result = false;

  try {
    EditExecutablesDialog dialog(m_OrganizerCore, selection, this);

    result = (dialog.exec() == QDialog::Accepted);

    refreshExecutablesList();
    updatePinnedExecutables();
  } catch (const std::exception& e) {
    reportError(e.what());
  }

  return result;
}

void MainWindow::on_executablesListBox_currentIndexChanged(int index)
{
  if (!ui->executablesListBox->isEnabled()) {
    return;
  }

  const int previousIndex = (m_OldExecutableIndex > 0 ? m_OldExecutableIndex : 1);

  m_OldExecutableIndex = index;

  if (index == 0) {
    modifyExecutablesDialog(previousIndex - 1);
    const auto newCount = ui->executablesListBox->count();

    if (previousIndex >= 0 && previousIndex < newCount) {
      ui->executablesListBox->setCurrentIndex(previousIndex);
    } else {
      ui->executablesListBox->setCurrentIndex(newCount - 1);
    }
  }
}

void MainWindow::helpTriggered()
{
  QWhatsThis::enterWhatsThisMode();
}

void MainWindow::configureEldenRingHelp()
{
  const auto* game = m_OrganizerCore.managedGame();
  if (!game || game->gameShortName().compare("eldenring", Qt::CaseInsensitive) != 0) {
    return;
  }

  ui->modList->setWhatsThis(tr(
      "Installed Elden Ring mods appear here. Checked mods are active in this "
      "profile and can provide virtual game files or native DLLs. Keep only one "
      "version of a mutually exclusive mod active."));
  ui->profileBox->setWhatsThis(tr(
      "Choose a profile to switch its enabled mod list, plugin selection, and "
      "plugin load order. Each profile keeps these lists separate for different mod setups or playthroughs."));
  ui->activeModsCounter->setWhatsThis(tr(
      "Shows how many active mods are visible in the mod list. Filtering the "
      "list changes this count; hover over the counter to see the total and "
      "visible counts by mod type."));
  ui->modFilterEdit->setWhatsThis(tr(
      "Filter the installed Elden Ring mods by name."));
  ui->dataTree->setWhatsThis(tr(
      "Shows the virtual Elden Ring Data directory after MO2 combines the game "
      "files with the active mods. The Mod column identifies which mod supplies "
      "a file. Standard archive layout keeps top-level DLLs at the game root and "
      "organizes DLLs from recognized native-mod folders under Game/DLLs."));
  ui->savegameList->setWhatsThis(tr(
      "Browse the save files available to this Elden Ring profile. The profile "
      "settings can isolate saves to this profile, share a named save profile "
      "within this MO2 instance, or use the global Steam save folder."));
  ui->downloadView->setWhatsThis(tr(
      "Downloaded archives for this instance appear here. Double-click an "
      "archive to install it. To choose its priority, drag it from this list "
      "onto the mod list while sorting by Priority."));
  ui->executablesListBox->setWhatsThis(tr(
      "Choose Elden Ring or another configured executable to launch through "
      "MO2's virtual file system."));
  ui->startButton->setWhatsThis(tr(
      "Launch the selected program with the active Elden Ring profile. Use the "
      "small arrow on the right for shortcut options."));
  ui->actionChange_Game->setWhatsThis(tr(
      "Open the instance manager to switch between this Elden Ring setup and "
      "other independent MO2 instances."));
  ui->actionNexus->setWhatsThis(tr(
      "Open the Nexus Mods page for the currently managed game."));
  ui->actionSettings->setWhatsThis(tr(
      "Open MO2 settings. These settings belong to this MO2 installation and "
      "may be separate from settings in other instances."));
  ui->actionTool->setWhatsThis(tr(
      "Open tools available in this MO2 instance. Elden Ring tools include "
      "Save Isolation for save routes and named saves, Startup Options for "
      "launch and cleanup settings, and Native DLL Profile for native DLL "
      "load order and optional initializers."));
  ui->actionHelp->setWhatsThis(tr(
      "Open interface help, the Elden Ring guide and tour, MO2 Revamped "
      "project resources, and support links."));
  ui->actionInstallMod->setWhatsThis(tr(
      "Install a mod archive. For Elden Ring, keep the original archive paths "
      "when the mod relies on its packaged folders; use standard MO2 layout "
      "when the archive follows common MO2 or game-data paths."));
}

void MainWindow::showEldenRingGuide()
{
  QDialog guide(this);
  guide.setObjectName(QStringLiteral("EldenRingGuideDialog"));
  guide.setWindowTitle(tr("Elden Ring Guide"));
  guide.setWindowIcon(windowIcon());
  guide.setMinimumSize(760, 520);
  guide.resize(900, 660);
  guide.setWindowFlag(Qt::WindowType::WindowContextHelpButtonHint, false);

  auto* layout = new QVBoxLayout(&guide);
  layout->setContentsMargins(14, 12, 14, 12);
  layout->setSpacing(10);

  auto* header = new QHBoxLayout();
  header->setSpacing(12);
  auto* logo = new QLabel(&guide);
  logo->setObjectName(QStringLiteral("eldenRingGuideLogo"));
  logo->setFixedSize(72, 72);
  logo->setAlignment(Qt::AlignmentFlag::AlignCenter);
  const QString appDirectory = QApplication::applicationDirPath();
  QPixmap logoPixmap(appDirectory + QStringLiteral("/resources/mo_icon.png"));
  if (logoPixmap.isNull()) {
    logoPixmap.load(appDirectory + QStringLiteral("/splash.png"));
  }
  if (!logoPixmap.isNull()) {
    logo->setPixmap(logoPixmap.scaled(
        logo->size(), Qt::AspectRatioMode::KeepAspectRatio,
        Qt::TransformationMode::SmoothTransformation));
  }
  header->addWidget(logo);

  QString activeGame = tr("Not detected");
  if (const auto* game = m_OrganizerCore.managedGame()) {
    activeGame = game->gameName();
  }
  QString activeProfile = tr("Not selected");
  if (const auto* profile = m_OrganizerCore.currentProfile()) {
    activeProfile = profile->name();
  }
  auto* heading = new QLabel(
      tr("<h2>Using Elden Ring with MO2 Revamped</h2>"
         "<p>A practical guide to profiles, archives, native DLLs, saves, "
         "and launching the game.</p>"
         "<p><b>Active game:</b> %1 &nbsp; <b>Active profile:</b> %2</p>")
          .arg(activeGame.toHtmlEscaped())
          .arg(activeProfile.toHtmlEscaped()),
      &guide);
  heading->setWordWrap(true);
  header->addWidget(heading, 1);
  layout->addLayout(header);

  auto* tabs = new QTabWidget(&guide);
  tabs->setObjectName(QStringLiteral("eldenRingGuideTabs"));

  const auto makeCard = [](QWidget* parent, const QString& title,
                           const QString& content) {
    auto* card = new QGroupBox(title, parent);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 10, 10, 8);
    cardLayout->setSpacing(4);
    auto* text = new QLabel(content, card);
    text->setWordWrap(true);
    text->setTextFormat(Qt::TextFormat::RichText);
    text->setAlignment(Qt::AlignmentFlag::AlignLeft |
                       Qt::AlignmentFlag::AlignTop);
    cardLayout->addWidget(text);
    return card;
  };

  const auto addGuideTab = [&](const QString& tabTitle,
                               const QString& pageTitle,
                               const QString& pageDescription,
                               const std::vector<std::pair<QString, QString>>& cards) {
    auto* page = new QWidget(tabs);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(14, 12, 14, 12);
    pageLayout->setSpacing(8);

    auto* intro = new QLabel(
        QStringLiteral("<h3>%1</h3><p>%2</p>")
            .arg(pageTitle, pageDescription),
        page);
    intro->setWordWrap(true);
    pageLayout->addWidget(intro);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    for (int index = 0; index < static_cast<int>(cards.size()); ++index) {
      const int row = index / 2;
      const int column = index % 2;
      grid->addWidget(makeCard(page, cards[index].first, cards[index].second),
                      row, column);
    }
    pageLayout->addLayout(grid, 1);
    tabs->addTab(page, tabTitle);
  };

  addGuideTab(
      tr("Profiles and archives"), tr("Profiles and installation"),
      tr("Choose the active profile and understand how MO2 resolves files and "
         "archive layouts."),
      {{tr("Profiles"),
        tr("<p>Enable mods with their checkboxes. Each profile keeps its own "
           "enabled mod list and save settings.</p>")},
       {tr("Mod priority"),
        tr("<p>The left list controls file priority when mods provide the same "
           "file. The <b>Data</b> tab shows which mod wins. Avoid enabling two "
           "versions of the same mod at once.</p>")},
       {tr("Keep original archive structure"),
        tr("<p>Choose this when a mod expects its packaged paths to remain "
           "intact. Revamped removes only redundant outer wrappers and keeps "
           "the files inside their original folders, including DLL locations "
           "and documentation.</p>")},
       {tr("Standard MO2 layout"),
        tr("<p>Choose this when an archive follows common MO2 or game-data "
           "paths. Recognized native-mod DLLs and related files are routed to "
           "this instance's game DLLs folder.</p>")}});

  addGuideTab(
      tr("Native DLLs and saves"), tr("Native DLLs and save profiles"),
      tr("These tools keep their choices with the active MO2 profile."),
      {{tr("DLL detection"),
        tr("<p>Native DLL Profile lists DLL routes from enabled mods. MO2's "
           "route index is used when available; older mods are scanned in "
           "their native DLL folders. Detected paths are checked against MO2's "
           "virtual file system.</p>")},
       {tr("Load order and initializers"),
        tr("<p>Associate detected DLLs with their MO2 mods and set their load "
           "order for each profile. Enter the initializer export documented "
           "by a mod only when it requires one. This profile does not enable "
           "or disable the MO2 mod.</p>")},
       {tr("Save routes"),
        tr("<p>Save Isolation can keep saves per MO2 profile, share them among "
           "profiles in this instance, or use the global Steam save folder. "
           "The first two modes also support named save profiles while the "
           "game still receives <code>ER0000.sl2</code>.</p>")},
       {tr("Restart and online safety"),
        tr("<p>Restart MO2 after changing a save route and before launching "
           "the game. Routing only changes save files; it does not change Easy "
           "Anti-Cheat or network mode, and does not make modded saves safe "
           "for online play.</p>")}});

  addGuideTab(
      tr("Data and launch"), tr("Data, downloads, and launch"),
      tr("MO2 presents a virtual game directory and launches programs through "
         "the active profile."),
      {{tr("Data"),
        tr("<p><b>Data</b> shows the virtual game directory and the mod that "
           "provides each file. It does not copy files into the physical game "
           "folder.</p>")},
       {tr("Downloads"),
        tr("<p><b>Downloads</b> lists archives ready to install. Choose an "
           "archive there or use <b>Install Mod</b> to browse for one.</p>")},
       {tr("Run"),
        tr("<p>The <b>Run</b> button starts the selected program with the "
           "active profile. Its arrow opens shortcut options.</p>")},
       {tr("More help"),
        tr("<p>The <b>Help</b> menu offers this guide, the project repository, "
           "a game support wiki when its MO2 plugin provides one, <i>Report a Problem</i>, and <i>About MO2 "
           "Revamped</i>.</p>")}});

  layout->addWidget(tabs, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::StandardButton::Close,
                                       &guide);
  connect(buttons, &QDialogButtonBox::rejected, &guide, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &guide, &QDialog::accept);
  if (auto* closeButton =
          buttons->button(QDialogButtonBox::StandardButton::Close)) {
    connect(closeButton, &QPushButton::clicked, &guide, &QDialog::accept);
  }
  layout->addWidget(buttons);
  guide.exec();
}

void MainWindow::revampedRepositoryTriggered()
{
  shell::Open(QUrl("https://github.com/ArialSenki/MO2-Revamped"));
}

void MainWindow::gameSupportTriggered()
{
  shell::Open(QUrl(m_OrganizerCore.managedGame()->getSupportURL()));
}

void MainWindow::issueTriggered()
{
  shell::Open(QUrl("https://github.com/ArialSenki/MO2-Revamped/issues"));
}

void MainWindow::tutorialTriggered()
{
  QAction* tutorialAction = qobject_cast<QAction*>(sender());
  if (tutorialAction != nullptr) {
    TutorialManager::instance().activateTutorial("MainWindow",
                                                 tutorialAction->data().toString());
  }
}

void MainWindow::on_actionInstallMod_triggered()
{
  ui->modList->actions().installMod();
}

void MainWindow::on_action_Refresh_triggered()
{
  refreshProfile_activated();
}

void MainWindow::on_actionAdd_Profile_triggered()
{
  for (;;) {
    QString selectedProfile;
    bool profileDialogFailed = false;
    {
      ProfilesDialog profilesDialog(m_OrganizerCore.currentProfile()->name(),
                                    m_OrganizerCore, this);

      // workaround: need to disable monitoring of the saves directory, otherwise the
      // active profile directory is locked
      m_SavesTab->stopMonitorSaves();
      profilesDialog.exec();
      selectedProfile = profilesDialog.selectedProfile().value_or("");
      profileDialogFailed = profilesDialog.failed();
    }

    m_SavesTab->refreshSaveList();  // since the save list may now be outdated we have
                                    // to refresh it completely

    if (refreshProfiles(true, selectedProfile) && !profileDialogFailed) {
      break;
    }
  }

  auto saveGames = m_OrganizerCore.gameFeatures().gameFeature<LocalSavegames>();
  if (saveGames != nullptr) {
    if (saveGames->prepareProfile(m_OrganizerCore.currentProfile())) {
      m_SavesTab->refreshSaveList();
    }
  }

  auto invalidation = m_OrganizerCore.gameFeatures().gameFeature<BSAInvalidation>();
  if (invalidation != nullptr) {
    if (invalidation->prepareProfile(m_OrganizerCore.currentProfile())) {
      QTimer::singleShot(5, [this] {
        m_OrganizerCore.refresh();
      });
    }
  }
}

void MainWindow::on_actionModify_Executables_triggered()
{
  const auto sel = (m_OldExecutableIndex > 0 ? m_OldExecutableIndex - 1 : 0);

  if (modifyExecutablesDialog(sel)) {
    const auto newCount = ui->executablesListBox->count();
    if (m_OldExecutableIndex >= 0 && m_OldExecutableIndex < newCount) {
      ui->executablesListBox->setCurrentIndex(m_OldExecutableIndex);
    } else {
      ui->executablesListBox->setCurrentIndex(newCount - 1);
    }
  }
}

void MainWindow::refresherProgress(const DirectoryRefreshProgress* p)
{
  if (p->finished()) {
    setEnabled(true);
    ui->statusBar->setProgress(100);
  } else {
    setEnabled(false);
    ui->statusBar->setProgress(p->percentDone());
  }
}

void MainWindow::onDirectoryStructureChanged()
{
  // some problem-reports may rely on the virtual directory tree so they need to be
  // updated now
  scheduleCheckForProblems();
  m_DataTab->updateTree();
}

void MainWindow::modInstalled(const QString& modName)
{
  if (!m_OrganizerCore.settings().interface().checkUpdateAfterInstallation()) {
    return;
  }

  unsigned int index = ModInfo::getIndex(modName);

  if (index == UINT_MAX) {
    return;
  }

  const auto installedMod = ModInfo::getByIndex(index);
  if (installedMod->nexusId() <= 0 ||
      installedMod->repository().compare("Nexus", Qt::CaseInsensitive) != 0) {
    log::info("Skipping automatic Nexus update check for '{}' (repository '{}', "
              "mod ID {}).",
              modName, installedMod->repository(), installedMod->nexusId());
    return;
  }

  // force an update to happen
  ui->modList->actions().checkModsForUpdates(
      {m_OrganizerCore.modList()->index(index, 0)});
}

void MainWindow::importCategories(bool)
{
  NexusInterface& nexus = NexusInterface::instance();
  nexus.setPluginContainer(&m_OrganizerCore.pluginContainer());
  nexus.requestGameInfo(Settings::instance().game().plugin()->gameShortName(), this,
                        QVariant(), QString());
}

void MainWindow::showMessage(const QString& message)
{
  MessageDialog::showMessage(message, this);
}

void MainWindow::showError(const QString& message)
{
  reportError(message);
}

void MainWindow::modRenamed(const QString& oldName, const QString& newName)
{
  Profile::renameModInAllProfiles(oldName, newName);

  // immediately refresh the active profile because the data in memory is invalid
  m_OrganizerCore.currentProfile()->refreshModStatus();

  // also fix the directory structure
  try {
    if (m_OrganizerCore.directoryStructure()->originExists(ToWString(oldName))) {
      FilesOrigin& origin =
          m_OrganizerCore.directoryStructure()->getOriginByName(ToWString(oldName));
      origin.setName(ToWString(newName));
    } else {
    }
  } catch (const std::exception& e) {
    reportError(tr("failed to change origin name: %1").arg(e.what()));
  }
}

void MainWindow::fileMoved(const QString& filePath, const QString& oldOriginName,
                           const QString& newOriginName)
{
  const FileEntryPtr filePtr =
      m_OrganizerCore.directoryStructure()->findFile(ToWString(filePath));
  if (filePtr.get() != nullptr) {
    try {
      if (m_OrganizerCore.directoryStructure()->originExists(
              ToWString(newOriginName))) {
        FilesOrigin& newOrigin = m_OrganizerCore.directoryStructure()->getOriginByName(
            ToWString(newOriginName));

        QString fullNewPath = ToQString(newOrigin.getPath()) + "\\" + filePath;
        WIN32_FIND_DATAW findData;
        HANDLE hFind;
        hFind = ::FindFirstFileW(ToWString(fullNewPath).c_str(), &findData);
        filePtr->addOrigin(newOrigin.getID(), findData.ftCreationTime, L"", -1);
        FindClose(hFind);
      }
      if (m_OrganizerCore.directoryStructure()->originExists(
              ToWString(oldOriginName))) {
        FilesOrigin& oldOrigin = m_OrganizerCore.directoryStructure()->getOriginByName(
            ToWString(oldOriginName));
        filePtr->removeOrigin(oldOrigin.getID());
      }
    } catch (const std::exception& e) {
      reportError(tr("failed to move \"%1\" from mod \"%2\" to \"%3\": %4")
                      .arg(filePath)
                      .arg(oldOriginName)
                      .arg(newOriginName)
                      .arg(e.what()));
    }
  } else {
    // this is probably not an error, the specified path is likely a directory
  }
}

void MainWindow::modRemoved(const QString& fileName)
{
  if (!fileName.isEmpty() && !QFileInfo(fileName).isAbsolute()) {
    m_OrganizerCore.downloadManager()->markUninstalled(fileName);
  }
}

void MainWindow::windowTutorialFinished(const QString& windowName)
{
  m_OrganizerCore.settings().interface().setTutorialCompleted(windowName);
}

void MainWindow::displayModInformation(ModInfo::Ptr modInfo, unsigned int modIndex,
                                       ModInfoTabIDs tabID)
{
  ui->modList->actions().displayModInformation(modInfo, modIndex, tabID);
}

bool MainWindow::closeWindow()
{
  return close();
}

void MainWindow::setWindowEnabled(bool enabled)
{
  setEnabled(enabled);
}

void MainWindow::refreshProfile_activated()
{
  m_OrganizerCore.refresh();
}

void MainWindow::saveArchiveList()
{
  if (m_OrganizerCore.isArchivesInit()) {
    SafeWriteFile archiveFile(m_OrganizerCore.currentProfile()->getArchivesFileName());
    for (int i = 0; i < ui->bsaList->topLevelItemCount(); ++i) {
      QTreeWidgetItem* tlItem = ui->bsaList->topLevelItem(i);
      for (int j = 0; j < tlItem->childCount(); ++j) {
        QTreeWidgetItem* item = tlItem->child(j);
        if (item->checkState(0) == Qt::Checked) {
          archiveFile->write(item->text(0).toUtf8().append("\r\n"));
        }
      }
    }
    archiveFile.commitIfDifferent(m_ArchiveListHash);
  } else {
    log::debug("archive list not initialised");
  }
}

void MainWindow::openInstanceFolder()
{
  QString dataPath = qApp->property("dataPath").toString();
  shell::Explore(dataPath);
}

void MainWindow::openInstallFolder()
{
  shell::Explore(qApp->applicationDirPath());
}

void MainWindow::openPluginsFolder()
{
  QString pluginsPath =
      QCoreApplication::applicationDirPath() + "/" + ToQString(AppConfig::pluginPath());
  shell::Explore(pluginsPath);
}

void MainWindow::openStylesheetsFolder()
{
  QString ssPath = QCoreApplication::applicationDirPath() + "/" +
                   ToQString(AppConfig::stylesheetsPath());
  shell::Explore(ssPath);
}

void MainWindow::openProfileFolder()
{
  shell::Explore(m_OrganizerCore.currentProfile()->absolutePath());
}

void MainWindow::openIniFolder()
{
  if (m_OrganizerCore.currentProfile()->localSettingsEnabled()) {
    shell::Explore(m_OrganizerCore.currentProfile()->absolutePath());
  } else {
    shell::Explore(m_OrganizerCore.managedGame()->documentsDirectory());
  }
}

void MainWindow::openDownloadsFolder()
{
  shell::Explore(m_OrganizerCore.settings().paths().downloads());
}

void MainWindow::openModsFolder()
{
  shell::Explore(m_OrganizerCore.settings().paths().mods());
}

void MainWindow::openGameFolder()
{
  shell::Explore(m_OrganizerCore.managedGame()->gameDirectory());
}

void MainWindow::openMyGamesFolder()
{
  shell::Explore(m_OrganizerCore.managedGame()->documentsDirectory());
}

QMenu* MainWindow::createModListOptionsMenu()
{
  if (m_OrganizerCore.gameFeatures().gameFeature<GamePlugins>()) {
    return new ModListGlobalContextMenu(
        m_OrganizerCore, ui->modList, this,
        [this] { createModListBackup(); },
        [this] { restoreModListBackup(); },
        [this] { deleteProfileModListBackup(); },
        [this] { createPluginOrderBackup(); },
        [this] { restorePluginOrderBackup(); },
        [this] { deleteProfileLoadOrderBackup(); });
  }

  return new ModListGlobalContextMenu(
      m_OrganizerCore, ui->modList, this,
      [this] { createModListBackup(); },
      [this] { restoreModListBackup(); },
      [this] { deleteProfileModListBackup(); });
}

QMenu* MainWindow::openFolderMenu()
{
  QMenu* FolderMenu = new QMenu(this);
  auto addFolderAction = [this, FolderMenu](const QString& text, const QString& icon,
                                             const char* slot) {
    QAction* action = FolderMenu->addAction(text, this, slot);
    action->setIcon(QIcon(icon));
  };

  FolderMenu->addSection(tr("Game folders"));
  addFolderAction(tr("Game"), ":/MO/gui/mainwindow/files/game-data.svg",
                  SLOT(openGameFolder()));
  addFolderAction(tr("My Games"), ":/MO/gui/mainwindow/files/folder.svg",
                  SLOT(openMyGamesFolder()));
  addFolderAction(tr("INI files"), ":/MO/gui/mainwindow/files/config.svg",
                  SLOT(openIniFolder()));

  FolderMenu->addSection(tr("MO2 instance"));
  addFolderAction(tr("Instance"), ":/MO/gui/mainwindow/instance.svg",
                  SLOT(openInstanceFolder()));
  addFolderAction(tr("Mods"), ":/MO/gui/contextmenu/all-mods.svg",
                  SLOT(openModsFolder()));
  addFolderAction(tr("Profile"), ":/MO/gui/mainwindow/profile.svg",
                  SLOT(openProfileFolder()));
  addFolderAction(tr("Downloads"), ":/MO/gui/mainwindow/install.svg",
                  SLOT(openDownloadsFolder()));

  FolderMenu->addSection(tr("MO2 installation"));
  addFolderAction(tr("Program folder"), ":/MO/gui/mainwindow/files/folder.svg",
                  SLOT(openInstallFolder()));
  addFolderAction(tr("Plugins"), ":/MO/gui/mainwindow/status/plugin-flag.svg",
                  SLOT(openPluginsFolder()));
  addFolderAction(tr("Stylesheets"), ":/MO/gui/mainwindow/files/text.svg",
                  SLOT(openStylesheetsFolder()));
  QAction* logsAction = FolderMenu->addAction(tr("Logs"), [=] {
    ui->logList->openLogsFolder();
  });
  logsAction->setIcon(QIcon(":/MO/gui/mainwindow/status/debug.svg"));

  return FolderMenu;
}

void MainWindow::linkToolbar()
{
  Executable* exe = getSelectedExecutable();
  if (!exe) {
    return;
  }

  exe->setShownOnToolbar(!exe->isShownOnToolbar());
  updatePinnedExecutables();
}

void MainWindow::linkDesktop()
{
  if (auto* exe = getSelectedExecutable()) {
    env::Shortcut(*exe).toggle(env::Shortcut::Desktop);
  }
}

void MainWindow::linkMenu()
{
  if (auto* exe = getSelectedExecutable()) {
    env::Shortcut(*exe).toggle(env::Shortcut::StartMenu);
  }
}

void MainWindow::updateShortcutActionIcons()
{
  const Executable* exe = getSelectedExecutable();
  if (!exe) {
    m_LinkToolbar->setEnabled(false);
    m_LinkDesktop->setEnabled(false);
    m_LinkStartMenu->setEnabled(false);
    return;
  }

  m_LinkToolbar->setEnabled(true);
  m_LinkDesktop->setEnabled(true);
  m_LinkStartMenu->setEnabled(true);

  const QIcon addIcon(":/MO/gui/mainwindow/shortcut-add.svg");
  const QIcon removeIcon(":/MO/gui/mainwindow/shortcut-remove.svg");

  env::Shortcut shortcut(*exe);

  m_LinkToolbar->setIcon(exe->isShownOnToolbar() ? removeIcon : addIcon);

  m_LinkDesktop->setIcon(shortcut.exists(env::Shortcut::Desktop) ? removeIcon
                                                                 : addIcon);

  m_LinkStartMenu->setIcon(shortcut.exists(env::Shortcut::StartMenu) ? removeIcon
                                                                     : addIcon);
}

void MainWindow::on_actionSettings_triggered()
{
  Settings& settings = m_OrganizerCore.settings();

  QString oldModDirectory(settings.paths().mods());
  QString oldCacheDirectory(settings.paths().cache());
  QString oldProfilesDirectory(settings.paths().profiles());
  QString oldManagedGameDirectory(settings.game().directory().value_or(""));
  bool oldDisplayForeign(settings.interface().displayForeign());
  bool oldArchiveParsing(settings.archiveParsing());
  bool proxy                    = settings.network().useProxy();
  DownloadManager* dlManager    = m_OrganizerCore.downloadManager();
  const bool oldCheckForUpdates = settings.checkForUpdates();
  const int oldMaxDumps         = settings.diagnostics().maxCoreDumps();

  SettingsDialog dialog(&m_PluginContainer, settings, this);
  dialog.exec();

  auto e = dialog.exitNeeded();

  if (oldManagedGameDirectory != settings.game().directory()) {
    e |= Exit::Restart;
  }

  if (e.testFlag(Exit::Restart)) {
    const auto r =
        MOBase::TaskDialog(this)
            .title(tr("Restart Mod Organizer"))
            .main("Restart Mod Organizer")
            .content(tr("Mod Organizer must restart to finish configuration changes"))
            .icon(QMessageBox::Question)
            .button({tr("Restart"), QMessageBox::Yes})
            .button(
                {tr("Continue"), tr("Some things might be weird."), QMessageBox::No})
            .exec();

    if (r == QMessageBox::Yes) {
      ExitModOrganizer(e);
    }
  }

  InstallationManager* instManager = m_OrganizerCore.installationManager();
  instManager->setModsDirectory(settings.paths().mods());
  instManager->setDownloadDirectory(settings.paths().downloads());

  // Schedule a problem check since diagnose plugins may have been enabled / disabled.
  scheduleCheckForProblems();

  fixCategories();
  ui->modList->refreshFilters();
  ui->modList->refresh();

  m_OrganizerCore.refreshLists();

  updateSortButton();

  if (settings.paths().profiles() != oldProfilesDirectory) {
    refreshProfiles();
  }

  if (dlManager->getOutputDirectory() != settings.paths().downloads()) {
    if (dlManager->downloadsInProgress()) {
      MessageDialog::showMessage(tr("Can't change download directory while "
                                    "downloads are in progress!"),
                                 this);
    } else {
      dlManager->setOutputDirectory(settings.paths().downloads());
    }
  }

  if ((settings.paths().mods() != oldModDirectory) ||
      (settings.interface().displayForeign() != oldDisplayForeign)) {
    m_OrganizerCore.refresh();
  }

  const auto state = settings.archiveParsing();
  if (state != oldArchiveParsing) {
    if (!state) {
      ui->dataTabShowFromArchives->setCheckState(Qt::Unchecked);
      ui->dataTabShowFromArchives->setEnabled(false);
    } else {
      ui->dataTabShowFromArchives->setCheckState(Qt::Checked);
      ui->dataTabShowFromArchives->setEnabled(true);
    }
    m_OrganizerCore.refresh();
  }

  if (settings.paths().cache() != oldCacheDirectory) {
    NexusInterface::instance().setCacheDirectory(settings.paths().cache());
  }

  if (proxy != settings.network().useProxy()) {
    activateProxy(settings.network().useProxy());
  }

  ui->statusBar->checkSettings(m_OrganizerCore.settings());
  m_DownloadsTab->update();

  m_OrganizerCore.setLogLevel(settings.diagnostics().logLevel());

  if (settings.diagnostics().maxCoreDumps() != oldMaxDumps) {
    m_OrganizerCore.cycleDiagnostics();
  }

  toggleMO2EndorseState();

  if (oldCheckForUpdates != settings.checkForUpdates()) {
    if (settings.checkForUpdates()) {
      m_OrganizerCore.checkForUpdates();
    }
  }
}

void MainWindow::onPluginRegistrationChanged()
{
  m_ToolMenuDirty = true;
  updateModPageMenu();
  scheduleCheckForProblems();
  m_DownloadsTab->update();
}

void MainWindow::refreshNexusCategories(CategoriesDialog* dialog)
{
  NexusInterface& nexus = NexusInterface::instance();
  nexus.setPluginContainer(&m_PluginContainer);
  if (!Settings::instance().game().plugin()->primarySources().isEmpty()) {
    nexus.requestGameInfo(
        Settings::instance().game().plugin()->primarySources().first(), dialog,
        QVariant(), QString());
  } else {
    nexus.requestGameInfo(Settings::instance().game().plugin()->gameShortName(), dialog,
                          QVariant(), QString());
  }
}

void MainWindow::categoriesSaved()
{
  for (auto modName : m_OrganizerCore.modList()->allMods()) {
    auto mod = ModInfo::getByName(modName);
    for (auto category : mod->getCategories()) {
      if (!m_CategoryFactory.categoryExists(category))
        mod->setCategory(category, false);
    }
  }
}

void MainWindow::on_actionNexus_triggered()
{
  const IPluginGame* game = m_OrganizerCore.managedGame();
  QString gameName        = game->gameShortName();
  if (game->gameNexusName().isEmpty() && game->primarySources().count())
    gameName = game->primarySources()[0];
  shell::Open(QUrl(NexusInterface::instance().getGameURL(gameName)));
}

void MainWindow::installTranslator(const QString& name)
{
  QTranslator* translator = new QTranslator(this);
  QString fileName        = name + "_" + m_CurrentLanguage;
  if (!translator->load(fileName, qApp->applicationDirPath() + "/translations")) {
    if (m_CurrentLanguage.contains(QRegularExpression("^.*_(EN|en)(-.*)?$"))) {
      log::debug("localization file %s not found", fileName);
    }  // we don't actually expect localization files for English (en, en-us, en-uk, and
       // any variation thereof)
  }

  qApp->installTranslator(translator);
  m_Translators.push_back(translator);
}

void MainWindow::languageChange(const QString& newLanguage)
{
  m_ToolMenuDirty = true;
  for (QTranslator* trans : m_Translators) {
    qApp->removeTranslator(trans);
  }
  m_Translators.clear();

  m_CurrentLanguage = newLanguage;

  installTranslator("qt");
  installTranslator("qtbase");
  installTranslator(ToQString(AppConfig::translationPrefix()));
  for (const QString& fileName : m_PluginContainer.pluginFileNames()) {
    installTranslator(QFileInfo(fileName).baseName());
  }
  ui->retranslateUi(this);
  if (m_FilterOptionsDialog) {
    m_FilterOptionsDialog->setWindowTitle(tr("Filter options"));
    m_FilterDialogTitle->setText(tr("Filter options"));
    m_FilterDialogDescription->setText(
        ui->filters->headerItem()->toolTip(1));
    ui->categoriesGroup->setTitle(QString());
    ui->displayCategoriesBtn->setText(QString());
    ui->displayCategoriesBtn->setIcon(
        QIcon(QStringLiteral(":/MO/gui/mainwindow/filter.svg")));
    ui->displayCategoriesBtn->setIconSize(QSize(16, 16));
    ui->displayCategoriesBtn->setToolTip(tr("Filter options"));

    if (auto* buttons = m_FilterOptionsDialog->findChild<QDialogButtonBox*>()) {
      if (auto* closeButton = buttons->button(QDialogButtonBox::Close)) {
        closeButton->setText(tr("Close"));
      }
    }
  }
  log::debug("loaded language {}", newLanguage);

  ui->profileBox->setItemText(0, QObject::tr("<Manage...>"));

  createHelpMenu();

  if (m_DownloadsTab) {
    m_DownloadsTab->update();
  }

  ui->listOptionsBtn->setMenu(createModListOptionsMenu());
  ui->openFolderMenu->setMenu(openFolderMenu());
}

void MainWindow::originModified(int originID)
{
  FilesOrigin& origin = m_OrganizerCore.directoryStructure()->getOriginByID(originID);
  origin.enable(false);

  DirectoryStats dummy;
  m_OrganizerCore.directoryStructure()->addFromOrigin(
      origin.getName(), origin.getPath(), origin.getPriority(), dummy);

  DirectoryRefresher::cleanStructure(m_OrganizerCore.directoryStructure());
}

void MainWindow::updateAvailable()
{
  ui->actionUpdate->setEnabled(true);
  const QString updateHint =
      tr("A newer MO2 Revamped release is available. Check MO2 Revamped on "
         "GitHub for release details.");
  ui->actionUpdate->setToolTip(updateHint);
  ui->actionUpdate->setStatusTip(updateHint);
  ui->statusBar->setUpdateAvailable(true);
}

void MainWindow::motdReceived(const QString& motd)
{
  // don't show motd after 5 seconds, may be annoying. Hopefully the user's
  // internet connection is faster next time
  if (m_StartTime.secsTo(QTime::currentTime()) < 5) {
    uint hash = qHash(motd);
    if (hash != m_OrganizerCore.settings().motdHash()) {
      MotDDialog dialog(motd);
      dialog.exec();
      m_OrganizerCore.settings().setMotdHash(hash);
    }
  }
}

void MainWindow::on_actionUpdate_triggered()
{
  revampedRepositoryTriggered();
}

void MainWindow::on_actionExit_triggered()
{
  ExitModOrganizer();
}

void MainWindow::actionEndorseMO()
{
  // Normally this would be the managed game but MO2 is only uploaded to the Skyrim SE
  // site right now
  IPluginGame* game = m_OrganizerCore.getGame("skyrimse");
  if (!game)
    return;

  if (QMessageBox::question(
          this, tr("Endorse Mod Organizer"),
          tr("Do you want to endorse Mod Organizer on %1 now?")
              .arg(NexusInterface::instance().getGameURL(game->gameShortName())),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    NexusInterface::instance().requestToggleEndorsement(
        game->gameShortName(), game->nexusModOrganizerID(),
        m_OrganizerCore.getVersion().canonicalString(), true, this, QVariant(),
        QString());
  }
}

void MainWindow::actionWontEndorseMO()
{
  // Normally this would be the managed game but MO2 is only uploaded to the Skyrim SE
  // site right now
  IPluginGame* game = m_OrganizerCore.getGame("skyrimse");
  if (!game)
    return;

  if (QMessageBox::question(
          this, tr("Abstain from Endorsing Mod Organizer"),
          tr("Are you sure you want to abstain from endorsing Mod Organizer 2?\n"
             "You will have to visit the mod page on the %1 Nexus site to change your "
             "mind.")
              .arg(NexusInterface::instance().getGameURL(game->gameShortName())),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    NexusInterface::instance().requestToggleEndorsement(
        game->gameShortName(), game->nexusModOrganizerID(),
        m_OrganizerCore.getVersion().canonicalString(), false, this, QVariant(),
        QString());
  }
}

void MainWindow::toggleMO2EndorseState()
{
  const auto& s = m_OrganizerCore.settings();

  if (!s.nexus().endorsementIntegration()) {
    ui->actionEndorseMO->setVisible(false);
    return;
  }

  ui->actionEndorseMO->setVisible(true);

  bool enabled = false;
  QString text;

  switch (s.nexus().endorsementState()) {
  case EndorsementState::Accepted: {
    text = tr("Thank you for endorsing MO2! :)");
    break;
  }

  case EndorsementState::Refused: {
    text = tr("Please reconsider endorsing MO2 on Nexus!");
    break;
  }

  case EndorsementState::NoDecision: {
    enabled = true;
    break;
  }
  }

  ui->actionEndorseMO->menu()->setEnabled(enabled);
  ui->actionEndorseMO->setToolTip(text);
  ui->actionEndorseMO->setStatusTip(text);
}

void MainWindow::updateSortButton()
{
  if (m_OrganizerCore.managedGame()->sortMechanism() !=
      IPluginGame::SortMechanism::NONE) {
    ui->sortButton->setEnabled(true);
    ui->sortButton->setToolTip(tr("Sort the plugins using LOOT."));
  } else {
    ui->sortButton->setDisabled(true);
    ui->sortButton->setToolTip(tr("There is no supported sort mechanism for this game. "
                                  "You will probably have to use a third-party tool."));
  }
}

void MainWindow::nxmEndorsementsAvailable(QVariant userData, QVariant resultData, int)
{
  QVariantList data = resultData.toList();
  std::multimap<QString, std::pair<int, QString>> sorted;
  QStringList games = m_OrganizerCore.managedGame()->validShortNames();
  games += m_OrganizerCore.managedGame()->gameShortName();
  bool searchedMO2NexusGame = false;
  for (auto endorsementData : data) {
    QVariantMap endorsement      = endorsementData.toMap();
    std::pair<int, QString> data = std::make_pair<int, QString>(
        endorsement["mod_id"].toInt(), endorsement["status"].toString());
    sorted.insert(std::pair<QString, std::pair<int, QString>>(
        endorsement["domain_name"].toString(), data));
  }
  for (auto game : games) {
    IPluginGame* gamePlugin = m_OrganizerCore.getGame(game);
    if (gamePlugin != nullptr &&
        gamePlugin->gameShortName().compare("SkyrimSE", Qt::CaseInsensitive) == 0)
      searchedMO2NexusGame = true;
    auto iter = sorted.equal_range(gamePlugin->gameNexusName());
    for (auto result = iter.first; result != iter.second; ++result) {
      std::vector<ModInfo::Ptr> modsList =
          ModInfo::getByModID(result->first, result->second.first);

      for (auto mod : modsList) {
        if (result->second.second == "Endorsed")
          mod->setIsEndorsed(true);
        else if (result->second.second == "Abstained")
          mod->setNeverEndorse();
        else
          mod->setIsEndorsed(false);
      }

      if (Settings::instance().nexus().endorsementIntegration()) {
        if (result->first == "skyrimspecialedition" &&
            result->second.first == gamePlugin->nexusModOrganizerID()) {
          m_OrganizerCore.settings().nexus().setEndorsementState(
              endorsementStateFromString(result->second.second));

          toggleMO2EndorseState();
        }
      }
    }
  }

  if (!searchedMO2NexusGame && Settings::instance().nexus().endorsementIntegration()) {
    auto gamePlugin = m_OrganizerCore.getGame("SkyrimSE");
    if (gamePlugin) {
      auto iter = sorted.equal_range(gamePlugin->gameNexusName());
      for (auto result = iter.first; result != iter.second; ++result) {
        if (result->second.first == gamePlugin->nexusModOrganizerID()) {
          m_OrganizerCore.settings().nexus().setEndorsementState(
              endorsementStateFromString(result->second.second));

          toggleMO2EndorseState();
          break;
        }
      }
    }
  }
}

void MainWindow::nxmUpdateInfoAvailable(QString gameName, QVariant userData,
                                        QVariant resultData, int)
{
  QString gameNameReal;
  for (IPluginGame* game : m_PluginContainer.plugins<IPluginGame>()) {
    if (game->gameNexusName() == gameName) {
      gameNameReal = game->gameShortName();
      break;
    }
  }
  QVariantList resultList = resultData.toList();

  auto* watcher = new QFutureWatcher<NxmUpdateInfoData>();
  QObject::connect(watcher, &QFutureWatcher<NxmUpdateInfoData>::finished,
                   [this, watcher]() {
                     finishUpdateInfo(watcher->result());
                     watcher->deleteLater();
                   });
  auto future = QtConcurrent::run([=]() {
    return NxmUpdateInfoData{
        gameNameReal,
        ModInfo::filteredMods(gameNameReal, resultList, userData.toBool(), true)};
  });
  watcher->setFuture(future);
  ui->modList->invalidateFilter();
}

void MainWindow::finishUpdateInfo(const NxmUpdateInfoData& data)
{
  if (data.finalMods.empty()) {
    log::info("{}", tr("None of your %1 mods appear to have had recent file updates.")
                        .arg(data.game));
  }

  std::set<std::pair<QString, int>> organizedGames;
  for (auto& mod : data.finalMods) {
    if (mod->canBeUpdated()) {
      organizedGames.insert(
          std::make_pair<QString, int>(mod->gameName().toLower(), mod->nexusId()));
    }
  }

  if (!data.finalMods.empty() && organizedGames.empty())
    log::info("{}", tr("All of your mods have been checked recently. We restrict "
                       "update checks to help preserve your available API requests."));

  for (const auto& game : organizedGames) {
    NexusInterface::instance().requestUpdates(game.second, this, QVariant(), game.first,
                                              QString());
  }
}

void MainWindow::nxmUpdatesAvailable(QString gameName, int modID, QVariant userData,
                                     QVariant resultData, int requestID)
{
  QVariantMap resultInfo = resultData.toMap();
  QList files            = resultInfo["files"].toList();
  QList fileUpdates      = resultInfo["file_updates"].toList();
  QString gameNameReal;

  for (IPluginGame* game : m_PluginContainer.plugins<IPluginGame>()) {
    if (game->gameNexusName() == gameName) {
      gameNameReal = game->gameShortName();
      break;
    }
  }

  std::vector<ModInfo::Ptr> modsList = ModInfo::getByModID(gameNameReal, modID);

  bool requiresInfo = false;

  for (auto mod : modsList) {
    QString validNewVersion;
    int newModStatus      = -1;
    QString installedFile = QFileInfo(mod->installationFile()).fileName();

    if (!installedFile.isEmpty()) {
      QVariantMap foundFileData;

      // update the file status
      for (auto& file : files) {
        QVariantMap fileData = file.toMap();

        if (fileData["file_name"].toString().compare(installedFile,
                                                     Qt::CaseInsensitive) == 0) {
          foundFileData = fileData;
          newModStatus  = foundFileData["category_id"].toInt();

          if (newModStatus != NexusInterface::FileStatus::OLD_VERSION &&
              newModStatus != NexusInterface::FileStatus::REMOVED &&
              newModStatus != NexusInterface::FileStatus::ARCHIVED) {

            // since the file is still active if there are no updates for it, use this
            // as current version
            validNewVersion = foundFileData["version"].toString();
          }
          break;
        }
      }

      if (foundFileData.isEmpty()) {
        // The file was not listed, the file is likely archived and archived files are
        // being hidden on the mod
        newModStatus = NexusInterface::FileStatus::ARCHIVED_HIDDEN;
      }

      // look for updates of the file
      int currentUpdateId = -1;

      // find installed file ID from the updates list since old filenames are not
      // guaranteed to be unique
      for (auto& updateEntry : fileUpdates) {
        const QVariantMap& updateData = updateEntry.toMap();

        if (installedFile.compare(updateData["old_file_name"].toString(),
                                  Qt::CaseInsensitive) == 0) {
          currentUpdateId = updateData["old_file_id"].toInt();
          break;
        }
      }

      bool foundActiveUpdate = false;

      // there is at least one update
      if (currentUpdateId > 0) {
        bool lookForMoreUpdates = true;

        // follow the update chain until there are no more updates
        while (lookForMoreUpdates) {
          lookForMoreUpdates = false;

          for (auto& updateEntry : fileUpdates) {
            const QVariantMap& updateData = updateEntry.toMap();

            if (currentUpdateId == updateData["old_file_id"].toInt()) {
              currentUpdateId = updateData["new_file_id"].toInt();

              // check if the new file is still active
              for (auto& file : files) {
                const QVariantMap& fileData = file.toMap();

                if (currentUpdateId == fileData["file_id"].toInt()) {
                  int updateStatus = fileData["category_id"].toInt();

                  if (updateStatus != NexusInterface::FileStatus::OLD_VERSION &&
                      updateStatus != NexusInterface::FileStatus::REMOVED &&
                      updateStatus != NexusInterface::FileStatus::ARCHIVED) {

                    // new version is active, so record it
                    validNewVersion   = fileData["version"].toString();
                    foundActiveUpdate = true;
                  }
                  break;
                }
              }

              lookForMoreUpdates = true;
              break;
            }
          }
        }
      }

      // if there were no active direct file updates for the installedFile
      if (!foundActiveUpdate) {
        // get the global mod version in case the file isn't an optional
        if (newModStatus != NexusInterface::FileStatus::OPTIONAL_FILE &&
            newModStatus != NexusInterface::FileStatus::MISCELLANEOUS) {
          requiresInfo = true;
        }
      }
    } else {
      // No installedFile means we don't know what to look at for a version so
      // just get the global mod version
      requiresInfo = true;
    }

    if (newModStatus > 0) {
      mod->setNexusFileStatus(newModStatus);
    }

    if (!validNewVersion.isEmpty()) {
      mod->setNewestVersion(validNewVersion);
      mod->setLastNexusUpdate(QDateTime::currentDateTimeUtc());
    }
  }

  // invalidate the filter to display mods with an update
  ui->modList->invalidateFilter();

  if (requiresInfo) {
    NexusInterface::instance().requestModInfo(gameNameReal, modID, this, QVariant(),
                                              QString());
  }
}

void MainWindow::nxmModInfoAvailable(QString gameName, int modID, QVariant userData,
                                     QVariant resultData, int requestID)
{
  QVariantMap result = resultData.toMap();
  QString gameNameReal;
  bool foundUpdate = false;

  for (IPluginGame* game : m_PluginContainer.plugins<IPluginGame>()) {
    if (game->gameNexusName() == gameName) {
      gameNameReal = game->gameShortName();
      break;
    }
  }

  std::vector<ModInfo::Ptr> modsList = ModInfo::getByModID(gameNameReal, modID);

  for (auto mod : modsList) {
    QDateTime now          = QDateTime::currentDateTimeUtc();
    QDateTime updateTarget = mod->getExpires();

    // if file is still listed as optional or miscellaneous don't update the version as
    // often optional files are left with an older version than the main mod version.
    if (!result["version"].toString().isEmpty() &&
        mod->getNexusFileStatus() != NexusInterface::FileStatus::OPTIONAL_FILE &&
        mod->getNexusFileStatus() != NexusInterface::FileStatus::MISCELLANEOUS) {

      mod->setNewestVersion(result["version"].toString());
      foundUpdate = true;
    }

    // update the LastNexusUpdate time in any case since we did perform the check.
    mod->setLastNexusUpdate(QDateTime::currentDateTimeUtc());

    mod->setNexusDescription(result["description"].toString());

    mod->setNexusCategory(result["category_id"].toInt());

    if ((mod->endorsedState() != EndorsedState::ENDORSED_NEVER) &&
        (result.contains("endorsement"))) {
      QVariantMap endorsement   = result["endorsement"].toMap();
      QString endorsementStatus = endorsement["endorse_status"].toString();

      if (endorsementStatus.compare("Endorsed") == 00)
        mod->setIsEndorsed(true);
      else if (endorsementStatus.compare("Abstained") == 00)
        mod->setNeverEndorse();
      else
        mod->setIsEndorsed(false);
    }

    mod->setLastNexusQuery(QDateTime::currentDateTimeUtc());
    mod->setNexusLastModified(
        QDateTime::fromSecsSinceEpoch(result["updated_timestamp"].toInt(), Qt::UTC));

    m_OrganizerCore.modList()->notifyChange(ModInfo::getIndex(mod->name()));
  }

  if (foundUpdate) {
    // invalidate the filter to display mods with an update
    ui->modList->invalidateFilter();
  }
}

void MainWindow::nxmEndorsementToggled(QString, int, QVariant, QVariant resultData, int)
{
  const QMap results = resultData.toMap();

  auto itor = results.find("status");
  if (itor == results.end()) {
    log::error("endorsement response has no status");
    return;
  }

  const auto s = endorsementStateFromString(itor->toString());

  switch (s) {
  case EndorsementState::Accepted: {
    QMessageBox::information(this, tr("Thank you!"),
                             tr("Thank you for your endorsement!"));
    break;
  }

  case EndorsementState::Refused: {
    // don't spam message boxes if the user doesn't want to endorse
    log::info(
        "Mod Organizer will not be endorsed and will no longer ask you to endorse.");
    break;
  }

  case EndorsementState::NoDecision: {
    log::error("bad status '{}' in endorsement response", itor->toString());
    return;
  }
  }

  m_OrganizerCore.settings().nexus().setEndorsementState(s);
  toggleMO2EndorseState();

  if (!disconnect(sender(),
                  SIGNAL(nxmEndorsementToggled(QString, int, QVariant, QVariant, int)),
                  this,
                  SLOT(nxmEndorsementToggled(QString, int, QVariant, QVariant, int)))) {
    log::error("failed to disconnect endorsement slot");
  }
}

void MainWindow::nxmTrackedModsAvailable(QVariant userData, QVariant resultData, int)
{
  QMap<QString, QString> gameNames;
  for (auto game : m_PluginContainer.plugins<IPluginGame>()) {
    gameNames[game->gameNexusName()] = game->gameShortName();
  }

  for (unsigned int i = 0; i < ModInfo::getNumMods(); i++) {
    auto modInfo = ModInfo::getByIndex(i);
    if (modInfo->nexusId() <= 0)
      continue;

    bool found       = false;
    auto resultsList = resultData.toList();
    for (auto item : resultsList) {
      auto results = item.toMap();
      if ((gameNames[results["domain_name"].toString()].compare(
               modInfo->gameName(), Qt::CaseInsensitive) == 0) &&
          (results["mod_id"].toInt() == modInfo->nexusId())) {
        found = true;
        break;
      }
    }

    modInfo->setIsTracked(found);
    modInfo->saveMeta();
  }
}

void MainWindow::nxmDownloadURLs(QString, int, int, QVariant, QVariant resultData, int)
{
  auto servers = m_OrganizerCore.settings().network().servers();

  for (const QVariant& var : resultData.toList()) {
    const QVariantMap map = var.toMap();

    const auto name = map["short_name"].toString();
    const auto isPremium =
        map["name"].toString().contains("Premium", Qt::CaseInsensitive);
    const auto isCDN =
        map["short_name"].toString().contains("CDN", Qt::CaseInsensitive);

    bool found = false;

    for (auto& server : servers) {
      if (server.name() == name) {
        // already exists, update
        server.setPremium(isPremium);
        server.updateLastSeen();
        found = true;
        break;
      }
    }

    if (!found) {
      // new server
      ServerInfo server(name, isPremium, QDate::currentDate(), isCDN ? 1 : 0, {});
      servers.add(std::move(server));
    }
  }

  m_OrganizerCore.settings().network().updateServers(servers);
}

void MainWindow::nxmGameInfoAvailable(QString gameName, QVariant, QVariant resultData,
                                      int)
{
  QVariantMap result          = resultData.toMap();
  QVariantList categories     = result["categories"].toList();
  CategoryFactory& catFactory = CategoryFactory::instance();
  QStringList gameNames{gameName};
  if (auto* game = Settings::instance().game().plugin()) {
    gameNames << game->gameName() << game->gameShortName() << game->gameNexusName();
  }

  catFactory.reset();
  for (auto category : categories) {
    auto catMap = category.toMap();
    const QString categoryName = catMap["name"].toString();
    const int categoryID       = catMap["category_id"].toInt();
    if (CategoryFactory::isNexusGameRootCategory(categoryName, categoryID,
                                                 gameNames)) {
      continue;
    }

    std::vector<CategoryFactory::NexusCategory> nexusCat;
    nexusCat.push_back(CategoryFactory::NexusCategory(categoryName, categoryID));
    catFactory.addCategory(categoryName, nexusCat, 0);
  }
}

void MainWindow::nxmRequestFailed(QString gameName, int modID, int, QVariant, int,
                                  int errorCode, const QString& errorString)
{
  if (errorCode == QNetworkReply::ContentAccessDenied ||
      errorCode == QNetworkReply::ContentNotFoundError) {
    log::debug("{}",
               tr("Mod ID %1 no longer seems to be available on Nexus.").arg(modID));

    // update last checked timestamp on orphaned mods as well to avoid repeating
    // requests
    QString gameNameReal;
    for (IPluginGame* game : m_PluginContainer.plugins<IPluginGame>()) {
      if (game->gameNexusName() == gameName) {
        gameNameReal = game->gameShortName();
        break;
      }
    }
    auto orphanedMods = ModInfo::getByModID(gameNameReal, modID);
    for (auto mod : orphanedMods) {
      mod->setLastNexusUpdate(QDateTime::currentDateTimeUtc());
      mod->setLastNexusQuery(QDateTime::currentDateTimeUtc());
    }
  } else {
    MessageDialog::showMessage(
        tr("Error %1: Request to Nexus failed: %2").arg(errorCode).arg(errorString),
        this);
  }
}

BSA::EErrorCode MainWindow::extractBSA(BSA::Archive& archive, BSA::Folder::Ptr folder,
                                       const QString& destination,
                                       QProgressDialog& progress)
{
  QDir().mkdir(destination);
  BSA::EErrorCode result = BSA::ERROR_NONE;
  QString errorFile;

  for (unsigned int i = 0; i < folder->getNumFiles(); ++i) {
    BSA::File::Ptr file = folder->getFile(i);
    BSA::EErrorCode res = archive.extract(file, qUtf8Printable(destination));
    if (res != BSA::ERROR_NONE) {
      reportError(tr("failed to read %1: %2").arg(file->getName().c_str()).arg(res));
      result = res;
    }
    progress.setLabelText(file->getName().c_str());
    progress.setValue(progress.value() + 1);
    QCoreApplication::processEvents();
    if (progress.wasCanceled()) {
      result = BSA::ERROR_CANCELED;
    }
  }

  if (result != BSA::ERROR_NONE) {
    if (QMessageBox::critical(
            this, tr("Error"),
            tr("failed to extract %1 (errorcode %2)").arg(errorFile).arg(result),
            QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Cancel) {
      return result;
    }
  }

  for (unsigned int i = 0; i < folder->getNumSubFolders(); ++i) {
    BSA::Folder::Ptr subFolder = folder->getSubFolder(i);
    BSA::EErrorCode res        = extractBSA(
        archive, subFolder,
        destination.mid(0).append("/").append(subFolder->getName().c_str()), progress);
    if (res != BSA::ERROR_NONE) {
      return res;
    }
  }
  return BSA::ERROR_NONE;
}

bool MainWindow::extractProgress(QProgressDialog& progress, int percentage,
                                 std::string fileName)
{
  progress.setLabelText(fileName.c_str());
  progress.setValue(percentage);
  QCoreApplication::processEvents();
  return !progress.wasCanceled();
}

void MainWindow::extractBSATriggered(QTreeWidgetItem* item)
{
  using namespace boost::placeholders;

  QString origin;

  QString targetFolder =
      FileDialogMemory::getExistingDirectory("extractBSA", this, tr("Extract BSA"));
  QStringList archives = {};
  if (!targetFolder.isEmpty()) {
    if (!item->parent()) {
      for (int i = 0; i < item->childCount(); ++i) {
        archives.append(item->child(i)->text(0));
      }
      origin = QDir::fromNativeSeparators(
          ToQString(m_OrganizerCore.directoryStructure()
                        ->getOriginByName(ToWString(item->text(0)))
                        .getPath()));
    } else {
      origin = QDir::fromNativeSeparators(
          ToQString(m_OrganizerCore.directoryStructure()
                        ->getOriginByName(ToWString(item->text(1)))
                        .getPath()));
      archives = QStringList({item->text(0)});
    }

    for (auto archiveName : archives) {
      BSA::Archive archive;
      QString archivePath = QString("%1\\%2").arg(origin).arg(archiveName);
      BSA::EErrorCode result =
          archive.read(archivePath.toLocal8Bit().constData(), true);
      if ((result != BSA::ERROR_NONE) && (result != BSA::ERROR_INVALIDHASHES)) {
        reportError(tr("failed to read %1: %2").arg(archivePath).arg(result));
        return;
      }

      QProgressDialog progress(this);
      progress.setMaximum(100);
      progress.setValue(0);
      progress.show();
      archive.extractAll(
          QDir::toNativeSeparators(targetFolder).toLocal8Bit().constData(),
          boost::bind(&MainWindow::extractProgress, this, boost::ref(progress), _1,
                      _2));
      if (result == BSA::ERROR_INVALIDHASHES) {
        reportError(
            tr("This archive contains invalid hashes. Some files may be broken."));
      }
      archive.close();
    }
  }
}

void MainWindow::on_bsaList_customContextMenuRequested(const QPoint& pos)
{
  QMenu menu;
  QAction* extractAction = menu.addAction(
      tr("Extract..."), [=, item = ui->bsaList->itemAt(pos)]() {
    extractBSATriggered(item);
  });
  extractAction->setIcon(QIcon(":/MO/gui/contextmenu/export.svg"));

  menu.exec(ui->bsaList->viewport()->mapToGlobal(pos));
}

void MainWindow::on_bsaList_itemChanged(QTreeWidgetItem*, int)
{
  m_ArchiveListWriter.write();
  m_CheckBSATimer.start(500);
}

void MainWindow::on_actionNotifications_triggered()
{
  auto future = checkForProblemsAsync();

  future.waitForFinished();

  ProblemsDialog problems(m_PluginContainer, this);
  connect(&problems, &ProblemsDialog::manageOverwriteRequested, this, [this, &problems]() {
    problems.accept();
    ui->modList->actions().displayModInformation(QStringLiteral("Overwrite"));
  });
  problems.exec();

  scheduleCheckForProblems();
}

void MainWindow::on_actionChange_Game_triggered()
{
  InstanceManagerDialog dlg(m_PluginContainer, this);
  dlg.exec();
}

void MainWindow::on_displayCategoriesBtn_toggled(bool checked)
{
  if (!m_FilterOptionsDialog) {
    return;
  }

  if (checked) {
    m_FilterOptionsDialog->show();
    m_FilterOptionsDialog->raise();
    m_FilterOptionsDialog->activateWindow();
  } else {
    m_FilterOptionsDialog->hide();
  }
}

void MainWindow::removeFromToolbar(QAction* action)
{
  const auto& title = action->text();
  auto& list        = *m_OrganizerCore.executablesList();

  auto itor = list.find(title);
  if (itor == list.end()) {
    log::warn("removeFromToolbar(): executable '{}' not found", title);
    return;
  }

  itor->setShownOnToolbar(false);
  updatePinnedExecutables();
}

void MainWindow::toolBar_customContextMenuRequested(const QPoint& point)
{
  QAction* action = ui->toolBar->actionAt(point);

  if (action != nullptr) {
    if (action->objectName().startsWith("custom_")) {
      QMenu menu;
      QAction* removeAction = menu.addAction(
          tr("Remove '%1' from the toolbar").arg(action->text()),
          [&, action]() { removeFromToolbar(action); });
      removeAction->setIcon(QIcon(":/MO/gui/contextmenu/remove.svg"));
      menu.exec(ui->toolBar->mapToGlobal(point));
      return;
    }
  }

  // did not click a link button, show the default context menu
  auto* m = createPopupMenu();
  m->exec(ui->toolBar->mapToGlobal(point));
}

Executable* MainWindow::getSelectedExecutable()
{
  const QString name =
      ui->executablesListBox->itemText(ui->executablesListBox->currentIndex());

  try {
    return &m_OrganizerCore.executablesList()->get(name);
  } catch (std::runtime_error&) {
    return nullptr;
  }
}

void MainWindow::on_showHiddenBox_toggled(bool checked)
{
  m_OrganizerCore.downloadManager()->setShowHidden(checked);
}

const char* MainWindow::PATTERN_BACKUP_GLOB = ".????_??_??_??_??_??";
const char* MainWindow::PATTERN_BACKUP_REGEX =
    "\\.(\\d\\d\\d\\d_\\d\\d_\\d\\d_\\d\\d_\\d\\d_\\d\\d)";
const char* MainWindow::PATTERN_BACKUP_DATE = "yyyy_MM_dd_hh_mm_ss";

bool MainWindow::createBackup(const QString& filePath, const QDateTime& time)
{
  QString outPath = filePath + "." + time.toString(PATTERN_BACKUP_DATE);
  if (shellCopy(QStringList(filePath), QStringList(outPath), this)) {
    QFileInfo fileInfo(filePath);
    removeOldFiles(fileInfo.absolutePath(), fileInfo.fileName() + PATTERN_BACKUP_GLOB,
                   10, QDir::Name);
    return true;
  } else {
    return false;
  }
}

void MainWindow::createProfileBackup(bool includeModList, bool includePluginOrder)
{
  if (!includeModList && !includePluginOrder) {
    return;
  }

  if (includePluginOrder && !profileHasPluginOrderData(m_OrganizerCore)) {
    if (!includeModList) {
      showProfileBackupPrompt(
          this, tr("Plugin order unavailable"),
          tr("There is no plugin order to back up."),
          tr("This profile has no ESP, ESM, or ESL plugins. Mod priorities are "
             "included in the mod list backup."),
          tr("OK"));
      return;
    }
    includePluginOrder = false;
  }

  const auto profile = m_OrganizerCore.currentProfile();
  const QDateTime now = QDateTime::currentDateTime();
  QStringList failedParts;

  if (includeModList) {
    profile->writeModlistNow(true);
    if (!createBackup(profile->getModlistFileName(), now)) {
      failedParts.append(tr("mod list"));
    }
  }

  if (includePluginOrder) {
    m_OrganizerCore.savePluginList();
    const QStringList pluginOrderFiles{
        profile->getPluginsFileName(), profile->getLoadOrderFileName(),
        profile->getLockedOrderFileName()};
    bool pluginOrderCreated = true;
    for (const QString& filePath : pluginOrderFiles) {
      if (!createBackup(filePath, now)) {
        pluginOrderCreated = false;
        break;
      }
    }
    if (!pluginOrderCreated) {
      failedParts.append(tr("plugin order"));
    }
  }

  if (!failedParts.isEmpty()) {
    QString message =
        tr("MO2 could not create these backup parts: %1.")
            .arg(failedParts.join(tr(", ")));
    message += tr("\n\nAny parts that succeeded are still available to restore.");
    if (includePluginOrder && failedParts.contains(tr("plugin order"))) {
      message += tr(" An incomplete plugin-order backup cannot be restored.");
    }
    showProfileBackupPrompt(this, tr("Backup incomplete"),
                            tr("The backup is incomplete."), message,
                            tr("OK"));
    return;
  }

  if (includeModList && includePluginOrder) {
    showProfileBackupPrompt(
        this, tr("Profile backup created"), tr("Backup created"),
        tr("The selected parts of this profile were saved and are ready to "
           "restore."),
        tr("OK"));
  } else if (includeModList) {
    showProfileBackupPrompt(
        this, tr("Backup of mod list created"), tr("Backup created"),
        tr("The mod list backup is ready to restore."), tr("OK"));
  } else {
    showProfileBackupPrompt(
        this, tr("Backup of load order created"), tr("Backup created"),
        tr("The plugin-order backup is ready to restore."), tr("OK"));
  }
}

void MainWindow::createPluginOrderBackup()
{
  const bool pluginOrderAvailable =
      profileHasPluginOrderData(m_OrganizerCore);
  if (!pluginOrderAvailable) {
    showProfileBackupPrompt(
        this, tr("Plugin order unavailable"),
        tr("There is no plugin order to back up."),
        tr("This profile has no ESP, ESM, or ESL plugins. Mod priorities are "
           "included in the mod list backup."),
        tr("OK"));
    return;
  }

  const ProfileBackupSelection selection =
      chooseProfileBackupContents(this, false, true, pluginOrderAvailable);
  if (!selection.accepted) {
    return;
  }
  createProfileBackup(selection.modList, selection.pluginOrder);
}

QString MainWindow::queryRestore(const QString& filePath)
{
  QFileInfo pluginFileInfo(filePath);
  QString pattern     = pluginFileInfo.fileName() + ".*";
  QFileInfoList files = pluginFileInfo.absoluteDir().entryInfoList(
      QStringList(pattern), QDir::Files, QDir::Name);

  const bool isModListBackup =
      pluginFileInfo.fileName() ==
      QFileInfo(m_OrganizerCore.currentProfile()->getModlistFileName()).fileName();
  const QString loadOrderFileName =
      m_OrganizerCore.currentProfile()->getLoadOrderFileName();
  const QString lockedOrderFileName =
      m_OrganizerCore.currentProfile()->getLockedOrderFileName();
  bool incompleteLoadOrderBackupFound = false;
  const auto hasCompleteLoadOrderBackup = [&](const QString& suffix) {
    const QDir backupDirectory(pluginFileInfo.absolutePath());
    const QString loadOrderBackup = backupDirectory.filePath(
        QFileInfo(loadOrderFileName).fileName() + "." + suffix);
    const QString lockedOrderBackup = backupDirectory.filePath(
        QFileInfo(lockedOrderFileName).fileName() + "." + suffix);
    return QFileInfo(loadOrderBackup).isFile() &&
           QFileInfo(lockedOrderBackup).isFile();
  };
  const QString dialogTitle = isModListBackup ? tr("Restore mod list backup")
                                               : tr("Restore load order backup");
  const QString dialogDescription =
      isModListBackup
          ? tr("Choose a saved mod list for this profile. Restoring it replaces "
               "the current mod list; MO2 does not back up the current version "
               "automatically.")
          : tr("Choose a saved plugin load order for this profile. Restoring it "
               "replaces the current plugin load order; MO2 does not back up the "
               "current version automatically.");
  const QString choiceDescription =
      isModListBackup ? tr("Restore this saved mod list.")
                      : tr("Restore this saved plugin load order.");
  SelectionDialog dialog(
      dialogDescription, this, QSize(28, 28));
  dialog.setWindowTitle(dialogTitle);
  if (auto* titleLabel = dialog.findChild<QLabel*>(QStringLiteral("titleLabel"))) {
    titleLabel->setText(dialogTitle);
  }

  QRegularExpression exp(QRegularExpression::anchoredPattern(pluginFileInfo.fileName() +
                                                             PATTERN_BACKUP_REGEX));
  QRegularExpression exp2(
      QRegularExpression::anchoredPattern(pluginFileInfo.fileName() + "\\.(.*)"));
  const auto addBackupChoice = [&](const QString& suffix,
                                   const QString& displayName) {
    if (!isModListBackup && !hasCompleteLoadOrderBackup(suffix)) {
      incompleteLoadOrderBackupFound = true;
      return;
    }
    dialog.addChoice(QIcon(":/MO/gui/mainwindow/restore.svg"), displayName,
                     choiceDescription, suffix);
  };
  for (const QFileInfo& info : boost::adaptors::reverse(files)) {
    auto match  = exp.match(info.fileName());
    auto match2 = exp2.match(info.fileName());
    if (match.hasMatch()) {
      QDateTime time = QDateTime::fromString(match.captured(1), PATTERN_BACKUP_DATE);
      addBackupChoice(match.captured(1),
                      tr("Backup from %1").arg(time.toString()));
    } else if (match2.hasMatch()) {
      addBackupChoice(match2.captured(1), match2.captured(1));
    }
  }

  if (dialog.numChoices() == 0) {
    const QString message =
        incompleteLoadOrderBackupFound
            ? tr("No complete plugin order backup is available. The saved "
                 "plugin list, load order, and locked order must all be present.")
            : tr("There are no backups to restore");
    showProfileBackupPrompt(
        this, tr("No Backups"),
        incompleteLoadOrderBackupFound
            ? tr("No complete plugin-order backup is available.")
            : tr("There are no backups to restore."),
        message, tr("OK"));
    return QString();
  }

  resizeBackupSelectionDialog(dialog);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getChoiceData().toString();
  } else {
    return QString();
  }
}

void MainWindow::restorePluginOrderBackup()
{
  const QString pluginName =
      m_OrganizerCore.currentProfile()->getPluginsFileName();
  const QString choice = queryRestore(pluginName);
  if (!choice.isEmpty()) {
    const QString loadOrderName =
        m_OrganizerCore.currentProfile()->getLoadOrderFileName();
    const QString lockedName =
        m_OrganizerCore.currentProfile()->getLockedOrderFileName();
    const QStringList destinationFiles{pluginName, loadOrderName, lockedName};
    const QStringList backupFiles{pluginName + "." + choice,
                                  loadOrderName + "." + choice,
                                  lockedName + "." + choice};
    for (const QString& backupFile : backupFiles) {
      if (!QFileInfo(backupFile).isFile()) {
        showProfileBackupPrompt(
            this, tr("Restore failed"),
            tr("The plugin-order backup is incomplete or unavailable."),
            tr("The selected backup is no longer available. No files were "
               "restored."),
            tr("OK"));
        return;
      }
    }

    const QString backupName = QFileInfo(pluginName).fileName() + "." + choice;
    if (!showProfileBackupPrompt(
            this, tr("Confirm restore"), tr("Restore this plugin order?"),
            tr("This replaces the current plugin list and load order. MO2 does "
               "not back up the current version automatically.\n\n%1")
                .arg(backupName),
            tr("Restore backup"), tr("Cancel"))) {
      return;
    }

    struct RestoreEntry
    {
      QString destination;
      QString stagedBackup;
      QString stagedOriginal;
      bool hadOriginal = false;
    };

    const QString profileDirectory = QFileInfo(pluginName).absolutePath();
    QTemporaryDir stagingDirectory(
        QDir(profileDirectory).filePath(".mo2-load-order-restore-XXXXXX"));
    if (!stagingDirectory.isValid()) {
      showProfileBackupPrompt(
          this, tr("Restore failed"),
          tr("MO2 could not prepare the restore."),
          tr("Temporary recovery files could not be prepared. No files were "
             "restored."),
          tr("OK"));
      return;
    }

    QList<RestoreEntry> entries;
    entries.reserve(destinationFiles.size());
    QString stagingError;
    for (qsizetype index = 0; index < destinationFiles.size(); ++index) {
      RestoreEntry entry;
      entry.destination = destinationFiles.at(index);
      entry.stagedBackup = stagingDirectory.filePath(
          QStringLiteral("backup-%1").arg(index));
      entry.stagedOriginal = stagingDirectory.filePath(
          QStringLiteral("original-%1").arg(index));

      if (!QFile::copy(backupFiles.at(index), entry.stagedBackup)) {
        stagingError = tr("MO2 could not prepare the selected backup. "
                          "No files were restored.");
        break;
      }

      const QFileInfo destinationInfo(entry.destination);
      if (destinationInfo.exists()) {
        if (!destinationInfo.isFile()) {
          stagingError = tr("A destination for the plugin order is not a file. "
                            "No files were restored.");
          break;
        }
        entry.hadOriginal = true;
        if (!QFile::copy(entry.destination, entry.stagedOriginal)) {
          stagingError =
              tr("MO2 could not preserve the current plugin order before "
                 "restoring. No files were restored.");
          break;
        }
      }
      entries.append(std::move(entry));
    }

    if (!stagingError.isEmpty()) {
      showProfileBackupPrompt(this, tr("Restore failed"),
                              tr("MO2 could not prepare the restore."),
                              stagingError, tr("OK"));
      return;
    }

    for (qsizetype index = 0; index < entries.size(); ++index) {
      const RestoreEntry& entry = entries.at(index);
      if (shellCopy(entry.stagedBackup, entry.destination, true, this)) {
        continue;
      }

      const auto e = GetLastError();
      QStringList rollbackFailures;
      for (qsizetype rollbackIndex = index + 1; rollbackIndex > 0;
           --rollbackIndex) {
        const RestoreEntry& rollbackEntry = entries.at(rollbackIndex - 1);
        if (rollbackEntry.hadOriginal) {
          if (!shellCopy(rollbackEntry.stagedOriginal,
                         rollbackEntry.destination, true, this)) {
            rollbackFailures.append(rollbackEntry.destination);
          }
        } else if (QFileInfo::exists(rollbackEntry.destination) &&
                   !QFile::remove(rollbackEntry.destination)) {
          rollbackFailures.append(rollbackEntry.destination);
        }
      }

      QString errorMessage =
          tr("Failed to restore the plugin order backup. Errorcode: %1")
              .arg(QString::fromStdWString(formatSystemMessage(e)));
      if (rollbackFailures.isEmpty()) {
        errorMessage +=
            tr("\n\nMO2 restored the original files that were in place "
               "before this operation.");
      } else {
        stagingDirectory.setAutoRemove(false);
        errorMessage +=
            tr("\n\nMO2 could not fully recover these files: %1\n"
               "Recovery snapshots were kept at: %2")
                .arg(rollbackFailures.join(", "), stagingDirectory.path());
      }

      showProfileBackupPrompt(
          this, tr("Restore failed"),
          tr("The plugin-order backup could not be restored."), errorMessage,
          tr("OK"));
      m_OrganizerCore.refreshESPList(true);
      return;
    }

    m_OrganizerCore.refreshESPList(true);
  }
}

void MainWindow::createModListBackup()
{
  const ProfileBackupSelection selection =
      chooseProfileBackupContents(
          this, true, false, profileHasPluginOrderData(m_OrganizerCore));
  if (!selection.accepted) {
    return;
  }
  createProfileBackup(selection.modList, selection.pluginOrder);
}

void MainWindow::restoreModListBackup()
{
  QString modlistName = m_OrganizerCore.currentProfile()->getModlistFileName();
  QString choice      = queryRestore(modlistName);
  if (!choice.isEmpty()) {
    const QString backupPath = modlistName + "." + choice;
    const QString backupName = QFileInfo(backupPath).fileName();
    if (!QFileInfo(backupPath).isFile()) {
      showProfileBackupPrompt(this, tr("Restore failed"),
                              tr("The selected backup is no longer available."),
                              backupName, tr("OK"));
      return;
    }
    if (!showProfileBackupPrompt(
            this, tr("Confirm restore"), tr("Restore this mod list?"),
            tr("This replaces the current mod list and does not create a "
               "backup of it automatically.\n\n%1")
                .arg(backupName),
            tr("Restore backup"), tr("Cancel"))) {
      return;
    }
    if (!shellCopy(modlistName + "." + choice, modlistName, true, this)) {
      const auto e = GetLastError();
      showProfileBackupPrompt(
          this, tr("Restore failed"), tr("The backup could not be restored."),
          tr("Error code: %1").arg(formatSystemMessage(e)), tr("OK"));
    }
    m_OrganizerCore.refresh(false);
  }
}

void MainWindow::deleteProfileModListBackup()
{
  const QString modlistPath =
      m_OrganizerCore.currentProfile()->getModlistFileName();
  const QFileInfo modlistInfo(modlistPath);
  const QString pattern = modlistInfo.fileName() + ".*";
  const QFileInfoList files = modlistInfo.absoluteDir().entryInfoList(
      QStringList(pattern), QDir::Files, QDir::Name);

  SelectionDialog dialog(
      tr("Choose a saved mod list backup to delete. The current mod list will "
         "not be changed."),
      this, QSize(28, 28));
  const QString dialogTitle = tr("Delete mod list backup");
  dialog.setWindowTitle(dialogTitle);
  if (auto* titleLabel = dialog.findChild<QLabel*>(QStringLiteral("titleLabel"))) {
    titleLabel->setText(dialogTitle);
  }

  const QRegularExpression timestampPattern(
      QRegularExpression::anchoredPattern(
          QRegularExpression::escape(modlistInfo.fileName()) +
          PATTERN_BACKUP_REGEX));
  const QRegularExpression legacyPattern(
      QRegularExpression::anchoredPattern(
          QRegularExpression::escape(modlistInfo.fileName()) + "\\.(.*)"));

  for (const QFileInfo& info : boost::adaptors::reverse(files)) {
    const auto timestampMatch = timestampPattern.match(info.fileName());
    const auto legacyMatch = legacyPattern.match(info.fileName());
    if (timestampMatch.hasMatch()) {
      const QDateTime time = QDateTime::fromString(
          timestampMatch.captured(1), PATTERN_BACKUP_DATE);
      dialog.addChoice(QIcon(":/MO/gui/contextmenu/remove.svg"),
                       tr("Backup from %1").arg(time.toString()),
                       tr("Permanently delete this saved mod list backup."),
                       info.absoluteFilePath());
    } else if (legacyMatch.hasMatch()) {
      dialog.addChoice(QIcon(":/MO/gui/contextmenu/remove.svg"),
                       legacyMatch.captured(1),
                       tr("Permanently delete this saved mod list backup."),
                       info.absoluteFilePath());
    }
  }

  if (dialog.numChoices() == 0) {
    showProfileBackupPrompt(this, tr("No Backups"),
                            tr("There are no mod list backups to delete."),
                            tr("This profile has no saved mod list backups."),
                            tr("OK"));
    return;
  }

  resizeBackupSelectionDialog(dialog);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString selectedPath = dialog.getChoiceData().toString();
  const QFileInfo selectedInfo(selectedPath);
  if (selectedPath.isEmpty() ||
      selectedInfo.absolutePath() != modlistInfo.absolutePath() ||
      !selectedInfo.fileName().startsWith(modlistInfo.fileName() + ".")) {
    return;
  }

  if (!showProfileBackupPrompt(
          this, tr("Delete mod list backup"), tr("Delete this backup?"),
          tr("Permanently delete this saved mod list backup?\n\n%1")
              .arg(selectedInfo.fileName()),
          tr("Delete backup"), tr("Cancel"))) {
    return;
  }

  if (!QFile::remove(selectedInfo.absoluteFilePath())) {
    showProfileBackupPrompt(
        this, tr("Delete failed"), tr("The backup could not be deleted."),
        tr("Could not delete the selected mod list backup."), tr("OK"));
  } else {
    showProfileBackupPrompt(
        this, tr("Mod list backup deleted"), tr("Backup deleted"),
        tr("The mod list backup was permanently deleted."), tr("OK"));
  }
}

void MainWindow::deleteProfileLoadOrderBackup()
{
  const QString pluginPath =
      m_OrganizerCore.currentProfile()->getPluginsFileName();
  const QFileInfo pluginInfo(pluginPath);
  const QFileInfoList files = pluginInfo.absoluteDir().entryInfoList(
      QStringList(pluginInfo.fileName() + ".*"), QDir::Files, QDir::Name);

  SelectionDialog dialog(
      tr("Choose a saved plugin load order to delete. The current load order "
         "will not be changed."),
      this, QSize(28, 28));
  const QString dialogTitle = tr("Delete plugin order backup");
  dialog.setWindowTitle(dialogTitle);
  if (auto* titleLabel = dialog.findChild<QLabel*>(QStringLiteral("titleLabel"))) {
    titleLabel->setText(dialogTitle);
  }

  const QRegularExpression timestampPattern(
      QRegularExpression::anchoredPattern(
          QRegularExpression::escape(pluginInfo.fileName()) +
          PATTERN_BACKUP_REGEX));
  const QRegularExpression legacyPattern(
      QRegularExpression::anchoredPattern(
          QRegularExpression::escape(pluginInfo.fileName()) + "\\.(.*)"));

  for (const QFileInfo& info : boost::adaptors::reverse(files)) {
    const auto timestampMatch = timestampPattern.match(info.fileName());
    const auto legacyMatch = legacyPattern.match(info.fileName());
    if (timestampMatch.hasMatch()) {
      const QDateTime time = QDateTime::fromString(
          timestampMatch.captured(1), PATTERN_BACKUP_DATE);
      dialog.addChoice(QIcon(":/MO/gui/contextmenu/remove.svg"),
                       tr("Backup from %1").arg(time.toString()),
                       tr("Permanently delete this saved plugin load order."),
                       info.absoluteFilePath());
    } else if (legacyMatch.hasMatch()) {
      dialog.addChoice(QIcon(":/MO/gui/contextmenu/remove.svg"),
                       legacyMatch.captured(1),
                       tr("Permanently delete this saved plugin load order."),
                       info.absoluteFilePath());
    }
  }

  if (dialog.numChoices() == 0) {
    showProfileBackupPrompt(
        this, tr("No Backups"),
        tr("There are no plugin-order backups to delete."),
        tr("This profile has no saved plugin-order backups."), tr("OK"));
    return;
  }

  resizeBackupSelectionDialog(dialog);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString selectedPath = dialog.getChoiceData().toString();
  const QFileInfo selectedInfo(selectedPath);
  if (selectedPath.isEmpty() ||
      selectedInfo.absolutePath() != pluginInfo.absolutePath() ||
      !selectedInfo.fileName().startsWith(pluginInfo.fileName() + ".")) {
    return;
  }

  const QString suffix = selectedInfo.fileName().mid(pluginInfo.fileName().size());
  const QStringList backupPaths{
      pluginPath + suffix,
      m_OrganizerCore.currentProfile()->getLoadOrderFileName() + suffix,
      m_OrganizerCore.currentProfile()->getLockedOrderFileName() + suffix};
  if (!showProfileBackupPrompt(
          this, tr("Delete plugin order backup"), tr("Delete this backup?"),
          tr("Permanently delete this saved plugin-order backup?\n\n%1")
              .arg(selectedInfo.fileName()),
          tr("Delete backup"), tr("Cancel"))) {
    return;
  }

  QStringList failedFiles;
  bool removedAny = false;
  for (const QString& path : backupPaths) {
    if (!QFileInfo::exists(path)) {
      continue;
    }
    if (QFile::remove(path)) {
      removedAny = true;
    } else {
      failedFiles.append(QFileInfo(path).fileName());
    }
  }

  if (!failedFiles.isEmpty()) {
    showProfileBackupPrompt(
        this, tr("Delete failed"),
        tr("Some files could not be deleted."),
        tr("The plugin-order backup is incomplete. These files remain:\n%1")
            .arg(failedFiles.join("\n")),
        tr("OK"));
  } else if (removedAny) {
    showProfileBackupPrompt(
        this, tr("Plugin order backup deleted"), tr("Backup deleted"),
        tr("The plugin-order backup was permanently deleted."), tr("OK"));
  }
}

void MainWindow::on_managedArchiveLabel_linkHovered(const QString&)
{
  QToolTip::showText(QCursor::pos(), ui->managedArchiveLabel->toolTip());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  // Accept copy or move drags to the download window. Link drags are not
  // meaningful (Well, they are - we could drop a link in the download folder,
  // but you need privileges to do that).
  if (ui->downloadTab->isVisible() &&
      (event->proposedAction() == Qt::CopyAction ||
       event->proposedAction() == Qt::MoveAction) &&
      event->answerRect().intersects(ui->downloadTab->rect())) {

    // If I read the documentation right, this won't work under a motif windows
    // manager and the check needs to be done at the drop. However, that means
    // the user might be allowed to drop things which we can't sanely process
    QMimeData const* data = event->mimeData();

    if (data->hasUrls()) {
      QStringList extensions =
          m_OrganizerCore.installationManager()->getSupportedExtensions();

      // This is probably OK - scan to see if these are moderately sane archive
      // types
      QList<QUrl> urls = data->urls();
      bool ok          = true;
      for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
          QString local = url.toLocalFile();
          bool fok      = false;
          for (auto ext : extensions) {
            if (local.endsWith(ext, Qt::CaseInsensitive)) {
              fok = true;
              break;
            }
          }
          if (!fok) {
            ok = false;
            break;
          }
        }
      }
      if (ok) {
        event->accept();
      }
    }
  }
}

void MainWindow::dropLocalFile(const QUrl& url, const QString& outputDir, bool move)
{
  QFileInfo file(url.toLocalFile());
  if (!file.exists()) {
    log::warn("invalid source file: {}", file.absoluteFilePath());
    return;
  }
  QString target = outputDir + "/" + file.fileName();
  if (QFile::exists(target)) {
    QMessageBox box(QMessageBox::Question, file.fileName(),
                    tr("A file with the same name has already been downloaded. "
                       "What would you like to do?"));
    box.addButton(tr("Overwrite"), QMessageBox::ActionRole);
    box.addButton(tr("Rename new file"), QMessageBox::YesRole);
    box.addButton(tr("Ignore file"), QMessageBox::RejectRole);

    box.exec();
    switch (box.buttonRole(box.clickedButton())) {
    case QMessageBox::RejectRole:
      return;
    case QMessageBox::ActionRole:
      break;
    default:
    case QMessageBox::YesRole:
      target = m_OrganizerCore.downloadManager()->getDownloadFileName(file.fileName());
      break;
    }
  }

  bool success = false;
  if (move) {
    success = shellMove(file.absoluteFilePath(), target, true, this);
  } else {
    success = shellCopy(file.absoluteFilePath(), target, true, this);
  }
  if (!success) {
    const auto e = GetLastError();
    log::error("file operation failed: {}", formatSystemMessage(e));
  }
}

void MainWindow::dropEvent(QDropEvent* event)
{
  Qt::DropAction action = event->proposedAction();
  QString outputDir     = m_OrganizerCore.downloadManager()->getOutputDirectory();
  if (action == Qt::MoveAction) {
    // Tell windows I'm taking control and will delete the source of a move.
    event->setDropAction(Qt::TargetMoveAction);
  }
  for (const QUrl& url : event->mimeData()->urls()) {
    if (url.isLocalFile()) {
      dropLocalFile(url, outputDir, action == Qt::MoveAction);
    } else {
      m_OrganizerCore.downloadManager()->startDownloadURLs(QStringList() << url.url());
    }
  }
  event->accept();
}
