/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "queryoverwritedialog.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "ui_queryoverwritedialog.h"

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace
{
// a Settings row that runs a button when it is clicked or when Enter or Space
// is pressed on it, like the choices of Windows 11's file conflict dialog
class ChoiceRow : public QObject
{
public:
  ChoiceRow(QFrame* row, QPushButton* button) : QObject(row), m_button(button)
  {
    row->installEventFilter(this);
    row->setFocusPolicy(Qt::StrongFocus);
    row->setCursor(Qt::PointingHandCursor);
    row->setAccessibleName(button->text().remove(QLatin1Char('&')));
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::MouseButtonRelease) {
      m_button->click();
      return true;
    }
    if (event->type() == QEvent::KeyPress) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        m_button->click();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPushButton* m_button;
};
}  // namespace

QueryOverwriteDialog::QueryOverwriteDialog(QWidget* parent, Backup b)
    : QDialog(parent), ui(new Ui::QueryOverwriteDialog), m_Action(ACT_NONE)
{
  ui->setupUi(this);
  ui->backupBox->setChecked(b == BACKUP_YES);
  QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion);
  ui->iconLabel->setPixmap(icon.pixmap(72));

  if (FluentIcons::active()) {
    buildFluentLayout();
  }
}

void QueryOverwriteDialog::setModName(const QString& name)
{
  if (!name.isEmpty()) {
    ui->overwritePageTitle->setText(tr("\"%1\" is already installed").arg(name));
  }
}

void QueryOverwriteDialog::buildFluentLayout()
{
  // the three answers become rows with a glyph and what each one does, the
  // backup a switch row, and only Cancel stays a button
  auto* root = qobject_cast<QVBoxLayout*>(layout());
  if (root == nullptr) {
    return;
  }
  ui->frame_2->hide();
  ui->backupCard->hide();
  for (QPushButton* button : {ui->mergeBtn, ui->replaceBtn, ui->renameBtn}) {
    button->hide();
  }
  root->setStretch(1, 0);

  auto* card = new QFrame(this);
  card->setProperty("fluentCard", true);
  auto* rows = new QVBoxLayout(card);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(0);

  const auto addChoice = [&](QPushButton* button, char16_t glyph, const QString& title,
                             const QString& description) {
    const auto detail = FluentPanels::addDetailRow(card, glyph, title);
    QFrame* row       = detail.frame;
    auto* value       = detail.value;
    if (value != nullptr) {
      // the description wraps instead of being cut
      value->hide();
      auto* text = new QLabel(description, row);
      text->setObjectName(QStringLiteral("fluentSettingDescription"));
      text->setWordWrap(true);
      static_cast<QBoxLayout*>(value->parentWidget()->layout()->itemAt(1)->layout())
          ->addWidget(text);
    }
    auto* chevron = FluentPanels::glyphLabel(0xE76C, row);
    row->layout()->addWidget(chevron);
    new ChoiceRow(row, button);
    return row;
  };
  QFrame* rename = addChoice(ui->renameBtn, 0xE8AC, tr("Install as a new mod"),
                             tr("Keeps the installed mod and adds this one under another "
                                "name (recommended)"));
  addChoice(ui->mergeBtn, 0xEA3C, tr("Merge"),
            tr("Adds the new files to the installed mod; files with the same name are "
               "replaced"));
  addChoice(ui->replaceBtn, 0xE72C, tr("Replace"),
            tr("Deletes the installed mod's files and installs this one in their place"));

  FluentPanels::addSwitchRow(card, 0xEA35, tr("Keep a backup of the current mod"),
                             tr("Saves a copy you can restore before any file changes"),
                             ui->backupBox);
  ui->backupBox->show();
  root->insertWidget(1, card);
  rename->setFocus();

  setMinimumWidth(620);
  resize(660, sizeHint().height());
}

QueryOverwriteDialog::~QueryOverwriteDialog()
{
  delete ui;
}

bool QueryOverwriteDialog::backup() const
{
  return ui->backupBox->isChecked();
}

void QueryOverwriteDialog::on_mergeBtn_clicked()
{
  this->m_Action = ACT_MERGE;
  this->accept();
}

void QueryOverwriteDialog::on_replaceBtn_clicked()
{
  this->m_Action = ACT_REPLACE;
  this->accept();
}

void QueryOverwriteDialog::on_renameBtn_clicked()
{
  this->m_Action = ACT_RENAME;
  this->accept();
}

void QueryOverwriteDialog::on_cancelBtn_clicked()
{
  this->reject();
}
