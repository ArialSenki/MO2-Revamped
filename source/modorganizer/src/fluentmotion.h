#ifndef FLUENTMOTION_H
#define FLUENTMOTION_H

#include <QEasingCurve>

#include <functional>

class QVariantAnimation;
class QObject;
class QToolButton;
class QWidget;

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

// Windows 11 popup entrance: a menu, list or tooltip window fades in while it
// slides `lift` pixels into place (0 for a plain fade). Replaces Qt's roll
// and fade effects.
void popIn(QWidget* popup, int lift = 6);

// fades a window out, then calls done (right away when animations are off)
void fadeOut(QWidget* window, std::function<void()> done);

// Windows 11 page entrance for a page that just became current (Settings
// pages, the main window's tabs): it fades in while it rises `rise` pixels
// into place. Pages with GPU-rendered children (web views) switch at once.
void pageIn(QWidget* page, int rise = 16);

// Windows 11 drop-down chevron for a tool button with a menu: the button's own
// arrow is hidden ([fluentChevron="true"] in the theme) and a chevron is drawn
// `right` pixels from its right edge, turning over while the menu is open.
// `drop` lowers it from the button's middle so it sits on the text's middle.
// Safe to call more than once; does nothing for a button without a menu.
void animateMenuChevron(QToolButton* button, qreal right = 13.0, qreal drop = 2.0);

}  // namespace FluentMotion

#endif  // FLUENTMOTION_H
