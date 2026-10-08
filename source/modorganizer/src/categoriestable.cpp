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

#include "categoriestable.h"

#include <QDropEvent>
#include <QListWidget>
#include <QSet>

CategoriesTable::CategoriesTable(QWidget* parent) : QTableWidget(parent) {}

void CategoriesTable::dropEvent(QDropEvent* event)
{
  QWidget* sourceWidget = qobject_cast<QWidget*>(event->source());
  while (sourceWidget && !qobject_cast<QListWidget*>(sourceWidget)) {
    sourceWidget = sourceWidget->parentWidget();
  }
  auto* source = qobject_cast<QListWidget*>(sourceWidget);
  if (!source || source->objectName() != QStringLiteral("nexusCategoryList")) {
    event->ignore();
    return;
  }

  const int row = rowAt(event->position().toPoint().y());
  if (row < 0 || row >= rowCount()) {
    event->ignore();
    return;
  }

  QVariantList draggedMappings;
  QSet<int> draggedIDs;
  for (const QListWidgetItem* draggedItem : source->selectedItems()) {
    if (!draggedItem) {
      continue;
    }

    const QString name = draggedItem->data(Qt::DisplayRole).toString().trimmed();
    bool idValid       = false;
    const int nexusID  = draggedItem->data(Qt::UserRole).toInt(&idValid);
    if (!idValid || nexusID <= 0 || name.isEmpty() || draggedIDs.contains(nexusID)) {
      continue;
    }

    QVariantList mapping;
    mapping.append(name);
    mapping.append(nexusID);
    draggedMappings.append(QVariant::fromValue(mapping));
    draggedIDs.insert(nexusID);
  }

  if (draggedMappings.isEmpty()) {
    event->ignore();
    return;
  }

  const bool wasSorting = isSortingEnabled();
  setSortingEnabled(false);

  for (int tableRow = 0; tableRow < rowCount(); ++tableRow) {
    auto* mappingItem = item(tableRow, 3);
    if (!mappingItem) {
      continue;
    }

    QVariantList keptMappings;
    QStringList names;
    for (const QVariant& entry : mappingItem->data(Qt::UserRole).toList()) {
      const QVariantList mapping = entry.toList();
      if (mapping.size() < 2) {
        continue;
      }

      bool idValid         = false;
      const int existingID = mapping.at(1).toInt(&idValid);
      const QString name   = mapping.at(0).toString().trimmed();
      if (!idValid || existingID <= 0 || name.isEmpty() ||
          draggedIDs.contains(existingID)) {
        continue;
      }

      keptMappings.append(QVariant::fromValue(mapping));
      names.append(name);
    }

    mappingItem->setData(Qt::UserRole, keptMappings);
    mappingItem->setData(Qt::DisplayRole, names.join(", "));
  }

  auto* targetItem = item(row, 3);
  if (!targetItem) {
    targetItem = new QTableWidgetItem();
    setItem(row, 3, targetItem);
  }

  QVariantList mappings = targetItem->data(Qt::UserRole).toList();
  QSet<int> targetIDs;
  QStringList names;
  for (const QVariant& entry : mappings) {
    const QVariantList mapping = entry.toList();
    if (mapping.size() < 2) {
      continue;
    }
    bool idValid         = false;
    const int nexusID    = mapping.at(1).toInt(&idValid);
    const QString name   = mapping.at(0).toString().trimmed();
    if (!idValid || nexusID <= 0 || name.isEmpty() || targetIDs.contains(nexusID)) {
      continue;
    }
    targetIDs.insert(nexusID);
    names.append(name);
  }

  for (const QVariant& entry : draggedMappings) {
    const QVariantList mapping = entry.toList();
    const int nexusID          = mapping.at(1).toInt();
    if (targetIDs.contains(nexusID)) {
      continue;
    }
    targetIDs.insert(nexusID);
    mappings.append(entry);
    names.append(mapping.at(0).toString());
  }

  targetItem->setData(Qt::UserRole, mappings);
  targetItem->setData(Qt::DisplayRole, names.join(", "));
  setSortingEnabled(wasSorting);

  if (event->possibleActions() & Qt::CopyAction) {
    event->setDropAction(Qt::CopyAction);
  }
  event->accept();
}
