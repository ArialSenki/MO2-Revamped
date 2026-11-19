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

#ifndef DOWNLOADLISTWIDGET_H
#define DOWNLOADLISTWIDGET_H

#include "downloadlist.h"
#include "downloadmanager.h"
#include <QHeaderView>
#include <QItemDelegate>
#include <QItemSelection>
#include <QLabel>
#include <QProgressBar>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QWidget>

namespace Ui
{
class DownloadListView;
}

class DownloadListView;

class DownloadProgressDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  DownloadProgressDelegate(DownloadManager* manager, DownloadListView* list);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;

private:
  DownloadManager* m_Manager;
  DownloadListView* m_List;

  // a group's header: arrow, icon, name, count and the size of its downloads
  void paintGroup(QPainter* painter, const QStyleOptionViewItem& option,
                  const QModelIndex& index) const;
};

class DownloadListHeader : public QHeaderView
{
  Q_OBJECT

public:
  explicit DownloadListHeader(Qt::Orientation orientation, QWidget* parent = nullptr)
      : QHeaderView(orientation, parent)
  {}
  void ensureReadableSections();

  // once, for columns saved when Status and the date took more than they
  // need: what they give up goes to the name
  void compactSections();

  // whether the first real layout already widened status and date; later
  // resizes keep the widths the user chose
  bool m_readableChecked = false;

private:
  void mouseReleaseEvent(QMouseEvent* event) override;
};

class DownloadListView : public QTreeView
{
  Q_OBJECT

public:
  explicit DownloadListView(QWidget* parent = 0);
  ~DownloadListView();

  void setManager(DownloadManager* manager);

  // MO2 Revamped: the model under the search's proxy, whose groups the view
  // opens and closes as the user left them
  void setGroupsModel(DownloadList* model);
  // while searching every group is open, so no match is hidden in one
  void setSearching(bool searching);

  // the download behind a row of the view, -1 for a group
  int downloadRow(const QModelIndex& index) const;

signals:
  void installDownload(int index);
  // MO2 Revamped: several selected downloads, installed one after another
  void installDownloads(const QStringList& names);
  void queryInfoMd5(int index);
  void removeDownload(int index, bool deleteFile);
  void restoreDownload(int index);
  void cancelDownload(int index);
  void pauseDownload(int index);
  void resumeDownload(int index);
  void visitOnNexus(int index);
  void openFile(int index);
  void openMetaFile(int index);
  void openInDownloadsFolder(int index);

protected:
  // an empty list explains where downloads come from
  void paintEvent(QPaintEvent* event) override;

  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

  // downloads dragged onto a category move into it
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  // logs where a drag gets (it is hard to see from outside)
  void startDrag(Qt::DropActions supportedActions) override;

private slots:
  void onDoubleClick(const QModelIndex& index);
  void onCustomContextMenu(const QPoint& point);
  void onHeaderCustomContextMenu(const QPoint& point);

  void issueInstall(int index);
  void issueDelete(int index);
  void issueRemoveFromView(int index);
  void issueRestoreToView(int index);
  void issueRestoreToViewAll();
  void issueVisitOnNexus(int index);
  void issueOpenFile(int index);
  void issueOpenMetaFile(int index);
  void issueOpenInDownloadsFolder(int index);
  void issueCancel(int index);
  // asks first: cancelling throws away what was downloaded so far
  void issueCancelAsk(int index);
  // confirms a bulk delete, naming what goes; mode as removeDownload (-1 all
  // finished, -2 installed, -3 uninstalled)
  // and returns the file names confirmed (empty when declined)
  QStringList confirmBulkDelete(int mode, const QString& title);
  // removes those downloads, found again by name one by one
  // removes those downloads, by their archives, in one go
  void deleteDownloads(const QStringList& archives);
  void issuePause(int index);
  void issueResume(int index);
  void issueDeleteAll();
  void issueDeleteCompleted();
  void issueDeleteUninstalled();
  void issueRemoveFromViewAll();
  void issueRemoveFromViewCompleted();
  void issueRemoveFromViewUninstalled();
  void issueQueryInfoMd5(int index);

private:
  DownloadManager* m_Manager;
  DownloadList* m_Groups = nullptr;
  bool m_Restoring       = false;
  bool m_Searching       = false;
  // the first move after a press is logged while drags are being looked at
  bool m_MoveLogged = true;
  // the group a drag is over
  QPersistentModelIndex m_DropGroup;

  void resizeEvent(QResizeEvent* event);

  // opens or closes the groups as saved (all open while searching), and lets
  // their headers span the row
  void applyGroupState(int first = 0, int last = -1);
  void rememberGroupState(const QModelIndex& index, bool expanded);
  // opens or closes every group, and keeps that
  void setAllGroupsExpanded(bool expanded);
  // the group a drop at that point goes to, if downloads can move there
  QModelIndex dropGroupAt(const QPoint& pos, const QStringList& files) const;
  // the downloads a drag from this list carries, by file name; empty for any
  // other drag
  static QStringList draggedFiles(const QMimeData* mime);
  // the selected downloads, top to bottom
  QStringList selectedFileNames() const;
  // the finished downloads among those names
  QStringList readyNames(const QStringList& names) const;
  // the selection when the button was last pressed: the press of a
  // double-click on a selected row leaves only that row selected
  QStringList m_PressSelection;
  QItemSelection m_PressRanges;
  void addGroupActions(QMenu& menu, const QModelIndex& index);
  void newCategory(const QStringList& moveHere);
};

#endif  // DOWNLOADLISTWIDGET_H
