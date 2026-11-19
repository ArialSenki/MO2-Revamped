#include "drophint.h"
#include <QAbstractScrollArea>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include "revampedtheme.h"
#include <algorithm>

DropHintOverlay::DropHintOverlay(QAbstractScrollArea* view, const QString& text)
    : QWidget(view->viewport()), m_text(text)
{
  // the drag and the drop go on to the view under it
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);
  setGeometry(view->viewport()->rect());
  view->viewport()->installEventFilter(this);
  hide();
}

DropHintOverlay* DropHintOverlay::forView(QPointer<DropHintOverlay>& slot,
                                          QAbstractScrollArea* view,
                                          const QString& text)
{
  if (!DropHint::Enabled || view == nullptr) {
    return nullptr;
  }
  if (slot == nullptr) {
    slot = new DropHintOverlay(view, text);
  }
  return slot;
}

void DropHintOverlay::showHint()
{
  if (isVisible()) {
    return;
  }
  setGeometry(parentWidget()->rect());
  raise();
  show();
}

void DropHintOverlay::hideHint()
{
  hide();
}

bool DropHintOverlay::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == parentWidget() && event->type() == QEvent::Resize) {
    setGeometry(parentWidget()->rect());
  }
  return QWidget::eventFilter(watched, event);
}

void DropHintOverlay::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);

  const bool dark    = MOBase::RevampedTheme::isDark();
  const QColor accent = palette().color(QPalette::Highlight);

  // the list shows through, a little dimmed toward the theme's surface
  QColor veil = dark ? QColor(32, 32, 32) : QColor(250, 250, 250);
  veil.setAlpha(150);
  QColor tint = accent;
  tint.setAlpha(dark ? 26 : 18);

  const QRectF frame = QRectF(rect()).adjusted(6.5, 6.5, -6.5, -6.5);
  QPainterPath path;
  path.addRoundedRect(frame, 8, 8);
  p.fillPath(path, veil);
  p.fillPath(path, tint);

  QPen border(accent, 2, Qt::DashLine);
  border.setDashPattern({4, 3});
  p.setPen(border);
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  // the label on a card of its own, centred
  QFont font = this->font();
  font.setPointSizeF(font.pointSizeF() * 1.15);
  font.setWeight(QFont::DemiBold);
  p.setFont(font);
  const QFontMetrics metrics(font);
  const QSize textSize(metrics.horizontalAdvance(m_text), metrics.height());
  const QSize cardSize(std::min(textSize.width() + 40, width() - 32),
                       textSize.height() + 20);
  QRect card(QPoint(0, 0), cardSize);
  card.moveCenter(rect().center());

  QColor cardColor = dark ? QColor(44, 44, 44) : QColor(255, 255, 255);
  cardColor.setAlpha(235);
  QPainterPath cardPath;
  cardPath.addRoundedRect(QRectF(card), 6, 6);
  p.fillPath(cardPath, cardColor);
  p.setPen(QPen(accent, 1));
  p.drawPath(cardPath);

  p.setPen(dark ? QColor(255, 255, 255) : QColor(27, 27, 27));
  p.drawText(card, Qt::AlignCenter,
             metrics.elidedText(m_text, Qt::ElideRight, card.width() - 16));
}
