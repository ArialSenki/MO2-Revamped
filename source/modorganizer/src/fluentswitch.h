#ifndef FLUENTSWITCH_H
#define FLUENTSWITCH_H

// Windows 11 toggle switches. One painter for every switch in MO2 (the mod
// list and the Settings-style rows), so they all move the same way: the knob
// slides and grows when switched on, the track fades between its two colors,
// and the knob grows a little more under the mouse.

#include <QColor>
#include <QRectF>
#include <QWidget>

class QCheckBox;
class QPainter;
class QVariantAnimation;

namespace FluentSwitch
{

struct Colors
{
  QColor on, off, border, knobOn, knobOff;
  bool isValid() const { return on.isValid() && off.isValid(); }
};

// paints a switch into track; position goes from 0 (off) to 1 (on), hover
// from 0 to 1 while the mouse is over it
void paint(QPainter* painter, const QRectF& track, qreal position, qreal hover,
           const Colors& colors, bool enabled = true);

// animates a check box that the theme shows as a switch
// ([fluentSwitch="true"]); safe to call more than once
void animate(QCheckBox* box);

}  // namespace FluentSwitch

// draws an animated switch over a check box's indicator; the theme sets its
// colors (qproperty-onColor and so on), like the mod list's switches
class FluentSwitchPainter : public QWidget
{
  Q_OBJECT
  Q_PROPERTY(QColor onColor MEMBER m_on)
  Q_PROPERTY(QColor offColor MEMBER m_off)
  Q_PROPERTY(QColor borderColor MEMBER m_border)
  Q_PROPERTY(QColor knobOnColor MEMBER m_knobOn)
  Q_PROPERTY(QColor knobOffColor MEMBER m_knobOff)

public:
  explicit FluentSwitchPainter(QCheckBox* box);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

private:
  QCheckBox* m_box;
  QVariantAnimation* m_position;
  QVariantAnimation* m_hover;
  QColor m_on, m_off, m_border, m_knobOn, m_knobOff;

  void place();
  qreal position() const;
};

#endif  // FLUENTSWITCH_H
