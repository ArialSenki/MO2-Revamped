#include "drophint.h"
#include <QAbstractScrollArea>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include "revampedtheme.h"
#include <algorithm>

DropHintOverlay::DropHintOverlay(QAbstractScrollArea* view, const QString& text,
                                 const QIcon& icon)
    : QWidget(view->viewport()), m_text(text), m_icon(icon)
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
                                          const QString& text, const QIcon& icon)
{
  if (!DropHint::Enabled || view == nullptr) {
    return nullptr;
  }
  if (slot == nullptr) {
    slot = new DropHintOverlay(view, text, icon);
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
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  const bool dark     = MOBase::RevampedTheme::isDark();
  const QColor accent = palette().color(QPalette::Highlight);

  // inset from the view's edges, more on the right where the scroll bar is;
  // the half pixel keeps the line on whole pixels
  const qreal penWidth = 1.5;
  const QRectF frame   = QRectF(rect()).adjusted(8, 8, -14, -8).adjusted(0.5, 0.5, -0.5, -0.5);
  if (frame.width() < 40 || frame.height() < 40) {
    return;
  }
  QPainterPath path;
  path.addRoundedRect(frame, 8, 8);

  // the list stays as it is under a light wash of the accent
  QColor tint = accent;
  tint.setAlphaF(dark ? 0.10 : 0.07);
  p.fillPath(path, tint);

  // 6 px dashes, 4 px gaps (the pattern counts in pen widths)
  QPen border(accent, penWidth);
  border.setStyle(Qt::CustomDashLine);
  border.setDashPattern({6 / penWidth, 4 / penWidth});
  border.setCapStyle(Qt::RoundCap);
  border.setJoinStyle(Qt::RoundJoin);
  p.setPen(border);
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  // the label: a pill with an icon, in the middle of the frame
  QFont font = this->font();
  font.setWeight(QFont::DemiBold);
  p.setFont(font);
  const QFontMetrics metrics(font);
  const int iconSize = 16;
  const int padH = 14, gap = 8;
  const int pillHeight = std::max(metrics.height(), iconSize) + 14;
  const int maxText    = std::max(0, int(frame.width()) - 32 - 2 * padH - iconSize - gap);
  const QString text   = metrics.elidedText(m_text, Qt::ElideRight, maxText);
  const int textWidth  = metrics.horizontalAdvance(text);
  const int pillWidth  = 2 * padH + iconSize + gap + textWidth;

  QRectF pill(0, 0, pillWidth, pillHeight);
  pill.moveCenter(frame.center());
  QPainterPath pillPath;
  pillPath.addRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);

  // the theme's card surface, with a hairline of the accent
  const QColor surface = dark ? QColor(0x2C, 0x2C, 0x2C) : QColor(0xFF, 0xFF, 0xFF);
  p.fillPath(pillPath, surface);
  QColor line = accent;
  line.setAlphaF(0.55);
  p.setPen(QPen(line, 1));
  p.drawPath(pillPath);

  const QRectF iconRect(pill.left() + padH, pill.center().y() - iconSize / 2.0, iconSize,
                        iconSize);
  if (!m_icon.isNull()) {
    m_icon.paint(&p, iconRect.toAlignedRect(), Qt::AlignCenter);
  }

  p.setPen(dark ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x1B, 0x1B, 0x1B));
  const QRectF textRect(iconRect.right() + gap, pill.top(), textWidth + 2, pill.height());
  p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
}
