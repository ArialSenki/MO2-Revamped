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

#ifndef DOWNLOADLIST_H
#define DOWNLOADLIST_H

#include <QAbstractItemModel>
#include <vector>

class OrganizerCore;
class DownloadManager;
class DownloadGroups;
class Settings;

/**
 * @brief model of the list of active and completed downloads
 **/
class DownloadList : public QAbstractItemModel
{

  Q_OBJECT

public:
  enum EColumn
  {
    COL_NAME = 0,
    COL_STATUS,
    COL_SIZE,
    COL_FILETIME,
    COL_MODNAME,
    COL_VERSION,
    COL_ID,
    COL_SOURCEGAME,

    // number of columns
    COL_COUNT
  };

  // MO2 Revamped: the list can be grouped by where the downloads came from
  // and into the user's own categories; a group is a row with the downloads
  // as its children
  enum GroupKind
  {
    GROUP_CATEGORY = 0,
    GROUP_LIST,
    GROUP_NEXUS,
    GROUP_OTHER
  };

  enum Roles
  {
    // the download's index in the download manager, -1 on a group row
    DownloadRowRole = Qt::UserRole + 40,
    // a group row's key ("cat:<name>", "list:<name>", "nexus", "other")
    GroupKeyRole,
    // a group row's GroupKind
    GroupKindRole,
    // a group row's name, without its count
    GroupNameRole,
    // the size of a group's downloads, as text
    GroupSizeRole
  };

public:
  explicit DownloadList(OrganizerCore& core, QObject* parent = 0);

  DownloadGroups& groups() { return *m_groups; }

  // the download behind an index of this model, -1 for a group row
  int downloadRow(const QModelIndex& index) const;
  static bool isGroup(const QModelIndex& index);

  // the downloads' grouping changed (categories, a move, grouping on or off)
  void regroup();

  /**
   * @brief retrieve the number of rows to display. Invoked by Qt
   *
   * @param parent not relevant for this implementation
   * @return number of rows to display
   **/
  virtual int rowCount(const QModelIndex& parent = QModelIndex()) const;
  virtual int columnCount(const QModelIndex& parent) const;

  QModelIndex index(int row, int column, const QModelIndex& parent) const;
  QModelIndex parent(const QModelIndex& child) const;
  Qt::ItemFlags flags(const QModelIndex& idx) const override;
  QMimeData* mimeData(const QModelIndexList& indexes) const override;
  QStringList mimeTypes() const override;

  virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;

  /**
   * @brief retrieve the data to display in a specific row. Invoked by Qt
   *
   * @param index location to look up
   * @param role ... Defaults to Qt::DisplayRole.
   * @return this implementation only returns the row, the QItemDelegate implementation
   *is expected to fetch its information from the DownloadManager
   **/
  virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

  // used in DownloadsTab as the sorting predicate for the filter widget
  //
  // groups keep their own order whichever way the list is sorted
  bool lessThanPredicate(const QModelIndex& left, const QModelIndex& right,
                         Qt::SortOrder order);

public slots:

  /**
   * @brief used to inform the model that data has changed
   *
   * @param row the row that changed. This can be negative to update the whole view
   **/
  void update(int row);

  void aboutToUpdate();

private:
  struct Group
  {
    QString key;
    QString name;
    GroupKind kind;
    std::vector<int> rows;
    qint64 size = 0;
  };

  DownloadManager& m_manager;
  Settings& m_settings;
  DownloadGroups* m_groups;
  bool m_grouped = false;
  std::vector<Group> m_groupList;
  // for each download (pending ones after the others): its group and its
  // place in it
  std::vector<int> m_groupOf;
  std::vector<int> m_placeOf;

  void buildGroups();
  QVariant groupData(const Group& group, const QModelIndex& index, int role) const;
  // MO2 Revamped: a reset aboutToUpdate() began and update(-1) has not ended
  bool m_resetting = false;
};

#endif  // DOWNLOADLIST_H
