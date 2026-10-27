#ifndef FLUENTCHOICES_H
#define FLUENTCHOICES_H

// Choices as cards, for the questions MO2 asks once (how to set up
// categories, which theme to use): Windows 11 settings cards with a glyph, a
// title and a line of description that act on a click, and theme tiles with a
// small picture of each theme. Both paint themselves in the active theme's
// colors, so they look right in the classic themes as well.

#include <QAbstractButton>
#include <QColor>

#include <functional>
#include <optional>

class FluentContentDialog;

// what the cards share: the subtle fill and stroke of a Windows 11 card, the
// hover and pressed fills, and the focus ring when reached by the keyboard
class FluentCardButton : public QAbstractButton
{
public:
  explicit FluentCardButton(QWidget* parent);

protected:
  // the card's own rectangle, inside the room kept for the focus ring
  QRectF cardRect() const;

  // paints the card; selected cards get the accent stroke
  void paintCard(QPainter& painter, bool selected) const;

  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  bool event(QEvent* event) override;

private:
  bool m_keyboardFocus = false;
};

// a settings card that acts when clicked: glyph, title (with its access
// key), a description below it and a chevron on the right
class FluentOptionCard : public FluentCardButton
{
public:
  FluentOptionCard(char16_t glyph, const QString& title, const QString& description,
                   QWidget* parent);

  // the recommended choice: its glyph is in the accent color
  void setRecommended(bool recommended);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  char16_t m_glyph;
  QString m_description;
  bool m_recommended = false;

  QFont descriptionFont() const;
  // the width left for the texts in a card of the given width
  int textWidth(int width) const;
};

// a theme tile: a small picture of the theme's window, its name, and the
// accent stroke and check mark when it is the chosen one
class FluentThemeCard : public FluentCardButton
{
public:
  struct Colors
  {
    QColor background, card, line, text, muted, accent, onAccent;
  };

  FluentThemeCard(const QString& name, const Colors& colors, QWidget* parent);

  // called on a double click: choose and close
  std::function<void()> activated;

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  Colors m_colors;
};

namespace FluentChoices
{

// adds an option card to the dialog's body; clicking it ends the dialog
// with result
FluentOptionCard* addOption(FluentContentDialog& dialog, char16_t glyph,
                            const QString& title, const QString& description, int result);

enum class Theme
{
  Light,
  Dark,
};

// asks for a theme with a tile each; nullopt when the window is closed
std::optional<Theme> chooseTheme(QWidget* parent, const QString& windowTitle,
                                 const QString& heading, const QString& description,
                                 const QString& lightLabel, const QString& darkLabel,
                                 const QString& acceptLabel,
                                 Theme current);

}  // namespace FluentChoices

#endif  // FLUENTCHOICES_H
