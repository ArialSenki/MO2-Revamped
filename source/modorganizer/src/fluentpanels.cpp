#include "fluentpanels.h"
#include "fluenticons.h"
#include "fluentswitch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace FluentPanels
{

namespace
{
QColor mutedInk(const QStyleOptionViewItem& option)
{
  return FluentIcons::active()
             ? FluentIcons::color(FluentIcons::Tone::Muted)
             : option.palette.color(QPalette::Disabled, QPalette::WindowText);
}

// shows and hides a row along with the control it holds
class VisibilityMirror : public QObject
{
public:
  VisibilityMirror(QWidget* source, QWidget* target) : QObject(source), m_target(target)
  {
    source->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::ShowToParent) {
      m_target->show();
    } else if (event->type() == QEvent::HideToParent) {
      m_target->hide();
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QWidget* m_target;
};
}  // namespace

constexpr int HeaderHeight = 34;

RowDelegate::RowDelegate(QObject* parent, int rowHeight, int iconSize)
    : QStyledItemDelegate(parent), m_rowHeight(rowHeight), m_iconSize(iconSize)
{}

QSize RowDelegate::sizeHint(const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
  return QSize(QStyledItemDelegate::sizeHint(option, index).width(),
               index.data(HeaderRole).toBool() ? HeaderHeight : m_rowHeight);
}

void RowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const
{
  const bool selected = option.state & QStyle::State_Selected;
  const bool hovered  = option.state & QStyle::State_MouseOver;
  const QColor ink    = option.palette.color(QPalette::Active, QPalette::WindowText);

  if (index.data(HeaderRole).toBool()) {
    // a group heading: semibold caption text, no hover
    painter->save();
    QFont heading = option.font;
    heading.setWeight(QFont::DemiBold);
    heading.setPointSizeF(option.font.pointSizeF() * 0.92);
    painter->setFont(heading);
    painter->setPen(mutedInk(option));
    painter->drawText(QRectF(option.rect).adjusted(10, 0, -8, -5),
                      Qt::AlignLeft | Qt::AlignBottom,
                      QFontMetrics(heading).elidedText(index.data().toString(),
                                                       Qt::ElideRight,
                                                       option.rect.width() - 18));
    painter->restore();
    return;
  }

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
  if (selected || hovered) {
    QColor fill = ink;
    fill.setAlpha(selected ? 26 : 15);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(row, 4, 4);
  }
  if (selected) {
    painter->setBrush(FluentIcons::active() ? FluentIcons::color(FluentIcons::Tone::Accent)
                                            : option.palette.color(QPalette::Highlight));
    painter->drawRoundedRect(QRectF(row.left(), row.center().y() - 8, 3, 16), 1.5, 1.5);
  }

  const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
  const int iconLeft = static_cast<int>(row.left()) + 12;
  if (!icon.isNull()) {
    const QRect iconRect(iconLeft, static_cast<int>(row.center().y()) - m_iconSize / 2,
                         m_iconSize, m_iconSize);
    icon.paint(painter, iconRect);
  }

  const int textLeft = icon.isNull() ? 14 : 12 + m_iconSize + 12;
  const QRectF text  = row.adjusted(textLeft, 8, -10, -8);
  const QString caption = index.data(CaptionRole).toString();

  QFont name = option.font;
  name.setWeight(selected ? QFont::DemiBold : QFont::Normal);
  painter->setFont(name);
  painter->setPen(ink);
  const QFontMetrics nameMetrics(name);
  const QRectF nameRect =
      caption.isEmpty() ? text
                        : QRectF(text.left(), text.top(), text.width(), text.height() / 2);
  painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                    nameMetrics.elidedText(index.data().toString(), Qt::ElideRight,
                                           static_cast<int>(text.width())));

  if (!caption.isEmpty()) {
    QFont captionFont = option.font;
    captionFont.setPointSizeF(option.font.pointSizeF() * 0.9);
    painter->setFont(captionFont);
    painter->setPen(mutedInk(option));
    painter->drawText(QRectF(text.left(), text.center().y(), text.width(), text.height() / 2),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(captionFont).elidedText(caption, Qt::ElideRight,
                                                     static_cast<int>(text.width())));
  }
  painter->restore();
}

void ElidedLabel::setFullText(const QString& text)
{
  m_full = text;
  setToolTip(text);
  updateGeometry();
  update();
}

QSize ElidedLabel::minimumSizeHint() const
{
  return QSize(40, QLabel::minimumSizeHint().height());
}

QSize ElidedLabel::sizeHint() const
{
  return QSize(fontMetrics().horizontalAdvance(m_full), fontMetrics().height());
}

void ElidedLabel::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setPen(palette().color(QPalette::WindowText));
  p.setFont(font());
  p.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
             fontMetrics().elidedText(m_full, Qt::ElideMiddle, contentsRect().width()));
}

QLabel* glyphLabel(char16_t glyph, QWidget* parent)
{
  auto* label = new QLabel(QString(QChar(glyph)), parent);
  label->setObjectName(QStringLiteral("instanceRowGlyph"));
  label->setFont(FluentIcons::font(16));
  label->setFixedWidth(24);
  label->setAlignment(Qt::AlignCenter);
  return label;
}

QToolButton* iconButton(char16_t glyph, const QString& tip, QWidget* parent)
{
  auto* button = new QToolButton(parent);
  button->setObjectName(QStringLiteral("instanceIconButton"));
  button->setIcon(FluentIcons::glyph(glyph));
  button->setAutoRaise(true);
  button->setToolTip(tip);
  button->setAccessibleName(tip);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

DetailRow addDetailRow(QWidget* parent, char16_t glyph, const QString& title,
                       const QString& tip, char16_t actionGlyph)
{
  DetailRow row;
  row.frame = new QFrame(parent);
  row.frame->setObjectName(QStringLiteral("fluentSettingRow"));
  row.frame->setMinimumHeight(62);

  auto* layout = new QHBoxLayout(row.frame);
  layout->setContentsMargins(18, 8, 12, 8);
  layout->setSpacing(14);
  layout->addWidget(glyphLabel(glyph, row.frame));

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  auto* titleLabel = new QLabel(title, row.frame);
  titleLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
  row.value = new ElidedLabel(row.frame);
  row.value->setObjectName(QStringLiteral("instanceDetailValue"));
  text->addWidget(titleLabel);
  text->addWidget(row.value);
  layout->addLayout(text, 1);

  if (!tip.isEmpty()) {
    row.action = iconButton(actionGlyph, tip, row.frame);
    layout->addWidget(row.action);
  }

  parent->layout()->addWidget(row.frame);
  return row;
}

namespace
{
// the left part of a Settings row: the frame, its glyph and its texts
QHBoxLayout* rowWithText(QFrame* frame, char16_t glyph, const QString& title,
                         const QString& description)
{
  frame->setObjectName(QStringLiteral("fluentSettingRow"));
  frame->setMinimumHeight(62);

  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(18, 8, 12, 8);
  layout->setSpacing(14);
  layout->addWidget(glyphLabel(glyph, frame));

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  auto* titleLabel = new QLabel(title, frame);
  titleLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
  text->addWidget(titleLabel);
  if (!description.isEmpty()) {
    auto* descriptionLabel = new QLabel(description, frame);
    descriptionLabel->setObjectName(QStringLiteral("fluentSettingDescription"));
    descriptionLabel->setWordWrap(true);
    text->addWidget(descriptionLabel);
  }
  layout->addLayout(text, 1);
  return layout;
}
}  // namespace

QFrame* addEditRow(QWidget* parent, char16_t glyph, const QString& title,
                   const QString& description, const QList<QWidget*>& widgets)
{
  auto* frame  = new QFrame(parent);
  auto* layout = rowWithText(frame, glyph, title, description);

  // the field takes the room the texts leave; buttons keep their size
  for (QWidget* w : widgets) {
    w->setParent(frame);
    const bool field = qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w);
    if (field) {
      w->setMinimumWidth(300);
    }
    layout->addWidget(w, field ? 2 : 0, Qt::AlignVCenter);
    w->show();
  }

  parent->layout()->addWidget(frame);
  return frame;
}

QFrame* addSwitchRow(QWidget* parent, char16_t glyph, const QString& title,
                     const QString& description, QCheckBox* box, QWidget* value)
{
  auto* frame  = new QFrame(parent);
  auto* layout = rowWithText(frame, glyph, title, description);

  if (value != nullptr) {
    value->setParent(frame);
    layout->addWidget(value, 0, Qt::AlignVCenter);
    value->show();
  }

  // the box keeps its name for screen readers and for plugins that look it up;
  // a new parent hides it, so it shows again unless its own code had hidden it
  const bool hiddenByCode =
      box->testAttribute(Qt::WA_WState_ExplicitShowHide) && box->isHidden();
  box->setParent(frame);
  if (!hiddenByCode) {
    box->show();
  }
  box->setText(QString());
  box->setAccessibleName(title);
  box->setAccessibleDescription(description);
  box->setProperty("fluentSwitch", true);
  box->setCursor(Qt::PointingHandCursor);
  if (FluentIcons::active()) {
    box->setProperty("fluentAnimated", true);
  }
  // the properties select the switch rules, so the style must look again
  box->style()->unpolish(box);
  box->style()->polish(box);
  if (FluentIcons::active()) {
    FluentSwitch::animate(box);
  }
  layout->addWidget(box, 0, Qt::AlignVCenter | Qt::AlignRight);
  new VisibilityMirror(box, frame);

  parent->layout()->addWidget(frame);
  return frame;
}

}  // namespace FluentPanels
