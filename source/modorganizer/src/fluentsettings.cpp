#include "fluentsettings.h"

#include <QAbstractItemView>
#include <QBoxLayout>
#include <QCheckBox>
#include <QEvent>
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
}  // namespace

namespace FluentSettings
{

void restyle(QWidget* root)
{
  if (root == nullptr) {
    return;
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
}

}  // namespace FluentSettings
