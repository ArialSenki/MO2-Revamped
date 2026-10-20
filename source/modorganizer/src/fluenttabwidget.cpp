#include "fluenttabwidget.h"
#include "fluentmotion.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace
{
// size of the pill, matching the Windows 11 selector bar
constexpr qreal IndicatorWidth  = 16.0;
constexpr qreal IndicatorHeight = 3.0;
}  // namespace

FluentTabBar::FluentTabBar(QWidget* parent)
    : QTabBar(parent), m_color(0x00, 0x5F, 0xB8),
      m_animation(FluentMotion::createAnimation(this))
{
  setDrawBase(false);
  setExpanding(false);

  connect(m_animation, &QVariantAnimation::valueChanged, this, [this](auto&& v) {
    m_indicator = v.toRectF();
    update();
  });

  connect(this, &QTabBar::currentChanged, this, [this] {
    moveIndicator(true);
  });
}

QColor FluentTabBar::indicatorColor() const
{
  return m_color;
}

void FluentTabBar::setIndicatorColor(const QColor& c)
{
  m_color = c;
  update();
}

QRectF FluentTabBar::indicatorRect(int index) const
{
  if (index < 0 || index >= count()) {
    return {};
  }

  const QRectF tab = tabRect(index);
  return QRectF(tab.center().x() - IndicatorWidth / 2,
                tab.bottom() - IndicatorHeight - 1, IndicatorWidth, IndicatorHeight);
}

void FluentTabBar::moveIndicator(bool animate)
{
  const QRectF target = indicatorRect(currentIndex());

  if (!animate || m_indicator.isNull() || !isVisible()) {
    m_animation->stop();
    m_indicator = target;
    update();
    return;
  }

  FluentMotion::start(m_animation, m_indicator, target, FluentMotion::Normal);
}

void FluentTabBar::paintEvent(QPaintEvent* e)
{
  QTabBar::paintEvent(e);

  if (m_indicator.isNull() || count() == 0) {
    return;
  }

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(isEnabled() ? m_color : palette().color(QPalette::Disabled, QPalette::Text));
  p.drawRoundedRect(m_indicator, IndicatorHeight / 2, IndicatorHeight / 2);
}

void FluentTabBar::resizeEvent(QResizeEvent* e)
{
  QTabBar::resizeEvent(e);
  moveIndicator(false);
}

void FluentTabBar::tabLayoutChange()
{
  QTabBar::tabLayoutChange();
  moveIndicator(false);
}

void FluentTabBar::showEvent(QShowEvent* e)
{
  QTabBar::showEvent(e);
  moveIndicator(false);
}

FluentTabWidget::FluentTabWidget(QWidget* parent) : QTabWidget(parent)
{
  setTabBar(new FluentTabBar(this));

  // with a Fluent theme the new page rises into place while the pill slides
  connect(this, &QTabWidget::currentChanged, this, [this](int index) {
    if (qApp->property("revampedFluent").toBool()) {
      FluentMotion::pageIn(widget(index), 12);
    }
  });
}
