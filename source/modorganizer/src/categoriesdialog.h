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

#ifndef CATEGORIESDIALOG_H
#define CATEGORIESDIALOG_H

#include "categories.h"
#include "tutorabledialog.h"
#include <QMap>
#include <set>

class QComboBox;
class QLabel;
class QScrollArea;
namespace FluentPanels
{
class ElidedLabel;
}
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace Ui
{
class CategoriesDialog;
}

/**
 * @brief Dialog that allows users to configure mod categories
 **/
class CategoriesDialog : public MOBase::TutorableDialog
{
  Q_OBJECT

public:
  explicit CategoriesDialog(QWidget* parent = 0);
  ~CategoriesDialog();

  // also saves and restores geometry
  //
  int exec() override;

  /**
   * @brief store changes here to the global categories store (categories.h)
   *
   **/
  void commitChanges();

public slots:
  void nxmGameInfoAvailable(QString gameName, QVariant, QVariant resultData, int);
  void nxmRequestFailed(QString, int, int, QVariant, int, int errorCode,
                        const QString& errorMessage);

private slots:
  void on_categoriesTable_customContextMenuRequested(const QPoint& pos);
  void addCategory_clicked();
  void removeCategory_clicked();
  void removeNexusMap_clicked();
  void nexusRefresh_clicked();
  void nexusImport_clicked();
  void cellChanged(int row, int column);

private:
  void refreshIDs();
  void fillTable();

  // Fluent v4: a list card of the categories as a tree and a details card of
  // Settings rows; the table and the Nexus list stay as the data behind them
  void buildFluentLayout();
  QWidget* createFluentList();
  QWidget* createFluentDetails();
  void queueFluentRefresh();
  void refreshFluentTree();
  void refreshFluentDetails();
  void showFluentListMenu(const QPoint& pos);
  void showFluentHelp();
  int rowForID(int id) const;
  int selectedID() const;
  void selectID(int id);
  void addFluentCategory(int parentID);
  void setRowMappings(int row, const QVariantList& mappings);
  void mapNexusCategory(int row, const QString& name, int nexusID);
  void moveCategory(int delta);
  void sortCategories();

private:
  Ui::CategoriesDialog* ui;
  int m_ContextRow;

  int m_HighestID;
  std::set<int> m_IDs;

  bool m_fluent              = false;
  bool m_fluentRefreshQueued = false;
  bool m_fluentSyncing       = false;
  QMap<int, int> m_modCounts;
  QTreeWidget* m_fluentTree        = nullptr;
  QLineEdit* m_fluentSearch        = nullptr;
  QLabel* m_fluentCount            = nullptr;
  QLabel* m_fluentName             = nullptr;
  QLabel* m_fluentCaption          = nullptr;
  QLineEdit* m_fluentNameEdit      = nullptr;
  QComboBox* m_fluentParent        = nullptr;
  FluentPanels::ElidedLabel* m_fluentID   = nullptr;
  FluentPanels::ElidedLabel* m_fluentMods = nullptr;
  QScrollArea* m_fluentScroll      = nullptr;
  QWidget* m_fluentMappings        = nullptr;
  QComboBox* m_fluentNexusPicker   = nullptr;
  QWidget* m_fluentDetailRows      = nullptr;
  QLabel* m_fluentEmpty            = nullptr;
  QPushButton* m_fluentAddSub      = nullptr;
  QPushButton* m_fluentRemove      = nullptr;
};

#endif  // CATEGORIESDIALOG_H
