#include "drophint.h"
#include <QAbstractScrollArea>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include "revampedtheme.h"
#include "usertheme.h"
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace
{
// the frame runs this far outside the list, in the gap between the list and
// its panel's border, the same on every side; its corners are the panels'
constexpr int Outside  = 5;
constexpr qreal Inset  = 1;
constexpr qreal Radius = 8;

// a logical length on whole device pixels, so lines stay sharp at 125% or 150%
qreal snap(qreal v, qreal dpr)
{
  return std::round(v * dpr) / dpr;
}

// black or white on a fill: the one with the higher contrast ratio
QColor inkOn(const QColor& fill)
{
  const auto channel = [](qreal c) {
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  const qreal l = 0.2126 * channel(fill.redF()) + 0.7152 * channel(fill.greenF()) +
                  0.0722 * channel(fill.blueF());
  const qreal onBlack = (l + 0.05) / 0.05;
  const qreal onWhite = 1.05 / (l + 0.05);
  return onBlack >= onWhite ? QColor(Qt::black) : QColor(Qt::white);
}
}  // namespace

DropHintOverlay::DropHintOverlay(QAbstractScrollArea* view, const QString& text,
                                 const QIcon& icon)
    : QWidget(view->window()), m_view(view), m_text(text), m_icon(icon)
{
  // around the whole list, header and scroll bars included, so the frame
  // never runs through a row or the column titles. A child of the window: a
  // child of the viewport is moved along when the list scrolls (a drag near
  // the edge scrolls it), and the list itself would clip the frame outside
  // it. The drag and the drop go on to the list under it
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);
  view->installEventFilter(this);
  followView();
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
  followView();
  if (isVisible()) {
    return;
  }
  raise();
  show();
}

void DropHintOverlay::hideHint()
{
  hide();
}

void DropHintOverlay::followView()
{
  if (m_view == nullptr || parentWidget() == nullptr) {
    return;
  }
  // the part of the list that shows (its panel may clip it), in the window
  QWidget* top = parentWidget();
  QRect shown(m_view->mapTo(top, QPoint(0, 0)), m_view->size());
  for (QWidget* w = m_view->parentWidget(); w != nullptr && w != top;
       w = w->parentWidget()) {
    shown &= QRect(w->mapTo(top, QPoint(0, 0)), w->size());
  }
  const QRect wanted = shown.adjusted(-Outside, -Outside, Outside, Outside);
  if (geometry() != wanted) {
    setGeometry(wanted);
  }
}

bool DropHintOverlay::eventFilter(QObject* watched, QEvent* event)
{
  if (m_view != nullptr && watched == m_view) {
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
      followView();
      break;
    case QEvent::Hide:
      // the downloads' tab changed while dragging
      hide();
      break;
    default:
      break;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void DropHintOverlay::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  const qreal dpr = devicePixelRatioF();
  const bool dark = MOBase::RevampedTheme::isDark();

  // the theme's accent (the Run button's), not the palette's Highlight: that
  // is the dark selection blue and vanishes on the dark rows
  const QColor accent = UserTheme::recolor(dark ? QColor(0x4C, 0xC2, 0xFF)
                                                        : QColor(0x00, 0x5F, 0xB8));

  // a 2 px line on whole device pixels, its middle on the pixel grid
  const qreal devicePen = std::max<qreal>(1, std::round(2 * dpr));
  const qreal penWidth  = devicePen / dpr;
  const qreal half      = std::fmod(devicePen, 2.0) == 1.0 ? 0.5 / dpr : 0;
  const qreal inset     = snap(Inset, dpr) + half;
  const QRectF frame    = QRectF(QPointF(inset, inset),
                                 QPointF(snap(width() - Inset, dpr) - half,
                                         snap(height() - Inset, dpr) - half));
  if (frame.width() < 40 || frame.height() < 40) {
    return;
  }
  QPainterPath path;
  path.addRoundedRect(frame, Radius, Radius);

  // the list stays readable under a light wash of the accent: soft, so the
  // rows don't turn blue (the light accent is darker, it needs less)
  QColor tint = accent;
  tint.setAlphaF(dark ? 0.06 : 0.04);
  p.fillPath(path, tint);

  // 6 px dashes, 4 px gaps (the pattern counts in pen widths)
  QColor line = accent;
  line.setAlphaF(dark ? 0.55 : 0.45);
  QPen border(line, penWidth);
  border.setStyle(Qt::CustomDashLine);
  border.setDashPattern({6 / penWidth, 4 / penWidth});
  border.setCapStyle(Qt::RoundCap);
  border.setJoinStyle(Qt::RoundJoin);
  p.setPen(border);
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  // the label: an accent pill like the Run button, icon and text on it
  QFont font = this->font();
  font.setWeight(QFont::DemiBold);
  p.setFont(font);
  const QFontMetricsF metrics(font);
  const qreal iconSize   = 16;
  const qreal padH       = 16, gap = 8;
  const qreal pillHeight = snap(std::max(metrics.height(), iconSize) + 16, dpr);
  const qreal maxText    = std::max<qreal>(0, frame.width() - 32 - 2 * padH - iconSize - gap);
  const QString text     = metrics.elidedText(m_text, Qt::ElideRight, maxText);
  const qreal textWidth  = std::ceil(metrics.horizontalAdvance(text));
  const qreal pillWidth  = snap(2 * padH + iconSize + gap + textWidth, dpr);

  // centred across, a third of the way down: the window the files come from
  // usually covers the lower half, and Windows draws the dragged file's image
  // under the cursor, which is mostly lower in the list
  const qreal top = std::clamp(frame.top() + frame.height() / 3.0 - pillHeight / 2.0,
                               frame.top() + 16, frame.bottom() - 16 - pillHeight);
  const QRectF pill(QPointF(snap((width() - pillWidth) / 2.0, dpr), snap(top, dpr)),
                    QSizeF(pillWidth, pillHeight));
  QPainterPath pillPath;
  pillPath.addRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);

  // a soft shadow lifts it off the rows
  QPainterPath shadow;
  shadow.addRoundedRect(pill.translated(0, 2), pill.height() / 2.0, pill.height() / 2.0);
  p.fillPath(shadow, QColor(0, 0, 0, dark ? 90 : 40));
  p.fillPath(pillPath, accent);

  // the text and icon on the accent: black or white, whichever stands out
  // more against it (WCAG contrast), so a user theme's light or dark accent
  // reads too (#4CC2FF and OLED's #5AA9D6 give black, #005FB8 white)
  const QColor ink = inkOn(accent);

  // icon and text as one block in the middle of the pill
  const qreal content = iconSize + gap + textWidth;
  const qreal left    = snap(pill.center().x() - content / 2.0, dpr);
  const QRectF iconRect(left, snap(pill.center().y() - iconSize / 2.0, dpr), iconSize,
                        iconSize);
  if (!m_icon.isNull()) {
    // the icon in the text's color
    QPixmap glyph = m_icon.pixmap(QSize(int(iconSize), int(iconSize)), dpr);
    {
      QPainter g(&glyph);
      g.setCompositionMode(QPainter::CompositionMode_SourceIn);
      g.fillRect(QRect(QPoint(0, 0), glyph.deviceIndependentSize().toSize()), ink);
    }
    p.drawPixmap(iconRect.topLeft(), glyph);
  }

  p.setPen(ink);
  const QRectF textRect(iconRect.right() + gap, pill.top(), textWidth + 1, pill.height());
  p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
}
