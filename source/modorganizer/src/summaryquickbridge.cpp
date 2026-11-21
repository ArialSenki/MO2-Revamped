#include "summaryquickbridge.h"

#include "fluentdashboard.h"
#include "quickui.h"

#include <QEvent>
#include <QHelpEvent>
#include <QPushButton>
#include <QQuickWidget>
#include <QTimer>
#include <QWhatsThis>

namespace
{
// the row's height, as the Widgets tiles' (FluentStatTile)
constexpr int RowHeight = 60;
// the gap between tiles, as qml/SummaryTiles.qml lays them out
constexpr int TileSpacing = 12;

// shown unless the dashboard hid it: the row is out of sight, so isVisible()
// and isVisibleTo() say no for every button, wanted or not
bool shownOnPurpose(const QWidget* w)
{
  return w != nullptr && !(w->testAttribute(Qt::WA_WState_ExplicitShowHide) &&
                           w->testAttribute(Qt::WA_WState_Hidden));
}

// What's This (Shift+F1, or the title bar's ?) on a tile tells what the
// Widgets tile tells: the view is one widget, so the tile under the pointer is
// worked out from where it is
class TileHelp : public QObject
{
public:
  TileHelp(QQuickWidget* view, SummaryQuickBridge* bridge)
      : QObject(view), m_view(view), m_bridge(bridge)
  {}

  bool eventFilter(QObject*, QEvent* event) override
  {
    if (event->type() != QEvent::WhatsThis && event->type() != QEvent::QueryWhatsThis) {
      return false;
    }
    const auto* help = static_cast<QHelpEvent*>(event);
    const QString text = textAt(help->pos().x());
    if (text.isEmpty()) {
      return false;
    }
    if (event->type() == QEvent::WhatsThis) {
      QWhatsThis::showText(help->globalPos(), text, m_view);
    }
    event->accept();
    return true;
  }

private:
  QQuickWidget* m_view;
  SummaryQuickBridge* m_bridge;

  QString textAt(int x) const
  {
    const QVariantList tiles = m_bridge->tiles();
    if (tiles.isEmpty()) {
      return {};
    }
    const int count = static_cast<int>(tiles.size());
    const int index = qBound(0, x * count / qMax(1, m_view->width() + TileSpacing), count - 1);
    return tiles[index].toMap().value(QStringLiteral("help")).toString();
  }
};
}  // namespace

SummaryQuickBridge::SummaryQuickBridge(QList<FluentStatTile*> tiles, QObject* parent)
    : QObject(parent)
{
  for (FluentStatTile* tile : tiles) {
    m_tiles << tile;
  }
  sync();
}

QQuickWidget* SummaryQuickBridge::createView(QWidget* parent)
{
  auto* view = QuickUi::createView(parent, QStringLiteral("SummaryTiles"),
                                   {{QStringLiteral("bridge"), QVariant::fromValue(this)}});
  if (view == nullptr) {
    return nullptr;
  }
  // the main window shows through between the tiles, whatever paints it (the
  // theme, Mica, OLED Black)
  view->setProperty("quickSeeThrough", true);
  view->setAttribute(Qt::WA_AlwaysStackOnTop);
  view->setClearColor(Qt::transparent);
  view->setFixedHeight(RowHeight);
  view->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  // Tab moves through the tile buttons inside the view; the view itself takes
  // no click focus, as the Widgets row has nothing focusable but its buttons
  view->setFocusPolicy(Qt::TabFocus);
  view->installEventFilter(new TileHelp(view, this));
  // the view goes first when the window closes, and the bridge with it
  setParent(view);
  return view;
}

void SummaryQuickBridge::scheduleSync()
{
  if (m_syncQueued) {
    return;
  }
  m_syncQueued = true;
  QTimer::singleShot(0, this, [this] {
    m_syncQueued = false;
    sync();
  });
}

void SummaryQuickBridge::sync()
{
  QVariantList rows;
  for (const auto& tile : m_tiles) {
    if (tile.isNull()) {
      continue;
    }
    QPushButton* action = tile->action();
    rows.append(QVariantMap{
        {QStringLiteral("glyph"), QString(QChar(tile->glyph()))},
        {QStringLiteral("caption"), tile->caption()},
        {QStringLiteral("value"), tile->value()},
        {QStringLiteral("tone"), tile->tone()},
        {QStringLiteral("help"), tile->whatsThis()},
        {QStringLiteral("action"), action != nullptr ? action->text() : QString()},
        {QStringLiteral("actionName"),
         action != nullptr && !action->accessibleName().isEmpty() ? action->accessibleName()
         : action != nullptr                                       ? action->text()
                                                                   : QString()},
        // the dashboard hides a button it has no use for
        {QStringLiteral("actionVisible"), shownOnPurpose(action)},
    });
  }
  if (rows != m_rows) {
    m_rows = rows;
    emit tilesChanged();
  }
}

void SummaryQuickBridge::act(int index)
{
  // after the QML click handler has returned: what the button does (a filter,
  // the Overwrite window) can change the row under it
  QMetaObject::invokeMethod(
      this,
      [this, index] {
        if (index < 0 || index >= m_tiles.size() || m_tiles[index].isNull()) {
          return;
        }
        QPushButton* action = m_tiles[index]->action();
        if (shownOnPurpose(action) && action->isEnabled()) {
          action->click();
        }
      },
      Qt::QueuedConnection);
}
