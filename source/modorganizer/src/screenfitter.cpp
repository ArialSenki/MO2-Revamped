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
  QSize wanted(qMin(available.width() * 7 / 10, 1500),
               qMin(available.height() * 8 / 10, 1000));
  return wanted.expandedTo(least).boundedTo(available.size() - FrameRoom);
}

bool ScreenFitter::opensMaximized(const QString& key)
{
  return QSettings(Settings::instance().filename(), QSettings::IniFormat)
      .value(QStringLiteral("Geometry/%1_maximized").arg(key), true)
      .toBool();
}

void ScreenFitter::rememberMaximized(const QString& key, const QWidget* window)
{
  // a minimized window says nothing of what the user wants
  if (window == nullptr || window->isMinimized()) {
    return;
  }
  QSettings(Settings::instance().filename(), QSettings::IniFormat)
      .setValue(QStringLiteral("Geometry/%1_maximized").arg(key), window->isMaximized());
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
    if (e->type() == QEvent::Show && !m_watching) {
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
