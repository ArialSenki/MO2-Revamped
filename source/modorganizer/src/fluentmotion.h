#ifndef FLUENTMOTION_H
#define FLUENTMOTION_H

#include <QEasingCurve>

class QVariantAnimation;
class QObject;

// Shared timing for the Fluent v4 animations. Everything that moves goes through
// here so the "Enable animations" setting can turn all of it off at once.
namespace FluentMotion
{

// standard Windows 11 durations, in milliseconds
constexpr int Fast   = 167;
constexpr int Normal = 250;
constexpr int Slow   = 333;

bool enabled();
void setEnabled(bool enabled);

// returns the duration to use, or 0 when animations are disabled
int duration(int ms);

// creates a QVariantAnimation owned by parent with the Fluent easing; its
// duration is set by start()
QVariantAnimation* createAnimation(QObject* parent,
                                   QEasingCurve::Type easing = QEasingCurve::OutCubic);

// restarts the animation from its current value towards end; jumps straight to
// end when animations are disabled
void start(QVariantAnimation* animation, const QVariant& start, const QVariant& end,
           int ms = Normal);

}  // namespace FluentMotion

#endif  // FLUENTMOTION_H
