#include "fluenttabwidget.h"
#include "fluentmotion.h"

#include <QApplication>
#include <QPainter>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QStackedWidget>
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

namespace
{
// what the pages show just before a press on the bar can change them
class PageSnapshot : public QObject
{
public:
  PageSnapshot(QTabWidget* tabs, QPixmap* target) : QObject(tabs), m_tabs(tabs), m_target(target)
  {}

protected:
  bool eventFilter(QObject*, QEvent* e) override
  {
    if (canChangeTab(e)) {
      if (auto* pages = m_tabs->findChild<QStackedWidget*>(); pages && pages->isVisible()) {
        *m_target = pages->grab();
      }
    }
    return false;
  }

private:
  QTabWidget* m_tabs;
  QPixmap* m_target;

  bool canChangeTab(QEvent* e) const
  {
    if (e->type() == QEvent::MouseButtonPress) {
      // only the left button selects, and the tab under it may be the current one
      const auto* mouse = static_cast<QMouseEvent*>(e);
      const int tab     = m_tabs->tabBar()->tabAt(mouse->position().toPoint());
      return mouse->button() == Qt::LeftButton && tab >= 0 &&
             tab != m_tabs->currentIndex();
    }

    if (e->type() == QEvent::KeyPress) {
      // the arrows on the bar, and the tab widget's Ctrl shortcuts
      const auto* key = static_cast<QKeyEvent*>(e);
      return key->key() == Qt::Key_Left || key->key() == Qt::Key_Right ||
             key->modifiers().testFlag(Qt::ControlModifier);
    }

    return false;
  }
};
}  // namespace

FluentTabWidget::FluentTabWidget(QWidget* parent) : QTabWidget(parent)
{
  setTabBar(new FluentTabBar(this));
  tabBar()->installEventFilter(new PageSnapshot(this, &m_before));

  // with a Fluent theme the old page fades into the new one while the pill
  // slides; nothing moves, so icons and text stay where they will be
  connect(this, &QTabWidget::currentChanged, this, [this](int) {
    FluentMotion::crossFade(findChild<QStackedWidget*>(), m_before);

    m_before = QPixmap();
  });
}
