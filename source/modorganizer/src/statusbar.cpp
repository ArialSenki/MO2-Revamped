#include "fluenticons.h"
#include "fluentpanels.h"
#include "statusbar.h"
#include <QMouseEvent>
#include "instancemanager.h"
#include "nexusinterface.h"
#include "organizercore.h"
#include "settings.h"
#include "ui_mainwindow.h"

#include <QAccessibleWidget>
#include <QKeyEvent>
#include <QPainter>
#include <QStyle>

namespace
{
// Notifications and Update act as buttons: screen readers announce them as
// buttons and can press them
class StatusBarActionAccessible : public QAccessibleWidget
{
public:
  explicit StatusBarActionAccessible(QWidget* widget)
      : QAccessibleWidget(widget, QAccessible::Button)
  {}

  QStringList actionNames() const override { return {pressAction()}; }

  void doAction(const QString& name) override
  {
    if (name == pressAction()) {
      static_cast<StatusBarAction*>(widget())->trigger();
    }
  }
};

QAccessibleInterface* statusBarActionAccessible(const QString& className, QObject* object)
{
  if (className == QLatin1String("StatusBarAction") && object != nullptr &&
      object->isWidgetType()) {
    return new StatusBarActionAccessible(static_cast<QWidget*>(object));
  }
  return nullptr;
}
}  // namespace

StatusBar::StatusBar(QWidget* parent)
    : QStatusBar(parent), ui(nullptr), m_normal(new FluentPanels::ElidedLabel),
      m_progress(new QProgressBar), m_progressSpacer1(new QWidget),
      m_progressSpacer2(new QWidget), m_notifications(nullptr), m_update(nullptr),
      m_api(new QLabel)
{}

void StatusBar::setup(Ui::MainWindow* mainWindowUI, const Settings& settings)
{
  ui              = mainWindowUI;

  // the window's margin keeps it clear of the edge
  setContentsMargins(0, 0, 0, 0);
  static const bool accessibleInstalled = [] {
    QAccessible::installFactory(statusBarActionAccessible);
    return true;
  }();
  Q_UNUSED(accessibleInstalled);
  m_notifications = new StatusBarAction(ui->actionNotifications);
  m_update        = new StatusBarAction(ui->actionUpdate);

  // long game, instance and profile names shorten instead of keeping the
  // window from narrowing; the whole text is in the tool tip
  m_normal->setElideMode(Qt::ElideRight);
  addWidget(m_normal, 1);

  m_progressSpacer1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  addPermanentWidget(m_progressSpacer1, 0);
  addPermanentWidget(m_progress);

  m_progressSpacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  addPermanentWidget(m_progressSpacer2, 0);

  addPermanentWidget(m_notifications);
  addPermanentWidget(m_update);
  addPermanentWidget(m_api);

  m_progress->setTextVisible(true);
  m_progress->setRange(0, 100);
  m_progress->setMaximumWidth(300);
  m_progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  m_update->set(false);
  m_notifications->set(false);

  m_api->setObjectName("apistats");

  clearMessage();
  setProgress(-1);
  setAPI({}, {});

  checkSettings(settings);
}

void StatusBar::setProgress(int percent)
{
  bool visible = true;

  if (percent < 0 || percent >= 100) {
    clearMessage();
    visible = false;
  } else {
    showMessage(QObject::tr("Loading..."));
    m_progress->setValue(percent);
  }

  m_progress->setVisible(visible);
  m_progressSpacer1->setVisible(visible);
  m_progressSpacer2->setVisible(visible);
}

void StatusBar::setNotifications(bool hasNotifications)
{
  if (m_notifications) {
    m_notifications->set(hasNotifications);
  }
}

void StatusBar::setAPI(const APIStats& stats, const APIUserAccount& user)
{
  QString text;
  QString apiState;

  if (user.type() == APIUserAccountTypes::None) {
    text     = tr("Nexus: not signed in");
    m_api->setToolTip(tr("Sign in to Nexus Mods in Settings to check for updates."));
    apiState = QStringLiteral("offline");
  } else {
    // one short figure in the bar, the full breakdown on hover
    // once the daily allowance is used up Nexus still answers from the hourly
    // one, which is then what limits MO2
    text = user.limits().remainingDailyRequests > 0
               ? tr("Nexus API: %1 left today").arg(user.limits().remainingDailyRequests)
               : tr("Nexus API: %1 left this hour")
                     .arg(user.limits().remainingHourlyRequests);
    // the figures keep their existing translation; the explanation is its own
    m_api->setToolTip(tr("Queued requests: %1\nRemaining today: %2\nRemaining this hour: %3")
                          .arg(stats.requestsQueued)
                          .arg(user.limits().remainingDailyRequests)
                          .arg(user.limits().remainingHourlyRequests) +
                      QStringLiteral("\n\n") +
                      tr("Nexus allows a number of requests per day and per hour;\n"
                         "downloads, update checks and mod information use them."));

    if (user.remainingRequests() > 500) {
      apiState = QStringLiteral("healthy");
    } else if (user.remainingRequests() > 200) {
      apiState = QStringLiteral("warning");
    } else {
      apiState = QStringLiteral("low");
    }
  }

  m_api->setText(text);
  if (m_api->property("apiState").toString() != apiState) {
    m_api->setProperty("apiState", apiState);
    m_api->style()->unpolish(m_api);
    m_api->style()->polish(m_api);
  }
  m_api->setAutoFillBackground(true);

  // the new state can change padding and border; without this the label keeps
  // its previous width and the end of the text and the border get clipped
  m_api->updateGeometry();
}

void StatusBar::setUpdateAvailable(bool b)
{
  m_update->set(b);
}

void StatusBar::checkSettings(const Settings& settings)
{
  m_api->setVisible(!settings.interface().hideAPICounter());
}

void StatusBar::updateNormalMessage(OrganizerCore& core)
{
  QString game;

  if (core.managedGame()) {
    game = core.managedGame()->displayGameName();
  } else {
    game = tr("Unknown game");
  }

  QString instance = "?";
  if (auto i = InstanceManager::singleton().currentInstance())
    instance = i->displayName();

  QString profile = core.profileName();

  const auto s = QString("%1 - %2 - %3").arg(game, instance, profile);

  m_normal->setFullText(s);
}

void StatusBar::showEvent(QShowEvent*)
{
  visibilityChanged(true);
}

void StatusBar::hideEvent(QHideEvent*)
{
  visibilityChanged(false);
}

void StatusBar::visibilityChanged(bool visible)
{
  // the central widget typically has no bottom padding because the status bar
  // is more than enough, but when it's hidden, the bottom widget (currently
  // the log) touches the bottom border of the window, which looks ugly
  //
  // when hiding the statusbar, the central widget is given the same border
  // margin as it has on the top (which is typically 6, as it's the default from
  // the qt designer)

  auto m = ui->centralWidget->layout()->contentsMargins();

  if (visible) {
    m.setBottom(0);
  } else {
    m.setBottom(m.top());
  }

  ui->centralWidget->layout()->setContentsMargins(m);
}

StatusBarAction::StatusBarAction(QAction* action)
    : m_action(action), m_icon(new QLabel), m_text(new QLabel)
{
  setLayout(new QHBoxLayout);
  layout()->setContentsMargins(0, 0, 0, 0);
  layout()->addWidget(m_icon);
  layout()->addWidget(m_text);
  setCursor(Qt::PointingHandCursor);
  // reachable with Tab and run with Enter or Space, like a button
  setFocusPolicy(Qt::TabFocus);

  // a theme change gives the action a new icon; the label keeps a copy
  QObject::connect(m_action, &QAction::changed, this, [this] {
    if (!isHidden()) {
      set(true);
    }
  });
}

void StatusBarAction::set(bool visible)
{
  if (visible) {
    m_icon->setPixmap(m_action->icon().pixmap(QSize(16, 16), devicePixelRatioF()));
    m_text->setText(cleanupActionText(m_action->text()));
    setToolTip(m_action->toolTip());
    setAccessibleName(m_text->text());
    setAccessibleDescription(m_action->toolTip());
  }

  setVisible(visible);
}

void StatusBarAction::trigger()
{
  if (m_action->isEnabled()) {
    m_action->trigger();
  }
}

bool StatusBarAction::event(QEvent* e)
{
  // the icon is drawn for the screen's scale: moved to another screen, it is
  // drawn again
  if (e->type() == QEvent::DevicePixelRatioChange && !isHidden()) {
    set(true);
  }
  return QWidget::event(e);
}

// one click, like the other items of the status bar
void StatusBarAction::mouseReleaseEvent(QMouseEvent* e)
{
  if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
    trigger();
  }
}

void StatusBarAction::keyPressEvent(QKeyEvent* e)
{
  // the keypad's Enter comes with the keypad modifier; a held key runs it once
  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter ||
       e->key() == Qt::Key_Space) &&
      (e->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
    if (!e->isAutoRepeat()) {
      trigger();
    }
    e->accept();
    return;
  }

  QWidget::keyPressEvent(e);
}

void StatusBarAction::paintEvent(QPaintEvent* e)
{
  QWidget::paintEvent(e);

  // the keyboard focus shows as a ring in the accent, easy to find while tabbing
  if (hasFocus()) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(FluentIcons::color(FluentIcons::Tone::Accent));
    pen.setWidthF(2.0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
  }
}

QString StatusBarAction::cleanupActionText(const QString& original) const
{
  QString s = original;

  s.replace(QRegularExpression("\\&([^&])"), "\\1");  // &Item -> Item
  s.replace("&&", "&");                               // &&Item -> &Item

  if (s.endsWith("...")) {
    s = s.left(s.size() - 3);
  }

  return s;
}
