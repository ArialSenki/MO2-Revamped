#pragma once

#include <guessedvalue.h>

#include "fluentcontentdialog.h"

class QComboBox;

/**
 * @brief The OMOD's name, asked before it installs. MO2 Revamped draws it as a
 * Windows 11 content dialog instead of 2.5.2's .ui, without its Manual button,
 * which was always off (OMODs can't be installed manually).
 **/
class NameDialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  NameDialog(const MOBase::GuessedValue<QString>& suggestedNames, QWidget* parent = nullptr);

  QString getName() const;

private slots:
  void installClicked();
  void nameEdited();

private:
  void nameChanged();

  QComboBox* m_name;
  QLabel* m_problem;
  QPushButton* m_install;
  bool m_edited = false;
};
