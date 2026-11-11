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

#ifndef SIMPLEINSTALLDIALOG_H
#define SIMPLEINSTALLDIALOG_H

#include <guessedvalue.h>

#include "fluentcontentdialog.h"

class QComboBox;
class QLabel;
class QPushButton;

/**
 * @brief Quick Install: asks only for the mod's name, for an archive whose
 * files are already laid out as the game expects. MO2 Revamped draws it as a
 * Windows 11 content dialog (the theme styles it by its object names).
 **/
class SimpleInstallDialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  /**
   * @param preset the guessed name of the mod and its other guesses
   * @param parent parent widget
   **/
  explicit SimpleInstallDialog(const MOBase::GuessedValue<QString>& preset,
                               QWidget* parent = nullptr);

  /**
   * @return true if the user asked for the manual installation dialog
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return the (user-modified) mod name
   **/
  QString getName() const;

  /**
   * @brief the game's plugin moved the archive's files to where the game
   * expects them: the dialog says so
   **/
  void setRearranged();

private:
  void nameChanged();

  QComboBox* m_name;
  QLabel* m_problem;
  QPushButton* m_install;
  bool m_Manual = false;
  bool m_edited = false;
};

#endif  // SIMPLEINSTALLDIALOG_H
