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

#include "questionboxmemory.h"
#include "log.h"

#include "fluentcontentdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QSettings>

// MO2 Revamped: the question is built in code as a Windows 11 content
// dialog (title, text, the "remember" boxes, a band of buttons) instead of
// the .ui of Mod Organizer 2. The class keeps its members, so its size and
// exports stay as they were; Ui::QuestionBoxMemory is now empty.
namespace Ui
{
class QuestionBoxMemory
{};
}  // namespace Ui

namespace MOBase
{

static QMutex g_mutex;
static QuestionBoxMemory::GetButton g_get;
static QuestionBoxMemory::SetWindowButton g_setWindow;
static QuestionBoxMemory::SetFileButton g_setFile;

namespace
{

// the order Windows puts a dialog's buttons in: the actions first
const QDialogButtonBox::StandardButton ButtonOrder[] = {
    QDialogButtonBox::Yes,     QDialogButtonBox::YesToAll,
    QDialogButtonBox::Ok,      QDialogButtonBox::Save,
    QDialogButtonBox::SaveAll, QDialogButtonBox::Open,
    QDialogButtonBox::Retry,   QDialogButtonBox::Ignore,
    QDialogButtonBox::Apply,   QDialogButtonBox::Reset,
    QDialogButtonBox::RestoreDefaults, QDialogButtonBox::Help,
    QDialogButtonBox::No,      QDialogButtonBox::NoToAll,
    QDialogButtonBox::Abort,   QDialogButtonBox::Discard,
    QDialogButtonBox::Close,   QDialogButtonBox::Cancel};

bool isAction(QDialogButtonBox::StandardButton button)
{
  switch (button) {
  case QDialogButtonBox::Yes:
  case QDialogButtonBox::YesToAll:
  case QDialogButtonBox::Ok:
  case QDialogButtonBox::Save:
  case QDialogButtonBox::SaveAll:
  case QDialogButtonBox::Open:
  case QDialogButtonBox::Retry:
    return true;
  default:
    return false;
  }
}

}  // namespace

QuestionBoxMemory::QuestionBoxMemory(QWidget* parent, const QString& title,
                                     const QString& text, QString const* filename,
                                     const QDialogButtonBox::StandardButtons buttons,
                                     QDialogButtonBox::StandardButton defaultButton)
    : QDialog(parent), m_Button(QDialogButtonBox::Cancel)
{
  setObjectName(QStringLiteral("RevampedQuestionMemory"));
  const RevampedFluent::ContentParts parts = RevampedFluent::buildContent(this, title, text);
  // as in Mod Organizer 2, the question may be rich text
  parts.text->setTextFormat(Qt::AutoText);

  // "remember" reads as in TaskDialog, and keeps Mod Organizer 2's
  // translations of those words
  const QString tooltip = QCoreApplication::translate(
      "QObject", "You can reset these choices with \"Reset dialog choices\" in Settings, "
                 "General.");
  auto* remember = new QCheckBox(
      QCoreApplication::translate("QObject", "Remember my choice"), this);
  remember->setObjectName(QStringLiteral("rememberCheckBox"));
  remember->setToolTip(tooltip);
  parts.body->addWidget(remember);
  if (filename != nullptr) {
    auto* rememberFor = new QCheckBox(
        QCoreApplication::translate("QObject", "Remember my choice for %1")
            .arg(*filename),
        this);
    rememberFor->setObjectName(QStringLiteral("rememberForCheckBox"));
    rememberFor->setToolTip(tooltip);
    parts.body->addWidget(rememberFor);
  }

  // Qt's own words for each button, in Windows' order; the accent on the
  // default button, or on the first action
  QDialogButtonBox names;
  bool hasDefault = false;
  QDialogButtonBox::StandardButton primary = defaultButton;
  if (primary == QDialogButtonBox::NoButton || !(buttons & primary)) {
    primary = QDialogButtonBox::NoButton;
    for (const auto button : ButtonOrder) {
      if ((buttons & button) && isAction(button)) {
        primary = button;
        break;
      }
    }
  }
  for (const auto button : ButtonOrder) {
    if (!(buttons & button)) {
      continue;
    }
    QPushButton* named = names.addButton(button);
    QPushButton* added = RevampedFluent::bandButton(
        this, parts.buttons, named != nullptr ? named->text() : buttonToString(button),
        button == primary, hasDefault);
    connect(added, &QPushButton::clicked, this, [this, button] {
      m_Button = button;
      accept();
    });
  }
}

QuestionBoxMemory::~QuestionBoxMemory() = default;

void QuestionBoxMemory::setCallbacks(GetButton get, SetWindowButton setWindow,
                                     SetFileButton setFile)
{
  QMutexLocker locker(&g_mutex);

  g_get       = get;
  g_setWindow = setWindow;
  g_setFile   = setFile;
}

void QuestionBoxMemory::buttonClicked(QAbstractButton*)
{
  // the buttons set m_Button themselves (kept for the class's exports)
}

QDialogButtonBox::StandardButton
QuestionBoxMemory::query(QWidget* parent, const QString& windowName,
                         const QString& title, const QString& text,
                         QDialogButtonBox::StandardButtons buttons,
                         QDialogButtonBox::StandardButton defaultButton)
{
  return queryImpl(parent, windowName, nullptr, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
QuestionBoxMemory::query(QWidget* parent, const QString& windowName,
                         const QString& fileName, const QString& title,
                         const QString& text, QDialogButtonBox::StandardButtons buttons,
                         QDialogButtonBox::StandardButton defaultButton)
{
  return queryImpl(parent, windowName, &fileName, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
QuestionBoxMemory::queryImpl(QWidget* parent, const QString& windowName,
                             const QString* fileName, const QString& title,
                             const QString& text,
                             QDialogButtonBox::StandardButtons buttons,
                             QDialogButtonBox::StandardButton defaultButton)
{
  QMutexLocker locker(&g_mutex);

  const auto button = getMemory(windowName, (fileName ? *fileName : ""));
  if (button != NoButton) {
    log::debug("{}: not asking because user always wants response {}",
               windowName + (fileName ? QString("/") + *fileName : ""),
               buttonToString(button));

    return button;
  }

  QuestionBoxMemory dialog(parent, title, text, fileName, buttons, defaultButton);
  dialog.exec();

  if (dialog.m_Button != QDialogButtonBox::Cancel) {
    const auto* remember = dialog.findChild<QCheckBox*>(QStringLiteral("rememberCheckBox"));
    if (remember != nullptr && remember->isChecked()) {
      setWindowMemory(windowName, dialog.m_Button);
    }

    const auto* rememberFor =
        dialog.findChild<QCheckBox*>(QStringLiteral("rememberForCheckBox"));
    if (fileName != nullptr && rememberFor != nullptr && rememberFor->isChecked()) {
      setFileMemory(windowName, *fileName, dialog.m_Button);
    }
  }

  return dialog.m_Button;
}

void QuestionBoxMemory::setWindowMemory(const QString& windowName, Button b)
{
  log::debug("remembering choice {} for window {}", buttonToString(b), windowName);

  g_setWindow(windowName, b);
}

void QuestionBoxMemory::setFileMemory(const QString& windowName,
                                      const QString& filename, Button b)
{
  log::debug("remembering choice {} for file {}", buttonToString(b),
             windowName + "/" + filename);

  g_setFile(windowName, filename, b);
}

QuestionBoxMemory::Button QuestionBoxMemory::getMemory(const QString& windowName,
                                                       const QString& filename)
{
  return g_get(windowName, filename);
}

QString QuestionBoxMemory::buttonToString(Button b)
{
  using BB = QDialogButtonBox;

  static const std::map<Button, QString> map = {
      {BB::NoButton, "none"},
      {BB::Ok, "ok"},
      {BB::Save, "save"},
      {BB::SaveAll, "saveall"},
      {BB::Open, "open"},
      {BB::Yes, "yes"},
      {BB::YesToAll, "yestoall"},
      {BB::No, "no"},
      {BB::NoToAll, "notoall"},
      {BB::Abort, "abort"},
      {BB::Retry, "retry"},
      {BB::Ignore, "ignore"},
      {BB::Close, "close"},
      {BB::Cancel, "cancel"},
      {BB::Discard, "discard"},
      {BB::Help, "help"},
      {BB::Apply, "apply"},
      {BB::Reset, "reset"},
      {BB::RestoreDefaults, "restoredefaults"}};

  auto itor = map.find(b);

  if (itor == map.end()) {
    return QString("0x%1").arg(static_cast<int>(b), 0, 16);
  } else {
    return QString("'%1' (0x%2)").arg(itor->second).arg(static_cast<int>(b), 0, 16);
  }
}

}  // namespace MOBase
