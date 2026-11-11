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

#ifndef BAINCOMPLEXINSTALLERDIALOG_H
#define BAINCOMPLEXINSTALLERDIALOG_H

#include "fluentcontentdialog.h"
#include "ifiletree.h"
#include <guessedvalue.h>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

/**
 * @brief The BAIN package installer (packages split into numbered parts, with
 * no wizard script): the parts to install as check boxes, and the package's
 * package.txt beside them. MO2 Revamped draws it as a Windows 11 content
 * dialog.
 **/
class BainComplexInstallerDialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  /**
   * @param subpackages the package's parts, in the archive's order
   * @param modName the guessed name of the mod and its other guesses
   * @param defaultOptions the parts chosen the last time this mod was installed
   * @param packageTXT the extracted package.txt, or an empty string
   * @param parent parent widget
   **/
  explicit BainComplexInstallerDialog(
      const std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>& subpackages,
      const MOBase::GuessedValue<QString>& modName, const QStringList& defaultOptions,
      const QString& packageTXT, QWidget* parent);

  /**
   * @return true if the user asked for the manual installation dialog
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return the (user-modified) mod name
   **/
  QString getName() const;

  /**
   * @brief replaces the tree with the merge of the chosen parts
   * @return the names of the chosen parts
   **/
  QStringList updateTree(std::shared_ptr<MOBase::IFileTree>& tree);

private:
  void updateState();

  QComboBox* m_name;
  QLabel* m_nameProblem;
  QListWidget* m_parts;
  QLabel* m_count;
  QPushButton* m_install;
  bool m_Manual     = false;
  bool m_nameEdited = false;
};

#endif  // BAINCOMPLEXINSTALLERDIALOG_H
