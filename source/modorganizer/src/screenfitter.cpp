#include "screenfitter.h"
#include "settings.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QRect>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QWidget>
#include <QWindow>

namespace
{
// a window's frame and title bar around its contents
const QSize FrameRoom(16, 40);
}  // namespace

ScreenFitter::ScreenFitter(QWidget* window, QSize least)
    : QObject(window), m_window(window), m_least(least)
{
  window->installEventFilter(this);
}

QSize ScreenFitter::wantedSize(const QRect& available, QSize least)
{
  least = least.boundedTo(available.size());
  const QSize wanted(available.width() * 8 / 10, available.height() * 8 / 10);
  return wanted.expandedTo(least).boundedTo(available.size() - FrameRoom);
}

namespace
{
QString settingName(const QString& key)
{
  return QStringLiteral("Geometry/%1_window").arg(key);
}
}  // namespace

bool ScreenFitter::restore(const QString& key, QScreen* screen, const QPoint& middle)
{
  m_key = key;
  Settings* settings = Settings::maybeInstance();
  const QByteArray saved =
      settings != nullptr
          ? QSettings(settings->filename(), QSettings::IniFormat).value(settingName(key)).toByteArray()
          : QByteArray();
  if (saved.isEmpty() || !m_window->restoreGeometry(saved)) {
    place(screen, middle);
    return false;
  }
  m_userSized = true;
  // the screen it was left on, or this one when that one is gone; Qt already
  // brings a window from a missing screen back, this keeps it whole on it
  QScreen* on = QGuiApplication::screenAt(m_window->geometry().center());
  if (on == nullptr) {
    on = screen != nullptr ? screen : QGuiApplication::primaryScreen();
  }
  if (on == nullptr) {
    return true;
  }
  const QRect available = on->availableGeometry();
  m_window->setMinimumSize(m_least.boundedTo(available.size() - FrameRoom));
  if (m_window->isMaximized() || m_window->isFullScreen()) {
    return true;
  }
  // moved only when it doesn't fit: a window that does (snapped to an edge
  // too) stays exactly where it was. It fits when it isn't larger than the
  // work area and its title bar is on it
  const QRect g    = m_window->geometry();
  const bool fits  = g.width() <= available.width() && g.height() <= available.height() &&
                    g.top() >= available.top() && g.top() < available.bottom() - 40 &&
                    g.left() < available.right() - 100 && g.right() > available.left() + 100;
  if (!fits) {
    apply(available, m_window->size(),
          available.contains(g.center()) ? g.center() : available.center());
  }
  return true;
}

void ScreenFitter::remember() const
{
  Settings* settings = Settings::maybeInstance();
  // a minimized window says nothing of what the user wants
  if (m_key.isEmpty() || settings == nullptr || m_window->isMinimized()) {
    return;
  }
  QSettings(settings->filename(), QSettings::IniFormat)
      .setValue(settingName(m_key), m_window->saveGeometry());
}

void ScreenFitter::place(QScreen* screen, const QPoint& middle)
{
  if (screen == nullptr) {
    return;
  }
  const QRect available = screen->availableGeometry();
  apply(available, wantedSize(available, m_least), middle);
}

bool ScreenFitter::eventFilter(QObject* watched, QEvent* e)
{
  if (watched == m_window) {
    if ((e->type() == QEvent::Hide && !e->spontaneous()) || e->type() == QEvent::Close) {
      // closed (a minimize is a spontaneous hide): kept for the next time; a
      // close MO2 sends when it quits, too, which hides nothing
      remember();
    } else if (e->type() == QEvent::Show && !m_watching) {
      if (QWindow* handle = m_window->windowHandle(); handle != nullptr) {
        m_watching = true;
        connect(handle, &QWindow::screenChanged, this, &ScreenFitter::fitTo);
      }
    } else if (e->type() == QEvent::Resize && e->spontaneous() && !m_fitting &&
               m_window->isVisible() && !m_window->isMaximized() &&
               !m_window->isMinimized() && !m_window->isFullScreen() &&
               static_cast<QResizeEvent*>(e)->size() != m_applied) {
      // the user dragged its edge (a maximize or a restore isn't a size of theirs)
      m_userSized = true;
    }
  }
  return QObject::eventFilter(watched, e);
}

void ScreenFitter::fitTo(QScreen* screen)
{
  if (screen == nullptr || m_window->isMaximized() || m_window->isFullScreen()) {
    return;
  }
  // still being dragged across: refitted once the user lets go, so the window
  // doesn't jump under the cursor
  if (QGuiApplication::mouseButtons() != Qt::NoButton) {
    QPointer<QScreen> later(screen);
    QTimer::singleShot(200, this, [this, later] {
      if (later && m_window->screen() == later) {
        fitTo(later);
      }
    });
    return;
  }
  const QRect available = screen->availableGeometry();
  const QSize size =
      m_userSized ? m_window->size() : wantedSize(available, m_least);
  apply(available, size, m_window->frameGeometry().center());
}

void ScreenFitter::apply(const QRect& available, QSize size, const QPoint& middle)
{
  size      = size.boundedTo(available.size() - FrameRoom);
  m_fitting = true;
  m_window->setMinimumSize(m_least.boundedTo(available.size() - FrameRoom));
  m_applied = size;
  if (size != m_window->size()) {
    m_window->resize(size);
  }
  QPoint pos(middle.x() - size.width() / 2, middle.y() - size.height() / 2);
  pos.setX(qBound(available.left(), pos.x(),
                  qMax(available.left(), available.right() - size.width() - FrameRoom.width())));
  pos.setY(qBound(available.top(), pos.y(),
                  qMax(available.top(), available.bottom() - size.height() - FrameRoom.height())));
  if (pos != m_window->pos()) {
    m_window->move(pos);
  }
  m_fitting = false;
}
