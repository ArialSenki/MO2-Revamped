#include "fluentchoices.h"
#include "fluentdialogs.h"
#include "fluenticons.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr qreal FocusRoom = 3;  // around the card, for the keyboard focus ring
constexpr qreal Radius    = 6;

// the option card's columns (logical pixels, from the card's edge)
constexpr int OptionPadLeft   = 16;
constexpr int OptionGlyph     = 24;
constexpr int OptionGap       = 14;
constexpr int OptionChevron   = 16;
constexpr int OptionPadRight  = 14;
constexpr int OptionPadY      = 12;
constexpr int OptionMinHeight = 64;

// the theme tile
constexpr int TileWidth   = 176;
constexpr int TilePreview = 100;
constexpr int TileName    = 34;
constexpr int TilePad     = 8;

constexpr char16_t ChevronRightGlyph = 0xE76C;
constexpr char16_t CheckGlyph        = 0xE73E;

QColor withAlpha(QColor color, int alpha)
{
  color.setAlpha(alpha);
  return color;
}

// the access key underline follows the style (Fluent shows it only while Alt
// is held, like Windows 11)
int mnemonicFlag(const QWidget* widget)
{
  return widget->style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, widget)
             ? Qt::TextShowMnemonic
             : Qt::TextHideMnemonic;
}

// text on the accent: dark on a light accent, light on a dark one
QColor onAccent(const QColor& accent)
{
  return accent.lightness() > 150 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

// the colors of each theme, from Build-FluentTheme.ps1
FluentThemeCard::Colors themeColors(FluentChoices::Theme theme)
{
  switch (theme) {
  case FluentChoices::Theme::Light:
    return {QColor(0xEFF1F4), QColor(0xFBFBFB), QColor(0xE5E5E5), QColor(0x1A1A1A),
            QColor(0x5D5D5D), QColor(0x005FB8), QColor(0xFFFFFF)};
  case FluentChoices::Theme::Dark:
    return {QColor(0x202226), QColor(0x2B2B2B), QColor(0x363636), QColor(0xFFFFFF),
            QColor(0xC5C5C5), QColor(0x4CC2FF), QColor(Qt::black)};
  }
  return {};
}
}  // namespace

// ---------------------------------------------------------------------------

FluentCardButton::FluentCardButton(QWidget* parent) : QAbstractButton(parent)
{
  setAttribute(Qt::WA_Hover);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
}

QRectF FluentCardButton::cardRect() const
{
  return QRectF(rect()).adjusted(FocusRoom + 0.5, FocusRoom + 0.5, -FocusRoom - 0.5,
                                 -FocusRoom - 0.5);
}

void FluentCardButton::paintCard(QPainter& p, bool selected) const
{
  const QColor ink    = FluentIcons::color(FluentIcons::Tone::Ink);
  const QColor accent = FluentIcons::color(FluentIcons::Tone::Accent);
  const QRectF card   = cardRect();

  // Windows 11 cards: a faint fill of the text color that deepens under the
  // mouse and lightens while pressed
  int fill = 8;
  if (!isEnabled()) {
    fill = 4;
  } else if (isDown()) {
    fill = 5;
  } else if (underMouse()) {
    fill = 15;
  }
  p.setPen(QPen(withAlpha(ink, isEnabled() ? 34 : 20), 1.0));
  p.setBrush(withAlpha(ink, fill));
  p.drawRoundedRect(card, Radius, Radius);

  if (selected) {
    p.setPen(QPen(accent, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), Radius - 0.5, Radius - 0.5);
  }

  if (hasFocus() && m_keyboardFocus) {
    p.setPen(QPen(ink, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(-2, -2, 2, 2), Radius + 2, Radius + 2);
  }
}

void FluentCardButton::focusInEvent(QFocusEvent* event)
{
  // the ring shows for the keyboard only, like Windows 11
  m_keyboardFocus = event->reason() == Qt::TabFocusReason ||
                    event->reason() == Qt::BacktabFocusReason ||
                    event->reason() == Qt::ShortcutFocusReason;
  QAbstractButton::focusInEvent(event);
  update();
}

void FluentCardButton::focusOutEvent(QFocusEvent* event)
{
  QAbstractButton::focusOutEvent(event);
  update();
}

void FluentCardButton::keyPressEvent(QKeyEvent* event)
{
  if (!m_keyboardFocus) {
    m_keyboardFocus = true;
    update();
  }
  // Enter acts on a card; a checkable tile leaves it to the dialog's
  // default button
  if (!isCheckable() &&
      (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
    animateClick();
    event->accept();
    return;
  }
  QAbstractButton::keyPressEvent(event);
}

bool FluentCardButton::event(QEvent* event)
{
  if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
    update();
  }
  return QAbstractButton::event(event);
}

// ---------------------------------------------------------------------------

FluentOptionCard::FluentOptionCard(char16_t glyph, const QString& title,
                                   const QString& description, QWidget* parent)
    : FluentCardButton(parent), m_glyph(glyph), m_description(description)
{
  setObjectName(QStringLiteral("fluentOptionCard"));
  // the title keeps its access key ("&Import...")
  setText(title);
  setAccessibleDescription(description);
  QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);
}

void FluentOptionCard::setRecommended(bool recommended)
{
  m_recommended = recommended;
  update();
}

QFont FluentOptionCard::descriptionFont() const
{
  // the theme's caption size (8.5 pt beside 9 pt text)
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.92);
  return f;
}

int FluentOptionCard::textWidth(int width) const
{
  return std::max(80, width - static_cast<int>(2 * FocusRoom) - OptionPadLeft -
                          OptionGlyph - OptionGap - OptionGap - OptionChevron -
                          OptionPadRight);
}

int FluentOptionCard::heightForWidth(int width) const
{
  const int w      = textWidth(width);
  const int flags  = Qt::TextWordWrap | mnemonicFlag(this);
  const int title  = fontMetrics().boundingRect(QRect(0, 0, w, 10000), flags, text()).height();
  const int detail = m_description.isEmpty()
                         ? 0
                         : 2 + QFontMetrics(descriptionFont())
                                   .boundingRect(QRect(0, 0, w, 10000), Qt::TextWordWrap,
                                                 m_description)
                                   .height();
  return std::max(OptionMinHeight, OptionPadY + title + detail + OptionPadY) +
         static_cast<int>(2 * FocusRoom);
}

QSize FluentOptionCard::sizeHint() const
{
  const int width = 420;
  return QSize(width, heightForWidth(width));
}

QSize FluentOptionCard::minimumSizeHint() const
{
  return QSize(260, OptionMinHeight + static_cast<int>(2 * FocusRoom));
}

void FluentOptionCard::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  paintCard(p, false);

  const QRectF card = cardRect();
  const qreal fade  = isEnabled() ? 1.0 : 0.45;
  p.setOpacity(fade);

  // glyph, in the accent for the recommended choice
  p.setFont(FluentIcons::font(16));
  p.setPen(FluentIcons::color(m_recommended ? FluentIcons::Tone::Accent
                                            : FluentIcons::Tone::Ink));
  p.drawText(QRectF(card.left() + OptionPadLeft, card.top(), OptionGlyph, card.height()),
             Qt::AlignCenter, QString(QChar(m_glyph)));

  // the chevron that says the card goes on
  p.setFont(FluentIcons::font(12));
  p.setPen(FluentIcons::color(FluentIcons::Tone::Muted));
  p.drawText(QRectF(card.right() - OptionPadRight - OptionChevron, card.top(),
                    OptionChevron, card.height()),
             Qt::AlignCenter, QString(QChar(ChevronRightGlyph)));

  // title and description, centered as a block
  const int w          = textWidth(width());
  const int titleFlags = Qt::TextWordWrap | mnemonicFlag(this);
  const QFont detailFont = descriptionFont();
  const int titleH =
      fontMetrics().boundingRect(QRect(0, 0, w, 10000), titleFlags, text()).height();
  const int detailH = m_description.isEmpty()
                          ? 0
                          : QFontMetrics(detailFont)
                                .boundingRect(QRect(0, 0, w, 10000), Qt::TextWordWrap,
                                              m_description)
                                .height();
  const int blockH = titleH + (detailH > 0 ? 2 + detailH : 0);
  const qreal left = card.left() + OptionPadLeft + OptionGlyph + OptionGap;
  qreal top        = card.top() + std::max(0.0, (card.height() - blockH) / 2.0);

  p.setFont(font());
  p.setPen(FluentIcons::color(FluentIcons::Tone::Ink));
  p.drawText(QRectF(left, top, w, titleH), Qt::AlignLeft | titleFlags, text());

  if (detailH > 0) {
    top += titleH + 2;
    p.setFont(detailFont);
    p.setPen(FluentIcons::color(FluentIcons::Tone::Muted));
    p.drawText(QRectF(left, top, w, detailH), Qt::AlignLeft | Qt::TextWordWrap,
               m_description);
  }
}

// ---------------------------------------------------------------------------

FluentThemeCard::FluentThemeCard(const QString& name, const Colors& colors,
                                 QWidget* parent)
    : FluentCardButton(parent), m_colors(colors)
{
  setObjectName(QStringLiteral("fluentThemeCard"));
  setText(name);
  setCheckable(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize FluentThemeCard::sizeHint() const
{
  return QSize(TileWidth,
               TilePad + TilePreview + TileName + static_cast<int>(2 * FocusRoom));
}

void FluentThemeCard::mouseDoubleClickEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && activated) {
    setChecked(true);
    activated();
    return;
  }
  FluentCardButton::mouseDoubleClickEvent(event);
}

void FluentThemeCard::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  paintCard(p, isChecked());

  const QRectF card = cardRect();
  const auto& c     = m_colors;

  // the picture: a window of the theme with its navigation on the left and a
  // card with a heading, two lines of text and an accent button
  const QRectF preview(card.left() + TilePad, card.top() + TilePad,
                       card.width() - 2 * TilePad, TilePreview);
  p.setPen(QPen(c.line, 1.0));
  p.setBrush(c.background);
  p.drawRoundedRect(preview, 4, 4);

  p.setPen(Qt::NoPen);
  const auto bar = [&p](const QRectF& r, const QColor& color) {
    p.setBrush(color);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
  };

  // navigation: the selected item filled and marked by the accent
  const qreal navLeft  = preview.left() + 6;
  const qreal navWidth = preview.width() * 0.28;
  p.setBrush(withAlpha(c.text, 22));
  p.drawRoundedRect(QRectF(navLeft, preview.top() + 8, navWidth, 12), 3, 3);
  bar(QRectF(navLeft + 1, preview.top() + 11, 2.5, 6), c.accent);
  bar(QRectF(navLeft + 7, preview.top() + 12.5, navWidth * 0.62, 3), c.text);
  for (int i = 1; i <= 3; ++i) {
    bar(QRectF(navLeft + 7, preview.top() + 12.5 + i * 13, navWidth * (0.7 - i * 0.1), 3),
        withAlpha(c.muted, 170));
  }

  // the content card
  const QRectF content(navLeft + navWidth + 6, preview.top() + 8,
                       preview.right() - 6 - (navLeft + navWidth + 6),
                       preview.height() - 16);
  p.setPen(QPen(c.line, 1.0));
  p.setBrush(c.card);
  p.drawRoundedRect(content, 3, 3);
  p.setPen(Qt::NoPen);
  bar(QRectF(content.left() + 7, content.top() + 8, content.width() * 0.55, 5), c.text);
  bar(QRectF(content.left() + 7, content.top() + 19, content.width() * 0.75, 3),
      withAlpha(c.muted, 190));
  bar(QRectF(content.left() + 7, content.top() + 26, content.width() * 0.6, 3),
      withAlpha(c.muted, 190));
  const QRectF button(content.right() - 7 - 28, content.bottom() - 7 - 10, 28, 10);
  bar(button, c.accent);
  bar(QRectF(button.left() + 7, button.center().y() - 1, 14, 2), c.onAccent);

  // the name, and the check mark of the chosen theme
  const QRectF nameRow(card.left() + TilePad + 4, preview.bottom(),
                       card.width() - 2 * TilePad - 4, TileName);
  QFont nameFont = font();
  if (isChecked()) {
    nameFont.setWeight(QFont::DemiBold);
  }
  p.setFont(nameFont);
  p.setPen(FluentIcons::color(FluentIcons::Tone::Ink));
  const int badge = 18;
  p.drawText(nameRow.adjusted(0, 0, -(badge + 4), 0),
             Qt::AlignLeft | Qt::AlignVCenter | mnemonicFlag(this),
             QFontMetrics(nameFont).elidedText(text(), Qt::ElideRight,
                                               static_cast<int>(nameRow.width()) - badge - 4));

  if (isChecked()) {
    const QColor accent = FluentIcons::color(FluentIcons::Tone::Accent);
    const QRectF circle(nameRow.right() - badge, nameRow.center().y() - badge / 2.0, badge,
                        badge);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(circle);
    p.setFont(FluentIcons::font(11));
    p.setPen(onAccent(accent));
    p.drawText(circle, Qt::AlignCenter, QString(QChar(CheckGlyph)));
  }
}

// ---------------------------------------------------------------------------

namespace FluentChoices
{

FluentOptionCard* addOption(FluentContentDialog& dialog, char16_t glyph,
                            const QString& title, const QString& description, int result)
{
  // the cards sit close together, like a group of Settings cards; a layout
  // rather than a widget, which the theme would paint in the window color
  auto* list = dialog.findChild<QVBoxLayout*>(QStringLiteral("fluentOptionList"));
  if (list == nullptr) {
    list = new QVBoxLayout;
    list->setObjectName(QStringLiteral("fluentOptionList"));
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(0);
    dialog.body()->addLayout(list);
  }

  auto* card = new FluentOptionCard(glyph, title, description, &dialog);
  list->addWidget(card);
  QObject::connect(card, &QAbstractButton::clicked, &dialog, [&dialog, result] {
    dialog.done(result);
  });
  return card;
}

std::optional<Theme> chooseTheme(QWidget* parent, const QString& windowTitle,
                                 const QString& heading, const QString& description,
                                 const QString& lightLabel, const QString& darkLabel,
                                 const QString& acceptLabel,
                                 Theme current)
{
  FluentContentDialog dialog(parent, QStringLiteral("ThemeChoiceDialog"), heading,
                             description);
  dialog.setWindowTitle(windowTitle);

  auto* row = new QHBoxLayout;
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(6);

  auto* group = new QButtonGroup(&dialog);
  group->setExclusive(true);

  struct Entry
  {
    Theme theme;
    QString label;
  };
  const Entry entries[] = {
      {Theme::Light, lightLabel},
      {Theme::Dark, darkLabel},
  };

  FluentThemeCard* chosen = nullptr;
  for (const auto& entry : entries) {
    auto* card = new FluentThemeCard(entry.label, themeColors(entry.theme), &dialog);
    card->setProperty("fluentTheme", static_cast<int>(entry.theme));
    card->activated = [&dialog] {
      dialog.accept();
    };
    group->addButton(card);
    row->addWidget(card, 1);
    if (entry.theme == current) {
      card->setChecked(true);
      chosen = card;
    }
  }
  dialog.body()->addLayout(row);

  auto* accept = dialog.addButton(acceptLabel, true);
  QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);

  // the arrow keys move the choice between the tiles (they are a button
  // group); Enter takes the default button
  if (chosen != nullptr) {
    chosen->setFocus(Qt::ActiveWindowFocusReason);
  }
  dialog.resize(dialog.sizeHint());

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  if (auto* checked = group->checkedButton()) {
    return static_cast<Theme>(checked->property("fluentTheme").toInt());
  }
  return current;
}

}  // namespace FluentChoices
