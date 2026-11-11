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

#include "finddialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
// Enter on a focused button presses that button, as in Windows, not the
// dialog's default one
class EnterPressesItself : public QObject
{
public:
  using QObject::QObject;

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    auto* button = qobject_cast<QPushButton*>(watched);
    if (event->type() == QEvent::KeyPress && button != nullptr && button->isEnabled()) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        button->click();
        return true;
      }
    }
    return false;
  }
};
}  // namespace

// MO2 Revamped: built in code instead of from 2.5.2's .ui, as a Windows 11
// content dialog (the themes style it by these object names); the header,
// and so uibase's exports, stay as they were
namespace Ui
{
class FindDialog
{
public:
  QLineEdit* patternEdit;
  QPushButton* nextBtn;
  QPushButton* closeBtn;

  void setupUi(QDialog* dialog)
  {
    dialog->setObjectName(QStringLiteral("FindDialog"));
    dialog->setProperty("fluentContentDialog", true);
    dialog->setWindowTitle(QCoreApplication::translate("FindDialog", "Find"));
    dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog->setMinimumWidth(420);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* content = new QWidget(dialog);
    content->setObjectName(QStringLiteral("fluentDialogContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 20, 24, 24);
    contentLayout->setSpacing(8);
    auto* title = new QLabel(QCoreApplication::translate("FindDialog", "Find"), content);
    title->setObjectName(QStringLiteral("fluentDialogTitle"));
    contentLayout->addWidget(title);
    auto* label =
        new QLabel(QCoreApplication::translate("FindDialog", "Find what"), content);
    contentLayout->addWidget(label);
    patternEdit = new QLineEdit(content);
    patternEdit->setObjectName(QStringLiteral("patternEdit"));
    patternEdit->setPlaceholderText(
        QCoreApplication::translate("FindDialog", "Search term"));
    patternEdit->setAccessibleName(QCoreApplication::translate("FindDialog", "Find what"));
    patternEdit->setClearButtonEnabled(true);
    label->setBuddy(patternEdit);
    contentLayout->addWidget(patternEdit);
    layout->addWidget(content, 1);

    auto* band = new QFrame(dialog);
    band->setObjectName(QStringLiteral("fluentDialogFooter"));
    auto* buttons = new QHBoxLayout(band);
    buttons->setContentsMargins(24, 24, 24, 24);
    buttons->setSpacing(8);
    nextBtn = new QPushButton(QCoreApplication::translate("FindDialog", "Find next"), band);
    nextBtn->setObjectName(QStringLiteral("nextBtn"));
    nextBtn->setToolTip(QCoreApplication::translate(
        "FindDialog", "Find the next match after the cursor, from the top again at the end (F3)"));
    nextBtn->setDefault(true);
    nextBtn->setProperty("fluentPrimary", true);
    closeBtn = new QPushButton(QCoreApplication::translate("FindDialog", "Close"), band);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setAutoDefault(false);
    for (auto* button : {nextBtn, closeBtn}) {
      button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      button->setMinimumWidth(110);
      button->installEventFilter(new EnterPressesItself(button));
      buttons->addWidget(button);
    }
    layout->addWidget(band);

    // F3 finds the next match from here too, as the tooltip says
    nextBtn->setShortcut(QKeySequence::FindNext);

    // the slots are named on_<object>_<signal>
    QMetaObject::connectSlotsByName(dialog);
  }
};
}  // namespace Ui

namespace MOBase
{

FindDialog::FindDialog(QWidget* parent) : QDialog(parent), ui(new Ui::FindDialog)
{
  ui->setupUi(this);
}

FindDialog::~FindDialog()
{
  delete ui;
}

void FindDialog::on_nextBtn_clicked()
{
  emit findNext();
}

void FindDialog::on_patternEdit_textChanged(const QString& pattern)
{
  emit patternChanged(pattern);
}

void FindDialog::on_closeBtn_clicked()
{
  this->close();
}
}  // namespace MOBase
