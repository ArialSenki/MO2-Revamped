#include "handlereditdialog.h"

#include "fluentstyle.h"
#include "handlerstorage.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace
{

QLabel* fieldLabel(const QString& text, QWidget* parent)
{
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("fieldLabel"));
  return label;
}

QLabel* caption(const QString& text, QWidget* parent)
{
  auto* label = new QLabel(text, parent);
  label->setProperty("caption", true);
  label->setWordWrap(true);
  return label;
}

// Fluent check boxes for the game list; a click anywhere on a row toggles it
class GameCheckDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    return QSize(QStyledItemDelegate::sizeHint(option, index).width(), 30);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    const Fluent::Theme& t = Fluent::theme();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF row = QRectF(option.rect).adjusted(1, 1, -1, -1);
    if (option.state & (QStyle::State_MouseOver | QStyle::State_HasFocus)) {
      QPainterPath path;
      path.addRoundedRect(row, 4, 4);
      painter->fillPath(path, t.hover);
    }
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    const QRectF box(row.left() + 8, row.center().y() - 9, 18, 18);
    QPainterPath boxPath;
    boxPath.addRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    if (checked) {
      painter->fillPath(boxPath, t.accent);
      QFont glyph(Fluent::iconFontFamily());
      glyph.setPixelSize(12);
      painter->setFont(glyph);
      painter->setPen(t.accentFg);
      painter->drawText(box, Qt::AlignCenter, QString(QChar(0xE73E)));
    } else {
      painter->fillPath(boxPath, t.field);
      painter->setPen(QPen(t.fieldBottom, 1));
      painter->drawPath(boxPath);
    }
    painter->setFont(option.font);
    painter->setPen(t.text);
    painter->drawText(QRectF(box.right() + 10, row.top(), row.right() - box.right() - 14, row.height()),
                      Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
  }

  bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem&,
                   const QModelIndex& index) override
  {
    bool toggle = false;
    if (event->type() == QEvent::MouseButtonRelease) {
      toggle = static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton;
    } else if (event->type() == QEvent::MouseButtonPress ||
               event->type() == QEvent::MouseButtonDblClick) {
      return static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton;
    } else if (event->type() == QEvent::KeyPress) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      toggle        = key == Qt::Key_Space || key == Qt::Key_Select;
    }
    if (!toggle) {
      return false;
    }
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
  }
};

}  // namespace

HandlerEditDialog::HandlerEditDialog(Mode mode, QWidget* parent) : QDialog(parent)
{
  setObjectName(QStringLiteral("HandlerEditDialog"));
  setWindowTitle(mode == Mode::Add ? tr("Add a program") : tr("Edit program"));
  setMinimumWidth(520);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("dialogContent"));
  content->setAttribute(Qt::WA_StyledBackground, true);
  auto* body = new QVBoxLayout(content);
  body->setContentsMargins(24, 20, 24, 24);
  body->setSpacing(6);

  auto* title = new QLabel(windowTitle(), content);
  title->setObjectName(QStringLiteral("pageTitle"));
  body->addWidget(title);

  auto* text = new QLabel(tr("Pick the program that should open Nexus links, usually a "
                             "ModOrganizer.exe, and the games it handles."),
                          content);
  text->setObjectName(QStringLiteral("dialogText"));
  text->setWordWrap(true);
  body->addWidget(text);
  body->addSpacing(12);

  // program
  body->addWidget(fieldLabel(tr("Program"), content));
  auto* programRow = new QHBoxLayout;
  programRow->setSpacing(8);
  m_Executable = new QLineEdit(content);
  m_Executable->setPlaceholderText(tr("Path to ModOrganizer.exe"));
  m_Executable->setClearButtonEnabled(true);
  programRow->addWidget(m_Executable, 1);
  auto* browseButton = new QPushButton(tr("Browse..."), content);
  browseButton->setAutoDefault(false);
  programRow->addWidget(browseButton);
  body->addLayout(programRow);
  m_Problem = caption(QString(), content);
  m_Problem->setObjectName(QStringLiteral("fieldProblem"));
  m_Problem->hide();
  body->addWidget(m_Problem);
  body->addSpacing(8);

  // arguments
  body->addWidget(fieldLabel(tr("Arguments (optional)"), content));
  m_Arguments = new QLineEdit(content);
  m_Arguments->setPlaceholderText(tr("Passed to the program before the link"));
  body->addWidget(m_Arguments);
  body->addSpacing(8);

  // games
  body->addWidget(fieldLabel(tr("Games"), content));
  body->addWidget(caption(tr("Links for these games go to this program."), content));
  m_Games = new QListWidget(content);
  m_Games->setObjectName(QStringLiteral("gameList"));
  m_Games->setSelectionMode(QAbstractItemView::NoSelection);
  m_Games->setFocusPolicy(Qt::StrongFocus);
  m_Games->setMinimumHeight(170);
  m_Games->setMouseTracking(true);
  m_Games->setItemDelegate(new GameCheckDelegate(m_Games));
  for (const auto& game : HandlerStorage::knownGames()) {
    auto* item = new QListWidgetItem(std::get<0>(game), m_Games);
    item->setData(Qt::UserRole, std::get<1>(game));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    if (std::get<1>(game) == QLatin1String("other")) {
      item->setText(tr("Other games (any game not handled above)"));
    }
  }
  body->addWidget(m_Games, 1);
  body->addSpacing(8);

  body->addWidget(fieldLabel(tr("Other game IDs"), content));
  m_OtherGames = new QLineEdit(content);
  m_OtherGames->setPlaceholderText(tr("For example: eldenring, starfield"));
  body->addWidget(m_OtherGames);
  body->addWidget(caption(tr("Nexus game IDs that are not in the list, separated by commas. "
                             "Mod Organizer adds its own game here when it registers."),
                          content));
  layout->addWidget(content, 1);

  // footer band with the buttons, as in MO2 Revamped's dialogs
  auto* footer = new QFrame(this);
  footer->setObjectName(QStringLiteral("footer"));
  auto* buttons = new QHBoxLayout(footer);
  buttons->setContentsMargins(24, 20, 24, 20);
  buttons->setSpacing(8);
  m_Accept = new QPushButton(mode == Mode::Add ? tr("Add") : tr("Save"), footer);
  m_Accept->setProperty("primary", true);
  m_Accept->setDefault(true);
  auto* cancel = new QPushButton(tr("Cancel"), footer);
  cancel->setAutoDefault(false);
  for (QPushButton* button : {m_Accept, cancel}) {
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumWidth(110);
    buttons->addWidget(button);
  }
  layout->addWidget(footer);

  connect(browseButton, &QPushButton::clicked, this, &HandlerEditDialog::browse);
  connect(m_Accept, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_Executable, &QLineEdit::textChanged, this, &HandlerEditDialog::updateState);

  updateState();
}

void HandlerEditDialog::setValues(const QStringList& games, const QString& executable,
                                  const QString& arguments)
{
  QStringList others;
  for (const QString& raw : games) {
    const QString game = raw.trimmed();
    if (game.isEmpty()) {
      continue;
    }
    bool known = false;
    for (int i = 0; i < m_Games->count(); ++i) {
      QListWidgetItem* item = m_Games->item(i);
      if (item->data(Qt::UserRole).toString().compare(game, Qt::CaseInsensitive) == 0) {
        item->setCheckState(Qt::Checked);
        known = true;
      }
    }
    if (!known && !others.contains(game, Qt::CaseInsensitive)) {
      others.append(game);
    }
  }
  m_OtherGames->setText(others.join(QStringLiteral(", ")));
  m_Executable->setText(QDir::toNativeSeparators(executable));
  m_Arguments->setText(arguments);
  updateState();
}

QStringList HandlerEditDialog::gameIDs() const
{
  QStringList result;
  for (int i = 0; i < m_Games->count(); ++i) {
    if (m_Games->item(i)->checkState() == Qt::Checked) {
      result.append(m_Games->item(i)->data(Qt::UserRole).toString());
    }
  }
  const QStringList others =
      m_OtherGames->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
  for (const QString& game : others) {
    if (!result.contains(game, Qt::CaseInsensitive)) {
      result.append(game.toLower());
    }
  }
  return result;
}

QString HandlerEditDialog::executable() const
{
  return QDir::toNativeSeparators(m_Executable->text().trimmed());
}

QString HandlerEditDialog::arguments() const
{
  return m_Arguments->text();
}

void HandlerEditDialog::browse()
{
  const QString start = QFileInfo(m_Executable->text().trimmed()).absolutePath();
  const QString file  = QFileDialog::getOpenFileName(
      this, tr("Select Executable"), m_Executable->text().trimmed().isEmpty() ? QString() : start,
      tr("Executable (*.exe)"));
  if (!file.isEmpty()) {
    m_Executable->setText(QDir::toNativeSeparators(file));
  }
}

void HandlerEditDialog::updateState()
{
  const QString path = m_Executable->text().trimmed();
  m_Accept->setEnabled(!path.isEmpty());
  if (!path.isEmpty() && !QFileInfo(path).isFile()) {
    m_Problem->setText(tr("This file does not exist. Links sent to it cannot be opened."));
    m_Problem->show();
  } else {
    m_Problem->hide();
  }
}
