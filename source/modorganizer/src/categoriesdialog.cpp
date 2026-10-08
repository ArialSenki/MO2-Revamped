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

#include "categoriesdialog.h"
#include "categories.h"
#include "categoryimportdialog.h"
#include "messagedialog.h"
#include "nexusinterface.h"
#include "settings.h"
#include "ui_categoriesdialog.h"
#include "utility.h"
#include <QItemDelegate>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QRegularExpressionValidator>
#include <QSet>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QVector>

class NewIDValidator : public QIntValidator
{
public:
  NewIDValidator(const std::set<int>& ids) : m_UsedIDs(ids) {}
  virtual State validate(QString& input, int& pos) const
  {
    State intRes = QIntValidator::validate(input, pos);
    if (intRes == Acceptable) {
      bool ok = false;
      int id  = input.toInt(&ok);
      if (m_UsedIDs.find(id) != m_UsedIDs.end()) {
        return QValidator::Intermediate;
      }
    }
    return intRes;
  }

private:
  const std::set<int>& m_UsedIDs;
};

class ExistingIDValidator : public QIntValidator
{
public:
  ExistingIDValidator(const std::set<int>& ids) : m_UsedIDs(ids) {}
  virtual State validate(QString& input, int& pos) const
  {
    State intRes = QIntValidator::validate(input, pos);
    if (intRes == Acceptable) {
      bool ok = false;
      int id  = input.toInt(&ok);
      if ((id == 0) || (m_UsedIDs.find(id) != m_UsedIDs.end())) {
        return QValidator::Acceptable;
      } else {
        return QValidator::Intermediate;
      }
    } else {
      return intRes;
    }
  }

private:
  const std::set<int>& m_UsedIDs;
};

class ValidatingDelegate : public QItemDelegate
{

public:
  ValidatingDelegate(QObject* parent, QValidator* validator)
      : QItemDelegate(parent), m_Validator(validator)
  {}

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                        const QModelIndex&) const
  {
    QLineEdit* edit = new QLineEdit(parent);
    edit->setValidator(m_Validator);
    return edit;
  }
  virtual void setModelData(QWidget* editor, QAbstractItemModel* model,
                            const QModelIndex& index) const
  {
    QLineEdit* edit  = qobject_cast<QLineEdit*>(editor);
    int pos          = 0;
    QString editText = edit->text();
    if (m_Validator->validate(editText, pos) == QValidator::Acceptable) {
      QItemDelegate::setModelData(editor, model, index);
    }
  }

private:
  QValidator* m_Validator;
};

CategoriesDialog::CategoriesDialog(QWidget* parent)
    : TutorableDialog("Categories", parent), ui(new Ui::CategoriesDialog),
      m_ContextRow(-1), m_HighestID(0)
{
  ui->setupUi(this);
  if (auto* game = Settings::instance().game().plugin()) {
    setWindowTitle(tr("Manage Categories - %1").arg(game->gameName()));
    ui->dialogHeading->setText(
        tr("Organize categories for %1").arg(game->gameName()));
  }
  ui->contentLayout->setStretch(0, 3);
  ui->contentLayout->setStretch(1, 2);
  setMinimumSize(920, 540);
  resize(1020, 620);
  setSizeGripEnabled(true);
  ui->categoriesTable->setMinimumWidth(500);
  ui->categoriesTable->setAlternatingRowColors(true);
  ui->categoriesTable->setShowGrid(false);
  ui->categoriesTable->verticalHeader()->hide();
  ui->categoriesTable->verticalHeader()->setDefaultSectionSize(30);
  ui->categoriesTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  ui->categoriesTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  ui->categoriesTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  ui->categoriesTable->horizontalHeader()->setStretchLastSection(true);
  ui->nexusCategoriesCard->setMinimumWidth(300);
  ui->nexusCategoryList->setAlternatingRowColors(true);
  ui->nexusCategoryList->setUniformItemSizes(true);
  ui->nexusCategoryList->setSpacing(1);
  ui->nexusRefresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  ui->nexusImportButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  ui->addCategoryButton->setIcon(QIcon(":/MO/gui/contextmenu/categories.svg"));
  ui->removeCategoryButton->setIcon(QIcon(":/MO/gui/contextmenu/remove.svg"));
  ui->nexusRefresh->setToolTip(
      tr("Load the latest category list for the active game from Nexus."));
  ui->nexusImportButton->setToolTip(
      tr("Add the categories currently listed here to your MO2 category list."));
  fillTable();
  connect(ui->categoriesTable, SIGNAL(cellChanged(int, int)), this,
          SLOT(cellChanged(int, int)));
  ui->removeCategoryButton->setEnabled(false);
  connect(ui->addCategoryButton, &QPushButton::clicked, this, [this]() {
    m_ContextRow = ui->categoriesTable->rowCount();
    addCategory_clicked();
  });
  connect(ui->removeCategoryButton, &QPushButton::clicked, this, [this]() {
    m_ContextRow = ui->categoriesTable->currentRow();
    removeCategory_clicked();
  });
  connect(ui->categoriesTable, &QTableWidget::itemSelectionChanged, this,
          [this]() {
            ui->removeCategoryButton->setEnabled(
                !ui->categoriesTable->selectedItems().isEmpty());
          });
  if (Settings::instance().nexus().categoryMappings()) {
    connect(ui->nexusRefresh, SIGNAL(clicked()), this, SLOT(nexusRefresh_clicked()));
    connect(ui->nexusImportButton, SIGNAL(clicked()), this,
            SLOT(nexusImport_clicked()));
    ui->nexusCategoryList->setDisabled(false);
  } else {
    ui->nexusCategoryList->setDisabled(true);
    ui->nexusStatus->setText(
        tr("Nexus category mapping is disabled in Settings."));
    ui->nexusRefresh->setDisabled(true);
    ui->nexusImportButton->setDisabled(true);
  }
}

CategoriesDialog::~CategoriesDialog()
{
  delete ui;
}

int CategoriesDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);
  return QDialog::exec();
}

void CategoriesDialog::cellChanged(int row, int)
{
  if (row >= 0 && row < ui->categoriesTable->rowCount()) {
    refreshIDs();
  }
}

void CategoriesDialog::commitChanges()
{
  struct PendingCategory
  {
    int id;
    QString name;
    int parentID;
    std::vector<CategoryFactory::NexusCategory> nexusCategories;
  };

  std::vector<PendingCategory> pendingCategories;
  QTableWidget* table = ui->categoriesTable;
  for (int visualRow = 0; visualRow < table->rowCount(); ++visualRow) {
    const int row = table->verticalHeader()->logicalIndex(visualRow);
    if (row < 0 || row >= table->rowCount()) {
      continue;
    }

    const auto* idItem       = table->item(row, 0);
    const auto* nameItem     = table->item(row, 1);
    const auto* parentIDItem = table->item(row, 2);
    if (!idItem || !nameItem || !parentIDItem) {
      continue;
    }

    bool idValid = false;
    const int id = idItem->text().toInt(&idValid);
    if (!idValid || id <= 0) {
      continue;
    }

    bool parentIDValid = false;
    int parentID = parentIDItem->text().toInt(&parentIDValid);
    if (!parentIDValid) {
      parentID = 0;
    }

    PendingCategory pending{id, nameItem->text(), parentID, {}};
    const auto* mappingItem = table->item(row, 3);
    const QVariantList nexusData = mappingItem
                                       ? mappingItem->data(Qt::UserRole).toList()
                                       : QVariantList();
    for (const QVariant& nexusEntry : nexusData) {
      const QVariantList mapping = nexusEntry.toList();
      if (mapping.size() < 2) {
        continue;
      }

      const QString nexusName = mapping.at(0).toString().trimmed();
      bool nexusIDValid = false;
      const int nexusID = mapping.at(1).toInt(&nexusIDValid);
      if (nexusName.isEmpty() || !nexusIDValid || nexusID <= 0) {
        continue;
      }
      pending.nexusCategories.emplace_back(nexusName, nexusID);
    }
    pendingCategories.push_back(std::move(pending));
  }

  CategoryFactory& categories = CategoryFactory::instance();
  categories.reset();
  for (const auto& category : pendingCategories) {
    categories.addCategory(category.id, category.name, category.nexusCategories,
                           category.parentID);
  }

  categories.setParents();

  std::vector<CategoryFactory::NexusCategory> nexusCats;
  for (int i = 0; i < ui->nexusCategoryList->count(); ++i) {
    const auto* item = ui->nexusCategoryList->item(i);
    if (!item) {
      continue;
    }
    bool nexusIDValid = false;
    const int nexusID = item->data(Qt::UserRole).toInt(&nexusIDValid);
    const QString name = item->data(Qt::DisplayRole).toString().trimmed();
    if (nexusIDValid && nexusID > 0 && !name.isEmpty()) {
      nexusCats.emplace_back(name, nexusID);
    }
  }

  categories.setNexusCategories(nexusCats);

  categories.saveCategories();
}

void CategoriesDialog::refreshIDs()
{
  m_HighestID = 0;
  m_IDs.clear();
  for (int i = 0; i < ui->categoriesTable->rowCount(); ++i) {
    const auto* idItem = ui->categoriesTable->item(i, 0);
    if (!idItem) {
      continue;
    }
    int id = idItem->text().toInt();
    if (id > m_HighestID) {
      m_HighestID = id;
    }
    m_IDs.insert(id);
  }
}

void CategoriesDialog::fillTable()
{
  CategoryFactory& categories = CategoryFactory::instance();
  QTableWidget* table         = ui->categoriesTable;
  QListWidget* list           = ui->nexusCategoryList;
  const bool wasSorting       = table->isSortingEnabled();
  table->setSortingEnabled(false);
  QStringList gameNames;
  if (auto* game = Settings::instance().game().plugin()) {
    gameNames << game->gameName() << game->gameShortName() << game->gameNexusName();
  }

  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table->setColumnWidth(0, 54);
  table->setColumnWidth(1, 150);
  table->setColumnWidth(2, 82);
  table->horizontalHeader()->setMinimumSectionSize(54);
  table->verticalHeader()->setSectionsMovable(true);
  table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  table->setItemDelegateForColumn(
      0, new ValidatingDelegate(this, new NewIDValidator(m_IDs)));
  table->setItemDelegateForColumn(
      2, new ValidatingDelegate(this, new ExistingIDValidator(m_IDs)));

  table->setRowCount(0);
  list->clear();

  QHash<int, int> tableRowsByCategoryID;
  QHash<QString, int> localCategoryIDsByName;
  QSet<QString> ambiguousLocalNames;
  for (const auto& category : categories.m_Categories) {
    if (category.ID() == 0) {
      continue;
    }

    const int row = table->rowCount();
    table->insertRow(row);

    QScopedPointer<QTableWidgetItem> idItem(new QTableWidgetItem());
    idItem->setData(Qt::DisplayRole, category.ID());

    QScopedPointer<QTableWidgetItem> nameItem(new QTableWidgetItem(category.name()));
    QScopedPointer<QTableWidgetItem> parentIDItem(new QTableWidgetItem());
    parentIDItem->setData(Qt::DisplayRole, category.parentID());
    QScopedPointer<QTableWidgetItem> nexusCatItem(new QTableWidgetItem());

    table->setItem(row, 0, idItem.take());
    table->setItem(row, 1, nameItem.take());
    table->setItem(row, 2, parentIDItem.take());
    nexusCatItem->setFlags(nexusCatItem->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 3, nexusCatItem.take());

    tableRowsByCategoryID.insert(category.ID(), row);
    const QString nameKey = category.name().trimmed().toCaseFolded();
    if (!nameKey.isEmpty()) {
      if (localCategoryIDsByName.contains(nameKey)) {
        ambiguousLocalNames.insert(nameKey);
      } else {
        localCategoryIDsByName.insert(nameKey, category.ID());
      }
    }
  }

  // Old drag/drop data can contain a fake Nexus entry whose name is only the
  // first character and whose ID is the second character's code point. Hide
  // that entry in the editor even if the startup migration could not repair it.
  QSet<int> staleLegacyIDs;
  for (const auto& entry : categories.m_NexusMap) {
    const CategoryFactory::NexusCategory& legacy = entry.second;
    const QString legacyName                   = legacy.name().trimmed();
    const int localCategoryID                  = legacy.categoryID();
    if (localCategoryID <= 0 || !categories.categoryExists(localCategoryID) ||
        legacyName.size() != 1 || entry.first <= 0 || entry.first > 0xFFFF) {
      continue;
    }

    const QChar secondCharacter(static_cast<ushort>(entry.first));
    if (!secondCharacter.isLetterOrNumber()) {
      continue;
    }

    const QString expectedPrefix = legacyName + secondCharacter;
    const QString localName =
        categories.getCategoryNameByID(localCategoryID).trimmed();
    int matchingFullNames = 0;
    for (const auto& candidate : categories.m_NexusMap) {
      if (candidate.first != entry.first &&
          candidate.second.name().compare(localName, Qt::CaseInsensitive) == 0 &&
          candidate.second.name().startsWith(expectedPrefix, Qt::CaseInsensitive)) {
        ++matchingFullNames;
      }
    }
    if (matchingFullNames == 1) {
      staleLegacyIDs.insert(entry.first);
    }
  }

  QVector<QVariantList> rowMappings(table->rowCount());
  QVector<QSet<int>> rowMappingIDs(table->rowCount());
  int displayedNexusCategories = 0;
  for (const auto& nexusCat : categories.m_NexusMap) {
    if (staleLegacyIDs.contains(nexusCat.first)) {
      continue;
    }
    if (CategoryFactory::isNexusGameRootCategory(nexusCat.second.name(),
                                                 nexusCat.second.ID(), gameNames)) {
      continue;
    }

    QScopedPointer<QListWidgetItem> nexusItem(new QListWidgetItem());
    nexusItem->setData(Qt::DisplayRole, nexusCat.second.name());
    nexusItem->setData(Qt::UserRole, nexusCat.second.ID());
    nexusItem->setData(
        Qt::ToolTipRole,
        tr("Nexus category ID: %1. Drag this entry onto a local category to map it.")
            .arg(nexusCat.second.ID()));
    list->addItem(nexusItem.take());

    ++displayedNexusCategories;
    const QString nameKey = nexusCat.second.name().trimmed().toCaseFolded();
    int localCategoryID = -1;
    if (!nameKey.isEmpty() && !ambiguousLocalNames.contains(nameKey) &&
        localCategoryIDsByName.contains(nameKey)) {
      // Prefer a unique full-name match. Besides filling missing assignments,
      // this replaces legacy one-letter labels with the canonical Nexus name.
      localCategoryID = localCategoryIDsByName.value(nameKey);
    } else if (categories.categoryExists(nexusCat.second.categoryID())) {
      localCategoryID = nexusCat.second.categoryID();
    }

    const auto rowIt = tableRowsByCategoryID.constFind(localCategoryID);
    if (rowIt == tableRowsByCategoryID.cend()) {
      continue;
    }

    const int row = rowIt.value();
    if (rowMappingIDs[row].contains(nexusCat.second.ID())) {
      continue;
    }

    QVariantList mapping;
    mapping.append(nexusCat.second.name().trimmed());
    mapping.append(nexusCat.second.ID());
    rowMappings[row].append(QVariant::fromValue(mapping));
    rowMappingIDs[row].insert(nexusCat.second.ID());
  }

  for (int row = 0; row < table->rowCount(); ++row) {
    QStringList names;
    for (const QVariant& entry : rowMappings[row]) {
      const QVariantList mapping = entry.toList();
      if (!mapping.isEmpty()) {
        names.append(mapping.first().toString());
      }
    }

    QTableWidgetItem* item = table->item(row, 3);
    if (!item) {
      item = new QTableWidgetItem();
      table->setItem(row, 3, item);
    }
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setData(Qt::UserRole, rowMappings[row]);
    item->setText(names.join(", "));
  }

  ui->nexusStatus->setText(
      displayedNexusCategories == 0
          ? tr("No Nexus categories loaded. Refresh the list to fetch categories for this game.")
          : tr("%1 Nexus categories available; full category names are shown in the mappings.")
                .arg(displayedNexusCategories));

  table->setSortingEnabled(wasSorting);
  refreshIDs();
}

void CategoriesDialog::addCategory_clicked()
{
  const bool wasSorting = ui->categoriesTable->isSortingEnabled();
  ui->categoriesTable->setSortingEnabled(false);
  int row = m_ContextRow >= 0 ? m_ContextRow : 0;
  ui->categoriesTable->insertRow(row);
  ui->categoriesTable->setVerticalHeaderItem(row, new QTableWidgetItem("  "));
  const int newID = ++m_HighestID;
  m_IDs.insert(newID);
  ui->categoriesTable->setItem(row, 0,
                               new QTableWidgetItem(QString::number(newID)));
  ui->categoriesTable->setItem(row, 1, new QTableWidgetItem("new"));
  ui->categoriesTable->setItem(row, 2, new QTableWidgetItem("0"));
  auto* mappingItem = new QTableWidgetItem("");
  mappingItem->setFlags(mappingItem->flags() & ~Qt::ItemIsEditable);
  ui->categoriesTable->setItem(row, 3, mappingItem);
  ui->categoriesTable->setSortingEnabled(wasSorting);
  m_ContextRow = -1;
  refreshIDs();
}

void CategoriesDialog::removeCategory_clicked()
{
  if (m_ContextRow >= 0) {
    ui->categoriesTable->removeRow(m_ContextRow);
    m_ContextRow = -1;
    refreshIDs();
  }
}

void CategoriesDialog::removeNexusMap_clicked()
{
  if (m_ContextRow >= 0) {
    ui->categoriesTable->item(m_ContextRow, 3)->setData(Qt::UserRole, QVariantList());
    ui->categoriesTable->item(m_ContextRow, 3)->setData(Qt::DisplayRole, QString());
  }
}

void CategoriesDialog::nexusRefresh_clicked()
{
  ui->nexusStatus->setText(
      tr("Loading the active game's categories from Nexus…"));
  CategoryFactory::instance().refreshNexusCategories(this);
}

void CategoriesDialog::nexusImport_clicked()
{
  if (ui->nexusCategoryList->count() == 0) {
    MessageDialog::showMessage(
        tr("The Nexus list is empty. Refresh it before importing categories."), this);
    return;
  }

  auto importDialog = CategoryImportDialog(this);
  if (importDialog.exec() && importDialog.strategy()) {
    refreshIDs();
    QTableWidget* table = ui->categoriesTable;
    QListWidget* list   = ui->nexusCategoryList;
    const bool wasSorting = table->isSortingEnabled();
    table->setSortingEnabled(false);
    if (importDialog.strategy() == CategoryImportDialog::Overwrite) {
      table->setRowCount(0);
      m_HighestID = 0;
      m_IDs.clear();
    }

    const auto mappedRowForNexusID = [table](int nexusID) {
      for (int row = 0; row < table->rowCount(); ++row) {
        const auto* mappingItem = table->item(row, 3);
        if (!mappingItem) {
          continue;
        }
        for (const QVariant& entry : mappingItem->data(Qt::UserRole).toList()) {
          const QVariantList mapping = entry.toList();
          if (mapping.size() >= 2 && mapping[1].toInt() == nexusID) {
            return row;
          }
        }
      }
      return -1;
    };

    for (int i = 0; i < list->count(); ++i) {
      QString name = list->item(i)->data(Qt::DisplayRole).toString();
      int nexusID  = list->item(i)->data(Qt::UserRole).toInt();
      QVariantList nexusData;
      QVariantList data;
      data.append(QVariant(name));
      data.append(QVariant(nexusID));
      nexusData.append(QVariant::fromValue(data));
      QScopedPointer<QTableWidgetItem> nexusCatItem(
          new QTableWidgetItem(name));
      nexusCatItem->setData(Qt::UserRole, nexusData);
      nexusCatItem->setFlags(nexusCatItem->flags() & ~Qt::ItemIsEditable);

      int existingRow = -1;
      for (int row = 0; row < table->rowCount(); ++row) {
        const auto* existingName = table->item(row, 1);
        if (existingName &&
            existingName->text().compare(name, Qt::CaseInsensitive) == 0) {
          existingRow = row;
          break;
        }
      }

      const int mappedRow = mappedRowForNexusID(nexusID);
      if (existingRow < 0 && mappedRow >= 0 && !importDialog.remap()) {
        // Preserve a user's existing category mapping instead of creating a
        // second local category that would silently take over the same Nexus ID.
        continue;
      }

      if (existingRow < 0) {
        const int row = table->rowCount();
        table->insertRow(row);

        QScopedPointer<QTableWidgetItem> idItem(new QTableWidgetItem());
        idItem->setData(Qt::DisplayRole, ++m_HighestID);

        QScopedPointer<QTableWidgetItem> nameItem(new QTableWidgetItem(name));
        QScopedPointer<QTableWidgetItem> parentIDItem(new QTableWidgetItem());
        parentIDItem->setData(Qt::DisplayRole, 0);  // No parent

        table->setItem(row, 0, idItem.take());
        table->setItem(row, 1, nameItem.take());
        table->setItem(row, 2, parentIDItem.take());
        if (importDialog.assign()) {
          table->setItem(row, 3, nexusCatItem.take());
        } else {
          table->setItem(row, 3, new QTableWidgetItem());
        }
      } else if (importDialog.assign()) {
        if (importDialog.remap()) {
          table->setItem(existingRow, 3, nexusCatItem.take());
        } else if (mappedRow < 0 || mappedRow == existingRow) {
          auto* mappingItem = table->item(existingRow, 3);
          if (!mappingItem) {
            mappingItem = new QTableWidgetItem();
            table->setItem(existingRow, 3, mappingItem);
          }
          QVariantList mappings = mappingItem->data(Qt::UserRole).toList();
          bool alreadyMapped   = false;
          QStringList names;
          for (const QVariant& entry : mappings) {
            const QVariantList mapping = entry.toList();
            if (mapping.size() >= 2) {
              names.append(mapping[0].toString());
              alreadyMapped = alreadyMapped || mapping[1].toInt() == nexusID;
            }
          }
          if (!alreadyMapped) {
            mappings.append(QVariant::fromValue(data));
            names.append(name);
          }
          mappingItem->setData(Qt::UserRole, mappings);
          mappingItem->setData(Qt::DisplayRole, names.join(", "));
          mappingItem->setFlags(mappingItem->flags() & ~Qt::ItemIsEditable);
        }
      }
    }
    table->setSortingEnabled(wasSorting);
    refreshIDs();
    ui->nexusStatus->setText(
        tr("Category list updated. Review the MO2 categories, then choose OK to save."));
  }
}

void CategoriesDialog::nxmGameInfoAvailable(QString gameName, QVariant,
                                            QVariant resultData, int)
{
  QVariantMap result          = resultData.toMap();
  QVariantList categories     = result["categories"].toList();
  QListWidget* list           = ui->nexusCategoryList;
  QStringList gameNames{gameName};
  QString gameDisplayName = gameName;
  if (auto* game = Settings::instance().game().plugin()) {
    gameNames << game->gameName() << game->gameShortName() << game->gameNexusName();
    gameDisplayName = game->gameName();
  }
  std::vector<CategoryFactory::NexusCategory> refreshedCategories;
  std::set<int> seenNexusIDs;
  for (const auto& category : categories) {
    auto catMap = category.toMap();
    const QString categoryName = catMap["name"].toString();
    const int categoryID       = catMap["category_id"].toInt();
    if (CategoryFactory::isNexusGameRootCategory(categoryName, categoryID,
                                                 gameNames)) {
      continue;
    }
    if (categoryName.trimmed().isEmpty() || !seenNexusIDs.insert(categoryID).second) {
      continue;
    }
    refreshedCategories.emplace_back(categoryName, categoryID);
  }

  if (refreshedCategories.empty()) {
    ui->nexusStatus->setText(
        tr("Nexus returned no valid categories. The current list and mappings were kept."));
    return;
  }

  list->clear();
  for (const auto& category : refreshedCategories) {
    QScopedPointer<QListWidgetItem> nexusItem(new QListWidgetItem());
    nexusItem->setData(Qt::DisplayRole, category.name());
    nexusItem->setData(Qt::UserRole, category.ID());
    nexusItem->setData(
        Qt::ToolTipRole,
        tr("Nexus category ID: %1. Drag this entry onto a local category to map it.")
            .arg(category.ID()));
    list->addItem(nexusItem.take());
  }

  QTableWidget* table       = ui->categoriesTable;
  const bool wasSorting     = table->isSortingEnabled();
  table->setSortingEnabled(false);

  QHash<QString, int> localRows;
  QSet<QString> ambiguousLocalNames;
  for (int row = 0; row < table->rowCount(); ++row) {
    const auto* idItem   = table->item(row, 0);
    const auto* nameItem = table->item(row, 1);
    if (!idItem || !nameItem || idItem->text().toInt() <= 0) {
      continue;
    }

    const QString key = nameItem->text().trimmed().toCaseFolded();
    if (key.isEmpty()) {
      continue;
    }
    if (localRows.contains(key)) {
      ambiguousLocalNames.insert(key);
    } else {
      localRows.insert(key, row);
    }
  }

  QHash<QString, int> nexusNameCounts;
  for (const auto& category : refreshedCategories) {
    ++nexusNameCounts[category.name().trimmed().toCaseFolded()];
  }

  QSet<int> refreshedIDs;
  for (const auto& category : refreshedCategories) {
    refreshedIDs.insert(category.ID());
  }

  QHash<int, int> targetRows;
  int preservedAssignments = 0;
  int exactNameMappings    = 0;
  for (const auto& category : refreshedCategories) {
    const QString key = category.name().trimmed().toCaseFolded();
    int idMatchRow    = -1;
    int nameMatchRow  = -1;
    int shortMatchRow = -1;

    for (int row = 0; row < table->rowCount(); ++row) {
      const auto* mappingItem = table->item(row, 3);
      if (!mappingItem) {
        continue;
      }

      const QString localName = table->item(row, 1)
                                    ? table->item(row, 1)->text().trimmed()
                                    : QString();
      const bool localNameMatches =
          localName.compare(category.name().trimmed(), Qt::CaseInsensitive) == 0;
      for (const QVariant& entry : mappingItem->data(Qt::UserRole).toList()) {
        const QVariantList mapping = entry.toList();
        if (mapping.size() < 2) {
          continue;
        }

        bool idValid      = false;
        const int nexusID = mapping.at(1).toInt(&idValid);
        const QString name = mapping.at(0).toString().trimmed();
        if (idValid && nexusID == category.ID()) {
          idMatchRow = (localNameMatches || idMatchRow < 0) ? row : idMatchRow;
        } else if (nexusNameCounts.value(key) == 1 &&
                   name.compare(category.name().trimmed(),
                                Qt::CaseInsensitive) == 0) {
          nameMatchRow =
              (localNameMatches || nameMatchRow < 0) ? row : nameMatchRow;
        } else if (nexusNameCounts.value(key) == 1 && localNameMatches &&
                   name.size() == 1 &&
                   name.compare(category.name().left(1),
                                Qt::CaseInsensitive) == 0) {
          shortMatchRow = row;
        }
      }
    }

    // Prefer a unique exact local-name match over a saved Nexus ID. Older
    // drag/drop mappings can contain a stale or truncated label while their ID
    // still looks valid; trusting that ID first can keep the bad row assignment.
    int targetRow = -1;
    const bool hasUniqueLocalName =
        !key.isEmpty() && nexusNameCounts.value(key) == 1 &&
        !ambiguousLocalNames.contains(key) && localRows.contains(key);
    if (hasUniqueLocalName) {
      targetRow = localRows.value(key);
      if (idMatchRow >= 0 || nameMatchRow >= 0 || shortMatchRow >= 0) {
        ++preservedAssignments;
      } else {
        ++exactNameMappings;
      }
    } else if (idMatchRow >= 0) {
      targetRow = idMatchRow;
      ++preservedAssignments;
    } else if (nameMatchRow >= 0) {
      targetRow = nameMatchRow;
      ++preservedAssignments;
    } else if (shortMatchRow >= 0) {
      targetRow = shortMatchRow;
      ++preservedAssignments;
    }

    if (targetRow >= 0) {
      targetRows.insert(category.ID(), targetRow);
    }
  }

  QVector<QVariantList> rowMappings(table->rowCount());
  for (int row = 0; row < table->rowCount(); ++row) {
    const auto* mappingItem = table->item(row, 3);
    if (!mappingItem) {
      continue;
    }

    const QString localName = table->item(row, 1)
                                  ? table->item(row, 1)->text().trimmed()
                                  : QString();
    for (const QVariant& entry : mappingItem->data(Qt::UserRole).toList()) {
      QVariantList mapping = entry.toList();
      if (mapping.size() < 2) {
        continue;
      }

      bool idValid      = false;
      const int nexusID = mapping.at(1).toInt(&idValid);
      const QString name = mapping.at(0).toString().trimmed();
      bool belongsToRefreshedCategory = idValid && refreshedIDs.contains(nexusID);
      for (const auto& category : refreshedCategories) {
        const QString key = category.name().trimmed().toCaseFolded();
        if (nexusNameCounts.value(key) != 1) {
          continue;
        }

        const bool fullNameMatch =
            name.compare(category.name().trimmed(), Qt::CaseInsensitive) == 0;
        const bool truncatedNameMatch =
            !localName.isEmpty() &&
            localName.compare(category.name().trimmed(), Qt::CaseInsensitive) == 0 &&
            name.size() == 1 &&
            name.compare(category.name().left(1), Qt::CaseInsensitive) == 0;
        belongsToRefreshedCategory =
            belongsToRefreshedCategory || fullNameMatch || truncatedNameMatch;
      }

      if (belongsToRefreshedCategory || !idValid || nexusID <= 0 || name.isEmpty()) {
        continue;
      }

      bool alreadyKept = false;
      for (const QVariant& kept : rowMappings[row]) {
        const QVariantList keptMapping = kept.toList();
        if (keptMapping.size() >= 2 && keptMapping.at(1).toInt() == nexusID) {
          alreadyKept = true;
          break;
        }
      }
      if (!alreadyKept) {
        rowMappings[row].append(QVariant::fromValue(mapping));
      }
    }
  }

  for (const auto& category : refreshedCategories) {
    const auto target = targetRows.constFind(category.ID());
    if (target == targetRows.cend()) {
      continue;
    }

    QVariantList mapping;
    mapping.append(category.name().trimmed());
    mapping.append(category.ID());
    rowMappings[target.value()].append(QVariant::fromValue(mapping));
  }

  for (int row = 0; row < table->rowCount(); ++row) {
    QStringList displayNames;
    QSet<int> displayedIDs;
    for (const QVariant& entry : rowMappings[row]) {
      const QVariantList mapping = entry.toList();
      if (mapping.size() >= 2) {
        bool idValid = false;
        const int nexusID = mapping.at(1).toInt(&idValid);
        const QString name = mapping.at(0).toString().trimmed();
        if (idValid && nexusID > 0 && !displayedIDs.contains(nexusID) &&
            !name.isEmpty()) {
          displayNames.append(name);
          displayedIDs.insert(nexusID);
        }
      }
    }

    // Replace the item so Qt cannot retain a stale edit/display value from a
    // legacy mapping cell after the canonical Nexus names have been rebuilt.
    auto* mappingItem = new QTableWidgetItem(displayNames.join(", "));
    mappingItem->setData(Qt::UserRole, rowMappings[row]);
    mappingItem->setFlags(mappingItem->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 3, mappingItem);
  }

  table->setSortingEnabled(wasSorting);
  table->viewport()->update();

  ui->nexusStatus->setText(
      tr("Loaded %1 Nexus categories for %2. Kept %3 existing assignments and added %4 exact-name mappings.")
          .arg(refreshedCategories.size())
          .arg(gameDisplayName)
          .arg(preservedAssignments)
          .arg(exactNameMappings));
}

void CategoriesDialog::nxmRequestFailed(QString, int, int, QVariant, int, int errorCode,
                                        const QString& errorMessage)
{
  ui->nexusStatus->setText(
      tr("Could not refresh the Nexus list. Check your connection and Nexus access."));
  MessageDialog::showMessage(
      tr("Error %1: Request to Nexus failed: %2").arg(errorCode).arg(errorMessage),
      this);
}

void CategoriesDialog::on_categoriesTable_customContextMenuRequested(const QPoint& pos)
{
  m_ContextRow = ui->categoriesTable->rowAt(pos.y());
  QMenu menu;
  QAction* addAction = menu.addAction(tr("Add"), this, SLOT(addCategory_clicked()));
  addAction->setIcon(QIcon(":/MO/gui/contextmenu/categories.svg"));
  QAction* removeAction =
      menu.addAction(tr("Remove"), this, SLOT(removeCategory_clicked()));
  removeAction->setIcon(QIcon(":/MO/gui/contextmenu/remove.svg"));
  if (Settings::instance().nexus().categoryMappings()) {
    QAction* removeMappingAction = menu.addAction(
        tr("Remove Nexus Mapping(s)"), this, SLOT(removeNexusMap_clicked()));
    removeMappingAction->setIcon(
        QIcon(":/MO/gui/contextmenu/remap-category.svg"));
  }

  menu.exec(ui->categoriesTable->mapToGlobal(pos));
}
