#ifndef SCREENFITTER_H
#define SCREENFITTER_H

// MO2 Revamped: a large window's size, by the screen it is on. It opens at a
// size for that screen's work area (at most 1500x1000, or 70% x 80% of it) and,
// moved to another screen (a 2K one to a 1080p one), takes that screen's size;
// once the user sized it by hand it keeps that size but never gets larger than
// the screen. As CollectionDialog does it.

#include <QObject>
#include <QSize>

class QRect;
class QScreen;
class QWidget;

class ScreenFitter : public QObject
{
  Q_OBJECT

public:
  // watches window, a top-level window, for as long as it lives
  ScreenFitter(QWidget* window, QSize least);

  // the size for a work area: large enough for the window's contents, not the
  // whole of a big screen
  static QSize wantedSize(const QRect& available, QSize least);

  // the window at its size for screen, centred on middle and kept on it
  void place(QScreen* screen, const QPoint& middle);

  // a size the user chose (a restored one): a move to another screen keeps it
  // where it fits
  void setUserSized(bool sized) { m_userSized = sized; }

  // MO2 Revamped: large browsers open maximized, so everything fits, until
  // the user leaves one not maximized; key names the window in the
  // instance's settings
  static bool opensMaximized(const QString& key);
  // what the window was when it closed, for opensMaximized()
  static void rememberMaximized(const QString& key, const QWidget* window);

protected:
  bool eventFilter(QObject* watched, QEvent* e) override;

private:
  QWidget* m_window;
  QSize m_least;
  bool m_userSized    = false;
  bool m_fitting      = false;
  bool m_watching     = false;
  QSize m_applied;  // the last size this set: a resize to another one is the user's

  void fitTo(QScreen* screen);
  void apply(const QRect& available, QSize size, const QPoint& middle);
};

#endif  // SCREENFITTER_H
