/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "messagedialog.h"
#include "fluenticons.h"
#include "fluentmotion.h"

#include <QAccessible>
#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTextLayout>
#include <QTimer>
#include <QtMath>

#include <log.h>

#include <algorithm>

using namespace MOBase;

namespace
{
// logical pixels; Qt scales them for the screen the notice is on
constexpr int Shadow     = 12;   // room around the card for its shadow
constexpr int PadX       = 16;   // card padding left and right
constexpr int PadY       = 12;   // card padding top and bottom
constexpr int GlyphSize  = 16;   // the info glyph
constexpr int GlyphGap   = 12;   // between the glyph and the text
constexpr int MaxText    = 360;  // the text wraps beyond this width
constexpr int MinCard    = 240;
constexpr int EdgeMargin = 20;   // from the bottom of the window it is shown over
constexpr int StackGap   = 8;    // between notices shown at the same time
constexpr int Radius     = 8;

constexpr char16_t InfoGlyph = 0xE946;

// notices that are still on screen, so a new one stacks above them
QList<QPointer<MessageDialog>>& liveNotices()
{
  static QList<QPointer<MessageDialog>> list;
  list.removeAll(nullptr);
  return list;
}

// lays the text out in lines (a long path or URL breaks anywhere rather than
// running off the card) and draws them when painter is set; returns the height
qreal layoutText(const QString& text, const QFont& font, qreal width, QPainter* painter,
                 const QPointF& origin)
{
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  qreal y = 0;
  for (const QString& paragraph : text.split(QLatin1Char('\n'))) {
    QTextLayout layout(paragraph, font);
    layout.setTextOption(option);
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
      line.setLineWidth(width);
      line.setPosition(QPointF(0, y));
      y += line.height();
    }
    layout.endLayout();
    if (painter != nullptr) {
      layout.draw(painter, origin);
    }
  }
  return y;
}

int naturalWidth(const QString& text, const QFont& font)
{
  const QFontMetrics metrics(font);
  int width = 0;
  for (const QString& line : text.split(QLatin1Char('\n'))) {
    width = std::max(width, metrics.horizontalAdvance(line));
  }
  return width + 1;
}

// how long the notice stays: long enough to read, a little longer for
// longer texts
int readingTime(const QString& text)
{
  return std::clamp(2000 + static_cast<int>(text.length()) * 50, 3000, 10000);
}
}  // namespace

MessageDialog::MessageDialog(const QString& text, QWidget* reference)
    : QWidget(reference != nullptr ? reference->window() : nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                  Qt::NoDropShadowWindowHint |
                  // without a window to stay above, it stays above everything
                  (reference == nullptr ? Qt::WindowStaysOnTopHint : Qt::Widget)),
      m_text(text.trimmed()), m_reference(reference), m_timer(new QTimer(this))
{
  setObjectName(QStringLiteral("MessageDialog"));
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_DeleteOnClose);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setAccessibleName(m_text);

  m_timer->setSingleShot(true);
  connect(m_timer, &QTimer::timeout, this, &MessageDialog::dismiss);

  // follows the window it is shown over
  if (reference != nullptr) {
    reference->window()->installEventFilter(this);
  }
}

MessageDialog::~MessageDialog() = default;

QSize MessageDialog::sizeHint() const
{
  const int textWidth = std::min(naturalWidth(m_text, font()), MaxText);
  const int cardWidth = std::max(MinCard, PadX + GlyphSize + GlyphGap + textWidth + PadX);
  const int inner     = cardWidth - (PadX + GlyphSize + GlyphGap + PadX);
  const int cardHeight =
      PadY + std::max(GlyphSize + 4, textHeight(inner)) + PadY;
  return QSize(cardWidth + 2 * Shadow, cardHeight + 2 * Shadow);
}

int MessageDialog::textHeight(int width) const
{
  return qCeil(layoutText(m_text, font(), width, nullptr, {}));
}

QRect MessageDialog::cardRect() const
{
  return rect().adjusted(Shadow, Shadow, -Shadow, -Shadow);
}

void MessageDialog::place()
{
  // the area the notice is centered at the bottom of: the reference widget
  // inside its window, or the screen under the mouse
  QRect anchor;
  QScreen* screen = nullptr;
  if (m_reference != nullptr && m_reference->isVisible()) {
    const QRect window(m_reference->window()->mapToGlobal(QPoint(0, 0)),
                       m_reference->window()->size());
    anchor = QRect(m_reference->mapToGlobal(QPoint(0, 0)), m_reference->size())
                 .intersected(window);
    if (anchor.isEmpty()) {
      anchor = window;
    }
    screen = QGuiApplication::screenAt(anchor.center());
    if (screen == nullptr) {
      screen = m_reference->screen();
    }
  } else {
    screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
      screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
      anchor = screen->availableGeometry();
    }
  }
  if (screen == nullptr) {
    return;
  }

  // newer notices stack above the ones still showing over the same window
  int stack = 0;
  for (const auto& other : liveNotices()) {
    if (other == this) {
      break;
    }
    if (other->isVisible() && other->parentWidget() == parentWidget()) {
      stack += other->height() - 2 * Shadow + StackGap;
    }
  }

  const QSize size = sizeHint();
  resize(size);
  QPoint pos(anchor.center().x() - size.width() / 2,
             anchor.bottom() - EdgeMargin - stack - size.height() + Shadow);

  // never past the work area of the screen it is on
  const QRect available = screen->availableGeometry().adjusted(-Shadow, -Shadow, Shadow, Shadow);
  pos.setX(std::clamp(pos.x(), available.left(),
                      std::max(available.left(), available.right() - size.width() + 1)));
  pos.setY(std::clamp(pos.y(), available.top(),
                      std::max(available.top(), available.bottom() - size.height() + 1)));
  move(pos);
}

void MessageDialog::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const bool dark  = palette().color(QPalette::Window).lightness() < 128;
  const QColor ink = m_ink.isValid() ? m_ink : palette().color(QPalette::WindowText);
  QColor card      = m_card;
  if (!card.isValid()) {
    card = palette().color(QPalette::Window);
    card = dark ? card.lighter(118) : card.lighter(104);
  }
  QColor line = m_line;
  if (!line.isValid()) {
    line = ink;
    line.setAlpha(dark ? 40 : 30);
  }

  const QRectF body = QRectF(cardRect()).adjusted(0.5, 0.5, -0.5, -0.5);

  // a soft shadow below the card, like a Windows 11 flyout
  p.setPen(Qt::NoPen);
  for (int i = Shadow; i > 0; --i) {
    const qreal t = 1.0 - static_cast<qreal>(i) / Shadow;
    p.setBrush(QColor(0, 0, 0, qRound((dark ? 26 : 12) * t * t)));
    p.drawRoundedRect(body.adjusted(-i * 0.6, -i * 0.3, i * 0.6, i * 0.9), Radius + i * 0.6,
                      Radius + i * 0.6);
  }

  p.setPen(QPen(line, 1.0));
  p.setBrush(card);
  p.drawRoundedRect(body, Radius, Radius);

  const QRect inner = cardRect().adjusted(PadX, PadY, -PadX, -PadY);
  const int textWidth = inner.width() - GlyphSize - GlyphGap;
  const int textH     = textHeight(textWidth);

  // the glyph sits on the first line, the text in the middle of the card
  const int textTop = inner.top() + std::max(0, (inner.height() - textH) / 2);
  const int firstLine = QFontMetrics(font()).height();
  p.setFont(FluentIcons::font(GlyphSize));
  p.setPen(FluentIcons::color(FluentIcons::Tone::Accent));
  p.drawText(QRect(inner.left(), textTop + (firstLine - GlyphSize - 4) / 2, GlyphSize,
                   GlyphSize + 4),
             Qt::AlignCenter, QString(QChar(InfoGlyph)));

  p.setFont(font());
  p.setPen(ink);
  layoutText(m_text, font(), textWidth, &p,
             QPointF(inner.left() + GlyphSize + GlyphGap, textTop));
}

void MessageDialog::enterEvent(QEnterEvent* event)
{
  // a notice the user is reading stays
  m_timer->stop();
  QWidget::enterEvent(event);
}

void MessageDialog::leaveEvent(QEvent* event)
{
  if (!m_closing) {
    m_timer->start(1500);
  }
  QWidget::leaveEvent(event);
}

void MessageDialog::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    dismiss();
  }
  QWidget::mouseReleaseEvent(event);
}

bool MessageDialog::eventFilter(QObject* watched, QEvent* event)
{
  if ((event->type() == QEvent::Move || event->type() == QEvent::Resize) && isVisible() &&
      !m_closing) {
    place();
  }
  return QWidget::eventFilter(watched, event);
}

void MessageDialog::dismiss()
{
  if (m_closing) {
    return;
  }
  m_closing = true;
  m_timer->stop();
  FluentMotion::fadeOut(this, [this] {
    close();
  });
}

void MessageDialog::showMessage(const QString& text, QWidget* reference,
                                bool bringToFront)
{
  log::debug("{}", text);

  if (reference == nullptr) {
    for (QWidget* w : qApp->topLevelWidgets()) {
      if (qobject_cast<QMainWindow*>(w) != nullptr && w->isVisible()) {
        reference = w;
        break;
      }
    }
  }

  auto* notice = new MessageDialog(text, reference);
  liveNotices().push_back(notice);
  notice->ensurePolished();  // the theme's colors and font
  notice->place();

  // the notice rises into place; it starts transparent so its first frame
  // does not flash at full opacity
  const bool animate = FluentMotion::enabled();
  if (animate) {
    notice->setWindowOpacity(0.0);
  }
  notice->show();
  if (animate) {
    FluentMotion::popIn(notice, -6);
  }
  notice->m_timer->start(readingTime(notice->m_text));

  // screen readers announce it like a Windows notification
  QAccessibleEvent alert(notice, QAccessible::Alert);
  QAccessible::updateAccessibility(&alert);

  if (bringToFront && reference != nullptr) {
    reference->activateWindow();
  }
}
