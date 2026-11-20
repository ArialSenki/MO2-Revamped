#ifndef MO2R_INSTANCESQUICKBRIDGE_H
#define MO2R_INSTANCESQUICKBRIDGE_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class InstanceManagerDialog;
class QAbstractButton;

// MO2 Revamped: what the Qt Quick Instances window (qml/InstancePicker.qml)
// shows and does. The window keeps its Widgets controls, unshown, and does all
// its work with them as always; this reads their texts and states for the view
// and clicks them for it, so both versions behave the same in every case
class InstancesQuickBridge : public QObject
{
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("made by the Instances window")

  // the list: headings and instances, as the Widgets list shows them now
  Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
  // the selected instance's place in rows, -1 for none
  Q_PROPERTY(int currentRow READ currentRow NOTIFY stateChanged)
  // everything else: texts, values, and each button's text, tip and state
  Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
  explicit InstancesQuickBridge(InstanceManagerDialog* dialog);

  QVariantList rows() const { return m_rows; }
  int currentRow() const { return m_currentRow; }
  QVariantMap state() const { return m_state; }

  // reads the window again, once the current change is done
  void scheduleSync();

  // Ctrl+F: the search shows and takes the focus
  void openSearch();

  Q_INVOKABLE void selectRow(int row);
  // Enter or a double click
  Q_INVOKABLE void activateRow(int row);
  Q_INVOKABLE void rowMenu(int row, qreal globalX, qreal globalY);
  Q_INVOKABLE void viewMenu(qreal globalX, qreal globalY);
  // a details row's button: "location", "base", "game", "profiles", "ini",
  // "group" or "notes"
  Q_INVOKABLE void detailAction(const QString& key);
  // "rename", "duplicate", "shortcut", "delete", "open", "new", "help" or "close"
  Q_INVOKABLE void press(const QString& button);
  Q_INVOKABLE void nolvusPress(int id);
  Q_INVOKABLE void setFilter(const QString& text);

signals:
  void rowsChanged();
  void stateChanged();
  void searchRequested();

private:
  InstanceManagerDialog* m_dialog;
  QVariantList m_rows;
  QVariantMap m_state;
  int m_currentRow   = -1;
  bool m_pending     = false;
  bool m_searchOpen  = false;
  QList<QPointer<QAbstractButton>> m_nolvusButtons;

  void sync();
  QAbstractButton* button(const QString& name) const;
};

#endif  // MO2R_INSTANCESQUICKBRIDGE_H
