#ifndef INSTALLER_FOMOD_POSTDIALOG_H
#define INSTALLER_FOMOD_POSTDIALOG_H

#include "fluentcontentdialog.h"
#include "psettings.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QPlainTextEdit>

#include <map>

/**
 * @brief Asked after a FOMOD's C# script asked for INI changes: what they are,
 * one INI file at a time, and whether to apply them to the profile's INI files,
 * keep them in the mod's INI Tweaks folder, or discard them. MO2 Revamped draws
 * it as a Windows 11 content dialog instead of 2.5.2's .ui.
 **/
class InstallerFomodPostDialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  enum class Result
  {
    APPLY,
    DISCARD,
    MOVE,
  };

  explicit InstallerFomodPostDialog(QWidget* parent = nullptr)
      : RevampedFluent::ContentDialog(
            parent, QStringLiteral("FomodCSharpPostDialog"),
            tr("Settings modification required"),
            tr("You can apply them to the INI files of the current profile, keep them in "
               "the mod's folder under INI Tweaks, or discard them."))
  {
    setHeading(tr("The installer wants to change these settings"));
    setMinimumSize(560, 420);
    resize(640, 480);
    setSizeGripEnabled(true);

    m_files = new QComboBox(this);
    body()->addWidget(RevampedFluent::fieldLabel(tr("INI file"), m_files, this));
    body()->addWidget(m_files);
    m_settings = new QPlainTextEdit(this);
    m_settings->setReadOnly(true);
    m_settings->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_settings->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_settings->setAccessibleName(tr("Settings"));
    body()->addWidget(m_settings, 1);

    // the default keeps the changes in the mod: Apply rewrites the profile's
    // INI files
    auto* move = addButton(tr("Save as INI Tweaks"), true);
    move->setToolTip(tr("Create files under \"INI Tweaks\" in the mod folder with these "
                        "settings"));
    auto* apply = addButton(tr("Apply"));
    apply->setToolTip(tr("Apply the settings to the INI files of the current profile"));
    auto* discard = addButton(tr("Discard"));
    discard->setToolTip(tr("Discard the settings"));
    auto* cancel = addButton(tr("Cancel"));
    cancel->setToolTip(tr("Cancel the installation"));

    // string connects: this header is built as C++/CLI, where Qt finds no
    // signal for a function pointer ("signal not found")
    connect(apply, SIGNAL(clicked()), this, SLOT(applyClicked()));
    connect(move, SIGNAL(clicked()), this, SLOT(moveClicked()));
    connect(discard, SIGNAL(clicked()), this, SLOT(discardClicked()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(m_files, SIGNAL(currentIndexChanged(int)), this, SLOT(fileChosen(int)));
  }

  /**
   * @return the result of this dialog if the user did not cancel.
   */
  Result result() const { return m_Result; }

  /**
   * @brief the settings the script asked for, by INI file
   */
  void setIniSettings(std::map<QString, PSettings> const& settings)
  {
    for (auto const& p : settings) {
      m_texts.append(p.second.toString());
      m_files->addItem(p.first);
    }
    // one file needs no choice
    m_files->setEnabled(m_files->count() > 1);
    m_files->setCurrentIndex(0);
    m_settings->setPlainText(m_texts.isEmpty() ? QString() : m_texts.first());
  }

private slots:
  void applyClicked()
  {
    m_Result = Result::APPLY;
    accept();
  }

  void moveClicked()
  {
    m_Result = Result::MOVE;
    accept();
  }

  void discardClicked()
  {
    m_Result = Result::DISCARD;
    accept();
  }

  void fileChosen(int index)
  {
    m_settings->setPlainText(index >= 0 && index < m_texts.size() ? m_texts[index]
                                                                   : QString());
  }

private:
  QComboBox* m_files;
  QPlainTextEdit* m_settings;
  QStringList m_texts;
  Result m_Result{Result::APPLY};
};

#endif
