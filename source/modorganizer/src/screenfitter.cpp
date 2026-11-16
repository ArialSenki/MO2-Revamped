#include "screenfitter.h"

#include <QEvent>
#include <QRect>
#include <QResizeEvent>
#include <QScreen>
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
               m_window->isVisible()) {
      // the user dragged its edge
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
