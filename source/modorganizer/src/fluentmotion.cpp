#include "fluentmotion.h"
#include "fluenticons.h"

#include <log.h>

#include <QAbstractAnimation>
#include <QAction>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPixmap>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

#include <memory>
#include <vector>

#include <QApplication>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace FluentMotion
{

namespace
{
  bool g_enabled = true;
  Level g_level  = Level::Standard;
  // the level animations go back to when they are turned on again
  Level g_lastOn = Level::Standard;

  // a screen that says it draws more often than this is drawn at this; one
  // that doesn't say is taken as 60
  constexpr int MaxRate      = 240;
  constexpr int StandardRate = 60;
  constexpr int SaverRate    = 30;

  // Advances every Qt animation in the program at a set number of frames a
  // second. Qt's own driver ticks every 16 ms whatever the screen; this one
  // waits for each frame's own time (6 or 7 ms in turn at 144), so the rate is
  // the screen's and not the nearest whole millisecond's. Qt starts it when
  // something begins to animate and stops it once nothing does: idle, it
  // costs nothing.
  class RateDriver : public QAnimationDriver
  {
  public:
    RateDriver()
    {
      m_timer.setSingleShot(true);
      m_timer.setTimerType(Qt::PreciseTimer);
      QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
        tick();
      });
      m_stats = qEnvironmentVariableIsSet("MO2R_ANIMATION_STATS");
    }

    void setRate(int framesPerSecond)
    {
      m_rate     = framesPerSecond;
      m_periodNs = 1000000000LL / framesPerSecond;
    }

    int rate() const { return m_rate; }

  protected:
    void start() override
    {
      QAnimationDriver::start();
      m_clock.start();
      m_next   = m_periodNs;
      m_frames = 0;
      schedule();
    }

    void stop() override
    {
      m_timer.stop();
      if (m_stats && m_frames > 1) {
        const qint64 ms = m_clock.elapsed();
        MOBase::log::info("animations: {} frames in {} ms ({:.1f} a second, set to {})",
                           m_frames, ms, ms > 0 ? m_frames * 1000.0 / ms : 0.0, m_rate);
      }
      QAnimationDriver::stop();
    }

  private:
    void tick()
    {
      advance();
      ++m_frames;
      // the last animation may have ended in that frame
      if (!isRunning()) {
        return;
      }
      // a late frame (a busy moment) drops the frames it missed rather than
      // drawing them in a burst
      const qint64 now = m_clock.nsecsElapsed();
      m_next += m_periodNs;
      if (m_next <= now) {
        m_next = now + m_periodNs - (now - m_next) % m_periodNs;
      }
      schedule();
    }

    void schedule()
    {
      const qint64 wait = m_next - m_clock.nsecsElapsed();
      m_timer.start(static_cast<int>(qMax<qint64>(0, (wait + 500000) / 1000000)));
    }

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_periodNs = 1000000000LL / StandardRate;
    qint64 m_next     = 0;
    qint64 m_frames   = 0;
    int m_rate        = StandardRate;
    bool m_stats      = false;
  };

  RateDriver* g_driver = nullptr;
  int g_logged         = 0;
  Level g_loggedLevel  = Level::Standard;
  QPointer<QWidget> g_window;
  QMetaObject::Connection g_screenRate;

  QScreen* monitorScreen()
  {
    QScreen* screen = g_window != nullptr ? g_window->screen() : nullptr;
    return screen != nullptr ? screen : QGuiApplication::primaryScreen();
  }

  int rateFor(Level level)
  {
    switch (level) {
    case Level::Saver:
      return SaverRate;
    case Level::Monitor:
      return monitorRate();
    default:
      // off: what still moves by itself (Qt's smooth scrolling) moves as before
      return StandardRate;
    }
  }

  void applyRate()
  {
    if (QCoreApplication::instance() == nullptr) {
      return;
    }
    if (g_driver == nullptr) {
      g_driver = new RateDriver();
      g_driver->setParent(QCoreApplication::instance());
      g_driver->install();
    }
    const int rate = rateFor(g_level);
    if (rate != g_driver->rate()) {
      g_driver->setRate(rate);
    }
    if (rate != g_logged || g_level != g_loggedLevel) {
      g_logged      = rate;
      g_loggedLevel = g_level;
      QScreen* screen = monitorScreen();
      MOBase::log::info("animations: {} ({} frames a second; screen '{}' at {:.2f} Hz)",
                        levelKey(g_level), g_level == Level::Off ? 0 : rate,
                        screen != nullptr ? screen->name() : QStringLiteral("?"),
                        screen != nullptr ? screen->refreshRate() : 0.0);
    }
  }

  // the screen the main window is on, and its rate, as either changes
  void watchScreen(QScreen* screen)
  {
    QObject::disconnect(g_screenRate);
    if (screen != nullptr) {
      g_screenRate = QObject::connect(screen, &QScreen::refreshRateChanged, screen, [] {
        applyRate();
      });
    }
    applyRate();
  }

  // MO2R_ANIMATION_BENCH=<seconds>: once the main window shows, a corner of
  // it the size of a switch or a tab's underline is redrawn in a loop for that
  // long and the frames drawn are logged (measurements)
  void runBench(QWidget* window)
  {
    bool ok           = false;
    const int seconds = qEnvironmentVariableIntValue("MO2R_ANIMATION_BENCH", &ok);
    if (!ok || seconds <= 0) {
      return;
    }
    auto* bench = new QVariantAnimation(window);
    bench->setStartValue(0.0);
    bench->setEndValue(1.0);
    bench->setDuration(seconds * 1000);
    auto frames = std::make_shared<qint64>(0);
    auto clock  = std::make_shared<QElapsedTimer>();
    QObject::connect(bench, &QVariantAnimation::valueChanged, window, [window, frames] {
      ++*frames;
      window->update(QRect(16, 16, 240, 48));
    });
    QObject::connect(bench, &QAbstractAnimation::finished, window, [bench, frames, clock] {
      const qint64 ms = clock->elapsed();
      MOBase::log::info("animation bench: {} frames in {} ms ({:.1f} a second, {} at {})",
                        *frames, ms, ms > 0 ? *frames * 1000.0 / ms : 0.0,
                        levelKey(g_level), frameRate());
      bench->deleteLater();
    });
    // after the start's own work, so the window is idle around it
    QTimer::singleShot(8000, window, [bench, clock] {
      MOBase::log::info("animation bench: start");
      clock->start();
      bench->start();
    });
  }

  class ScreenFollower : public QObject
  {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
      auto* window = qobject_cast<QWidget*>(watched);
      // shown, or its native window made again (window flags, Mica): the
      // screen of the window it has now
      if (window != nullptr &&
          (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) &&
          window->windowHandle() != nullptr && window->windowHandle() != m_handle) {
        QObject::disconnect(m_screenChanged);
        m_handle        = window->windowHandle();
        m_screenChanged = QObject::connect(m_handle, &QWindow::screenChanged, this,
                                           [](QScreen* screen) {
                                             watchScreen(screen);
                                           });
        watchScreen(m_handle->screen());
        if (!m_benched) {
          m_benched = true;
          runBench(window);
        }
      }
      return QObject::eventFilter(watched, event);
    }

  private:
    QPointer<QWindow> m_handle;
    QMetaObject::Connection m_screenChanged;
    bool m_benched = false;
  };

  // drawn over a tool button: its chevron, at the angle the menu animation is
  // at; the button's mouse events go through
  // the menu a tool button opens: its own, or its action's on a toolbar
  QMenu* menuOf(QToolButton* button)
  {
    if (button->menu() != nullptr) {
      return button->menu();
    }
    return button->defaultAction() != nullptr ? button->defaultAction()->menu() : nullptr;
  }

  class MenuChevron : public QWidget
  {
  public:
    MenuChevron(QToolButton* button, qreal right, qreal drop)
        : QWidget(button), m_button(button), m_right(right), m_drop(drop),
          m_turn(createAnimation(this))
    {
      setObjectName(QStringLiteral("fluentMenuChevron"));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      // the theme's QWidget rule would paint the button's surface over it
      setStyleSheet(QStringLiteral("background: transparent;"));
      setGeometry(button->rect());
      button->installEventFilter(this);

      connect(m_turn, &QVariantAnimation::valueChanged, this, [this] {
        update();
      });
      connect(menuOf(button), &QMenu::aboutToShow, this, [this] {
        start(m_turn, angle(), 180.0, Fast);
      });
      connect(menuOf(button), &QMenu::aboutToHide, this, [this] {
        start(m_turn, angle(), 0.0, Fast);
      });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
      if (watched == m_button && event->type() == QEvent::Resize) {
        setGeometry(m_button->rect());
      }
      return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override
    {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      const QColor ink = m_button->palette().color(
          m_button->isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
      p.translate(width() - m_right, height() / 2.0 + m_drop);
      p.rotate(angle());
      FluentIcons::drawGlyph(p, QRectF(-6, -6, 12, 12), 0xE70D, ink, 10);  // ChevronDown
    }

  private:
    QToolButton* m_button;
    qreal m_right;
    qreal m_drop;
    QVariantAnimation* m_turn;

    qreal angle() const
    {
      const QVariant v = m_turn->currentValue();
      return v.isValid() ? v.toReal() : 0.0;
    }
  };
}

bool enabled()
{
  return g_enabled;
}

void setEnabled(bool enabled)
{
  setLevel(enabled ? (g_level == Level::Off ? g_lastOn : g_level) : Level::Off);
}

Level level()
{
  return g_level;
}

void setLevel(Level level)
{
  g_level   = level;
  g_enabled = level != Level::Off;
  if (g_enabled) {
    g_lastOn = level;
  }
  applyRate();
}

void followScreenOf(QWidget* window)
{
  if (window == nullptr || g_window == window) {
    return;
  }
  g_window = window;
  window->installEventFilter(new ScreenFollower(window));
  applyRate();
}

int monitorRate()
{
  QScreen* screen = monitorScreen();
  const qreal hz  = screen != nullptr ? screen->refreshRate() : 0.0;
  if (hz < 1.0) {
    return StandardRate;
  }
  return qBound(SaverRate, qRound(hz), MaxRate);
}

int frameRate()
{
  if (g_level == Level::Off) {
    return 0;
  }
  return g_driver != nullptr ? g_driver->rate() : rateFor(g_level);
}

QString levelKey(Level level)
{
  switch (level) {
  case Level::Off:
    return QStringLiteral("off");
  case Level::Saver:
    return QStringLiteral("saver");
  case Level::Monitor:
    return QStringLiteral("monitor");
  default:
    return QStringLiteral("standard");
  }
}

Level levelFromKey(const QString& key)
{
  for (Level level : {Level::Off, Level::Saver, Level::Standard, Level::Monitor}) {
    if (key.compare(levelKey(level), Qt::CaseInsensitive) == 0) {
      return level;
    }
  }
  return Level::Standard;
}

QVariantAnimation* createAnimation(QObject* parent, QEasingCurve::Type easing)
{
  auto* animation = new QVariantAnimation(parent);
  animation->setEasingCurve(easing);
  return animation;
}

void start(QVariantAnimation* animation, const QVariant& start, const QVariant& end,
           int ms)
{
  animation->stop();

  if (!g_enabled || ms <= 0) {
    // a zero-length animation still emits valueChanged() with the end value
    animation->setDuration(0);
    animation->setStartValue(end);
    animation->setEndValue(end);
  } else {
    animation->setDuration(ms);
    animation->setStartValue(start);
    animation->setEndValue(end);
  }

  animation->start();
}

namespace
{
  const char* const PopupAnimationName = "fluentPopupMotion";

  // one window animation at a time: a new one replaces the running one
  QVariantAnimation* windowAnimation(QWidget* window)
  {
    for (auto* old : window->findChildren<QVariantAnimation*>(
             QLatin1String(PopupAnimationName), Qt::FindDirectChildrenOnly)) {
      old->stop();
      old->deleteLater();
    }

    auto* animation = createAnimation(window, QEasingCurve::OutCubic);
    animation->setObjectName(QLatin1String(PopupAnimationName));
    return animation;
  }
}  // namespace

void popIn(QWidget* popup, int lift)
{
  if (!g_enabled || popup == nullptr || !popup->isWindow()) {
    return;
  }

  const QPoint end = popup->pos();
  QVariantAnimation* animation = windowAnimation(popup);
  popup->setWindowOpacity(0.0);
  if (lift != 0) {
    popup->move(end.x(), end.y() - lift);
  }

  QObject::connect(animation, &QVariantAnimation::valueChanged, popup,
                   [popup, end, lift](const QVariant& value) {
                     const double t = value.toDouble();
                     popup->setWindowOpacity(t);
                     if (lift != 0) {
                       popup->move(end.x(), end.y() - qRound((1.0 - t) * lift));
                     }
                   });
  QObject::connect(animation, &QVariantAnimation::finished, popup, [popup, end, lift] {
    popup->setWindowOpacity(1.0);
    if (lift != 0) {
      popup->move(end);
    }
  });

  animation->setStartValue(0.0);
  animation->setEndValue(1.0);
  animation->setDuration(Fast);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void fadeOut(QWidget* window, std::function<void()> done)
{
  if (!g_enabled || window == nullptr || !window->isVisible()) {
    if (done) {
      done();
    }
    return;
  }

  QVariantAnimation* animation = windowAnimation(window);
  // exits accelerate (Windows 11 "fast out"): the window leaves without the
  // lingering tail of an entrance curve
  animation->setEasingCurve(QEasingCurve::InCubic);
  QObject::connect(animation, &QVariantAnimation::valueChanged, window,
                   [window](const QVariant& value) {
                     window->setWindowOpacity(value.toDouble());
                   });
  QObject::connect(animation, &QVariantAnimation::finished, window,
                   [window, done = std::move(done)] {
                     if (done) {
                       done();
                     }
                     window->setWindowOpacity(1.0);
                   });

  animation->setStartValue(window->windowOpacity());
  animation->setEndValue(0.0);
  animation->setDuration(Fast);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void crossFade(QWidget* host, const QPixmap& before)
{
  if (!g_enabled || host == nullptr || !host->isVisible() || before.isNull()) {
    return;
  }

  auto* picture = new QLabel(host);
  picture->setAttribute(Qt::WA_TransparentForMouseEvents);
  picture->setPixmap(before);
  picture->setGeometry(host->rect());
  auto* effect = new QGraphicsOpacityEffect(picture);
  picture->setGraphicsEffect(effect);
  picture->show();
  picture->raise();

  QVariantAnimation* animation = createAnimation(effect, QEasingCurve::OutCubic);
  QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                   [effect](const QVariant& value) {
                     effect->setOpacity(1.0 - value.toReal());
                   });
  QObject::connect(animation, &QVariantAnimation::finished, picture, &QObject::deleteLater);
  animation->setStartValue(0.0);
  animation->setEndValue(1.0);
  animation->setDuration(Fast);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

namespace
{
  // the picture of a window laid over it while its theme changes. Drawn by
  // hand: a theme's rules for labels or plain widgets don't reach it
  class WindowPicture : public QWidget
  {
  public:
    WindowPicture(QWidget* window, QPixmap picture)
        : QWidget(window), m_picture(std::move(picture))
    {
      setObjectName(QStringLiteral("fluentWindowPicture"));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_NoSystemBackground);
      setFocusPolicy(Qt::NoFocus);
      setGeometry(window->rect());
      show();
      raise();
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
      QPainter p(this);
      p.drawPixmap(0, 0, m_picture);
    }

  private:
    QPixmap m_picture;
  };

  // counts the changes: a change's pictures wait only for that change
  int g_coverTurn = 0;

  std::vector<QPointer<WindowPicture>>& windowPictures()
  {
    static std::vector<QPointer<WindowPicture>> pictures;
    return pictures;
  }

  // the window's pictures take the place of nothing anymore
  void dropWindowPictures()
  {
    for (const auto& picture : windowPictures()) {
      delete picture.data();
    }
    windowPictures().clear();
  }

  // Windows' "Animation effects" (Accessibility > Visual effects)
  bool systemAnimations()
  {
#ifdef Q_OS_WIN
    BOOL on = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) {
      return on != FALSE;
    }
#endif
    return true;
  }
}  // namespace

void coverWindows()
{
  // a second change before the first one's pictures are gone: the windows are
  // pictured as they are now, not under the old pictures
  dropWindowPictures();
  ++g_coverTurn;

  for (QWidget* window : QApplication::topLevelWidgets()) {
    const Qt::WindowType type = window->windowType();
    if (!window->isVisible() || window->isMinimized() || window->size().isEmpty() ||
        (type != Qt::Window && type != Qt::Dialog && type != Qt::Tool)) {
      continue;
    }

    const QPixmap grabbed = window->grab();
    if (grabbed.isNull()) {
      continue;
    }
    QPixmap picture = grabbed;
    if (window->property("fluentMica").toBool()) {
      // the Mica backdrop doesn't come with the picture, and Windows redraws it
      // in the new theme only a while after the window's own surfaces: under the
      // picture it would turn dark (or light) before everything else. The
      // picture gets the backdrop's color in the theme it shows: Windows' own
      // (the theme makes the window's background transparent, so the palette
      // has none)
      picture = QPixmap(grabbed.size());
      picture.setDevicePixelRatio(grabbed.devicePixelRatio());
      picture.fill(qApp->property("revampedDark").toBool() ? QColor(0x20, 0x20, 0x20)
                                                           : QColor(0xF3, 0xF3, 0xF3));
      QPainter p(&picture);
      p.drawPixmap(0, 0, grabbed);
    }
    windowPictures().emplace_back(new WindowPicture(window, std::move(picture)));
  }
}

void uncoverWindows()
{
  if (windowPictures().empty()) {
    return;
  }

  // the windows redraw in the next turn of the event loop (the native frames
  // and the Mica backdrop after that, see applyNativeTitleBarTheme()) and the
  // Qt Quick views in their own frames: the pictures stay three frames more
  const int turn = g_coverTurn;
  QTimer::singleShot(0, qApp, [turn] {
    QTimer::singleShot(50, qApp, [turn] {
      if (turn != g_coverTurn) {
        return;
      }
      auto pictures = std::move(windowPictures());
      windowPictures().clear();
      const bool fade = g_enabled && systemAnimations();
      for (const auto& picture : pictures) {
        if (picture.isNull()) {
          continue;
        }
        if (!fade) {
          delete picture.data();
          continue;
        }
        auto* effect = new QGraphicsOpacityEffect(picture);
        picture->setGraphicsEffect(effect);
        QVariantAnimation* animation = createAnimation(effect, QEasingCurve::InOutQuad);
        QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                         [effect](const QVariant& value) {
                           effect->setOpacity(1.0 - value.toReal());
                         });
        QObject::connect(animation, &QVariantAnimation::finished, picture.data(),
                         &QObject::deleteLater);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setDuration(200);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        // a newer change drops it while it fades
        windowPictures().push_back(picture);
      }
    });
  });
}

void pageIn(QWidget* page, int rise)
{
  if (!g_enabled || page == nullptr || !page->isVisible()) {
    return;
  }

  // a graphics effect does not compose GPU-rendered children (web views,
  // OpenGL); those pages switch without the entrance
  for (QWidget* child : page->findChildren<QWidget*>()) {
    if (child->inherits("QWebEngineView") || child->inherits("QOpenGLWidget") ||
        child->inherits("QQuickWidget") || child->inherits("QWebView")) {
      return;
    }
  }

  // a page switched again before its entrance ended starts from where it is
  QPoint end = page->pos();
  if (const QVariant settled = page->property("fluentPageEnd"); settled.isValid()) {
    end = settled.toPoint();
  }
  page->setProperty("fluentPageEnd", end);

  auto* effect = new QGraphicsOpacityEffect(page);
  effect->setOpacity(0.0);
  page->setGraphicsEffect(effect);  // replaces (and deletes) a running one
  page->move(end.x(), end.y() + rise);

  // the fade ends after two thirds of the slide, like Windows 11 (167 ms of
  // opacity, 250 ms of movement)
  QVariantAnimation* animation = createAnimation(effect, QEasingCurve::OutCubic);
  QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                   [page, effect, end, rise](const QVariant& value) {
                     const qreal t = value.toReal();
                     effect->setOpacity(qMin(1.0, t * 1.5));
                     page->move(end.x(), end.y() + qRound((1.0 - t) * rise));
                   });
  QObject::connect(animation, &QVariantAnimation::finished, page, [page, effect, end] {
    page->move(end);
    page->setProperty("fluentPageEnd", QVariant());
    if (page->graphicsEffect() == effect) {
      page->setGraphicsEffect(nullptr);  // deletes the effect and the animation
    }
  });
  start(animation, 0.0, 1.0, Normal);
}

void animateMenuChevron(QToolButton* button, qreal right, qreal drop)
{
  if (button == nullptr || menuOf(button) == nullptr ||
      button->property("fluentChevron").toBool()) {
    return;
  }

  // the theme hides the button's own arrow
  button->setProperty("fluentChevron", true);
  button->style()->unpolish(button);
  button->style()->polish(button);

  new MenuChevron(button, right, drop);
}

}  // namespace FluentMotion
