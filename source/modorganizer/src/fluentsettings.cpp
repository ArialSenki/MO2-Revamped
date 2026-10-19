#include "fluentsettings.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QScrollArea>
#include <QBoxLayout>
#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QStyle>
#include <QTextDocumentFragment>

namespace
{
constexpr int DescriptionLimit = 180;

// the first sentence of a (possibly rich text) tooltip, or nothing when the
// tooltip only repeats the label
QString describe(const QCheckBox* box)
{
  QString text = QTextDocumentFragment::fromHtml(box->toolTip()).toPlainText();
  text         = text.simplified();
  if (text.isEmpty()) {
    return {};
  }

  const QString label = box->text().remove(QLatin1Char('&')).simplified();
  if (text.compare(label, Qt::CaseInsensitive) == 0) {
    return {};
  }

  static const QRegularExpression sentenceEnd(QStringLiteral("[.!?](\\s|$)"));
  const auto match = sentenceEnd.match(text);
  if (match.hasMatch() && match.capturedStart() + 1 < text.size()) {
    text = text.left(match.capturedStart() + 1);
  }
  if (text.size() > DescriptionLimit) {
    text = text.left(DescriptionLimit - 1).trimmed() + QChar(0x2026);
  }
  return text;
}

// toggles the switch when the row or its labels are clicked, and dims the
// labels with the switch
class RowBehavior : public QObject
{
public:
  RowBehavior(QFrame* row, QCheckBox* box, QList<QLabel*> labels)
      : QObject(row), m_row(row), m_box(box), m_labels(std::move(labels))
  {
    row->installEventFilter(this);
    box->installEventFilter(this);
    for (QLabel* label : m_labels) {
      label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    syncEnabled();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (watched == m_box && event->type() == QEvent::EnabledChange) {
      syncEnabled();
    } else if (watched == m_row && event->type() == QEvent::MouseButtonRelease) {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() == Qt::LeftButton && m_box->isEnabled() &&
          m_row->rect().contains(mouse->position().toPoint())) {
        m_box->click();
        return true;
      }
    }
    return false;
  }

private:
  QFrame* m_row;
  QCheckBox* m_box;
  QList<QLabel*> m_labels;

  void syncEnabled()
  {
    for (QLabel* label : m_labels) {
      label->setEnabled(m_box->isEnabled());
    }
    m_row->setCursor(m_box->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }
};

void makeRow(QCheckBox* box)
{
  QWidget* parent = box->parentWidget();
  if (parent == nullptr || parent->layout() == nullptr) {
    return;
  }

  auto* row = new QFrame(parent);
  row->setObjectName(QStringLiteral("fluentSettingRow"));
  row->setProperty("fluentGridRow", qobject_cast<QGridLayout*>(parent->layout()) != nullptr);
  if (parent->layout()->replaceWidget(box, row) == nullptr) {
    delete row;
    return;
  }

  const QString title       = box->text();
  const QString description = describe(box);

  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(4, 8, 4, 8);
  layout->setSpacing(16);

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  auto* titleLabel = new QLabel(title, row);
  titleLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
  titleLabel->setTextFormat(Qt::PlainText);
  titleLabel->setWordWrap(true);
  text->addWidget(titleLabel);
  QList<QLabel*> labels = {titleLabel};
  if (!description.isEmpty()) {
    auto* descriptionLabel = new QLabel(description, row);
    descriptionLabel->setObjectName(QStringLiteral("fluentSettingDescription"));
    descriptionLabel->setTextFormat(Qt::PlainText);
    descriptionLabel->setWordWrap(true);
    text->addWidget(descriptionLabel);
    labels << descriptionLabel;
  }
  layout->addLayout(text, 1);
  layout->setAlignment(text, Qt::AlignVCenter);

  // the box keeps its name and tooltip for screen readers and the tutorial
  box->setAccessibleName(QString(title).remove(QLatin1Char('&')));
  box->setText(QString());
  box->setProperty("fluentSwitch", true);
  // the property selects the switch rules; repolish when the page is shown
  box->style()->unpolish(box);
  box->style()->polish(box);
  row->setToolTip(box->toolTip());
  layout->addWidget(box, 0, Qt::AlignVCenter | Qt::AlignRight);

  new RowBehavior(row, box, labels);
}

// settings code shows and hides some controls (per game, per option); the
// row that now holds a control follows it
class VisibilityMirror : public QObject
{
public:
  VisibilityMirror(QWidget* control, QWidget* row) : QObject(row), m_row(row)
  {
    control->installEventFilter(this);
    // only an explicit hide counts; moving into the row hid it implicitly
    const bool hidden = control->testAttribute(Qt::WA_WState_ExplicitShowHide) &&
                        control->testAttribute(Qt::WA_WState_Hidden);
    if (hidden) {
      m_row->hide();
    }
  }

protected:
  bool eventFilter(QObject*, QEvent* event) override
  {
    if (event->type() == QEvent::ShowToParent) {
      m_row->show();
    } else if (event->type() == QEvent::HideToParent) {
      m_row->hide();
    }
    return false;
  }

private:
  QWidget* m_row;
};

// pages without a scroll area of their own get one, so they can grow into a
// single column without clipping
void ensureScrolling(QWidget* page)
{
  if (page->layout() == nullptr || page->findChild<QScrollArea*>() != nullptr) {
    return;
  }

  auto* content = new QWidget;
  content->setObjectName(QStringLiteral("fluentPageContent"));
  // takes the page's layout and moves its widgets along
  content->setLayout(page->layout());

  auto* scroll = new QScrollArea(page);
  scroll->setObjectName(QStringLiteral("fluentPageScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(content);

  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(scroll);
}

// a settings card: a group box, or a container the pages name "...Card"
QWidget* asCard(QWidget* w)
{
  if (w == nullptr) {
    return nullptr;
  }
  if (qobject_cast<QGroupBox*>(w) != nullptr ||
      w->objectName().endsWith(QLatin1String("Card"), Qt::CaseInsensitive) ||
      w->property("fluentCard").toBool()) {
    return w;
  }
  return nullptr;
}

// cards laid out side by side become one column, like Windows 11 Settings
void singleColumn(QWidget* root)
{
  for (QGridLayout* grid : root->findChildren<QGridLayout*>()) {
    if (grid->columnCount() < 2 || grid->count() < 2) {
      continue;
    }

    // a cell holds a card, or a column layout of cards (pages build their
    // columns in code)
    struct Placed
    {
      QWidget* widget;
      int row, column, order;
    };
    std::vector<Placed> cards;
    std::vector<QLayout*> columns;
    bool onlyCards = true, sideBySide = false;
    for (int i = 0; i < grid->count() && onlyCards; ++i) {
      int row, column, rowSpan, columnSpan;
      grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
      QLayoutItem* item = grid->itemAt(i);
      sideBySide        = sideBySide || column > 0;
      if (QWidget* group = asCard(item->widget())) {
        cards.push_back({group, row, column, 0});
      } else if (auto* box = qobject_cast<QBoxLayout*>(item->layout())) {
        columns.push_back(box);
        for (int j = 0; j < box->count(); ++j) {
          QLayoutItem* inner = box->itemAt(j);
          if (inner->spacerItem() != nullptr) {
            continue;
          }
          QWidget* group = asCard(inner->widget());
          if (group == nullptr) {
            onlyCards = false;
            break;
          }
          cards.push_back({group, row, column, j});
        }
      } else if (item->spacerItem() == nullptr) {
        onlyCards = false;
      }
    }
    if (!onlyCards || !sideBySide || cards.size() < 2) {
      continue;
    }

    std::stable_sort(cards.begin(), cards.end(), [](auto&& a, auto&& b) {
      if (a.row != b.row) {
        return a.row < b.row;
      }
      return a.column != b.column ? a.column < b.column : a.order < b.order;
    });
    for (const auto& card : cards) {
      grid->removeWidget(card.widget);
      for (QLayout* column : columns) {
        column->removeWidget(card.widget);
      }
    }
    for (QLayout* column : columns) {
      grid->removeItem(column);
      delete column;
    }
    for (int column = 0; column < grid->columnCount(); ++column) {
      grid->setColumnStretch(column, column == 0 ? 1 : 0);
    }
    int row = 0;
    for (const auto& card : cards) {
      grid->setRowStretch(row, 0);
      grid->addWidget(card.widget, row++, 0);
    }
    // the cards stay at the top of the page
    grid->setRowStretch(row, 1);
  }
}

// cards in a horizontal box stack vertically instead
void stackCardRows(QWidget* root)
{
  for (QBoxLayout* box : root->findChildren<QBoxLayout*>()) {
    if (box->direction() != QBoxLayout::LeftToRight || box->count() < 2) {
      continue;
    }
    int cards = 0;
    bool onlyCards = true;
    for (int i = 0; i < box->count(); ++i) {
      QLayoutItem* item = box->itemAt(i);
      if (asCard(item->widget()) != nullptr) {
        ++cards;
      } else if (item->spacerItem() == nullptr) {
        onlyCards = false;
        break;
      }
    }
    if (onlyCards && cards >= 2) {
      box->setDirection(QBoxLayout::TopToBottom);
    }
  }
}

// grids of "label | option rowsâ€¦" (several switches per line) become a list:
// the label as a small heading, then one row per option
void flattenOptionGrids(QWidget* root)
{
  for (QGridLayout* grid : root->findChildren<QGridLayout*>()) {
    std::vector<std::vector<QWidget*>> lines(grid->rowCount());
    bool matches = false, other = false;
    for (int i = 0; i < grid->count(); ++i) {
      int row, column, rowSpan, columnSpan;
      grid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
      QWidget* w = grid->itemAt(i)->widget();
      if (w == nullptr) {
        if (grid->itemAt(i)->spacerItem() == nullptr) {
          other = true;
        }
        continue;
      }
      lines[row].push_back(w);
    }

    for (auto& line : lines) {
      std::sort(line.begin(), line.end(), [grid](QWidget* a, QWidget* b) {
        int ra, ca, rsa, csa, rb, cb, rsb, csb;
        grid->getItemPosition(grid->indexOf(a), &ra, &ca, &rsa, &csa);
        grid->getItemPosition(grid->indexOf(b), &rb, &cb, &rsb, &csb);
        return ca < cb;
      });
      // a row, or a plain container that lays out rows side by side
      const auto isRows = [](QWidget* w) {
        if (w->objectName() == QLatin1String("fluentSettingRow")) {
          return true;
        }
        auto* box = qobject_cast<QBoxLayout*>(w->layout());
        if (box == nullptr || box->count() == 0) {
          return false;
        }
        for (int i = 0; i < box->count(); ++i) {
          QWidget* inner = box->itemAt(i)->widget();
          if (inner == nullptr ? box->itemAt(i)->spacerItem() == nullptr
                               : inner->objectName() != QLatin1String("fluentSettingRow")) {
            return false;
          }
        }
        return true;
      };
      if (line.size() >= 2 && qobject_cast<QLabel*>(line[0]) != nullptr &&
          std::all_of(line.begin() + 1, line.end(), isRows)) {
        for (auto it = line.begin() + 1; it != line.end(); ++it) {
          if (auto* box = qobject_cast<QBoxLayout*>((*it)->layout())) {
            box->setDirection(QBoxLayout::TopToBottom);
            box->setSpacing(0);
            box->setContentsMargins(0, 0, 0, 0);
          }
        }
        matches = true;
      } else if (line.size() > 1) {
        other = true;
      }
    }
    if (!matches || other) {
      continue;
    }

    std::vector<QWidget*> order;
    for (auto& line : lines) {
      for (QWidget* w : line) {
        grid->removeWidget(w);
        order.push_back(w);
      }
      if (line.size() >= 2) {
        line[0]->setObjectName(QStringLiteral("fluentSettingSubheading"));
        line[0]->style()->unpolish(line[0]);
        line[0]->style()->polish(line[0]);
      }
    }
    const int span = std::max(1, grid->columnCount());
    for (int column = 0; column < span; ++column) {
      grid->setColumnStretch(column, column == 0 ? 1 : 0);
    }
    int row = 0;
    for (int r = 0; r < grid->rowCount(); ++r) {
      grid->setRowStretch(r, 0);
      grid->setRowMinimumHeight(r, 0);
    }
    grid->setVerticalSpacing(0);
    for (QWidget* w : order) {
      // sizes from the old side-by-side cells no longer apply
      w->setMinimumSize(0, 0);
      w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      grid->addWidget(w, row++, 0, 1, span);
    }
  }
}

bool isFieldControl(const QWidget* w)
{
  return qobject_cast<const QComboBox*>(w) != nullptr ||
         qobject_cast<const QAbstractSpinBox*>(w) != nullptr ||
         qobject_cast<const QLineEdit*>(w) != nullptr ||
         qobject_cast<const QKeySequenceEdit*>(w) != nullptr;
}

QWidget* widgetAt(QGridLayout* grid, int row, int column, int* columnSpan = nullptr)
{
  QLayoutItem* item = grid->itemAtPosition(row, column);
  if (item == nullptr || item->widget() == nullptr) {
    return nullptr;
  }
  if (columnSpan != nullptr) {
    int r, c, rs;
    grid->getItemPosition(grid->indexOf(item->widget()), &r, &c, &rs, columnSpan);
    if (c != column) {
      return nullptr;  // spans into this column from the left
    }
  }
  return item->widget();
}

// "label | control [| button]" rows of a grid become Settings rows: the
// title on the left, the control (and its button) on the right
// the same for form layouts: "label | field" where the field is a control or
// a row of a control and its buttons
void makeFormRows(QFormLayout* form)
{
  QWidget* parent = form->parentWidget();
  if (parent == nullptr) {
    return;
  }

  for (int row = 0; row < form->rowCount(); ++row) {
    QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
    QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
    auto* label = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
    if (label == nullptr || fieldItem == nullptr || label->text().trimmed().isEmpty()) {
      continue;
    }

    QWidget* control = fieldItem->widget();
    auto* fieldLayout = qobject_cast<QBoxLayout*>(fieldItem->layout());
    if (control == nullptr && fieldLayout != nullptr && fieldLayout->count() > 0) {
      control = fieldLayout->itemAt(0)->widget();
    }
    if (control == nullptr || !isFieldControl(control)) {
      continue;
    }

    const auto taken = form->takeRow(row);
    label->hide();

    auto* rowFrame = new QFrame(parent);
    rowFrame->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* layout = new QHBoxLayout(rowFrame);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(12);

    auto* title = new QLabel(label->text(), rowFrame);
    title->setObjectName(QStringLiteral("fluentSettingTitle"));
    title->setTextFormat(label->textFormat());
    title->setToolTip(label->toolTip());
    title->setBuddy(control);

    const bool wide = qobject_cast<QLineEdit*>(control) != nullptr;
    if (wide) {
      title->setMinimumWidth(170);
      layout->addWidget(title);
    } else {
      layout->addWidget(title, 1);
      if (qobject_cast<QAbstractSpinBox*>(control) != nullptr) {
        control->setFixedWidth(140);
      } else {
        control->setMinimumWidth(260);
        control->setMaximumWidth(360);
      }
    }

    if (fieldLayout != nullptr) {
      layout->addLayout(fieldLayout, wide ? 1 : 0);
    } else {
      layout->addWidget(control, wide ? 1 : 0);
    }
    delete taken.labelItem;
    if (taken.fieldItem != nullptr && taken.fieldItem->layout() == nullptr) {
      delete taken.fieldItem;
    }

    form->insertRow(row, rowFrame);
    new VisibilityMirror(control, rowFrame);
  }
}

void makeFormRows(QGridLayout* grid)
{
  QWidget* parent = grid->parentWidget();
  if (parent == nullptr) {
    return;
  }

  for (int row = 0; row < grid->rowCount(); ++row) {
    int labelSpan = 1;
    auto* label   = qobject_cast<QLabel*>(widgetAt(grid, row, 0, &labelSpan));
    QWidget* control = widgetAt(grid, row, 1);
    QWidget* button  = grid->columnCount() > 2 ? widgetAt(grid, row, 2) : nullptr;
    if (button == control) {
      button = nullptr;  // the control spans the remaining columns
    }
    if (label == nullptr || labelSpan != 1 || label->text().trimmed().isEmpty() ||
        control == nullptr || !isFieldControl(control)) {
      continue;
    }
    if (button != nullptr && qobject_cast<QAbstractButton*>(button) == nullptr) {
      continue;
    }

    auto* rowFrame = new QFrame(parent);
    rowFrame->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* layout = new QHBoxLayout(rowFrame);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(12);

    auto* title = new QLabel(label->text(), rowFrame);
    title->setObjectName(QStringLiteral("fluentSettingTitle"));
    title->setTextFormat(label->textFormat());
    title->setToolTip(label->toolTip());
    title->setBuddy(control);

    const bool wide = qobject_cast<QLineEdit*>(control) != nullptr;
    grid->removeWidget(label);
    label->hide();
    grid->removeWidget(control);
    if (button != nullptr) {
      grid->removeWidget(button);
    }

    if (wide) {
      // paths and text fields take the room they need
      title->setMinimumWidth(170);
      layout->addWidget(title);
      layout->addWidget(control, 1);
    } else {
      layout->addWidget(title, 1);
      if (qobject_cast<QAbstractSpinBox*>(control) != nullptr) {
        control->setFixedWidth(140);
      } else {
        control->setMinimumWidth(260);
        control->setMaximumWidth(360);
      }
      layout->addWidget(control);
    }
    if (button != nullptr) {
      layout->addWidget(button);
    }

    grid->addWidget(rowFrame, row, 0, 1, std::max(1, grid->columnCount()));
    new VisibilityMirror(control, rowFrame);
  }
}
}  // namespace

namespace FluentSettings
{

void restyle(QWidget* root, bool allowScroll)
{
  if (root == nullptr || root->property("fluentRestyled").toBool()) {
    return;
  }
  root->setProperty("fluentRestyled", true);

  if (allowScroll) {
    ensureScrolling(root);
  }
  singleColumn(root);
  stackCardRows(root);
  for (QGridLayout* grid : root->findChildren<QGridLayout*>()) {
    makeFormRows(grid);
  }
  for (QFormLayout* form : root->findChildren<QFormLayout*>()) {
    makeFormRows(form);
  }

  for (QCheckBox* box : root->findChildren<QCheckBox*>()) {
    // already a row, part of an item view, or explicitly kept as is
    if (box->property("fluentSwitch").toBool() || box->property("fluentKeepCheckBox").toBool() ||
        box->text().isEmpty()) {
      continue;
    }
    bool inView = false;
    for (QWidget* p = box->parentWidget(); p != nullptr && p != root; p = p->parentWidget()) {
      if (qobject_cast<QAbstractItemView*>(p) != nullptr) {
        inView = true;
        break;
      }
    }
    if (!inView) {
      makeRow(box);
    }
  }

  flattenOptionGrids(root);
}

}  // namespace FluentSettings

namespace FluentSettings
{

void wrapInScrollArea(QWidget* page)
{
  if (page != nullptr) {
    ensureScrolling(page);
  }
}

}  // namespace FluentSettings
