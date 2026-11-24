#ifndef FLUENTMOTION_H
#define FLUENTMOTION_H

#include <QEasingCurve>
#include <QString>

#include <functional>

class QVariantAnimation;
class QObject;
class QPixmap;
class QToolButton;
class QWidget;

// Shared timing for the Fluent v4 animations. Everything that moves goes through
// here so the Animations setting can turn all of it off at once, or set how
// smoothly it moves.
namespace FluentMotion
{

// standard Windows 11 durations, in milliseconds
constexpr int Fast   = 167;
constexpr int Normal = 250;
constexpr int Slow   = 333;

bool enabled();
void setEnabled(bool enabled);

// how often everything that moves is drawn (Settings > General > Animations).
// Every Qt animation in the program (these, smooth scrolling, the switches)
// advances on one timer, which runs only while something animates
enum class Level
{
  Off,       // no animations: every change shows at once
  Saver,     // 30 frames a second
  Standard,  // 60 frames a second, as before
  Monitor    // the refresh rate of the screen the main window is on
};

Level level();
void setLevel(Level level);

// the main window: Monitor follows the screen it is on and that screen's rate
void followScreenOf(QWidget* window);

// the frames a second animations run at now (0 when they are off)
int frameRate();

// the frames a second Monitor runs at: the main window's screen, 30 to 240,
// 60 when the screen doesn't say
int monitorRate();

// the level's name as kept in the settings, and back (Standard if unknown)
QString levelKey(Level level);
Level levelFromKey(const QString& key);

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

// fades a picture of what a widget showed away over what it shows now, so a
// page change has no empty frame between the two
void crossFade(QWidget* host, const QPixmap& before);

// a change that redraws every window at once (a theme switch), seamless like
// Windows' own: coverWindows() lays a picture of what each visible window
// shows over it, and uncoverWindows(), called once the change is made, fades
// the pictures away when the windows have drawn themselves again. Without
// animations (MO2's or Windows' animation effects) the pictures go in one step,
// so no half-redrawn window shows either
void coverWindows();
void uncoverWindows();

// Windows 11 drop-down chevron for a tool button with a menu: the button's own
// arrow is hidden ([fluentChevron="true"] in the theme) and a chevron is drawn
// `right` pixels from its right edge, turning over while the menu is open.
// `drop` lowers it from the button's middle so it sits on the text's middle.
// Safe to call more than once; does nothing for a button without a menu.
void animateMenuChevron(QToolButton* button, qreal right = 13.0, qreal drop = 2.0);

}  // namespace FluentMotion

#endif  // FLUENTMOTION_H
