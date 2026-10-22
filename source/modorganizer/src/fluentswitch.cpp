#include "fluentswitch.h"
#include "fluentmotion.h"

#include <QCheckBox>
#include <QEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVariantAnimation>

namespace FluentSwitch
{

void paint(QPainter* painter, const QRectF& track, qreal position, qreal hover,
           const Colors& colors, bool enabled)
{
  const qreal t = std::clamp(position, 0.0, 1.0);
  auto mix      = [t](const QColor& a, const QColor& b) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
  };

  const qreal h      = track.height();
  const qreal radius = h / 2.0;
  const QRectF body  = track.adjusted(0.5, 0.5, -0.5, -0.5);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  if (!enabled) {
    painter->setOpacity(0.45);
  }

  const QColor border = colors.border.isValid() ? colors.border : colors.off;
  painter->setPen(QPen(mix(border, colors.on), 1.0));
  painter->setBrush(mix(colors.off, colors.on));
  painter->drawRoundedRect(body, radius - 0.5, radius - 0.5);

  // Windows 11: the knob is small when off, larger when on, and grows a
  // little more under the mouse (5, 6 and +1 px in a 20 px track)
  const qreal knob = h * (0.25 + 0.05 * t + 0.05 * std::clamp(hover, 0.0, 1.0));
  const qreal x    = track.left() + radius + (track.width() - 2 * radius) * t;
  const QColor knobOff = colors.knobOff.isValid() ? colors.knobOff : border;
  const QColor knobOn  = colors.knobOn.isValid() ? colors.knobOn : Qt::white;
  painter->setPen(Qt::NoPen);
  painter->setBrush(mix(knobOff, knobOn));
  painter->drawEllipse(QPointF(x, track.center().y()), knob, knob);

  painter->restore();
}

void animate(QCheckBox* box)
{
  if (box == nullptr || box->findChild<FluentSwitchPainter*>(QString(),
                                                            Qt::FindDirectChildrenOnly)) {
    return;
  }
  new FluentSwitchPainter(box);
}

}  // namespace FluentSwitch

namespace
{
// Windows 11 toggles take a little longer than a fade
constexpr int ToggleDuration = 200;
}  // namespace

FluentSwitchPainter::FluentSwitchPainter(QCheckBox* box)
    : QWidget(box), m_box(box), m_position(FluentMotion::createAnimation(this)),
      m_hover(FluentMotion::createAnimation(this))
{
  setObjectName(QStringLiteral("fluentSwitchPainter"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);

  m_position->setStartValue(box->isChecked() ? 1.0 : 0.0);
  m_position->setEndValue(box->isChecked() ? 1.0 : 0.0);
  m_hover->setStartValue(0.0);
  m_hover->setEndValue(0.0);

  connect(m_position, &QVariantAnimation::valueChanged, this, [this] { update(); });
  connect(m_hover, &QVariantAnimation::valueChanged, this, [this] { update(); });

  connect(box, &QCheckBox::toggled, this, [this](bool on) {
    // position() already reads the new state; start from the shown one
    const qreal from = m_position->state() == QAbstractAnimation::Running
                           ? m_position->currentValue().toReal()
                           : (on ? 0.0 : 1.0);
    FluentMotion::start(m_position, from, on ? 1.0 : 0.0, ToggleDuration);
  });

  box->installEventFilter(this);
  // the row around a Settings switch toggles it too; hover the row as well
  if (QWidget* row = box->parentWidget();
      row != nullptr && row->objectName() == QLatin1String("fluentSettingRow")) {
    row->installEventFilter(this);
  }

  place();
  show();
}

bool FluentSwitchPainter::eventFilter(QObject* watched, QEvent* event)
{
  switch (event->type()) {
  case QEvent::Resize:
  case QEvent::Show:
  case QEvent::StyleChange:
  case QEvent::LayoutRequest:
    if (watched == m_box) {
      place();
    }
    break;
  case QEvent::Enter:
    FluentMotion::start(m_hover, m_hover->currentValue().toReal(), 1.0, FluentMotion::Fast);
    break;
  case QEvent::Leave:
    FluentMotion::start(m_hover, m_hover->currentValue().toReal(), 0.0, FluentMotion::Fast);
    break;
  case QEvent::EnabledChange:
    update();
    break;
  default:
    break;
  }
  return false;
}

void FluentSwitchPainter::place()
{
  // over the indicator the style sheet draws (40x20), so the switch keeps
  // its place and size in the row
  QStyleOptionButton option;
  option.initFrom(m_box);
  const QRect indicator =
      m_box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, m_box);
  setGeometry(indicator.isValid() ? indicator : m_box->rect());
  raise();
}

void FluentSwitchPainter::paintEvent(QPaintEvent*)
{
  const FluentSwitch::Colors colors{m_on, m_off, m_border, m_knobOn, m_knobOff};
  if (!colors.isValid()) {
    return;  // the theme has no colors for it; its indicator image shows
  }

  QPainter painter(this);
  // cover the theme's still image of the switch underneath
  FluentSwitch::paint(&painter, QRectF(rect()), position(),
                      m_hover->currentValue().toReal(), colors, m_box->isEnabled());
}

qreal FluentSwitchPainter::position() const
{
  // the box's state wins once the slide is over: code that sets it with its
  // signals blocked (the profiles dialog) sends no toggled() to follow
  if (m_position->state() == QAbstractAnimation::Running) {
    return m_position->currentValue().toReal();
  }
  return m_box->isChecked() ? 1.0 : 0.0;
}
