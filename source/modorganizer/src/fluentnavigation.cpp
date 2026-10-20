#include "fluentnavigation.h"

#include "fluenticons.h"
#include "fluentmotion.h"
#include "revampedtheme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QVariantAnimation>
#include <QLayout>
#include <QListWidget>
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

class NavigationDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

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

    const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
    if (selected || hovered) {
      QColor fill = option.palette.color(QPalette::Active, QPalette::WindowText);
      fill.setAlpha(selected ? 26 : 15);
      painter->setPen(Qt::NoPen);
      painter->setBrush(fill);
      painter->drawRoundedRect(row, 4, 4);
    }
    if (selected) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(accentColor());
      painter->drawRoundedRect(QRectF(row.left(), row.center().y() - 8, 3, 16), 1.5, 1.5);
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
};

// the new page fades in, like a Windows 11 Settings page; the effect only
// lives for the animation so scrolling and painting stay cheap afterwards
void fadeIn(QWidget* page)
{
  if (page == nullptr || FluentMotion::duration(FluentMotion::Fast) == 0) {
    return;
  }

  // a graphics effect does not compose GPU-rendered children (web views,
  // OpenGL); those pages switch without the fade
  for (QWidget* child : page->findChildren<QWidget*>()) {
    if (child->inherits("QWebEngineView") || child->inherits("QOpenGLWidget") ||
        child->inherits("QQuickWidget") || child->inherits("QWebView")) {
      return;
    }
  }

  auto* effect = new QGraphicsOpacityEffect(page);
  effect->setOpacity(0.0);
  page->setGraphicsEffect(effect);

  QVariantAnimation* animation = FluentMotion::createAnimation(effect);
  QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                   [effect](const QVariant& value) {
                     effect->setOpacity(value.toReal());
                   });
  QObject::connect(animation, &QVariantAnimation::finished, page, [page, effect] {
    if (page->graphicsEffect() == effect) {
      page->setGraphicsEffect(nullptr);  // deletes the effect and the animation
    }
  });
  FluentMotion::start(animation, 0.0, 1.0, FluentMotion::Fast);
}

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
  nav->setItemDelegate(new NavigationDelegate(nav));
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
    fadeIn(page);
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
