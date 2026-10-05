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

#include "pluginlistsortproxy.h"
#include "messagedialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QMenu>
#include <QTreeView>
#include <QWidgetAction>

PluginListSortProxy::PluginListSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent), m_SortIndex(0), m_SortOrder(Qt::AscendingOrder)
{
  m_EnabledColumns.set(PluginList::COL_NAME);
  m_EnabledColumns.set(PluginList::COL_PRIORITY);
  m_EnabledColumns.set(PluginList::COL_MODINDEX);
  this->setDynamicSortFilter(true);
}

void PluginListSortProxy::setEnabledColumns(unsigned int columns)
{
  emit layoutAboutToBeChanged();
  for (int i = 0; i <= PluginList::COL_LASTCOLUMN; ++i) {
    m_EnabledColumns.set(i, (columns & (1 << i)) != 0);
  }
  emit layoutChanged();
}

void PluginListSortProxy::updateFilter(const QString& filter)
{
  if (m_CurrentFilter != filter) {
    m_CurrentFilter = filter;
    m_FilterSegments.clear();

    if (!m_CurrentFilter.isEmpty()) {
      QString filterCopy = m_CurrentFilter;
      filterCopy.replace("||", ";").replace("OR", ";").replace("|", ";");

      const auto orSegments = filterCopy.split(";", Qt::SkipEmptyParts);
      m_FilterSegments.reserve(static_cast<std::size_t>(orSegments.size()));
      for (const auto& segment : orSegments) {
        m_FilterSegments.push_back(segment.split(" ", Qt::SkipEmptyParts));
      }
    }
  }

  invalidateFilter();
}

bool PluginListSortProxy::filterAcceptsRow(int row, const QModelIndex&) const
{
  if (m_CurrentFilter.isEmpty()) {
    return true;
  }

  auto* plugins = qobject_cast<PluginList*>(sourceModel());
  return plugins != nullptr && filterMatchesPlugin(plugins->getName(row));
}

bool PluginListSortProxy::lessThan(const QModelIndex& left,
                                   const QModelIndex& right) const
{
  PluginList* plugins = qobject_cast<PluginList*>(sourceModel());
  switch (left.column()) {
  case PluginList::COL_NAME: {
    return QString::compare(plugins->getName(left.row()), plugins->getName(right.row()),
                            Qt::CaseInsensitive) < 0;
  } break;
  case PluginList::COL_FLAGS: {
    QVariantList lhsList = left.data(Qt::UserRole + 1).toList();
    QVariantList rhsList = right.data(Qt::UserRole + 1).toList();
    if (lhsList.size() != rhsList.size()) {
      return lhsList.size() < rhsList.size();
    } else {
      for (int i = 0; i < lhsList.size(); ++i) {
        if (lhsList.at(i) != rhsList.at(i)) {
          return lhsList.at(i).toString() < rhsList.at(i).toString();
        }
      }
      return false;
    }
  } break;
  case PluginList::COL_MODINDEX: {
    QString leftVal  = plugins->getIndexPriority(left.row());
    QString rightVal = plugins->getIndexPriority(right.row());
    return leftVal < rightVal;
  } break;
  default: {
    return plugins->getPriority(left.row()) < plugins->getPriority(right.row());
  } break;
  }
}

bool PluginListSortProxy::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                       int row, int column, const QModelIndex& parent)
{
  if ((sortColumn() != PluginList::COL_PRIORITY) &&
      (sortColumn() != PluginList::COL_MODINDEX)) {
    QWidget* wid = qApp->activeWindow()->findChild<QTreeView*>("espList");
    MessageDialog::showMessage(
        tr("Drag&Drop is only supported when sorting by priority or mod index"), wid);
    return false;
  }

  if ((row == -1) && (column == -1)) {
    return this->sourceModel()->dropMimeData(data, action, -1, -1, mapToSource(parent));
  }
  // in the regular model, when dropping between rows, the row-value passed to
  // the sourceModel is inconsistent between ascending and descending ordering.
  // This should fix that
  if (sortOrder() == Qt::DescendingOrder) {
    --row;
  }

  QModelIndex proxyIndex  = index(row, column, parent);
  QModelIndex sourceIndex = mapToSource(proxyIndex);
  return this->sourceModel()->dropMimeData(data, action, sourceIndex.row(),
                                           sourceIndex.column(), sourceIndex.parent());
}

bool PluginListSortProxy::filterMatchesPlugin(const QString& plugin) const
{
  if (!m_CurrentFilter.isEmpty()) {
    // split in ORSegments that internally use AND logic
    for (const auto& ANDKeywords : m_FilterSegments) {
      bool segmentGood = true;

      // check each word in the segment for match, each word needs to be matched but it
      // doesn't matter where.
      for (const auto& currentKeyword : ANDKeywords) {
        if (!plugin.contains(currentKeyword, Qt::CaseInsensitive)) {
          segmentGood = false;
          break;
        }
      }

      if (segmentGood) {
        // the last AND loop didn't break so the ORSegments is true so mod matches
        // filter
        return true;
      }

    }  // for ORSegments loop

    return false;
  } else
    return true;
}
