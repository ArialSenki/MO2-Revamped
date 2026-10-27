// Fluent v4 main window layer: the page header, the single "more" button of
// the mod list card, the Run button states, and the Fluent command bar
// defaults. The widgets themselves live in mainwindow.ui; this file only adds
// behavior that a stylesheet cannot express.

#include "createinstancedialog.h"
#include "fluentdashboard.h"
#include "fluenticons.h"
#include "fluentmotion.h"
#include "revampedtheme.h"
#include "mainwindow.h"
#include "instancemanager.h"
#include "instancemanagerdialog.h"
#include "organizercore.h"
#include "shared/util.h"
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
#include <algorithm>
#include <map>

namespace
{
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

    const QIcon source = FluentIcons::icon(m_path);
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

}  // namespace

void MainWindow::setupFluentLayout()
{
  FluentMotion::setEnabled(m_OrganizerCore.settings().interface().animationsEnabled());

  // the Run button follows the process from the moment it is clicked; other
  // launches (shortcuts, plugins) leave it alone
  setRunState(RunState::Idle);
  m_OrganizerCore.onAboutToRun([this](auto&&...) {
    if (m_runState != RunState::Mounting) {
      return true;
    }

    setRunState(RunState::Starting);
    waitForRunningProcess();
    return true;
  });

  // classic themes keep the original window; switching between Fluent and a
  // classic theme takes effect after a restart
  if (!FluentIcons::active()) {
    return;
  }

  // Mica needs a translucent window surface, which Qt only honors before the
  // native window exists. MOApplication turns the backdrop on and off with
  // the theme.
  if (QOperatingSystemVersion::current() >=
          QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621)) {
    setAttribute(Qt::WA_TranslucentBackground);
  }

  // one "more" button replaces the list options gear and the folder button;
  // both stay in the .ui (hidden) because other code still sets their menus
  ui->listOptionsBtn->hide();
  ui->openFolderMenu->hide();

  m_moreActionsButton = new QToolButton(ui->layoutWidget);
  m_moreActionsButton->setObjectName(QStringLiteral("moreActionsBtn"));
  m_moreActionsButton->setFont(FluentIcons::font(15));
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
  new IconTint(ui->startButton, QStringLiteral(":/MO/gui/mainwindow/run.svg"));
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
  folders->setIcon(FluentIcons::icon(QStringLiteral(":/MO/gui/contextmenu/explorer.svg")));

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
  if (!FluentIcons::active() || geometry.fluentToolbarApplied()) {
    return;
  }

  setToolbarSize(iconSize);
  setToolbarButtonStyle(Qt::ToolButtonTextBesideIcon);
  geometry.setFluentToolbarApplied(true);
}

QIcon MainWindow::fluentToolIcon(const QString& name)
{
  // tool plugins without an icon get a Fluent glyph picked from the tool
  // name, so the same tool keeps the same mark
  struct Mark
  {
    const char* keyword;
    char16_t glyph;
  };

  static const Mark marks[] = {
      {"startup", 0xE768},  // Play
      {"save", 0xE74E},     // Save
      {"dll", 0xE943},      // Code
      {"native", 0xE943},   // Code
      {"ini", 0xE70F},      // Edit
  };

  for (const auto& mark : marks) {
    if (name.contains(QLatin1String(mark.keyword), Qt::CaseInsensitive)) {
      return FluentIcons::glyph(mark.glyph);
    }
  }

  return FluentIcons::glyph(0xE90F);  // Repair
}

void MainWindow::setupFluentCommandBar()
{
  QToolBar* bar = ui->toolBar;

  // the left group keeps MO2's original order (install, Nexus, profiles,
  // refresh, executables, tools, settings); links and help move into one
  // overflow button at the far right. The actions stay on the window so their
  // shortcuts keep working.
  const QList<QAction*> overflow = {ui->actionEndorseMO, ui->actionUpdate,
                                    ui->actionHelp};
  for (QAction* action : overflow) {
    bar->removeAction(action);
    addAction(action);
  }

  auto* menu = new QMenu(this);
  menu->addAction(ui->actionEndorseMO);
  menu->addAction(ui->actionUpdate);
  menu->addAction(ui->actionHelp);

  m_fluentOverflowAction = new QAction(tr("More"), this);
  m_fluentOverflowAction->setObjectName(QStringLiteral("actionFluentMore"));
  m_fluentOverflowAction->setToolTip(tr("Links, updates and help"));
  m_fluentOverflowAction->setMenu(menu);
  bar->addAction(m_fluentOverflowAction);
  if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(m_fluentOverflowAction))) {
    button->setPopupMode(QToolButton::InstantPopup);
    button->setObjectName(QStringLiteral("actionFluentMore"));
  }

  // Manage Instances opens the instances to switch to; the manager itself
  // is the menu's last entry
  auto* instances = new QMenu(this);
  instances->setObjectName(QStringLiteral("instanceSwitchMenu"));
  ui->actionChange_Game->setMenu(instances);
  if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(ui->actionChange_Game))) {
    button->setPopupMode(QToolButton::InstantPopup);
  }
  connect(instances, &QMenu::aboutToShow, this, [this, instances] {
    fillInstanceSwitchMenu(instances);
  });

  // low priority actions show their icon only next to labeled ones
  for (QAction* action : {ui->actionNotifications, m_fluentOverflowAction}) {
    action->setPriority(QAction::LowPriority);
  }

  applyFluentActionIcons();

  // the menus of the command bar (Tools, Nexus...) open with a turning chevron
  if (FluentIcons::active()) {
    for (QAction* action : bar->actions()) {
      auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(action));
      if (button != nullptr && action != m_fluentOverflowAction) {
        // the command bar's labels have no descenders, so the chevron sits
        // a little higher than in the instance manager
        FluentMotion::animateMenuChevron(button, 10.0, 0.5);
      }
    }
  }
}

void MainWindow::fillInstanceSwitchMenu(QMenu* menu)
{
  menu->clear();

  struct Entry
  {
    QString path;
    bool portable;
    QString name;
    QString group;
    bool active;
  };

  // the instances by game, the games and the names in alphabetical order
  std::map<QString, std::pair<QString, std::vector<Entry>>> games;
  const auto& manager = InstanceManager::singleton();
  auto add            = [&](const QString& path, bool portable) {
    Instance instance(path, portable);
    instance.readFromIni();
    QString game = instance.gameName();
    if (game.isEmpty()) {
      game = tr("Unknown game");
    }
    auto& [title, entries] = games[game.toCaseFolded()];
    title                  = game;
    entries.push_back({path, portable, instance.displayName(),
                       GlobalSettings::instanceValue(QStringLiteral("InstanceGroups"),
                                                     instance.instanceIdentifier()),
                       instance.isActive()});
  };
  for (auto&& d : manager.globalInstancePaths()) {
    add(d, false);
  }
  for (auto&& d : manager.portableInstancePaths()) {
    add(d, true);
  }

  for (auto& [key, game] : games) {
    auto& [title, entries] = game;
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
      return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    // the theme hides section titles, so the game is a dimmed entry
    if (!menu->isEmpty()) {
      menu->addSeparator();
    }
    menu->addAction(QString(title).replace('&', QStringLiteral("&&")))->setEnabled(false);

    for (const auto& entry : entries) {
      // the group shows where a shortcut would, at the menu's right edge
      QString text = QString(entry.name).replace('&', QStringLiteral("&&"));
      // a game mode's instance is named after its group's instance already
      if (!entry.group.isEmpty() &&
          !entry.name.startsWith(entry.group + QStringLiteral(" ("))) {
        text += QLatin1Char('\t') + entry.group;
      }
      auto* action = menu->addAction(text);
      action->setCheckable(true);
      action->setChecked(entry.active);
      connect(action, &QAction::triggered, this, [this, entry] {
        if (entry.active) {
          return;
        }
        const Instance to(entry.path, entry.portable);
        if (!InstanceManagerDialog::confirmSwitch(this, to)) {
          return;
        }
        InstanceManager::singleton().setCurrentInstance(to);
        ExitModOrganizer(Exit::Restart);
      });
    }
  }

  menu->addSeparator();
  menu->addAction(FluentIcons::glyph(0xE710), tr("New instance\u2026"), this, [this] {
    CreateInstanceDialog dlg(m_PluginContainer, Settings::maybeInstance(), this);
    dlg.exec();
  });
  menu->addAction(FluentIcons::glyph(0xE8AB), tr("Manage instances\u2026"), this,
                  &MainWindow::on_actionChange_Game_triggered);
  FluentIcons::apply(menu);
}

void MainWindow::applyFluentActionIcons()
{
  // Fluent glyphs in the theme's ink replace the style sheet's colored
  // icons, so the command bar reads as one set with the rest of the window
  if (!FluentIcons::active()) {
    return;
  }

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
      {ui->actionUpdate, 0xE8A7},              // OpenInNewWindow: the repository
      {ui->actionHelp, 0xE897},                // Help
      {m_fluentOverflowAction, MoreGlyph},
  };

  for (const auto& [action, glyph] : glyphs) {
    if (action == nullptr) {
      continue;
    }
    const QIcon icon = FluentIcons::glyph(glyph);
    action->setIcon(icon);
    if (auto* button =
            qobject_cast<QAbstractButton*>(ui->toolBar->widgetForAction(action))) {
      button->setIcon(icon);

    }

    // Qt leaves only 4px between a tool button's icon and its text; the
    // command bars of Windows 11 leave 8px, so labeled buttons get a space.
    // The icon text is only shown on the toolbar; menus use the plain text.
    // (retranslateUi() sets the short labels again, then this runs again)
    if (action->priority() != QAction::LowPriority &&
        !action->iconText().startsWith(QLatin1Char(' '))) {
      action->setIconText(QLatin1Char(' ') + action->iconText());
    }
  }
}

QIcon MainWindow::fluentNotificationIcon(std::size_t count) const
{
  // the bell, with a Windows 11 attention dot while there are notifications
  const QSize canvas = ui->toolBar->iconSize();
  const qreal dpr    = devicePixelRatioF();
  const int extent   = qMin(canvas.width(), canvas.height());

  QPixmap pixmap(canvas * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);

  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  const QRect bell((canvas.width() - extent) / 2, (canvas.height() - extent) / 2,
                   extent, extent);
  FluentIcons::glyph(0xEA8F).paint(&p, bell);

  if (count > 0) {
    // an accent dot at the top right, cut out of the bell like the badges of
    // Windows 11; the count itself is in the tooltip and the status bar
    const qreal dot = qMax(5.0, extent * 0.42);
    const QRectF badge(bell.right() + 1 - dot, bell.top(), dot, dot);
    const qreal gap = qMax(1.0, extent / 12.0);

    p.setPen(Qt::NoPen);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setBrush(Qt::black);
    p.drawEllipse(badge.adjusted(-gap, -gap, gap, gap));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setBrush(FluentIcons::color(FluentIcons::Tone::Accent));
    p.drawEllipse(badge);
  }
  p.end();

  return QIcon(pixmap);
}
