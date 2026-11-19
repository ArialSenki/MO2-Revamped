#ifndef MODORGANIZER_DROPHINT_H
#define MODORGANIZER_DROPHINT_H

#include <QIcon>
#include <QPointer>
#include <QString>
#include <QWidget>

class QAbstractScrollArea;

// MO2 Revamped: while archives from Explorer are dragged over the mod list or
// the downloads, a translucent card over the list says what a drop does.
// A trial: set Enabled to false and no hint is shown anywhere (the class and
// its calls can then go, nothing else depends on them)
namespace DropHint
{
// only for archives from outside: the lists' own drags (mods reordered,
// downloads into a category) get none
inline constexpr bool Enabled = true;
}  // namespace DropHint

// a child of the view's viewport, the viewport's size; it takes no mouse
// events, so the drag and the drop still reach the view
class DropHintOverlay : public QWidget
{
  Q_OBJECT

public:
  DropHintOverlay(QAbstractScrollArea* view, const QString& text, const QIcon& icon);

  // made on first use, nothing when hints are off
  static DropHintOverlay* forView(QPointer<DropHintOverlay>& slot,
                                  QAbstractScrollArea* view, const QString& text,
                                  const QIcon& icon);

  void showHint();
  void hideHint();

protected:
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  QString m_text;
  QIcon m_icon;
};

#endif  // MODORGANIZER_DROPHINT_H
