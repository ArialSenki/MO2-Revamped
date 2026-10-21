#include "fluentmotion.h"
#include "fluenticons.h"

#include <QAction>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

namespace FluentMotion
{

namespace
{
  bool g_enabled = true;

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
      p.setFont(FluentIcons::font(10));
      p.setPen(m_button->palette().color(
          m_button->isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
      p.translate(width() - m_right, height() / 2.0 + m_drop);
      p.rotate(angle());
      p.drawText(QRectF(-6, -6, 12, 12), Qt::AlignCenter, QString(QChar(0xE70D)));  // ChevronDown
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
  g_enabled = enabled;
}

int duration(int ms)
{
  return g_enabled ? ms : 0;
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
