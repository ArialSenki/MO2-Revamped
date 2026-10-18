// Fluent v4 main window layer: the page header, the single "more" button of
// the mod list card, the Run button states, and the Fluent command bar
// defaults. The widgets themselves live in mainwindow.ui; this file only adds
// behavior that a stylesheet cannot express.

#include "fluentmotion.h"
#include "mainwindow.h"
#include "organizercore.h"
#include "settings.h"
#include "uilocker.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTimer>
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
  ui->pageTitle->setText(tr("Mods"));

  setRunState(RunState::Idle);

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

  QIcon icon;
  for (int size : {16, 20, 24, 32}) {
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
