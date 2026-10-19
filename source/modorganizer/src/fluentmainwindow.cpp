// Fluent v4 main window layer: the page header, the single "more" button of
// the mod list card, the Run button states, and the Fluent command bar
// defaults. The widgets themselves live in mainwindow.ui; this file only adds
// behavior that a stylesheet cannot express.

#include "fluentdashboard.h"
#include "fluentmotion.h"
#include "mainwindow.h"
#include "organizercore.h"
#include "settings.h"
#include "uilocker.h"
#include "ui_mainwindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace
{
// Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on Windows 10; both use
// the same code points
QFont fluentIconFont(int pointSize)
{
  QFont f;
  f.setFamilies({QStringLiteral("Segoe Fluent Icons"),
                 QStringLiteral("Segoe MDL2 Assets")});
  f.setPointSize(pointSize);
  return f;
}

constexpr char16_t MoreGlyph = 0xE712;

// keeps a monochrome icon in the text color of its button, so the Run glyph
// is white on the light theme's accent and black on the dark theme's
class IconTint : public QObject
{
public:
  IconTint(QToolButton* button, QString path)
      : QObject(button), m_button(button), m_path(std::move(path))
  {
    button->installEventFilter(this);
    apply();
  }

protected:
  bool eventFilter(QObject* o, QEvent* e) override
  {
    if (o == m_button && (e->type() == QEvent::PaletteChange ||
                          e->type() == QEvent::StyleChange)) {
      apply();
    }
    return false;
  }

private:
  QToolButton* m_button;
  QString m_path;
  QColor m_applied;

  void apply()
  {
    const QColor color = m_button->palette().color(QPalette::ButtonText);
    if (color == m_applied) {
      return;
    }
    m_applied = color;

    const QIcon source(m_path);
    QIcon tinted;
    for (int size : {16, 20, 24, 32, 48}) {
      QPixmap pixmap = source.pixmap(size, size);
      QPainter p(&pixmap);
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(pixmap.rect(), color);
      p.end();
      tinted.addPixmap(pixmap);
    }

    // setIcon() triggers no palette change, so this does not recurse
    m_button->setIcon(tinted);
  }
};

void repolish(QWidget* w)
{
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}

// a Segoe Fluent Icons glyph as a monochrome icon in the given color
QIcon glyphIcon(char16_t glyph, const QColor& color)
{
  QIcon icon;
  for (int size : {16, 20, 24, 32, 48}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = fluentIconFont(10);
    f.setPixelSize(size - 2);
    p.setFont(f);
    p.setPen(color);
    p.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(glyph)));
    p.end();

    icon.addPixmap(pixmap);
  }
  return icon;
}
}  // namespace

void MainWindow::setupFluentLayout()
{
  FluentMotion::setEnabled(m_OrganizerCore.settings().interface().animationsEnabled());

  // Mica needs a translucent window surface, which Qt only honors before the
  // native window exists. MOApplication turns the backdrop on and off with
  // the theme; switching into a Fluent theme takes effect after a restart.
  if (qApp->property("revampedFluent").toBool() &&
      QOperatingSystemVersion::current() >=
          QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621)) {
    setAttribute(Qt::WA_TranslucentBackground);
  }

  // one "more" button replaces the list options gear and the folder button;
  // both stay in the .ui (hidden) because other code still sets their menus
  ui->listOptionsBtn->hide();
  ui->openFolderMenu->hide();

  m_moreActionsButton = new QToolButton(ui->layoutWidget);
  m_moreActionsButton->setObjectName(QStringLiteral("moreActionsBtn"));
  m_moreActionsButton->setFont(fluentIconFont(11));
  m_moreActionsButton->setText(QString(QChar(MoreGlyph)));
  m_moreActionsButton->setToolTip(tr("Mod list actions, backups, and folders"));
  m_moreActionsButton->setAccessibleName(tr("More actions"));
  m_moreActionsButton->setPopupMode(QToolButton::InstantPopup);
  m_moreActionsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

  const int gearIndex = ui->horizontalLayout_6->indexOf(ui->listOptionsBtn);
  ui->horizontalLayout_6->insertWidget(gearIndex, m_moreActionsButton);
  updateMoreActionsMenu();

  ui->activeModslabel->setText(tr("Active"));

  m_fluentDashboard = new FluentDashboard(ui, m_OrganizerCore, this);
  setupFluentCommandBar();

  setRunState(RunState::Idle);
  new IconTint(ui->startButton, QStringLiteral(":/MO/gui/mainwindow/run.svg"));

  // the Run button follows the process from the moment it is clicked; other
  // launches (shortcuts, plugins) leave it alone
  m_OrganizerCore.onAboutToRun([this](auto&&...) {
    if (m_runState != RunState::Mounting) {
      return true;
    }

    setRunState(RunState::Starting);
    waitForRunningProcess();
    return true;
  });
}

void MainWindow::waitForRunningProcess()
{
  // a plugin connected after this one can still cancel the launch, so only
  // show "Running" once MO2 has locked its interface for the started process
  QTimer::singleShot(150, this, [this] {
    if (m_runState != RunState::Starting) {
      return;
    }

    if (UILocker::instance().locked()) {
      setRunState(RunState::Running);
    } else {
      waitForRunningProcess();
    }
  });
}

void MainWindow::updateMoreActionsMenu()
{
  if (m_moreActionsButton == nullptr) {
    return;
  }

  QMenu* menu    = createModListOptionsMenu();
  QMenu* folders = openFolderMenu();
  folders->setParent(menu, folders->windowFlags());
  folders->setTitle(tr("Open folder"));
  folders->setIcon(QIcon(QStringLiteral(":/MO/gui/contextmenu/explorer.svg")));

  // the list menu clears and repopulates itself in its own aboutToShow
  // handler, which was connected first; append the folders after it
  connect(menu, &QMenu::aboutToShow, folders, [menu, folders] {
    menu->addSeparator();
    menu->addMenu(folders);
  });

  QMenu* old = m_moreActionsButton->menu();
  m_moreActionsButton->setMenu(menu);
  if (old != nullptr) {
    old->deleteLater();
  }
}

void MainWindow::setRunState(RunState state)
{
  m_runState = state;

  QString text, property;
  switch (state) {
  case RunState::Idle:
    text     = tr("Run");
    property = QStringLiteral("idle");
    break;
  case RunState::Mounting:
    text     = tr("Mounting\xe2\x80\xa6");
    property = QStringLiteral("busy");
    break;
  case RunState::Starting:
    text     = tr("Starting\xe2\x80\xa6");
    property = QStringLiteral("busy");
    break;
  case RunState::Running:
    text     = tr("Running");
    property = QStringLiteral("running");
    break;
  }

  ui->startButton->setText(text);
  ui->startButton->setProperty("runState", property);
  repolish(ui->startButton);

  // the launch runs synchronously until MO2 starts waiting for the process, so
  // paint the new state right away
  ui->startButton->repaint();
}

void MainWindow::applyFluentToolbarDefaults(const QSize& iconSize)
{
  auto& geometry = m_OrganizerCore.settings().geometry();
  if (geometry.fluentToolbarApplied()) {
    return;
  }

  setToolbarSize(iconSize);
  setToolbarButtonStyle(Qt::ToolButtonTextBesideIcon);
  geometry.setFluentToolbarApplied(true);
}

QIcon MainWindow::fluentToolIcon(const QString& name)
{
  // tool plugins without an icon get a colored Fluent glyph, picked from the
  // tool name so the same tool keeps the same mark
  struct Mark
  {
    const char* keyword;
    char16_t glyph;
    QColor color;
  };

  static const Mark marks[] = {
      {"startup", 0xE768, QColor(0x00, 0x78, 0xD4)},  // Play
      {"save", 0xE74E, QColor(0x10, 0x7C, 0x10)},     // Save
      {"dll", 0xE943, QColor(0x87, 0x64, 0xB8)},      // Code
      {"ini", 0xE70F, QColor(0xCA, 0x50, 0x10)},      // Edit
      {"fnis", 0xE805, QColor(0x00, 0x99, 0xBC)},     // Walk
  };

  char16_t glyph = 0xE90F;  // Repair
  QColor color(0x5D, 0x5D, 0x5D);
  for (const auto& mark : marks) {
    if (name.contains(QLatin1String(mark.keyword), Qt::CaseInsensitive)) {
      glyph = mark.glyph;
      color = mark.color;
      break;
    }
  }

  return glyphIcon(glyph, color);
}

void MainWindow::setupFluentCommandBar()
{
  QToolBar* bar = ui->toolBar;

  // the command bar keeps the everyday actions; setup, links and help move
  // into one overflow button at the far right. The actions stay on the window
  // so their shortcuts keep working.
  const QList<QAction*> overflow = {ui->actionAdd_Profile, ui->actionModify_Executables,
                                    ui->action_Refresh, ui->actionEndorseMO,
                                    ui->actionUpdate, ui->actionHelp};
  for (QAction* action : overflow) {
    bar->removeAction(action);
    addAction(action);
  }

  bar->removeAction(ui->actionSettings);
  bar->addAction(ui->actionSettings);

  auto* menu = new QMenu(this);
  menu->addAction(ui->actionAdd_Profile);
  menu->addAction(ui->actionModify_Executables);
  menu->addAction(ui->action_Refresh);
  menu->addSeparator();
  menu->addAction(ui->actionEndorseMO);
  menu->addAction(ui->actionUpdate);
  menu->addAction(ui->actionHelp);

  m_fluentOverflowAction = new QAction(tr("More"), this);
  m_fluentOverflowAction->setObjectName(QStringLiteral("actionFluentMore"));
  m_fluentOverflowAction->setToolTip(tr("Profiles, executables, refresh, links and help"));
  m_fluentOverflowAction->setMenu(menu);
  bar->addAction(m_fluentOverflowAction);
  if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(m_fluentOverflowAction))) {
    button->setPopupMode(QToolButton::InstantPopup);
  }

  // low priority actions show their icon only next to labeled ones
  for (QAction* action : {ui->actionNotifications, ui->actionSettings,
                          m_fluentOverflowAction}) {
    action->setPriority(QAction::LowPriority);
  }

  applyFluentActionIcons();
}

void MainWindow::applyFluentActionIcons()
{
  // monochrome Fluent glyphs in the toolbar's text color replace the
  // stylesheet's colored icons, so the command bar reads as one set
  const QColor color = ui->toolBar->palette().color(QPalette::ButtonText);

  const std::pair<QAction*, char16_t> glyphs[] = {
      {ui->actionChange_Game, 0xE8AB},         // Switch
      {ui->actionInstallMod, 0xE896},          // Download
      {ui->actionNexus, 0xE774},               // Globe
      {ui->actionModPage, 0xE774},             // Globe
      {ui->actionAdd_Profile, 0xE716},         // People
      {ui->action_Refresh, 0xE72C},            // Refresh
      {ui->actionModify_Executables, 0xE71D},  // AllApps
      {ui->actionTool, 0xE90F},                // Repair
      {ui->actionSettings, 0xE713},            // Settings
      {ui->actionEndorseMO, 0xEB51},           // Heart
      {ui->actionNotifications, 0xEA8F},       // Ringer
      {ui->actionUpdate, 0xE943},              // Code
      {ui->actionHelp, 0xE897},                // Help
      {m_fluentOverflowAction, MoreGlyph},
  };

  for (const auto& [action, glyph] : glyphs) {
    if (action == nullptr) {
      continue;
    }
    const QIcon icon = glyphIcon(glyph, color);
    action->setIcon(icon);
    if (auto* button =
            qobject_cast<QAbstractButton*>(ui->toolBar->widgetForAction(action))) {
      button->setIcon(icon);
    }
    if (action == ui->actionNotifications) {
      m_originalNotificationIcon = icon;
    }
  }
}
