#include "fluentmotion.h"

#include <QVariantAnimation>

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

}  // namespace FluentMotion
