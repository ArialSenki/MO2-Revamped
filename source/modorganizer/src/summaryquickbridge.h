#ifndef MO2R_SUMMARYQUICKBRIDGE_H
#define MO2R_SUMMARYQUICKBRIDGE_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class FluentStatTile;
class QQuickWidget;
class QWidget;

// MO2 Revamped: the summary row of the main window (active mods, conflicts,
// Overwrite, updates) in Qt Quick (qml/SummaryTiles.qml), when QuickUi is on.
// The Widgets tiles go on being filled by FluentDashboard, out of sight; this
// mirrors what they show and clicks their buttons, so both versions count,
// filter and open the same
class SummaryQuickBridge : public QObject
{
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("made by FluentDashboard")

  // each tile: glyph, caption, value, tone (accent, caution, critical,
  // neutral), help (its What's This), and its button: action, actionName
  // (what a screen reader says), actionVisible
  Q_PROPERTY(QVariantList tiles READ tiles NOTIFY tilesChanged)

public:
  SummaryQuickBridge(QList<FluentStatTile*> tiles, QObject* parent);

  // the view in parent, or null when it can't be made (then the Widgets tiles
  // are shown). The bridge then belongs to the view
  QQuickWidget* createView(QWidget* parent);

  QVariantList tiles() const { return m_rows; }

  // a tile changed: the view follows soon (calls are coalesced)
  void scheduleSync();

  // the tile's button (Show or Manage), as a click on it
  Q_INVOKABLE void act(int index);

signals:
  void tilesChanged();

private:
  QList<QPointer<FluentStatTile>> m_tiles;
  QVariantList m_rows;
  bool m_syncQueued = false;

  void sync();
};

#endif  // MO2R_SUMMARYQUICKBRIDGE_H
