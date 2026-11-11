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

#ifndef INSTALLDIALOG_H
#define INSTALLDIALOG_H

#include "archivetree.h"
#include "fluentcontentdialog.h"

#include <guessedvalue.h>
#include <ifiletree.h>
#include <moddatachecker.h>

class QComboBox;
class QLabel;
class QPushButton;

/**
 * @brief The manual installation: the archive's content in a tree the user
 * rearranges by dragging, clears the check boxes of what not to install, and
 * picks the folder that goes into the game's data folder. MO2 Revamped draws it
 * as a Windows 11 content dialog, with buttons for what was only in the
 * right-click menu.
 **/
class InstallDialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  /**
   * @param tree the archive's content
   * @param modName the guessed name of the mod and its other guesses
   * @param modDataChecker the game's check of a data folder's layout, or null
   * @param dataName the name of the game's data folder ("data")
   * @param parent parent widget
   **/
  explicit InstallDialog(std::shared_ptr<MOBase::IFileTree> tree,
                         const MOBase::GuessedValue<QString>& modName,
                         std::shared_ptr<const MOBase::ModDataChecker> modDataChecker,
                         const QString& dataName, QWidget* parent = nullptr);

  QString getModName() const;

  /**
   * @return the tree as the user left it: a new tree, or a subtree of the
   * original one
   **/
  std::shared_ptr<MOBase::IFileTree> getModifiedTree() const;

signals:

  void openFile(const MOBase::FileTreeEntry* entry);

private:
  bool testForProblem();
  void updateProblems();
  void updateActions();
  void createDirectoryUnder(ArchiveTreeWidgetItem* treeItem);
  void showContextMenu(QPoint pos);
  void install();
  ArchiveTreeWidgetItem* currentItem() const;
  ArchiveTreeWidgetItem* folderFor(ArchiveTreeWidgetItem* item) const;
  QString dataName() const;

  std::shared_ptr<const MOBase::ModDataChecker> m_Checker;

  // Name of the "data" directory:
  QString m_DataFolderName;

  // the tree root is the initial root that will never change; it is not added
  // to the tree, but keeps the state of the tree so that no entry is lost when
  // the data root is unset
  ArchiveTreeWidget* m_Tree;
  ArchiveTreeWidgetItem* m_TreeRoot;

  QComboBox* m_name;
  QLabel* m_nameProblem;
  QLabel* m_status;
  QPushButton* m_setData;
  QPushButton* m_unsetData;
  QPushButton* m_newFolder;
  QPushButton* m_open;
  QPushButton* m_install;
  bool m_nameEdited = false;
};

#endif  // INSTALLDIALOG_H
