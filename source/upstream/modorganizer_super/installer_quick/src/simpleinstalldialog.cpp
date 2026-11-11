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
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{

// Enter on a focused button presses that button, as in Windows, not the
// dialog's default one (enterPressesItself() in MO2 Revamped's dialogs)
class EnterPressesItself : public QObject
{
public:
  using QObject::QObject;

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::KeyPress) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      auto* button  = qobject_cast<QPushButton*>(watched);
      if ((key == Qt::Key_Return || key == Qt::Key_Enter) && button != nullptr &&
          button->isEnabled()) {
        button->click();
        return true;
      }
    }
    return false;
  }
};

QPushButton* footerButton(QWidget* parent, const QString& text, bool primary)
{
  auto* button = new QPushButton(text, parent);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  button->setMinimumWidth(110);
  button->setAutoDefault(false);
  button->setDefault(primary);
  if (primary) {
    button->setProperty("fluentPrimary", true);
  }
  button->installEventFilter(new EnterPressesItself(button));
  return button;
}

}  // namespace

SimpleInstallDialog::SimpleInstallDialog(const GuessedValue<QString>& preset,
                                         QWidget* parent)
    : QDialog(parent)
{
  setObjectName(QStringLiteral("SimpleInstallDialog"));
  setProperty("fluentContentDialog", true);
  setWindowTitle(tr("Quick Install"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setMinimumWidth(460);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("fluentDialogContent"));
  auto* contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(24, 20, 24, 24);
  contentLayout->setSpacing(8);

  auto* title = new QLabel(tr("Install this mod"), content);
  title->setObjectName(QStringLiteral("fluentDialogTitle"));
  title->setWordWrap(true);
  contentLayout->addWidget(title);

  auto* text = new QLabel(tr("The archive's files are already arranged as the game's "
                             "data folder expects, so MO2 can install them as they are."),
                          content);
  text->setObjectName(QStringLiteral("fluentDialogText"));
  text->setWordWrap(true);
  contentLayout->addWidget(text);
  contentLayout->addSpacing(8);

  auto* nameLabel = new QLabel(tr("Mod name"), content);
  contentLayout->addWidget(nameLabel);

  m_name = new QComboBox(content);
  m_name->setEditable(true);
  m_name->setInsertPolicy(QComboBox::NoInsert);
  m_name->setAccessibleName(tr("Mod name"));
  nameLabel->setBuddy(m_name);
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
  contentLayout->addWidget(m_name);

  // the error tone of MO2's name checks
  m_problem = new QLabel(tr("Enter a name for the mod."), content);
  m_problem->setObjectName(QStringLiteral("instanceNameValidation"));
  m_problem->setWordWrap(true);
  m_problem->setVisible(false);
  contentLayout->addWidget(m_problem);
  contentLayout->addStretch(1);
  layout->addWidget(content, 1);

  auto* band = new QFrame(this);
  band->setObjectName(QStringLiteral("fluentDialogFooter"));
  auto* buttons = new QHBoxLayout(band);
  buttons->setContentsMargins(24, 24, 24, 24);
  buttons->setSpacing(8);
  m_install   = footerButton(band, tr("Install"), true);
  auto* manual = footerButton(band, tr("Install manually"), false);
  manual->setToolTip(tr("Choose which of the archive's files to install, and where"));
  auto* cancel = footerButton(band, tr("Cancel"), false);
  buttons->addWidget(m_install);
  buttons->addWidget(manual);
  buttons->addWidget(cancel);
  layout->addWidget(band);

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

QSize SimpleInstallDialog::sizeHint() const
{
  // at least the minimum width: Qt measures the wrapped text at this width
  // (FluentContentDialog::sizeHint in MO2 Revamped)
  QSize size = QDialog::sizeHint();
  size.setWidth(qMax(size.width(), minimumWidth()));
  return size;
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
