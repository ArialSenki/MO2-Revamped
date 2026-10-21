#ifndef FLUENTPANELS_H
#define FLUENTPANELS_H

// The pieces the manager dialogs share (instances, profiles, executables): a
// list card whose rows have an icon, a name and a caption, and a details card
// of Settings rows with a glyph, a title, a value and an optional button.

#include <QLabel>
#include <QStyledItemDelegate>

class QCheckBox;
class QFrame;
class QToolButton;

namespace FluentPanels
{

// item data the row delegate reads
constexpr int CaptionRole = Qt::UserRole + 1;  // the dimmed line below the name
constexpr int HeaderRole  = Qt::UserRole + 3;  // a group heading, not a row

// the rows of a list card: the item's icon, the name and its caption below
// it, a hover fill and the accent pill of the selected row like the Settings
// navigation; headings are semibold captions
class RowDelegate : public QStyledItemDelegate
{
public:
  explicit RowDelegate(QObject* parent, int rowHeight = 60, int iconSize = 32);

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;

private:
  int m_rowHeight;
  int m_iconSize;
};

// a label that elides its text in the middle, so a long path keeps its drive
// and its last folder; the whole text is in the tool tip
class ElidedLabel : public QLabel
{
public:
  using QLabel::QLabel;

  void setFullText(const QString& text);

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent*) override;

private:
  QString m_full;
};

// a Segoe Fluent glyph in a row's 24 px icon column
QLabel* glyphLabel(char16_t glyph, QWidget* parent);

// a borderless glyph button, like the row buttons of Settings
QToolButton* iconButton(char16_t glyph, const QString& tip, QWidget* parent);

struct DetailRow
{
  QFrame* frame       = nullptr;
  ElidedLabel* value  = nullptr;
  QToolButton* action = nullptr;
};

// appends a Settings row to the parent's layout: glyph, title with the value
// below it, and a glyph button on the right when tip is set
DetailRow addDetailRow(QWidget* parent, char16_t glyph, const QString& title,
                       const QString& tip = {}, char16_t actionGlyph = 0xE838);

// appends a Settings row with a switch: glyph, title, description, an
// optional value widget the switch turns on (a field, a picker, a button) and
// the check box drawn as a switch on the right. The row follows the box's
// visibility, so code that hides the box hides the whole row.
QFrame* addSwitchRow(QWidget* parent, char16_t glyph, const QString& title,
                     const QString& description, QCheckBox* box,
                     QWidget* value = nullptr);

// appends a Settings row whose value is edited in place: glyph, title and
// description on the left, the given widgets (a field and its browse button)
// on the right
QFrame* addEditRow(QWidget* parent, char16_t glyph, const QString& title,
                   const QString& description, const QList<QWidget*>& widgets);

}  // namespace FluentPanels

#endif  // FLUENTPANELS_H
