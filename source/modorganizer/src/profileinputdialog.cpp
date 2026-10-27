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

#include "profileinputdialog.h"
#include "filesystemutilities.h"
#include "ui_profileinputdialog.h"

#include <QDialogButtonBox>
#include "fluenticons.h"
#include "fluentpanels.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

ProfileInputDialog::ProfileInputDialog(QWidget* parent)
    : ProfileInputDialog(parent, Purpose::Create, QString())
{}

ProfileInputDialog::ProfileInputDialog(QWidget* parent, Purpose purpose,
                                       const QString& initialName)
    : QDialog(parent), ui(new Ui::ProfileInputDialog)
{
  ui->setupUi(this);
  setWindowTitle(tr("MO2 Revamped"));
  setMinimumWidth(520);
  ui->nameEdit->setClearButtonEnabled(true);

  // Fluent v4: the option is a Settings row with a switch, its explanation
  // as the row's description
  if (FluentIcons::active()) {
    ui->defaultSettingsDescription->hide();
    ui->profileSettingsLayout->setContentsMargins(0, 0, 0, 0);
    ui->profileSettingsCard->setProperty("fluentCard", true);
    FluentPanels::addSwitchRow(ui->profileSettingsCard, 0xE713,
                               ui->defaultSettingsBox->text(),
                               ui->defaultSettingsDescription->text(),
                               ui->defaultSettingsBox);
  }

  if (purpose == Purpose::Copy || purpose == Purpose::Rename) {
    ui->profileSettingsCard->hide();
    ui->defaultSettingsBox->setChecked(false);
    ui->profileNameLabel->setText(tr("Profile name"));
    ui->nameEdit->setText(purpose == Purpose::Rename ? initialName : QString());
    ui->nameEdit->setPlaceholderText(tr("For example: Challenge Run or Mod Test"));
    ui->profilePageTitle->setText(
        purpose == Purpose::Copy ? tr("Name the copy")
                                 : tr("Set a new profile name"));
    ui->profilePageSubtitle->setText(
        purpose == Purpose::Copy
            ? tr("It keeps the selected profile's mods and settings.")
            : tr("The profile's mods and settings will stay unchanged."));

    adjustSize();
    if (purpose == Purpose::Rename) {
      ui->nameEdit->selectAll();
    }
  }

  if (QPushButton* acceptButton =
          ui->buttonBox->button(QDialogButtonBox::Ok)) {
    acceptButton->setObjectName("profileAcceptButton");
    switch (purpose) {
    case Purpose::Create:
      acceptButton->setText(tr("Create"));
      break;
    case Purpose::Copy:
      acceptButton->setText(tr("Copy"));
      break;
    case Purpose::Rename:
      acceptButton->setText(tr("Rename"));
      break;
    }
    acceptButton->setDefault(true);
  }
  if (QPushButton* cancelButton =
          ui->buttonBox->button(QDialogButtonBox::Cancel)) {
    cancelButton->setObjectName("profileCancelButton");
  }

  connect(ui->nameEdit, &QLineEdit::textChanged, this,
          [this]() { updateAcceptState(); });
  ui->nameEdit->setFocus(Qt::OtherFocusReason);
  updateAcceptState();
}

ProfileInputDialog::~ProfileInputDialog()
{
  delete ui;
}

QString ProfileInputDialog::getName() const
{
  QString result = ui->nameEdit->text();
  MOBase::fixDirectoryName(result);
  return result;
}

bool ProfileInputDialog::getPreferDefaultSettings() const
{
  return ui->defaultSettingsBox->checkState() == Qt::Checked;
}

void ProfileInputDialog::updateAcceptState()
{
  const QString name = ui->nameEdit->text();
  QString normalizedName = name;
  const bool valid = MOBase::fixDirectoryName(normalizedName) &&
                     !normalizedName.isEmpty();
  if (QPushButton* acceptButton =
          ui->buttonBox->button(QDialogButtonBox::Ok)) {
    acceptButton->setEnabled(valid);
  }
}
