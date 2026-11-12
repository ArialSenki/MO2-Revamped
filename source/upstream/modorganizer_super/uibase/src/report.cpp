/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "report.h"
#include "expanderwidget.h"
#include "log.h"
#include "questionboxmemory.h"
#include "fluentcontentdialog.h"
#include "utility.h"
#include <QComboBox>
#include <QCommandLinkButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTextDocument>
#include <QVBoxLayout>
#include <cmath>
#include <QRadioButton>
#include <Windows.h>

namespace Ui
{
class TaskDialog
{};
}  // namespace Ui

namespace MOBase
{

QWidget* topLevelWindow()
{
  if (!qApp) {
    return nullptr;
  }

  for (QWidget* w : qApp->topLevelWidgets()) {
    if (dynamic_cast<QMainWindow*>(w)) {
      return w;
    }
  }

  return nullptr;
}

void criticalOnTop(const QString& message)
{
  QMessageBox mb(QMessageBox::Critical, "Mod Organizer", message);

  mb.show();
  mb.activateWindow();
  mb.raise();
  mb.exec();
}

void reportError(const QString& message)
{
  log::error("{}", message);

  if (QApplication::topLevelWidgets().count() != 0) {
    if (auto* mw = topLevelWindow()) {
      QMessageBox::warning(mw, QObject::tr("Error"), message, QMessageBox::Ok);
    } else {
      criticalOnTop(message);
    }
  } else {
    ::MessageBoxW(0, message.toStdWString().c_str(),
                  QObject::tr("Error").toStdWString().c_str(), MB_ICONERROR | MB_OK);
  }
}

TaskDialogButton::TaskDialogButton(QString t, QString d, QMessageBox::StandardButton b)
    : text(std::move(t)), description(std::move(d)), button(b)
{}

TaskDialogButton::TaskDialogButton(QString t, QMessageBox::StandardButton b)
    : TaskDialogButton(std::move(t), {}, b)
{}

// MO2 Revamped: TaskDialog is built in code as a Windows 11 content dialog
// instead of the .ui of Mod Organizer 2: the main text is the title, the
// content its text, choices with a description are option cards (MO2 draws
// them from the command links marked "revampedOptionCard"), the others and
// OK are the band's buttons, and the details open under a Details button.
// The class keeps its members and functions, so its size and exports stay
// as they were; Ui::TaskDialog is now empty.

TaskDialog::TaskDialog(QWidget* parent, QString title)
    : m_dialog(new QDialog(parent)), m_title(std::move(title)),
      m_icon(QMessageBox::NoIcon), m_result(QMessageBox::Cancel), m_width(-1),
      m_rememberCheck(nullptr), m_rememberCombo(nullptr)
{
  m_dialog->setObjectName(QStringLiteral("RevampedTaskDialog"));
  // what addContent() adds waits here until the dialog is built
  auto* extra = new QWidget(m_dialog.get());
  extra->setObjectName(QStringLiteral("taskExtraContent"));
  auto* extraLayout = new QVBoxLayout(extra);
  extraLayout->setContentsMargins(0, 0, 0, 0);
  extraLayout->setSpacing(8);
  extra->hide();
}

TaskDialog::~TaskDialog() = default;

TaskDialog& TaskDialog::title(const QString& s)
{
  m_title = s;
  return *this;
}

TaskDialog& TaskDialog::main(const QString& s)
{
  m_main = s;
  return *this;
}

TaskDialog& TaskDialog::content(const QString& s)
{
  m_content = s;
  return *this;
}

TaskDialog& TaskDialog::details(const QString& s)
{
  m_details = s;
  return *this;
}

TaskDialog& TaskDialog::icon(QMessageBox::Icon i)
{
  m_icon = i;
  return *this;
}

TaskDialog& TaskDialog::button(TaskDialogButton b)
{
  m_buttons.emplace_back(std::move(b));
  return *this;
}

TaskDialog& TaskDialog::remember(const QString& action, const QString& file)
{
  m_rememberAction = action;
  m_rememberFile   = file;
  return *this;
}

void TaskDialog::addContent(QWidget* w)
{
  if (auto* extra = m_dialog->findChild<QWidget*>(QStringLiteral("taskExtraContent"))) {
    extra->layout()->addWidget(w);
  }
}

void TaskDialog::setWidth(int w)
{
  m_width = w;
}

QMessageBox::StandardButton TaskDialog::exec()
{
  const auto b = checkMemory();
  if (b != QMessageBox::NoButton) {
    return b;
  }

  setDialog();
  setWidgets();
  setChoices();
  setButtons();
  setDetails();

  m_dialog->setMinimumWidth(std::max(440, m_width));
  m_dialog->adjustSize();

  if (m_dialog->exec() != QDialog::Accepted) {
    return QMessageBox::Cancel;
  }

  rememberChoice();
  return m_result;
}

QMessageBox::StandardButton TaskDialog::checkMemory() const
{
  if (m_rememberAction.isEmpty() && m_rememberFile.isEmpty()) {
    return QMessageBox::NoButton;
  }

  const auto b = QuestionBoxMemory::getMemory(m_rememberAction, m_rememberFile);

  const auto logName = m_rememberAction +
                       (m_rememberFile.isEmpty() ? "" : QString("/") + m_rememberFile);

  if (b == QDialogButtonBox::NoButton) {
    log::debug("{}: asking because the user has not set a choice before", logName);
  } else {
    log::debug("{}: not asking because user always wants response {}", logName,
               QuestionBoxMemory::buttonToString(b));
  }

  return static_cast<QMessageBox::StandardButton>(b);
}

void TaskDialog::rememberChoice()
{
  if (m_rememberAction.isEmpty() && m_rememberFile.isEmpty()) {
    // nothing
    return;
  }

  const auto b = static_cast<QuestionBoxMemory::Button>(m_result);

  if (m_rememberCheck) {
    // action only
    if (m_rememberCheck->isChecked()) {
      QuestionBoxMemory::setWindowMemory(m_rememberAction, b);
    }
  } else if (m_rememberCombo) {
    Q_ASSERT(m_rememberCombo->count() == 3);

    if (m_rememberCombo->currentIndex() == 1) {
      // remember action
      QuestionBoxMemory::setWindowMemory(m_rememberAction, b);
    } else if (m_rememberCombo->currentIndex() == 2) {
      // remember file
      QuestionBoxMemory::setFileMemory(m_rememberAction, m_rememberFile, b);
    }
  }
}

namespace
{

// the parts setDialog() built: found again by the steps after it
QVBoxLayout* taskBody(QDialog* dialog)
{
  auto* content = dialog->findChild<QWidget*>(QStringLiteral("fluentDialogContent"));
  if (content == nullptr || content->layout() == nullptr) {
    return nullptr;
  }
  // the content's layout: title, text, then the body
  auto* layout = static_cast<QVBoxLayout*>(content->layout());
  for (int i = 0; i < layout->count(); ++i) {
    if (auto* body = qobject_cast<QVBoxLayout*>(layout->itemAt(i)->layout())) {
      return body;
    }
  }
  return nullptr;
}

QHBoxLayout* taskBand(QDialog* dialog)
{
  auto* band = dialog->findChild<QFrame*>(QStringLiteral("fluentDialogFooter"));
  return band != nullptr ? qobject_cast<QHBoxLayout*>(band->layout()) : nullptr;
}

}  // namespace

void TaskDialog::setDialog()
{
  // built once: exec() may run again on the same dialog
  if (m_dialog->property("fluentContentDialog").toBool()) {
    return;
  }
  RevampedFluent::buildContent(m_dialog.get(), m_title, m_content);
  m_dialog->setWindowTitle(m_title);
}

void TaskDialog::setWidgets()
{
  // the main text is the dialog's title; without one, the window's title.
  // Both may hold rich text, as in Mod Organizer 2 (callers bold a folder)
  auto* title = m_dialog->findChild<QLabel*>(QStringLiteral("fluentDialogTitle"));
  if (title != nullptr) {
    title->setTextFormat(Qt::AutoText);
    title->setText(m_main.isEmpty() ? m_title : m_main);
  }
  if (auto* text = m_dialog->findChild<QLabel*>(QStringLiteral("fluentDialogText"))) {
    text->setTextFormat(Qt::AutoText);
    text->setText(m_content);
    text->setVisible(!m_content.isEmpty());
  }

  // an error, a warning or a notice shows its sign by the title; a question
  // asks in words, as Windows 11 dialogs do
  if (title != nullptr && m_icon != QMessageBox::NoIcon && m_icon != QMessageBox::Question &&
      m_dialog->findChild<QLabel*>(QStringLiteral("taskIcon")) == nullptr) {
    auto* layout = qobject_cast<QVBoxLayout*>(title->parentWidget()->layout());
    const QPixmap sign = standardIcon(m_icon);
    if (layout != nullptr && !sign.isNull()) {
      const int at = layout->indexOf(title);
      auto* row    = new QHBoxLayout;
      row->setSpacing(12);
      auto* icon = new QLabel(title->parentWidget());
      icon->setObjectName(QStringLiteral("taskIcon"));
      const qreal ratio = m_dialog->devicePixelRatioF();
      QPixmap scaled    = sign.scaled(QSize(28, 28) * ratio, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
      scaled.setDevicePixelRatio(ratio);
      icon->setPixmap(scaled);
      icon->setAlignment(Qt::AlignTop);
      layout->removeWidget(title);
      row->addWidget(icon, 0, Qt::AlignTop);
      row->addWidget(title, 1);
      layout->insertLayout(at, row);
    }
  }
  m_dialog->setAccessibleDescription(m_content);

  QVBoxLayout* body = taskBody(m_dialog.get());
  auto* extra = m_dialog->findChild<QWidget*>(QStringLiteral("taskExtraContent"));
  if (body != nullptr && extra != nullptr && extra->layout()->count() > 0 &&
      body->indexOf(extra) < 0) {
    body->addWidget(extra);
    extra->show();
  }
}

void TaskDialog::setChoices()
{
  if (m_rememberAction.isEmpty() && m_rememberFile.isEmpty()) {
    return;
  }
  QVBoxLayout* body = taskBody(m_dialog.get());
  if (body == nullptr || m_rememberCheck != nullptr || m_rememberCombo != nullptr) {
    return;
  }

  const QString tooltip = QCoreApplication::translate(
      "QObject", "You can reset these choices with \"Reset dialog choices\" in Settings, "
                 "General.");

  if (!m_rememberAction.isEmpty() && !m_rememberFile.isEmpty()) {
    // both
    m_rememberCombo = new QComboBox(m_dialog.get());
    m_rememberCombo->setToolTip(tooltip);
    m_rememberCombo->addItem(QObject::tr("Always ask"));
    m_rememberCombo->addItem(QObject::tr("Remember my choice"));
    m_rememberCombo->addItem(QObject::tr("Remember my choice for %1").arg(m_rememberFile));
    m_rememberCombo->setAccessibleName(QObject::tr("Remember my choice"));
    body->addWidget(m_rememberCombo, 0, Qt::AlignLeft);
  } else {
    // either
    m_rememberCheck = new QCheckBox(QObject::tr("Remember my choice"), m_dialog.get());
    m_rememberCheck->setToolTip(tooltip);
    body->addWidget(m_rememberCheck);
  }
}

void TaskDialog::setButtons()
{
  // built once: exec() may run again on the same dialog
  if (m_dialog->property("taskButtonsBuilt").toBool()) {
    return;
  }
  m_dialog->setProperty("taskButtonsBuilt", true);
  if (m_buttons.empty()) {
    setStandardButtons();
  } else {
    setCommandButtons();
  }
}

void TaskDialog::setStandardButtons()
{
  QHBoxLayout* band = taskBand(m_dialog.get());
  if (band == nullptr || band->count() > 0) {
    return;
  }
  // Qt's own word for OK
  QDialogButtonBox names;
  QPushButton* named = names.addButton(QDialogButtonBox::Ok);
  bool hasDefault    = false;
  auto* ok = RevampedFluent::bandButton(m_dialog.get(), band,
                                        named != nullptr ? named->text() : QStringLiteral("OK"),
                                        true, hasDefault);
  QObject::connect(ok, &QPushButton::clicked, m_dialog.get(), [this] {
    m_result = QMessageBox::Ok;
    m_dialog->accept();
  });
  ok->setFocus();
}

void TaskDialog::setCommandButtons()
{
  QVBoxLayout* body  = taskBody(m_dialog.get());
  QHBoxLayout* band  = taskBand(m_dialog.get());
  if (body == nullptr || band == nullptr || band->count() > 0) {
    return;
  }

  bool hasCards     = false;
  bool hasDefault   = false;
  QPushButton* first = nullptr;
  for (auto&& b : m_buttons) {
    QAbstractButton* control = nullptr;
    if (!b.description.isEmpty()) {
      // an option card: a command link MO2 draws as one
      auto* card = new QCommandLinkButton(b.text, b.description, m_dialog.get());
      card->setProperty("revampedOptionCard", true);
      body->addWidget(card);
      control  = card;
      hasCards = true;
    } else {
      auto* button =
          RevampedFluent::bandButton(m_dialog.get(), band, b.text, false, hasDefault);
      if (first == nullptr) {
        first = button;
      }
      control = button;
    }
    const auto result = b.button;
    QObject::connect(control, &QAbstractButton::clicked, m_dialog.get(), [this, result] {
      m_result = result;
      m_dialog->accept();
    });
  }
  // with no cards, the first choice is the one Enter takes
  if (first != nullptr && !hasCards) {
    first->setDefault(true);
    first->setProperty("fluentPrimary", true);
  }
  // only cards: no band
  if (first == nullptr) {
    band->parentWidget()->hide();
  }
}

void TaskDialog::setDetails()
{
  QVBoxLayout* body = taskBody(m_dialog.get());
  if (m_details.isEmpty() || body == nullptr ||
      m_dialog->findChild<QPlainTextEdit*>(QStringLiteral("taskDetails")) != nullptr) {
    return;
  }

  auto* toggle = RevampedFluent::contentButton(QObject::tr("Details"), m_dialog.get());
  toggle->setObjectName(QStringLiteral("taskDetailsToggle"));
  toggle->setCheckable(true);
  toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto* details = new QPlainTextEdit(m_details, m_dialog.get());
  details->setObjectName(QStringLiteral("taskDetails"));
  details->setReadOnly(true);
  details->setAccessibleName(QObject::tr("Details"));
  details->setLineWrapMode(QPlainTextEdit::NoWrap);
  setFontPercent(details, 0.9);
  setVisibleLines(details, 10);
  details->hide();
  body->addWidget(toggle, 0, Qt::AlignLeft);
  body->addWidget(details);

  QObject::connect(toggle, &QPushButton::toggled, m_dialog.get(), [this, details](bool on) {
    details->setVisible(on);
    m_dialog->adjustSize();
  });
}

QColor TaskDialog::detailsColor() const
{
  auto b = std::make_unique<QPushButton>();
  m_dialog->style()->polish(b.get());
  return b->palette().color(b->backgroundRole());
}

QPixmap TaskDialog::standardIcon(QMessageBox::Icon icon) const
{
  QStyle* s = m_dialog->style();
  QIcon i;

  switch (icon) {
  case QMessageBox::Information:
    i = s->standardIcon(QStyle::SP_MessageBoxInformation, 0, m_dialog.get());
    break;
  case QMessageBox::Warning:
    i = s->standardIcon(QStyle::SP_MessageBoxWarning, 0, m_dialog.get());
    break;
  case QMessageBox::Critical:
    i = s->standardIcon(QStyle::SP_MessageBoxCritical, 0, m_dialog.get());
    break;
  case QMessageBox::Question:
    i = s->standardIcon(QStyle::SP_MessageBoxQuestion, 0, m_dialog.get());
    break;
  case QMessageBox::NoIcon:
    [[fallthrough]];
  default:
    break;
  }

  if (i.isNull()) {
    return {};
  }

  const int iconSize = s->pixelMetric(QStyle::PM_MessageBoxIconSize, 0, m_dialog.get());
  return i.pixmap(QSize(iconSize, iconSize));
}

void TaskDialog::setVisibleLines(QPlainTextEdit* w, int lines)
{
  QTextDocument* d = w->document();
  QFontMetrics fm(d->defaultFont());

  const QMargins margins = w->contentsMargins();

  double height = 0;

  // lines
  height += fm.lineSpacing() * lines;

  // top and bottom margins for document and frame
  height += (d->documentMargin() + w->frameWidth()) * 2;

  // widget margins
  height += margins.top() + margins.bottom();

  w->setMaximumHeight(static_cast<int>(std::round(height)));
}

void TaskDialog::setFontPercent(QWidget* w, double p)
{
  auto f = w->font();

  if (f.pointSizeF() > 0) {
    f.setPointSizeF(f.pointSizeF() * p);
  } else if (f.pixelSize() > 0) {
    f.setPixelSize(static_cast<int>(std::round(f.pixelSize() * p)));
  }

  w->setFont(f);
}

}  // namespace MOBase
