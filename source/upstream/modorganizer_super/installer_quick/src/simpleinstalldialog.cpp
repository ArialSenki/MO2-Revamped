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

#include "simpleinstalldialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

using namespace MOBase;

SimpleInstallDialog::SimpleInstallDialog(const GuessedValue<QString>& preset,
                                         QWidget* parent)
    : RevampedFluent::ContentDialog(
          parent, QStringLiteral("SimpleInstallDialog"), tr("Quick install"),
          tr("The archive's files are already arranged as the game's data folder "
             "expects, so MO2 can install them as they are."))
{
  setHeading(tr("Install this mod"));
  setMinimumWidth(460);

  m_name = new QComboBox(this);
  m_name->setEditable(true);
  m_name->setInsertPolicy(QComboBox::NoInsert);
  body()->addWidget(RevampedFluent::fieldLabel(tr("Mod name"), m_name, this));
  for (const QString& variant : preset.variants()) {
    m_name->addItem(variant);
  }
  if (m_name->completer() != nullptr) {
    m_name->completer()->setCaseSensitivity(Qt::CaseSensitive);
  }
  // the likeliest guess, chosen in the list; typed in when it isn't one of
  // the variants (MO2 2.5.2 showed an empty name then)
  const int presetIndex = m_name->findText(preset);
  if (presetIndex >= 0) {
    m_name->setCurrentIndex(presetIndex);
  } else {
    m_name->setEditText(preset);
  }
  body()->addWidget(m_name);
  m_problem = RevampedFluent::problemLabel(tr("Enter a name for the mod."), this);
  body()->addWidget(m_problem);
  body()->addStretch(1);

  m_install    = addButton(tr("Install"), true);
  auto* manual = addButton(tr("Install manually"));
  manual->setToolTip(tr("Choose which of the archive's files to install, and where"));
  auto* cancel = addButton(tr("Cancel"));

  connect(m_install, &QPushButton::clicked, this, [this] {
    if (!getName().isEmpty()) {
      accept();
    }
  });
  connect(manual, &QPushButton::clicked, this, [this] {
    m_Manual = true;
    reject();
  });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_name, &QComboBox::editTextChanged, this, [this] {
    // the problem shows once the name is edited, not at open
    m_edited = true;
    nameChanged();
  });

  setTabOrder(m_name, m_install);
  setTabOrder(m_install, manual);
  setTabOrder(manual, cancel);
  m_name->setFocus();
  if (m_name->lineEdit() != nullptr) {
    m_name->lineEdit()->selectAll();
  }
  nameChanged();
}

QString SimpleInstallDialog::getName() const
{
  return m_name->currentText().trimmed();
}

void SimpleInstallDialog::nameChanged()
{
  const bool empty = getName().isEmpty();
  m_install->setEnabled(!empty);
  m_problem->setVisible(empty && m_edited);
  m_name->setAccessibleDescription(empty ? m_problem->text() : QString());
}
