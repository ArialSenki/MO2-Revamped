#ifndef FLUENTSCROLLBAR_H
#define FLUENTSCROLLBAR_H

// Windows 11 scroll bars. At rest a bar is a thin line along the edge; under
// the mouse it widens into a rounded thumb over a faint track, with small
// arrows at its ends, and it folds back a moment after the mouse leaves. One
// painter over each QScrollBar draws it, so the theme only sizes the bar and
// hides its own handle ([fluentScroll="true"]).

#include <QWidget>

class QScrollBar;
class QTimer;
class QVariantAnimation;

namespace FluentScrollBar
{

// takes over the painting of a scroll bar; safe to call more than once
void attach(QScrollBar* bar);

}  // namespace FluentScrollBar

class FluentScrollBarPainter : public QWidget
{
  Q_OBJECT

public:
  explicit FluentScrollBarPainter(QScrollBar* bar);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

private:
  QScrollBar* m_bar;
  QVariantAnimation* m_expand;
  QTimer* m_collapse;
  bool m_hovered = false;
  bool m_pressed = false;

  void expand(bool open);
  qreal expansion() const;
};

#endif  // FLUENTSCROLLBAR_H
