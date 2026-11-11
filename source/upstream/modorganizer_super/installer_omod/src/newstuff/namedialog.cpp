#include "namedialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>

NameDialog::NameDialog(const MOBase::GuessedValue<QString>& suggestedNames, QWidget* parent)
    : RevampedFluent::ContentDialog(
          parent, QStringLiteral("NameDialog"), tr("OMOD Installer"),
          tr("This mod is an OMOD. MO2 installs it with its own installer, which may "
             "ask you questions next."))
{
  setHeading(tr("Install this mod"));
  setMinimumWidth(460);

  m_name = new QComboBox(this);
  m_name->setEditable(true);
  m_name->setInsertPolicy(QComboBox::NoInsert);
  body()->addWidget(RevampedFluent::fieldLabel(tr("Mod name"), m_name, this));
  for (const auto& name : suggestedNames.variants()) {
    m_name->addItem(name);
  }
  if (m_name->completer() != nullptr) {
    m_name->completer()->setCaseSensitivity(Qt::CaseSensitive);
  }
  // the likeliest guess; typed in when it isn't one of the variants (MO2 2.5.2
  // showed an empty name then)
  const int index = m_name->findText(suggestedNames);
  if (index >= 0) {
    m_name->setCurrentIndex(index);
  } else {
    m_name->setEditText(suggestedNames);
  }
  body()->addWidget(m_name);
  m_problem = RevampedFluent::problemLabel(tr("Enter a name for the mod."), this);
  body()->addWidget(m_problem);
  body()->addStretch(1);

  m_install    = addButton(tr("Install"), true);
  auto* cancel = addButton(tr("Cancel"));
  // string connects: this is built as C++/CLI, where Qt finds no signal for
  // a function pointer ("signal not found")
  connect(m_install, SIGNAL(clicked()), this, SLOT(installClicked()));
  connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
  connect(m_name, SIGNAL(editTextChanged(QString)), this, SLOT(nameEdited()));

  setTabOrder(m_name, m_install);
  setTabOrder(m_install, cancel);
  m_name->setFocus();
  if (m_name->lineEdit() != nullptr) {
    m_name->lineEdit()->selectAll();
  }
  nameChanged();
}

void NameDialog::installClicked()
{
  if (!getName().isEmpty()) {
    accept();
  }
}

void NameDialog::nameEdited()
{
  m_edited = true;
  nameChanged();
}

QString NameDialog::getName() const
{
  return m_name->currentText().trimmed();
}

void NameDialog::nameChanged()
{
  const bool empty = getName().isEmpty();
  m_install->setEnabled(!empty);
  m_problem->setVisible(empty && m_edited);
  m_name->setAccessibleDescription(empty ? m_problem->text() : QString());
}
