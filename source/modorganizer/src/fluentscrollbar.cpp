#include "fluentscrollbar.h"
#include "fluentmotion.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>

namespace
{
// Windows 11 sizes, in pixels: a 2 px line at rest hugging the outer edge, a
// 6 px thumb when open that grows inwards from it; the arrows are Segoe
// Fluent Icons solid carets at 8 px
constexpr qreal RestThickness = 2.0;
constexpr qreal OpenThickness = 6.0;
constexpr qreal RestInset     = 1.0;
constexpr qreal OpenInset     = 2.0;
constexpr int ArrowPixels     = 8;
// how long the bar stays open after the mouse leaves it
constexpr int CollapseDelay = 500;

constexpr char16_t CaretUp    = 0xEDDB;
constexpr char16_t CaretDown  = 0xEDDC;
constexpr char16_t CaretLeft  = 0xEDD9;
constexpr char16_t CaretRight = 0xEDDA;

QColor withAlpha(QColor color, qreal alpha)
{
  color.setAlphaF(std::clamp(alpha, 0.0, 1.0));
  return color;
}
}  // namespace

namespace FluentScrollBar
{

void attach(QScrollBar* bar)
{
  if (bar == nullptr || bar->findChild<FluentScrollBarPainter*>(QString(),
                                                                Qt::FindDirectChildrenOnly)) {
    return;
  }
  // the theme hides its own handle for bars that carry the property
  bar->setProperty("fluentScroll", true);
  bar->style()->unpolish(bar);
  bar->style()->polish(bar);
  new FluentScrollBarPainter(bar);
}

}  // namespace FluentScrollBar

FluentScrollBarPainter::FluentScrollBarPainter(QScrollBar* bar)
    : QWidget(bar), m_bar(bar), m_expand(FluentMotion::createAnimation(this)),
      m_collapse(new QTimer(this))
{
  setObjectName(QStringLiteral("fluentScrollBarPainter"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);

  m_expand->setStartValue(0.0);
  m_expand->setEndValue(0.0);
  connect(m_expand, &QVariantAnimation::valueChanged, this, [this] {
    update();
  });

  m_collapse->setSingleShot(true);
  connect(m_collapse, &QTimer::timeout, this, [this] {
    if (!m_hovered && !m_pressed) {
      expand(false);
    }
  });

  // the thumb follows the bar; the painter repaints with it
  connect(bar, &QScrollBar::valueChanged, this, [this] {
    update();
  });
  connect(bar, &QScrollBar::rangeChanged, this, [this] {
    update();
  });
  connect(bar, &QScrollBar::sliderPressed, this, [this] {
    m_pressed = true;
    expand(true);
  });
  connect(bar, &QScrollBar::sliderReleased, this, [this] {
    m_pressed = false;
    if (!m_hovered) {
      m_collapse->start(CollapseDelay);
    }
  });

  bar->installEventFilter(this);
  setGeometry(bar->rect());
  raise();
  show();
}

qreal FluentScrollBarPainter::expansion() const
{
  return m_expand->state() == QAbstractAnimation::Running ? m_expand->currentValue().toReal()
                                                           : m_expand->endValue().toReal();
}

void FluentScrollBarPainter::expand(bool open)
{
  const qreal target = open ? 1.0 : 0.0;
  if (m_expand->endValue().toReal() == target &&
      m_expand->state() != QAbstractAnimation::Running) {
    return;
  }
  FluentMotion::start(m_expand, expansion(), target, FluentMotion::Fast);
}

bool FluentScrollBarPainter::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_bar) {
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::LayoutRequest:
      setGeometry(m_bar->rect());
      raise();
      break;
    case QEvent::Enter:
      m_hovered = true;
      m_collapse->stop();
      expand(true);
      break;
    case QEvent::Leave:
      m_hovered = false;
      if (!m_pressed) {
        m_collapse->start(CollapseDelay);
      }
      break;
    case QEvent::EnabledChange:
      update();
      break;
    default:
      break;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void FluentScrollBarPainter::paintEvent(QPaintEvent*)
{
  // a theme change away from Fluent v4 brings the theme's own handle back
  if (!qApp->property("revampedFluent").toBool()) {
    return;
  }
  if (m_bar->maximum() <= m_bar->minimum()) {
    return;  // nothing to scroll: Windows draws no thumb either
  }

  QStyleOptionSlider option;
  option.initFrom(m_bar);
  option.subControls        = QStyle::SC_All;
  option.orientation        = m_bar->orientation();
  option.minimum            = m_bar->minimum();
  option.maximum            = m_bar->maximum();
  option.sliderPosition     = m_bar->sliderPosition();
  option.sliderValue        = m_bar->value();
  option.singleStep         = m_bar->singleStep();
  option.pageStep           = m_bar->pageStep();
  option.upsideDown         = m_bar->invertedAppearance();
  if (m_bar->orientation() == Qt::Horizontal) {
    option.state |= QStyle::State_Horizontal;
  }
  const QRect slider =
      m_bar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, m_bar);
  if (!slider.isValid()) {
    return;
  }

  const bool vertical = m_bar->orientation() == Qt::Vertical;
  const qreal t       = expansion();
  const bool dark     = qApp->property("revampedDark").toBool();
  const QColor ink    = palette().color(QPalette::Active, QPalette::WindowText);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  if (!m_bar->isEnabled()) {
    painter.setOpacity(0.45);
  }

  // the open bar sits on a faint rounded track
  if (t > 0.0) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(ink, (dark ? 0.06 : 0.05) * t));
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal radius = (vertical ? track.width() : track.height()) / 2.0;
    painter.drawRoundedRect(track, radius, radius);
  }

  // the thumb: a line along the outer edge that widens inwards
  const qreal thickness = RestThickness + (OpenThickness - RestThickness) * t;
  const qreal across    = vertical ? width() : height();
  const qreal inset     = RestInset + (OpenInset - RestInset) * t;
  const qreal start     = across - inset - thickness;
  QRectF thumb = vertical ? QRectF(start, slider.top() + 1, thickness, slider.height() - 2)
                          : QRectF(slider.left() + 1, start, slider.width() - 2, thickness);
  const QColor thumbInk =
      withAlpha(ink, m_pressed ? (dark ? 0.72 : 0.62) : (dark ? 0.54 : 0.45));
  painter.setPen(Qt::NoPen);
  painter.setBrush(thumbInk);
  painter.drawRoundedRect(thumb, thickness / 2.0, thickness / 2.0);

  // the arrows fade in with the open bar, in the space the theme leaves at
  // both ends (sub-line and add-line)
  if (t > 0.0) {
    const QRect less =
        m_bar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSubLine, m_bar);
    const QRect more =
        m_bar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarAddLine, m_bar);
    QFont glyphs;
    glyphs.setFamilies({QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
    glyphs.setPixelSize(ArrowPixels);
    painter.setFont(glyphs);
    painter.setPen(withAlpha(ink, (dark ? 0.62 : 0.55) * t));
    if (less.isValid() && !less.isEmpty()) {
      painter.drawText(less, Qt::AlignCenter, QString(QChar(vertical ? CaretUp : CaretLeft)));
    }
    if (more.isValid() && !more.isEmpty()) {
      painter.drawText(more, Qt::AlignCenter, QString(QChar(vertical ? CaretDown : CaretRight)));
    }
  }
}
