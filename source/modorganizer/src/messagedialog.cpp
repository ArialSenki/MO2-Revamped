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
#include "settings.h"
#include "fluenticons.h"
#include "fluentmotion.h"

#include <QAccessible>
#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QScreen>
#include <QTextLayout>
#include <QTimer>
#include <QtMath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <log.h>

#include <algorithm>
#include <utility>

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
constexpr int EdgeMargin = 12;   // from the bottom of the window it is shown over
constexpr int StackGap   = 2;    // between notices shown at the same time
constexpr int MaxShown   = 4;    // over one window; a fifth closes the oldest
constexpr int Radius     = 8;

constexpr char16_t InfoGlyph = 0xE946;

// notices waiting for the dialog in front to close
struct WaitingNotice
{
  QString text;
  QPointer<QWidget> reference;
  bool bringToFront;
};

QList<WaitingNotice>& waitingNotices()
{
  static QList<WaitingNotice> waiting;
  return waiting;
}

QTimer& waitingTimer()
{
  static QTimer* timer = [] {
    auto* t = new QTimer(qApp);
    t->setInterval(400);
    QObject::connect(t, &QTimer::timeout, [] {
      MessageDialog::showWaitingNotices();
    });
    return t;
  }();
  return *timer;
}

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
      m_text(text.trimmed()), m_base(text.trimmed()), m_reference(reference),
      m_timer(new QTimer(this))
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
    // one that is fading out leaves its place to the others
    if (other->isVisible() && !other->m_closing && other->parentWidget() == parentWidget()) {
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
  // the ones above it move down into its place
  restack(parentWidget());
  FluentMotion::fadeOut(this, [this] {
    close();
  });
}

void MessageDialog::showWaitingNotices()
{
  // the same test showMessage makes: a progress window in front doesn't hold
  // them back (an installation's starts right after its question closes)
  const QWidget* front = QApplication::activeModalWidget();
  if (front != nullptr && front->isVisible() &&
      qobject_cast<const QProgressDialog*>(front) == nullptr) {
    return;  // still open: the timer asks again
  }
  waitingTimer().stop();
  const auto waiting = std::exchange(waitingNotices(), {});
  for (const auto& w : waiting) {
    showMessage(w.text, w.reference, w.bringToFront);
  }
}

void MessageDialog::restack(QWidget* parent)
{
  for (const auto& other : liveNotices()) {
    if (other != nullptr && other->isVisible() && !other->m_closing &&
        other->parentWidget() == parent) {
      other->place();
    }
  }
}

void MessageDialog::showMessage(const QString& text, QWidget* reference,
                                bool bringToFront)
{
  log::debug("{}", text);

  // MO2 Revamped: a collection running shows its own progress; hundreds of
  // notices, each with the system's sound, are only noise
  // (a notice from the dialog the user has open in front, such as Settings'
  // "applies when Mod Organizer starts again", is still shown; an
  // installation's progress window is modal too, so the modal window alone
  // doesn't let a notice through)
  const QWidget* front = QApplication::activeModalWidget();
  // (the collection's own window in front isn't a dialog the user opened: each
  // mod's "Installation successful" came from it)
  const bool fromFrontDialog =
      front != nullptr && reference != nullptr &&
      reference->window() == front->window() &&
      qobject_cast<const QProgressDialog*>(front) == nullptr &&
      qstrcmp(front->window()->metaObject()->className(), "CollectionDialog") != 0;
  // bringToFront is the caller saying the user must see it (Settings' restart
  // notice, errors and warnings such as a refused rename)
  if (qApp->property("mo2r.collectionQuiet").toBool() && !fromFrontDialog &&
      !bringToFront) {
    // still in the log, where a user looking for it finds it
    log::info("notice not shown while a collection installs: {}", text);
    qApp->setProperty("mo2r.collectionQuietHidden",
                      qApp->property("mo2r.collectionQuietHidden").toInt() + 1);
    return;
  }

  // MO2 Revamped: the notices as a sound only, if the user prefers it (one
  // sound for a burst of them); the dialog in front's own, such as Settings'
  // "applies when it starts again", still show
  if (GlobalSettings::noticeSoundOnly() && !fromFrontDialog && !bringToFront) {
    log::info("notice (sound only): {}", text);
    static QElapsedTimer lastSound;
    if (!lastSound.isValid() || lastSound.elapsed() > 1500) {
      lastSound.start();
#ifdef Q_OS_WIN
      ::MessageBeep(MB_ICONASTERISK);
#else
      QApplication::beep();
#endif
    }
    return;
  }

  // MO2 Revamped: never over a question the user has to answer (the theme
  // choice at the first start, a confirmation): a notice that isn't the open
  // dialog's own waits until it closes
  if (!fromFrontDialog && front != nullptr && front->isVisible() &&
      qobject_cast<const QProgressDialog*>(front) == nullptr) {
    waitingNotices().append({text, QPointer<QWidget>(reference), bringToFront});
    log::debug("notice waits for the dialog in front: {}", text);
    if (!waitingTimer().isActive()) {
      waitingTimer().start();
    }
    return;
  }

  // MO2 Revamped: the same notice again while it is still shown (one
  // "Installation successful" per mod): the one shown counts it and stays a
  // little longer, instead of a stack of the same card
  {
    const QString trimmed = text.trimmed();
    for (const auto& other : liveNotices()) {
      if (other != nullptr && other->isVisible() && !other->m_closing &&
          other->m_base == trimmed) {
        ++other->m_count;
        other->m_text = QStringLiteral("%1 (×%2)").arg(trimmed).arg(other->m_count);
        other->setAccessibleName(other->m_text);
        restack(other->parentWidget());
        other->update();
        other->m_timer->start(readingTime(other->m_text));
        return;
      }
    }
  }

  // MO2 Revamped: over the dialog in front (the instance wizard, Settings),
  // not the main window under it
  QWidget* modal = QApplication::activeModalWidget();
  const bool modalShown =
      modal != nullptr && modal->isVisible() && !modal->testAttribute(Qt::WA_DontShowOnScreen);
  if (modalShown &&
      (reference == nullptr || qobject_cast<QMainWindow*>(reference->window()) != nullptr)) {
    reference = modal->window();
  }
  if (reference == nullptr) {
    for (QWidget* w : qApp->topLevelWidgets()) {
      if (qobject_cast<QMainWindow*>(w) != nullptr && w->isVisible()) {
        reference = w;
        break;
      }
    }
  }

  auto* notice = new MessageDialog(text, reference);

  // a few at a time over one window: the oldest goes for the new one
  QList<MessageDialog*> shown;
  for (const auto& other : liveNotices()) {
    if (other != nullptr && other->isVisible() && !other->m_closing &&
        other->parentWidget() == notice->parentWidget()) {
      shown.push_back(other);
    }
  }
  for (int i = 0; i + MaxShown - 1 < shown.size(); ++i) {
    shown[i]->dismiss();
  }

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

  // never over a game or another program the user is in
  if (bringToFront && reference != nullptr &&
      QApplication::applicationState() == Qt::ApplicationActive) {
    reference->activateWindow();
  }
}
