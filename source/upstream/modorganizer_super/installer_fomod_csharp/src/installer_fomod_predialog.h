#ifndef INSTALLER_FOMOD_PREDIALOG_H
#define INSTALLER_FOMOD_PREDIALOG_H

#include "fluentcontentdialog.h"
#include "guessedvalue.h"

#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>

/**
 * @brief Asked before a FOMOD's C# script runs: the mod's name, and whether to
 * run the script or install manually. MO2 Revamped draws it as a Windows 11
 * content dialog instead of 2.5.2's .ui.
 **/
class InstallerFomodPredialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  /**
   * @param preset the guessed name of the mod and its other guesses
   * @param parent parent widget
   **/
  explicit InstallerFomodPredialog(const MOBase::GuessedValue<QString>& preset,
                                   QWidget* parent = nullptr)
      : RevampedFluent::ContentDialog(
            parent, QStringLiteral("FomodCSharpPredialog"), tr("FOMOD C# Installer"),
            tr("This mod's installer is a C# script. MO2 runs it when you choose "
               "Install; it may ask you questions in its own windows."))
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
    // the likeliest guess; typed in when it isn't one of the variants (MO2
    // 2.5.2 showed an empty name then)
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

    m_start      = addButton(tr("Install"), true);
    m_start->setToolTip(tr("Run the mod's installer script"));
    auto* manual = addButton(tr("Install manually"));
    manual->setToolTip(tr("Choose which of the archive's files to install, and where"));
    auto* cancel = addButton(tr("Cancel"));

    // string connects: this header is built as C++/CLI, where Qt finds no
    // signal for a function pointer ("signal not found")
    connect(m_start, SIGNAL(clicked()), this, SLOT(startClicked()));
    connect(manual, SIGNAL(clicked()), this, SLOT(manualClicked()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(m_name, SIGNAL(editTextChanged(QString)), this, SLOT(nameEdited()));

    setTabOrder(m_name, m_start);
    setTabOrder(m_start, manual);
    setTabOrder(manual, cancel);
    m_name->setFocus();
    if (m_name->lineEdit() != nullptr) {
      m_name->lineEdit()->selectAll();
    }
    nameChanged();
  }

  /**
   * @return true if the user requested a manual installation.
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return the (user-modified) mod name
   **/
  QString getName() const { return m_name->currentText().trimmed(); }

private slots:
  void startClicked()
  {
    if (!getName().isEmpty()) {
      accept();
    }
  }

  void manualClicked()
  {
    m_Manual = true;
    reject();
  }

  void nameEdited()
  {
    m_edited = true;
    nameChanged();
  }

private:
  void nameChanged()
  {
    const bool empty = getName().isEmpty();
    m_start->setEnabled(!empty);
    m_problem->setVisible(empty && m_edited);
    m_name->setAccessibleDescription(empty ? m_problem->text() : QString());
  }

  QComboBox* m_name;
  QLabel* m_problem;
  QPushButton* m_start;
  bool m_Manual = false;
  bool m_edited = false;
};

#endif
