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
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ProfileInputDialog::ProfileInputDialog(QWidget* parent)
    : ProfileInputDialog(parent, Purpose::Create, QString())
{}

ProfileInputDialog::ProfileInputDialog(QWidget* parent, Purpose purpose,
                                       const QString& initialName)
    : QDialog(parent), ui(new Ui::ProfileInputDialog)
{
  ui->setupUi(this);
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

  setWindowTitle(ui->profilePageTitle->text());
  if (FluentIcons::active()) {
    useContentDialogLayout();
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

// Fluent v4: the same shape as FluentContentDialog (title, text, the fields,
// then the buttons on the footer band), so it matches the other prompts; the
// widgets keep their names for the themes and for the plugins that look them
// up
void ProfileInputDialog::useContentDialogLayout()
{
  m_accept = ui->buttonBox->button(QDialogButtonBox::Ok);
  QPushButton* cancel = ui->buttonBox->button(QDialogButtonBox::Cancel);
  for (QPushButton* button : {m_accept, cancel}) {
    ui->buttonBox->removeButton(button);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumWidth(110);
  }
  m_accept->setProperty("fluentPrimary", true);
  connect(m_accept, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  ui->buttonBox->hide();
  ui->profileInputDivider->hide();
  ui->profileHeaderLayout->removeWidget(ui->profilePageTitle);
  ui->profileHeaderLayout->removeWidget(ui->profilePageSubtitle);
  ui->profileHeader->hide();

  // deleting the layouts keeps their widgets, which the new ones take
  delete layout();
  setProperty("fluentContentDialog", true);
  ui->profilePageTitle->setObjectName(QStringLiteral("fluentDialogTitle"));
  ui->profilePageSubtitle->setObjectName(QStringLiteral("fluentDialogText"));
  ui->profileNameLabel->setObjectName(QStringLiteral("instanceNameLabel"));

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("fluentDialogContent"));
  auto* contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(24, 20, 24, 24);
  contentLayout->setSpacing(8);
  contentLayout->addWidget(ui->profilePageTitle);
  contentLayout->addWidget(ui->profilePageSubtitle);
  contentLayout->addSpacing(8);
  contentLayout->addWidget(ui->profileNameLabel);
  contentLayout->addWidget(ui->nameEdit);
  contentLayout->addSpacing(8);
  contentLayout->addWidget(ui->profileSettingsCard);
  contentLayout->addStretch(1);
  outer->addWidget(content, 1);

  auto* band = new QFrame(this);
  band->setObjectName(QStringLiteral("fluentDialogFooter"));
  auto* buttons = new QHBoxLayout(band);
  buttons->setContentsMargins(24, 24, 24, 24);
  buttons->setSpacing(8);
  buttons->addWidget(m_accept);
  buttons->addWidget(cancel);
  outer->addWidget(band);

  // on the content, so adjustSize() wraps the text at the width it gets
  // (on the dialog it measured a narrower wrap and left a blank line)
  setMinimumWidth(0);
  content->setMinimumWidth(460);
  adjustSize();
}

void ProfileInputDialog::updateAcceptState()
{
  const QString name = ui->nameEdit->text();
  QString normalizedName = name;
  const bool valid = MOBase::fixDirectoryName(normalizedName) &&
                     !normalizedName.isEmpty();
  QPushButton* acceptButton =
      m_accept != nullptr ? m_accept : ui->buttonBox->button(QDialogButtonBox::Ok);
  if (acceptButton != nullptr) {
    acceptButton->setEnabled(valid);
  }
}
