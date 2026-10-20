#include "fluentnavigation.h"

#include "fluenticons.h"
#include "fluentmotion.h"
#include "revampedtheme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QPainter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>

namespace
{
constexpr int ItemHeight          = 36;
constexpr char16_t FallbackGlyph  = 0xE915;  // dot
constexpr int GlyphRole           = Qt::UserRole;
constexpr int DimmedRole          = Qt::UserRole + 1;
const char* const StateObjectName = "fluentNavigationState";

QColor accentColor()
{
  using namespace MOBase;
  if (RevampedTheme::isBlack()) {
    return QColor("#D6D6D6");
  }
  return RevampedTheme::isDark() ? QColor("#4CC2FF") : QColor("#005FB8");
}

// kept on the tab widget so sync() finds the list and the glyphs again
class NavigationState : public QObject
{
public:
  NavigationState(QTabWidget* tabs, QListWidget* nav, FluentNavigation::GlyphFor glyphFor)
      : QObject(tabs), list(nav), glyph(std::move(glyphFor))
  {
    setObjectName(QString::fromLatin1(StateObjectName));
  }

  QListWidget* list;
  FluentNavigation::GlyphFor glyph;
};

// Windows 11 NavigationView motion: the selection pill slides (and stretches
// a little) to the new page, and the hover fill fades in and out
class NavigationMotion : public QObject
{
public:
  explicit NavigationMotion(QListWidget* list)
      : QObject(list), m_list(list), m_overlay(new Overlay(this, list->viewport())),
        m_pill(FluentMotion::createAnimation(this)),
        m_hoverIn(FluentMotion::createAnimation(this)),
        m_hoverOut(FluentMotion::createAnimation(this, QEasingCurve::InCubic))
  {
    list->viewport()->installEventFilter(this);
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlay->setGeometry(list->viewport()->rect());
    m_overlay->raise();

    connect(m_pill, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      const qreal t   = v.toReal();
      const qreal y   = m_from + (m_to - m_from) * t;
      const qreal grow = qMin<qreal>(24.0, qAbs(m_to - m_from)) * 4.0 * t * (1.0 - t);
      m_pillRect      = QRectF(m_x, y - 8.0 - grow / 2.0, 3.0, 16.0 + grow);
      m_overlay->update();
    });
    for (auto* a : {m_hoverIn, m_hoverOut}) {
      connect(a, &QVariantAnimation::valueChanged, this, [this] {
        m_list->viewport()->update();
      });
    }
    connect(list, &QListWidget::currentRowChanged, this, [this](int) {
      movePill(true);
    });
    connect(list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
      movePill(false);
    });
  }

  // how strongly the hover fill shows on a row, 0 to 1
  qreal hover(int row) const
  {
    if (row == m_hoverRow) {
      return m_hoverIn->state() == QAbstractAnimation::Running
                 ? m_hoverIn->currentValue().toReal()
                 : 1.0;
    }
    if (row == m_fadeRow && m_hoverOut->state() == QAbstractAnimation::Running) {
      return m_hoverOut->currentValue().toReal();
    }
    return 0.0;
  }

  void movePill(bool animate)
  {
    const QModelIndex index = m_list->currentIndex();
    if (!index.isValid()) {
      m_pillRect = {};
      m_overlay->update();
      return;
    }

    const QRectF row = QRectF(m_list->visualRect(index)).adjusted(2, 2, -2, -2);
    m_x              = row.left();
    const qreal to   = row.center().y();
    if (!animate || m_pillRect.isNull() || !m_list->isVisible()) {
      m_pill->stop();
      m_from = m_to = to;
      m_pillRect    = QRectF(m_x, to - 8.0, 3.0, 16.0);
      m_overlay->update();
      return;
    }

    m_from = m_pillRect.center().y();
    m_to   = to;
    FluentMotion::start(m_pill, 0.0, 1.0, FluentMotion::Normal);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    switch (event->type()) {
    case QEvent::Resize:
      m_overlay->setGeometry(m_list->viewport()->rect());
      movePill(false);
      break;
    case QEvent::Show:
      movePill(false);
      break;
    case QEvent::MouseMove:
      setHoverRow(m_list->indexAt(static_cast<QMouseEvent*>(event)->position().toPoint()).row());
      break;
    case QEvent::Leave:
      setHoverRow(-1);
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  class Overlay : public QWidget
  {
  public:
    Overlay(NavigationMotion* motion, QWidget* parent) : QWidget(parent), m_motion(motion)
    {
      // the theme's QWidget rule would paint the window surface over the rows
      setStyleSheet(QStringLiteral("background: transparent;"));
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
      if (m_motion->m_pillRect.isNull()) {
        return;
      }
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(Qt::NoPen);
      p.setBrush(accentColor());
      p.drawRoundedRect(m_motion->m_pillRect, 1.5, 1.5);
    }

  private:
    NavigationMotion* m_motion;
  };

  void setHoverRow(int row)
  {
    if (row == m_hoverRow) {
      return;
    }
    if (m_hoverRow >= 0) {
      m_fadeRow = m_hoverRow;
      FluentMotion::start(m_hoverOut, hover(m_hoverRow), 0.0, 83);
    }
    m_hoverRow = row;
    if (row >= 0) {
      FluentMotion::start(m_hoverIn, 0.0, 1.0, 83);
    }
    m_list->viewport()->update();
  }

  QListWidget* m_list;
  Overlay* m_overlay;
  QVariantAnimation* m_pill;
  QVariantAnimation* m_hoverIn;
  QVariantAnimation* m_hoverOut;
  QRectF m_pillRect;
  qreal m_x = 0, m_from = 0, m_to = 0;
  int m_hoverRow = -1;
  int m_fadeRow  = -1;
};

class NavigationDelegate : public QStyledItemDelegate
{
public:
  NavigationDelegate(QListWidget* list, NavigationMotion* motion)
      : QStyledItemDelegate(list), m_motion(motion)
  {}

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override
  {
    return QSize(QStyledItemDelegate::sizeHint(option, index).width(), ItemHeight + 4);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered  = option.state & QStyle::State_MouseOver;
    const bool dimmed   = index.data(DimmedRole).toBool() ||
                        !(option.state & QStyle::State_Enabled);
    const QColor text = option.palette.color(
        dimmed ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF row  = QRectF(option.rect).adjusted(2, 2, -2, -2);
    const qreal hover = m_motion != nullptr ? m_motion->hover(index.row()) : (hovered ? 1.0 : 0.0);
    // the selected row keeps its fill; the hover fill fades (the accent pill
    // is drawn above the rows by NavigationMotion, so it can slide)
    const int alpha = selected ? 26 : qRound(15 * hover);
    if (alpha > 0) {
      QColor fill = option.palette.color(QPalette::Active, QPalette::WindowText);
      fill.setAlpha(alpha);
      painter->setPen(Qt::NoPen);
      painter->setBrush(fill);
      painter->drawRoundedRect(row, 4, 4);
    }

    QFont glyphs;
    glyphs.setFamilies({QStringLiteral("Segoe Fluent Icons"),
                        QStringLiteral("Segoe MDL2 Assets")});
    glyphs.setPixelSize(16);
    painter->setFont(glyphs);
    painter->setPen(text);
    const QRectF glyphRect(row.left() + 14, row.top(), 20, row.height());
    painter->drawText(glyphRect, Qt::AlignCenter, index.data(GlyphRole).toString());

    QFont label = option.font;
    label.setWeight(selected ? QFont::DemiBold : QFont::Normal);
    painter->setFont(label);
    const QRectF textRect = row.adjusted(46, 0, -8, 0);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(label).elidedText(index.data().toString(),
                                                     Qt::ElideRight,
                                                     static_cast<int>(textRect.width())));
    painter->restore();
  }

private:
  NavigationMotion* m_motion;
};

NavigationState* stateOf(QTabWidget* tabs)
{
  // the state object has no meta object of its own, so look it up by name
  QObject* found = tabs->findChild<QObject*>(QString::fromLatin1(StateObjectName),
                                             Qt::FindDirectChildrenOnly);
  return static_cast<NavigationState*>(found);
}
}  // namespace

namespace FluentNavigation
{

QListWidget* attach(QTabWidget* tabs, GlyphFor glyphFor)
{
  QWidget* parent = tabs->parentWidget();
  if (parent == nullptr || parent->layout() == nullptr) {
    return nullptr;
  }

  // set before the move below, which repolishes the pages anyway: the theme
  // gives the pages the window surface, no darker panel behind them
  tabs->setProperty("fluentNavigation", true);

  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("fluentNavPage"));
  if (parent->layout()->replaceWidget(tabs, page) == nullptr) {
    delete page;
    return nullptr;
  }

  auto* nav = new QListWidget(page);
  nav->setObjectName(QStringLiteral("fluentNav"));
  nav->setItemDelegate(new NavigationDelegate(nav, new NavigationMotion(nav)));
  nav->setFrameShape(QFrame::NoFrame);
  nav->setMouseTracking(true);
  nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  nav->setFixedWidth(232);
  nav->setAccessibleName(QObject::tr("Pages"));

  auto* layout = new QHBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);
  layout->addWidget(nav);
  layout->addWidget(tabs, 1);

  tabs->tabBar()->hide();
  tabs->setDocumentMode(true);

  new NavigationState(tabs, nav, std::move(glyphFor));
  sync(tabs);

  // selection rather than the current row: accessibility tools select items
  // without moving the current one
  QObject::connect(nav, &QListWidget::itemSelectionChanged, tabs, [tabs, nav] {
    const auto selected = nav->selectedItems();
    const int row       = selected.isEmpty() ? -1 : nav->row(selected.first());
    if (row >= 0 && row < tabs->count() && row != tabs->currentIndex()) {
      tabs->setCurrentIndex(row);
    }
  });
  QObject::connect(tabs, &QTabWidget::currentChanged, nav, [nav, tabs](int index) {
    if (index != nav->currentRow()) {
      nav->setCurrentRow(index);
    }
    QWidget* page = tabs->widget(index);
    if (page != nullptr && qApp->property("revampedFluent").toBool()) {
      // pages fill in their buttons late (Nexus info, conflicts)
      for (QAbstractButton* button : page->findChildren<QAbstractButton*>()) {
        FluentIcons::apply(button);
      }
    }
    FluentMotion::pageIn(page);
  });

  return nav;
}

QListWidget* attach(QTabWidget* tabs, const QList<char16_t>& glyphs)
{
  return attach(tabs, [glyphs](int index, QWidget*) {
    return index < glyphs.size() ? glyphs[index] : FallbackGlyph;
  });
}

void setHeader(QTabWidget* tabs, QWidget* header)
{
  auto* row = qobject_cast<QHBoxLayout*>(
      tabs->parentWidget() != nullptr ? tabs->parentWidget()->layout() : nullptr);
  if (header == nullptr || stateOf(tabs) == nullptr || row == nullptr) {
    return;
  }

  if (QWidget* old = header->parentWidget(); old != nullptr && old->layout() != nullptr) {
    old->layout()->removeWidget(header);
  }
  row->removeWidget(tabs);
  auto* column = new QVBoxLayout;
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(4);
  column->addWidget(header);
  column->addWidget(tabs, 1);
  row->addLayout(column, 1);
}

void sync(QTabWidget* tabs)
{
  NavigationState* state = stateOf(tabs);
  if (state == nullptr) {
    return;
  }

  QListWidget* nav = state->list;
  const QSignalBlocker blocker(nav);
  nav->clear();
  for (int i = 0; i < tabs->count(); ++i) {
    auto* item = new QListWidgetItem(tabs->tabText(i).remove(QLatin1Char('&')), nav);
    const char16_t glyph = state->glyph ? state->glyph(i, tabs->widget(i)) : FallbackGlyph;
    item->setData(GlyphRole, QString(QChar(glyph)));
    item->setData(DimmedRole, tabs->tabBar()->tabTextColor(i).isValid());
    item->setToolTip(tabs->tabToolTip(i));
    if (!tabs->isTabEnabled(i)) {
      item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
  }
  nav->setCurrentRow(tabs->currentIndex());
}

}  // namespace FluentNavigation
