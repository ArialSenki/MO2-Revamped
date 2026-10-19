#include "fluentmotion.h"

#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

namespace FluentMotion
{

namespace
{
  bool g_enabled = true;
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

}  // namespace FluentMotion
