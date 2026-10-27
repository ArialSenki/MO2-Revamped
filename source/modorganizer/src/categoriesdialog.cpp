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
#include "menuutils.h"
#include "fluentdialogs.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "modinfo.h"
#include "categories.h"
#include "categoryimportdialog.h"
#include "circularuihelpers.h"
#include "messagedialog.h"
#include "nexusinterface.h"
#include "settings.h"
#include "ui_categoriesdialog.h"
#include "utility.h"
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QItemDelegate>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
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
#include <functional>

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
  MOBase::CircularUi::addIconToHeading(
      ui->dialogHeading, ":/MO/gui/contextmenu/categories.svg",
      QStringLiteral("categoriesHeaderIcon"), 34, QStringLiteral("C"));
  MOBase::CircularUi::addGroupHeading(
      ui->localCategoriesCard, ":/MO/gui/contextmenu/categories.svg",
      QStringLiteral("localCategoriesIcon"), 26,
      QStringLiteral("categoryCardTitle"), QStringLiteral("M"));
  MOBase::CircularUi::addGroupHeading(
      ui->nexusCategoriesCard, ":/MO/gui/mainwindow/mod-page.svg",
      QStringLiteral("nexusCategoriesIcon"), 26,
      QStringLiteral("categoryCardTitle"), QStringLiteral("N"));
  ui->contentLayout->setStretch(0, 3);
  ui->contentLayout->setStretch(1, 2);
  setMinimumSize(960, 600);
  resize(1080, 700);
  setSizeGripEnabled(true);
  ui->categoriesTable->setMinimumWidth(520);
  ui->categoriesTable->setAlternatingRowColors(true);
  ui->categoriesTable->setShowGrid(false);
  ui->categoriesTable->verticalHeader()->hide();
  ui->categoriesTable->verticalHeader()->setDefaultSectionSize(30);
  auto* categoriesHeader = ui->categoriesTable->horizontalHeader();
  categoriesHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  categoriesHeader->setSectionResizeMode(1, QHeaderView::Stretch);
  categoriesHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  categoriesHeader->setSectionResizeMode(3, QHeaderView::Stretch);
  categoriesHeader->setStretchLastSection(false);
  ui->nexusCategoriesCard->setMinimumWidth(320);
  ui->nexusCategoryList->setAlternatingRowColors(true);
  ui->nexusCategoryList->setUniformItemSizes(true);
  ui->nexusCategoryList->setSpacing(1);
  ui->nexusRefresh->setIcon(FluentIcons::icon(":/MO/gui/refresh"));
  ui->nexusImportButton->setIcon(FluentIcons::active()
                                     ? FluentIcons::glyph(0xE896)  // Download
                                     : style()->standardIcon(QStyle::SP_DialogOpenButton));
  ui->addCategoryButton->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/categories.svg"));
  ui->removeCategoryButton->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"));
  ui->nexusRefresh->setIconSize(QSize(16, 16));
  ui->nexusImportButton->setIconSize(QSize(16, 16));
  ui->addCategoryButton->setIconSize(QSize(16, 16));
  ui->removeCategoryButton->setIconSize(QSize(16, 16));
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

  if (FluentIcons::active()) {
    buildFluentLayout();
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
  if (m_ContextRow >= 0 && m_fluent) {
    // the subcategories move up to the removed category's parent
    QTableWidget* table = ui->categoriesTable;
    const int id     = table->item(m_ContextRow, 0) ? table->item(m_ContextRow, 0)->text().toInt() : 0;
    const int parent = table->item(m_ContextRow, 2) ? table->item(m_ContextRow, 2)->text().toInt() : 0;
    for (int row = 0; row < table->rowCount(); ++row) {
      if (id > 0 && row != m_ContextRow && table->item(row, 2) &&
          table->item(row, 2)->text().toInt() == id) {
        table->item(row, 2)->setData(Qt::DisplayRole, parent);
      }
    }
  }
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

    // a Nexus ID belongs to one local category; remapping takes it off the
    // others, as dropping it on a row does
    const auto unmapNexusID = [table](int nexusID, int keepRow) {
      for (int row = 0; row < table->rowCount(); ++row) {
        auto* mappingItem = table->item(row, 3);
        if (row == keepRow || !mappingItem) {
          continue;
        }
        QVariantList keptMappings;
        QStringList names;
        for (const QVariant& entry : mappingItem->data(Qt::UserRole).toList()) {
          const QVariantList mapping = entry.toList();
          if (mapping.size() < 2 || mapping[1].toInt() == nexusID) {
            continue;
          }
          keptMappings.append(QVariant::fromValue(mapping));
          names.append(mapping[0].toString());
        }
        mappingItem->setData(Qt::UserRole, keptMappings);
        mappingItem->setData(Qt::DisplayRole, names.join(", "));
      }
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
          unmapNexusID(nexusID, row);
          table->setItem(row, 3, nexusCatItem.take());
        } else {
          table->setItem(row, 3, new QTableWidgetItem());
        }
      } else if (importDialog.assign()) {
        if (importDialog.remap()) {
          unmapNexusID(nexusID, existingRow);
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
  // adding first, the row's own commands next and removing last
  QAction* addAction =
      menu.addAction(tr("Add category"), this, SLOT(addCategory_clicked()));
  addAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/categories.svg"));

  if (m_ContextRow >= 0) {
    if (auto* name = ui->categoriesTable->item(m_ContextRow, 1)) {
      menu.addSeparator();
      addCopyAction(&menu, tr("Copy name"), name->text());
    }
  }

  if (Settings::instance().nexus().categoryMappings() && m_ContextRow >= 0) {
    QAction* removeMappingAction = menu.addAction(
        tr("Remove Nexus mappings"), this, SLOT(removeNexusMap_clicked()));
    removeMappingAction->setIcon(
        FluentIcons::icon(":/MO/gui/contextmenu/remap-category.svg"));
  }

  if (m_ContextRow >= 0) {
    menu.addSeparator();
    QAction* removeAction =
        menu.addAction(tr("Remove category"), this, SLOT(removeCategory_clicked()));
    removeAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"));
  }

  menu.exec(ui->categoriesTable->mapToGlobal(pos));
}

namespace
{
constexpr int CategoryIDRole = Qt::UserRole + 10;

// the Nexus categories a row maps, without broken or repeated entries
QVariantList validMappings(const QTableWidgetItem* item)
{
  QVariantList result;
  if (item == nullptr) {
    return result;
  }
  QSet<int> seen;
  for (const QVariant& entry : item->data(Qt::UserRole).toList()) {
    const QVariantList mapping = entry.toList();
    if (mapping.size() < 2) {
      continue;
    }
    bool idValid      = false;
    const int nexusID = mapping.at(1).toInt(&idValid);
    if (!idValid || nexusID <= 0 || mapping.at(0).toString().trimmed().isEmpty() ||
        seen.contains(nexusID)) {
      continue;
    }
    seen.insert(nexusID);
    result.append(QVariant::fromValue(mapping));
  }
  return result;
}
}  // namespace

void CategoriesDialog::buildFluentLayout()
{
  // the Designer layout stays for the classic themes; Fluent keeps its table
  // and Nexus list hidden as the data the cards edit, so loading, importing,
  // refreshing from Nexus and saving work as before
  auto* root = qobject_cast<QVBoxLayout*>(layout());
  if (root == nullptr) {
    return;
  }

  ui->categoriesHeaderPanel->hide();
  ui->localCategoriesCard->hide();
  ui->nexusCategoriesCard->hide();
  root->removeWidget(ui->categoriesHeaderPanel);
  root->removeItem(ui->contentLayout);
  root->removeItem(ui->actionsLayout);
  ui->actionsLayout->removeWidget(ui->buttonBox);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  // how many mods use each category, for the captions
  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    if (auto mod = ModInfo::getByIndex(i)) {
      for (int id : mod->getCategories()) {
        ++m_modCounts[id];
      }
    }
  }

  auto* body = new QHBoxLayout;
  body->setSpacing(8);
  body->addWidget(createFluentList());
  body->addWidget(createFluentDetails(), 1);
  root->addLayout(body, 1);

  // help on the left, OK and Cancel on the right
  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  auto* help = new QToolButton(this);
  help->setObjectName(QStringLiteral("instanceHelpButton"));
  help->setText(tr("What are categories for?"));
  help->setIcon(FluentIcons::glyph(0xE897, FluentIcons::Tone::Muted));
  help->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  help->setAutoRaise(true);
  help->setCursor(Qt::PointingHandCursor);
  connect(help, &QToolButton::clicked, this, &CategoriesDialog::showFluentHelp);
  footer->addWidget(help);
  footer->addStretch(1);
  footer->addWidget(ui->buttonBox);
  root->addLayout(footer);

  setMinimumSize(900, 600);
  resize(std::max(width(), 1000), std::max(height(), 700));

  // every change to the data, from the cards, an import or a Nexus refresh,
  // redraws the cards once
  auto* model = ui->categoriesTable->model();
  connect(model, &QAbstractItemModel::dataChanged, this, [this] { queueFluentRefresh(); });
  connect(model, &QAbstractItemModel::rowsInserted, this, [this] { queueFluentRefresh(); });
  connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { queueFluentRefresh(); });
  connect(model, &QAbstractItemModel::modelReset, this, [this] { queueFluentRefresh(); });
  connect(ui->categoriesTable->verticalHeader(), &QHeaderView::sectionMoved, this,
          [this] { queueFluentRefresh(); });
  auto* nexusModel = ui->nexusCategoryList->model();
  connect(nexusModel, &QAbstractItemModel::rowsInserted, this, [this] { queueFluentRefresh(); });
  connect(nexusModel, &QAbstractItemModel::rowsRemoved, this, [this] { queueFluentRefresh(); });
  connect(nexusModel, &QAbstractItemModel::modelReset, this, [this] { queueFluentRefresh(); });

  m_fluent = true;
  refreshFluentTree();
  if (m_fluentTree->topLevelItemCount() > 0) {
    m_fluentTree->setCurrentItem(m_fluentTree->topLevelItem(0));
  }
  refreshFluentDetails();
}

QWidget* CategoriesDialog::createFluentList()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("categoryListCard"));
  card->setProperty("fluentCard", true);
  card->setFixedWidth(320);

  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Categories"), card);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_fluentCount = new QLabel(card);
  m_fluentCount->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_fluentCount);
  layout->addLayout(title);

  m_fluentSearch = new QLineEdit(card);
  m_fluentSearch->setObjectName(QStringLiteral("instanceFilter"));
  m_fluentSearch->setPlaceholderText(tr("Search categories"));
  m_fluentSearch->setClearButtonEnabled(true);
  layout->addWidget(m_fluentSearch);
  connect(m_fluentSearch, &QLineEdit::textChanged, this, [this] { refreshFluentTree(); });

  m_fluentTree = new QTreeWidget(card);
  m_fluentTree->setObjectName(QStringLiteral("categoryTree"));
  m_fluentTree->setHeaderHidden(true);
  m_fluentTree->setColumnCount(1);
  m_fluentTree->setItemDelegate(new FluentPanels::RowDelegate(m_fluentTree, 48, 0));
  m_fluentTree->setFrameShape(QFrame::NoFrame);
  m_fluentTree->setMouseTracking(true);
  m_fluentTree->setIndentation(18);
  m_fluentTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_fluentTree->setContextMenuPolicy(Qt::CustomContextMenu);
  m_fluentTree->setAccessibleName(tr("Categories"));
  layout->addWidget(m_fluentTree, 1);
  connect(m_fluentTree, &QTreeWidget::customContextMenuRequested, this,
          &CategoriesDialog::showFluentListMenu);
  connect(m_fluentTree, &QTreeWidget::currentItemChanged, this, [this] {
    if (!m_fluentSyncing) {
      refreshFluentDetails();
    }
  });

  auto* add = new QPushButton(FluentIcons::glyph(0xE710), tr("New category"), card);
  add->setObjectName(QStringLiteral("categoryAddButton"));
  add->setAutoDefault(false);
  add->setToolTip(tr("Add a category at the top level (Ctrl+N)"));
  connect(add, &QPushButton::clicked, this, [this] { addFluentCategory(0); });
  layout->addWidget(add);

  // the Nexus list the import and the Nexus categories below use
  auto* nexus = new QHBoxLayout;
  nexus->setSpacing(6);
  for (QPushButton* button : {ui->nexusRefresh, ui->nexusImportButton}) {
    button->setParent(card);
    button->setAutoDefault(false);
    nexus->addWidget(button, 1);
    button->show();
  }
  ui->nexusRefresh->setText(tr("Load from Nexus"));
  ui->nexusImportButton->setText(tr("Import..."));
  layout->addLayout(nexus);
  ui->nexusStatus->setParent(card);
  ui->nexusStatus->setObjectName(QStringLiteral("fluentSettingDescription"));
  ui->nexusStatus->setWordWrap(true);
  ui->nexusStatus->setContentsMargins(4, 0, 4, 0);
  layout->addWidget(ui->nexusStatus);
  ui->nexusStatus->show();

  auto* newShortcut = new QShortcut(QKeySequence::New, this);
  connect(newShortcut, &QShortcut::activated, this, [this] { addFluentCategory(0); });
  auto* del = new QShortcut(QKeySequence(Qt::Key_Delete), m_fluentTree);
  del->setContext(Qt::WidgetShortcut);
  connect(del, &QShortcut::activated, this, [this] {
    if (selectedID() > 0) {
      m_ContextRow = rowForID(selectedID());
      removeCategory_clicked();
    }
  });
  auto* rename = new QShortcut(QKeySequence(Qt::Key_F2), this);
  connect(rename, &QShortcut::activated, this, [this] {
    if (selectedID() > 0) {
      m_fluentNameEdit->setFocus();
      m_fluentNameEdit->selectAll();
    }
  });
  auto* find = new QShortcut(QKeySequence::Find, this);
  connect(find, &QShortcut::activated, this, [this] {
    m_fluentSearch->setFocus();
    m_fluentSearch->selectAll();
  });

  return card;
}

QWidget* CategoriesDialog::createFluentDetails()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("categoryDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // the category's name and where it sits
  auto* head = new QHBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(12);
  auto* glyph = new QLabel(QString(QChar(char16_t(0xE8EC))), card);
  glyph->setObjectName(QStringLiteral("instanceRowGlyph"));
  glyph->setFont(FluentIcons::font(24));
  glyph->setFixedSize(32, 32);
  glyph->setAlignment(Qt::AlignCenter);
  head->addWidget(glyph, 0, Qt::AlignVCenter);
  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  m_fluentName = new QLabel(card);
  m_fluentName->setObjectName(QStringLiteral("instanceDetailName"));
  m_fluentCaption = new QLabel(card);
  m_fluentCaption->setObjectName(QStringLiteral("instanceDetailCaption"));
  names->addWidget(m_fluentName);
  names->addWidget(m_fluentCaption);
  head->addLayout(names, 1);
  layout->addLayout(head);

  m_fluentScroll = new QScrollArea(card);
  m_fluentScroll->setObjectName(QStringLiteral("instanceDetailsScroll"));
  m_fluentScroll->setFrameShape(QFrame::NoFrame);
  m_fluentScroll->setWidgetResizable(true);
  m_fluentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_fluentDetailRows = new QWidget(m_fluentScroll);
  m_fluentDetailRows->setObjectName(QStringLiteral("instanceDetailRows"));
  auto* rows = new QVBoxLayout(m_fluentDetailRows);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(0);
  m_fluentScroll->setWidget(m_fluentDetailRows);
  layout->addWidget(m_fluentScroll, 1);

  m_fluentEmpty = new QLabel(tr("Choose a category in the list, or add one."), card);
  m_fluentEmpty->setObjectName(QStringLiteral("instanceEmptyText"));
  m_fluentEmpty->setAlignment(Qt::AlignCenter);
  m_fluentEmpty->setWordWrap(true);
  layout->addWidget(m_fluentEmpty, 1);

  m_fluentNameEdit = new QLineEdit(m_fluentDetailRows);
  m_fluentNameEdit->setObjectName(QStringLiteral("categoryNameEdit"));
  m_fluentNameEdit->setAccessibleName(tr("Name"));
  FluentPanels::addEditRow(m_fluentDetailRows, 0xE8AC, tr("Name"),
                           tr("Shown in the mod list, its filters and the mod's menu"),
                           {m_fluentNameEdit});
  connect(m_fluentNameEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
    const int row = rowForID(selectedID());
    if (row >= 0 && ui->categoriesTable->item(row, 1) != nullptr) {
      ui->categoriesTable->item(row, 1)->setText(text);
    }
  });

  m_fluentParent = new QComboBox(m_fluentDetailRows);
  m_fluentParent->setObjectName(QStringLiteral("categoryParentBox"));
  m_fluentParent->setAccessibleName(tr("Inside"));
  m_fluentParent->setMaxVisibleItems(14);
  FluentPanels::addEditRow(m_fluentDetailRows, 0xE8B7, tr("Inside"),
                           tr("A subcategory is listed under the category it belongs to"),
                           {m_fluentParent});
  connect(m_fluentParent, &QComboBox::activated, this, [this](int index) {
    const int row = rowForID(selectedID());
    if (row >= 0 && ui->categoriesTable->item(row, 2) != nullptr) {
      ui->categoriesTable->item(row, 2)->setData(Qt::DisplayRole,
                                                 m_fluentParent->itemData(index).toInt());
    }
  });

  auto idRow = FluentPanels::addDetailRow(m_fluentDetailRows, 0xE8C8, tr("ID"),
                                          tr("Copy the ID"), 0xE8C8);
  m_fluentID = idRow.value;
  connect(idRow.action, &QToolButton::clicked, this, [this] {
    QGuiApplication::clipboard()->setText(QString::number(selectedID()));
  });
  auto modsRow = FluentPanels::addDetailRow(m_fluentDetailRows, 0xE74C, tr("Mods"));
  m_fluentMods = modsRow.value;

  // the Nexus categories that turn into this one when a mod is installed
  auto* nexusHeading = new QLabel(tr("Nexus categories"), m_fluentDetailRows);
  nexusHeading->setObjectName(QStringLiteral("fluentCardTitle"));
  nexusHeading->setContentsMargins(18, 16, 18, 8);
  rows->addWidget(nexusHeading);
  m_fluentMappings = new QWidget(m_fluentDetailRows);
  auto* mappingRows = new QVBoxLayout(m_fluentMappings);
  mappingRows->setContentsMargins(0, 0, 0, 0);
  mappingRows->setSpacing(0);
  rows->addWidget(m_fluentMappings);

  m_fluentNexusPicker = new QComboBox(m_fluentDetailRows);
  m_fluentNexusPicker->setObjectName(QStringLiteral("categoryNexusPicker"));
  m_fluentNexusPicker->setAccessibleName(tr("Add a Nexus category"));
  m_fluentNexusPicker->setMaxVisibleItems(16);
  FluentPanels::addEditRow(
      m_fluentDetailRows, 0xE710, tr("Add a Nexus category"),
      tr("Mods from Nexus in that category get this one when they are installed"),
      {m_fluentNexusPicker});
  connect(m_fluentNexusPicker, &QComboBox::activated, this, [this](int index) {
    const int nexusID = m_fluentNexusPicker->itemData(index).toInt();
    const int row     = rowForID(selectedID());
    if (nexusID > 0 && row >= 0) {
      mapNexusCategory(row,
                       m_fluentNexusPicker->itemData(index, Qt::UserRole + 1).toString(),
                       nexusID);
    }
  });
  rows->addStretch(1);

  // a subcategory on the left, remove on the right
  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  m_fluentAddSub =
      new QPushButton(FluentIcons::glyph(0xE710), tr("New subcategory"), actions);
  m_fluentAddSub->setAutoDefault(false);
  m_fluentRemove = new QPushButton(FluentIcons::glyph(0xE74D, FluentIcons::Tone::Critical),
                                   tr("Remove"), actions);
  m_fluentRemove->setAutoDefault(false);
  m_fluentRemove->setToolTip(
      tr("Remove the category; its subcategories move up a level (Delete)"));
  actionsLayout->addWidget(m_fluentAddSub);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_fluentRemove);
  layout->addWidget(actions);

  connect(m_fluentAddSub, &QPushButton::clicked, this, [this] {
    addFluentCategory(selectedID());
  });
  connect(m_fluentRemove, &QPushButton::clicked, this, [this] {
    m_ContextRow = rowForID(selectedID());
    removeCategory_clicked();
  });

  return card;
}

void CategoriesDialog::queueFluentRefresh()
{
  if (!m_fluent || m_fluentRefreshQueued) {
    return;
  }
  m_fluentRefreshQueued = true;
  QTimer::singleShot(0, this, [this] {
    m_fluentRefreshQueued = false;
    refreshFluentTree();
    refreshFluentDetails();
  });
}

int CategoriesDialog::rowForID(int id) const
{
  if (id <= 0) {
    return -1;
  }
  for (int row = 0; row < ui->categoriesTable->rowCount(); ++row) {
    const auto* item = ui->categoriesTable->item(row, 0);
    if (item != nullptr && item->text().toInt() == id) {
      return row;
    }
  }
  return -1;
}

int CategoriesDialog::selectedID() const
{
  if (m_fluentTree == nullptr || m_fluentTree->currentItem() == nullptr) {
    return 0;
  }
  return m_fluentTree->currentItem()->data(0, CategoryIDRole).toInt();
}

void CategoriesDialog::selectID(int id)
{
  QTreeWidgetItemIterator it(m_fluentTree);
  while (*it) {
    if ((*it)->data(0, CategoryIDRole).toInt() == id) {
      m_fluentTree->setCurrentItem(*it);
      m_fluentTree->scrollToItem(*it);
      return;
    }
    ++it;
  }
}

void CategoriesDialog::refreshFluentTree()
{
  if (!m_fluent) {
    return;
  }

  QTableWidget* table = ui->categoriesTable;
  struct Entry
  {
    int id;
    int parent;
    QString name;
    QString caption;
  };

  // the rows in the order they are saved in
  std::vector<Entry> entries;
  QHash<int, int> parents;
  for (int visual = 0; visual < table->rowCount(); ++visual) {
    const int row      = table->verticalHeader()->logicalIndex(visual);
    const auto* idItem = table->item(row, 0);
    if (idItem == nullptr || idItem->text().toInt() <= 0) {
      continue;
    }
    Entry entry;
    entry.id     = idItem->text().toInt();
    entry.parent = table->item(row, 2) ? table->item(row, 2)->text().toInt() : 0;
    entry.name   = table->item(row, 1) ? table->item(row, 1)->text() : QString();

    QStringList parts;
    const int mods = m_modCounts.value(entry.id);
    parts << (mods == 1 ? tr("1 mod") : tr("%1 mods").arg(mods));
    const int mapped = validMappings(table->item(row, 3)).size();
    if (mapped > 0) {
      parts << (mapped == 1 ? tr("1 Nexus category") : tr("%1 Nexus categories").arg(mapped));
    }
    entry.caption = parts.join(QStringLiteral(" · "));
    parents.insert(entry.id, entry.parent);
    entries.push_back(entry);
  }

  // a parent that is missing or loops back makes the category a top one
  const auto validParent = [&parents](int id) {
    const int parent = parents.value(id);
    QSet<int> seen{id};
    for (int walk = parent; walk > 0; walk = parents.value(walk)) {
      if (!parents.contains(walk) || seen.contains(walk)) {
        return 0;
      }
      seen.insert(walk);
    }
    return parent;
  };

  const int current    = selectedID();
  const QString search = m_fluentSearch->text().trimmed();
  m_fluentSyncing      = true;
  m_fluentTree->clear();
  QHash<int, QTreeWidgetItem*> items;
  for (const Entry& entry : entries) {
    auto* item = new QTreeWidgetItem;
    item->setText(0, entry.name.isEmpty() ? tr("(no name)") : entry.name);
    item->setData(0, FluentPanels::CaptionRole, entry.caption);
    item->setData(0, CategoryIDRole, entry.id);
    item->setToolTip(0, entry.name);
    items.insert(entry.id, item);
  }
  for (const Entry& entry : entries) {
    const int parent = validParent(entry.id);
    if (parent > 0 && items.contains(parent)) {
      items[parent]->addChild(items[entry.id]);
    } else {
      m_fluentTree->addTopLevelItem(items[entry.id]);
    }
  }

  // a search shows the matches and the categories they are inside
  if (!search.isEmpty()) {
    std::function<bool(QTreeWidgetItem*)> filter = [&](QTreeWidgetItem* item) {
      bool any = item->text(0).contains(search, Qt::CaseInsensitive);
      for (int i = 0; i < item->childCount(); ++i) {
        any = filter(item->child(i)) || any;
      }
      item->setHidden(!any);
      return any;
    };
    for (int i = 0; i < m_fluentTree->topLevelItemCount(); ++i) {
      filter(m_fluentTree->topLevelItem(i));
    }
  }
  m_fluentTree->expandAll();
  if (current > 0 && items.contains(current)) {
    m_fluentTree->setCurrentItem(items[current]);
  }
  m_fluentSyncing = false;

  m_fluentCount->setText(QString::number(entries.size()));
}

void CategoriesDialog::refreshFluentDetails()
{
  if (!m_fluent) {
    return;
  }

  QTableWidget* table = ui->categoriesTable;
  const int id        = selectedID();
  const int row       = rowForID(id);
  const bool has      = row >= 0;
  m_fluentScroll->setVisible(has);
  m_fluentEmpty->setVisible(!has);
  m_fluentAddSub->setEnabled(has);
  m_fluentRemove->setEnabled(has);
  if (!has) {
    m_fluentName->setText(tr("No category selected"));
    m_fluentCaption->setText(tr("Add one, or pick one in the list"));
    return;
  }

  const QString name  = table->item(row, 1) ? table->item(row, 1)->text() : QString();
  const int parent    = table->item(row, 2) ? table->item(row, 2)->text().toInt() : 0;
  const int parentRow = rowForID(parent);
  const QString parentName = parentRow >= 0 && table->item(parentRow, 1)
                                 ? table->item(parentRow, 1)->text()
                                 : QString();

  m_fluentName->setText(name.isEmpty() ? tr("(no name)") : name);
  m_fluentCaption->setText(parentName.isEmpty() ? tr("Top-level category")
                                                : tr("Inside %1").arg(parentName));
  if (m_fluentNameEdit->text() != name) {
    m_fluentNameEdit->setText(name);
  }
  m_fluentID->setFullText(QString::number(id));
  const int mods = m_modCounts.value(id);
  m_fluentMods->setFullText(mods == 0 ? tr("No mod uses this category")
                       : (mods == 1 ? tr("1 mod uses this category")
                                    : tr("%1 mods use this category").arg(mods)));

  // the parent can be any category but this one and the ones inside it
  QSet<int> excluded{id};
  for (bool grew = true; grew;) {
    grew = false;
    for (int r = 0; r < table->rowCount(); ++r) {
      const int childID  = table->item(r, 0) ? table->item(r, 0)->text().toInt() : 0;
      const int childPar = table->item(r, 2) ? table->item(r, 2)->text().toInt() : 0;
      if (childID > 0 && excluded.contains(childPar) && !excluded.contains(childID)) {
        excluded.insert(childID);
        grew = true;
      }
    }
  }
  QList<std::pair<QString, int>> choices;
  for (int r = 0; r < table->rowCount(); ++r) {
    const int choiceID = table->item(r, 0) ? table->item(r, 0)->text().toInt() : 0;
    if (choiceID > 0 && !excluded.contains(choiceID)) {
      choices.append({table->item(r, 1) ? table->item(r, 1)->text() : QString(), choiceID});
    }
  }
  std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) {
    return a.first.localeAwareCompare(b.first) < 0;
  });
  m_fluentParent->blockSignals(true);
  m_fluentParent->clear();
  m_fluentParent->addItem(tr("Nothing (top level)"), 0);
  for (const auto& [choiceName, choiceID] : choices) {
    m_fluentParent->addItem(choiceName, choiceID);
  }
  m_fluentParent->setCurrentIndex(std::max(0, m_fluentParent->findData(parent)));
  m_fluentParent->blockSignals(false);

  // one row per Nexus category, with a button that takes it off
  QLayout* mappingRows = m_fluentMappings->layout();
  while (QLayoutItem* item = mappingRows->takeAt(0)) {
    if (item->widget() != nullptr) {
      // out of the layout it would still paint where it was until deleted
      item->widget()->hide();
      item->widget()->deleteLater();
    }
    delete item;
  }
  const bool mappingsOn       = Settings::instance().nexus().categoryMappings();
  const QVariantList mappings = validMappings(table->item(row, 3));
  QSet<int> mapped;
  for (const QVariant& entry : mappings) {
    const QVariantList mapping = entry.toList();
    const int nexusID          = mapping.at(1).toInt();
    mapped.insert(nexusID);
    auto detail = FluentPanels::addDetailRow(m_fluentMappings, 0xE774,
                                             mapping.at(0).toString().trimmed(),
                                             tr("Take this Nexus category off"), 0xE711);
    detail.value->setFullText(tr("Nexus ID %1").arg(nexusID));
    detail.action->setEnabled(mappingsOn);
    connect(detail.action, &QToolButton::clicked, this, [this, nexusID] {
      const int r = rowForID(selectedID());
      if (r < 0) {
        return;
      }
      QVariantList kept;
      for (const QVariant& e : validMappings(ui->categoriesTable->item(r, 3))) {
        if (e.toList().at(1).toInt() != nexusID) {
          kept.append(e);
        }
      }
      setRowMappings(r, kept);
    });
  }
  if (mappings.isEmpty()) {
    auto detail = FluentPanels::addDetailRow(m_fluentMappings, 0xE774, tr("None"));
    detail.value->setFullText(
        mappingsOn ? tr("Mods from Nexus do not get this category by themselves")
                   : tr("Nexus category mapping is disabled in Settings."));
  }

  // the picker names the category a Nexus one belongs to now
  QHash<int, QString> owners;
  for (int r = 0; r < table->rowCount(); ++r) {
    for (const QVariant& entry : validMappings(table->item(r, 3))) {
      owners.insert(entry.toList().at(1).toInt(),
                    table->item(r, 1) ? table->item(r, 1)->text() : QString());
    }
  }
  const int available = ui->nexusCategoryList->count();
  QList<std::pair<QString, int>> nexus;
  for (int i = 0; i < available; ++i) {
    const auto* item  = ui->nexusCategoryList->item(i);
    const int nexusID = item->data(Qt::UserRole).toInt();
    if (nexusID > 0 && !mapped.contains(nexusID)) {
      nexus.append({item->data(Qt::DisplayRole).toString().trimmed(), nexusID});
    }
  }
  std::sort(nexus.begin(), nexus.end(), [](const auto& a, const auto& b) {
    return a.first.localeAwareCompare(b.first) < 0;
  });
  m_fluentNexusPicker->blockSignals(true);
  m_fluentNexusPicker->clear();
  m_fluentNexusPicker->addItem(available == 0 ? tr("Load the list from Nexus first")
                                              : tr("Choose a Nexus category"),
                               0);
  for (const auto& [nexusName, nexusID] : nexus) {
    const QString owner = owners.value(nexusID);
    m_fluentNexusPicker->addItem(
        owner.isEmpty() ? nexusName : tr("%1 (now in %2)").arg(nexusName, owner), nexusID);
    m_fluentNexusPicker->setItemData(m_fluentNexusPicker->count() - 1, nexusName,
                                     Qt::UserRole + 1);
  }
  m_fluentNexusPicker->setCurrentIndex(0);
  m_fluentNexusPicker->setEnabled(mappingsOn && available > 0);
  m_fluentNexusPicker->blockSignals(false);
}

void CategoriesDialog::setRowMappings(int row, const QVariantList& mappings)
{
  QTableWidgetItem* item = ui->categoriesTable->item(row, 3);
  if (item == nullptr) {
    item = new QTableWidgetItem();
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    ui->categoriesTable->setItem(row, 3, item);
  }
  QStringList names;
  for (const QVariant& entry : mappings) {
    names.append(entry.toList().at(0).toString());
  }
  item->setData(Qt::UserRole, mappings);
  item->setData(Qt::DisplayRole, names.join(", "));
}

void CategoriesDialog::mapNexusCategory(int row, const QString& name, int nexusID)
{
  // a Nexus category belongs to one category, as when it is dragged onto a row
  for (int r = 0; r < ui->categoriesTable->rowCount(); ++r) {
    if (r == row) {
      continue;
    }
    QVariantList kept;
    bool changed = false;
    for (const QVariant& entry : validMappings(ui->categoriesTable->item(r, 3))) {
      if (entry.toList().at(1).toInt() == nexusID) {
        changed = true;
      } else {
        kept.append(entry);
      }
    }
    if (changed) {
      setRowMappings(r, kept);
    }
  }

  QVariantList mappings = validMappings(ui->categoriesTable->item(row, 3));
  QVariantList mapping;
  mapping.append(name);
  mapping.append(nexusID);
  mappings.append(QVariant::fromValue(mapping));
  setRowMappings(row, mappings);
}

void CategoriesDialog::addFluentCategory(int parentID)
{
  // new categories go at the end, so the order of the others stays
  m_ContextRow = ui->categoriesTable->rowCount();
  addCategory_clicked();
  const int row = ui->categoriesTable->rowCount() - 1;
  const int id  = ui->categoriesTable->item(row, 0)->text().toInt();
  ui->categoriesTable->item(row, 1)->setText(tr("New category"));
  if (parentID > 0) {
    ui->categoriesTable->item(row, 2)->setData(Qt::DisplayRole, parentID);
  }
  m_fluentSearch->clear();
  refreshFluentTree();
  selectID(id);
  refreshFluentDetails();
  m_fluentNameEdit->setFocus();
  m_fluentNameEdit->selectAll();
}

void CategoriesDialog::moveCategory(int delta)
{
  // swaps the category with the one above or below it inside the same parent
  QTreeWidgetItem* item = m_fluentTree->currentItem();
  if (item == nullptr) {
    return;
  }
  QTreeWidgetItem* parent = item->parent();
  const int index = parent ? parent->indexOfChild(item) : m_fluentTree->indexOfTopLevelItem(item);
  const int count = parent ? parent->childCount() : m_fluentTree->topLevelItemCount();
  const int other = index + delta;
  if (other < 0 || other >= count) {
    return;
  }
  QTreeWidgetItem* neighbour =
      parent ? parent->child(other) : m_fluentTree->topLevelItem(other);
  const int id        = item->data(0, CategoryIDRole).toInt();
  QHeaderView* header = ui->categoriesTable->verticalHeader();
  const int from      = header->visualIndex(rowForID(id));
  const int to = header->visualIndex(rowForID(neighbour->data(0, CategoryIDRole).toInt()));
  header->moveSection(from, to);
  refreshFluentTree();
  selectID(id);
}

void CategoriesDialog::sortCategories()
{
  QTableWidget* table = ui->categoriesTable;
  QHeaderView* header = table->verticalHeader();
  std::vector<int> rows;
  for (int row = 0; row < table->rowCount(); ++row) {
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end(), [table](int a, int b) {
    const QString left  = table->item(a, 1) ? table->item(a, 1)->text() : QString();
    const QString right = table->item(b, 1) ? table->item(b, 1)->text() : QString();
    return left.localeAwareCompare(right) < 0;
  });
  for (int visual = 0; visual < static_cast<int>(rows.size()); ++visual) {
    header->moveSection(header->visualIndex(rows[visual]), visual);
  }
  const int id = selectedID();
  refreshFluentTree();
  selectID(id);
}

void CategoriesDialog::showFluentListMenu(const QPoint& pos)
{
  QTreeWidgetItem* item = m_fluentTree->itemAt(pos);
  QMenu menu(this);

  menu.addAction(FluentIcons::glyph(0xE710),
                 tr("New category") + QLatin1Char('\t') +
                     QKeySequence(QKeySequence::New).toString(QKeySequence::NativeText),
                 this, [this] {
                   addFluentCategory(0);
                 });
  if (item == nullptr) {
    menu.addSeparator();
    menu.addAction(FluentIcons::glyph(0xE8CB), tr("Sort A to Z"), this,
                   &CategoriesDialog::sortCategories);
    menu.exec(m_fluentTree->viewport()->mapToGlobal(pos));
    return;
  }

  m_fluentTree->setCurrentItem(item);
  const int id  = selectedID();
  const int row = rowForID(id);
  if (row < 0) {
    return;
  }
  menu.addAction(FluentIcons::glyph(0xE710), tr("New subcategory"), this, [this, id] {
    addFluentCategory(id);
  });
  menu.addAction(FluentIcons::glyph(0xE8AC),
                 tr("Rename") + QLatin1Char('\t') +
                     QKeySequence(Qt::Key_F2).toString(QKeySequence::NativeText),
                 this, [this] {
                   m_fluentNameEdit->setFocus();
                   m_fluentNameEdit->selectAll();
                 });
  menu.addSeparator();

  QTreeWidgetItem* parent = item->parent();
  const int index = parent ? parent->indexOfChild(item) : m_fluentTree->indexOfTopLevelItem(item);
  const int count = parent ? parent->childCount() : m_fluentTree->topLevelItemCount();
  menu.addAction(FluentIcons::glyph(0xE74A), tr("Move up"), this, [this] {
        moveCategory(-1);
      })->setEnabled(index > 0);
  menu.addAction(FluentIcons::glyph(0xE74B), tr("Move down"), this, [this] {
        moveCategory(1);
      })->setEnabled(index + 1 < count);
  menu.addAction(FluentIcons::glyph(0xE8CB), tr("Sort A to Z"), this,
                 &CategoriesDialog::sortCategories);
  menu.addSeparator();

  const QString name =
      ui->categoriesTable->item(row, 1) ? ui->categoriesTable->item(row, 1)->text() : QString();
  addCopyMenu(&menu, {{tr("Name"), name}, {tr("ID"), QString::number(id)}});
  if (Settings::instance().nexus().categoryMappings()) {
    auto* unmap = menu.addAction(FluentIcons::icon(":/MO/gui/contextmenu/remap-category.svg"),
                                 tr("Remove Nexus mappings"), this, [this, row] {
                                   setRowMappings(row, {});
                                 });
    unmap->setEnabled(!validMappings(ui->categoriesTable->item(row, 3)).isEmpty());
  }
  menu.addSeparator();
  // the key shows at the menu's right edge; the list's shortcut handles it
  menu.addAction(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
                 tr("Remove") + QLatin1Char('\t') +
                     QKeySequence(Qt::Key_Delete).toString(QKeySequence::NativeText),
                 this, [this, row] {
                   m_ContextRow = row;
                   removeCategory_clicked();
                 });

  menu.exec(m_fluentTree->viewport()->mapToGlobal(pos));
}

void CategoriesDialog::showFluentHelp()
{
  FluentContentDialog dlg(
      this, QStringLiteral("CategoryHelpDialog"), tr("What are categories for?"),
      tr("Categories sort your mods: the mod list can group and filter by them, and a "
         "mod can have several. A subcategory is listed inside another one.\n\nNexus "
         "categories link a category to the ones mods have on Nexus Mods, so a mod "
         "installed from Nexus gets the matching category by itself. Load the list from "
         "Nexus to pick them, or import it to create the categories Nexus uses.\n\n"
         "Changes are saved when you choose OK."));
  auto* ok = dlg.addButton(tr("OK"), true);
  connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  dlg.resize(540, dlg.sizeHint().height());
  dlg.exec();
}
