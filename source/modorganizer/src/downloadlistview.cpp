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

#include "downloadlistview.h"
#include <QAbstractProxyModel>
#include "fluenticons.h"
#include "menuutils.h"
#include "downloadlist.h"
#include "downloadgroups.h"
#include "startupdiagnostics.h"
#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QDragMoveEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QTimer>
#include <QSortFilterProxyModel>
#include <QWidgetAction>
#include <functional>
#include <log.h>
#include <report.h>

using namespace MOBase;

namespace
{
// what each column needs in the side panel, heading and sort arrow included
constexpr int DownloadListStatusWidth   = 84;
constexpr int DownloadListSizeWidth     = 72;
constexpr int DownloadListFiletimeWidth = 124;
}  // namespace

DownloadProgressDelegate::DownloadProgressDelegate(DownloadManager* manager,
                                                   DownloadListView* list)
    : QStyledItemDelegate(list), m_Manager(manager), m_List(list)
{}

void DownloadProgressDelegate::paint(QPainter* painter,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
  const int row = m_List->downloadRow(index);
  if (row < 0) {
    if (index.data(DownloadList::GroupKindRole).isValid()) {
      paintGroup(painter, option, index);
    } else {
      QStyledItemDelegate::paint(painter, option, index);
    }
    return;
  }

  bool pendingDownload = (row >= m_Manager->numTotalDownloads());
  if (index.column() == DownloadList::COL_STATUS && !pendingDownload &&
      m_Manager->getState(row) == DownloadManager::STATE_DOWNLOADING) {
    QProgressBar progressBar;
    progressBar.setProperty("downloadView", option.widget->property("downloadView"));
    progressBar.setProperty("downloadProgress", true);
    progressBar.resize(option.rect.width(), option.rect.height());
    progressBar.setTextVisible(true);
    progressBar.setAlignment(Qt::AlignCenter);
    progressBar.setMinimum(0);
    progressBar.setMaximum(100);
    progressBar.setValue(m_Manager->getProgress(row).first);
    progressBar.setFormat(m_Manager->getProgress(row).second);
    progressBar.setStyle(QApplication::style());

    // paint the background with default delegate first to preserve table cell styling
    QStyledItemDelegate::paint(painter, option, index);

    painter->save();
    painter->translate(option.rect.topLeft());
    progressBar.render(painter);
    painter->restore();
  } else {
    QStyledItemDelegate::paint(painter, option, index);
  }
}

void DownloadProgressDelegate::paintGroup(QPainter* painter,
                                          const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
  // the row's background, hover included, as the theme draws an item
  QStyleOptionViewItem opt(option);
  initStyleOption(&opt, index);
  opt.text.clear();
  opt.icon = QIcon();
  opt.features &= ~QStyleOptionViewItem::HasDecoration;
  const QWidget* widget = opt.widget;
  QStyle* style         = widget ? widget->style() : QApplication::style();
  style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

  const QModelIndex first = index.siblingAtColumn(0);
  const bool expanded     = m_List->isExpanded(first);
  const auto kind         = static_cast<DownloadList::GroupKind>(
      index.data(DownloadList::GroupKindRole).toInt());
  char16_t glyph = 0xE8B7;  // folder: the user's own category
  switch (kind) {
  case DownloadList::GROUP_LIST:
    glyph = 0xE8F1;  // library: a collection or list
    break;
  case DownloadList::GROUP_NEXUS:
    glyph = 0xE753;  // cloud
    break;
  case DownloadList::GROUP_OTHER:
    glyph = 0xE774;  // globe
    break;
  default:
    break;
  }

  const QColor ink   = opt.palette.color(QPalette::Text);
  const QColor muted = FluentIcons::color(FluentIcons::Tone::Muted);
  const QRect area   = opt.rect.adjusted(6, 0, -10, 0);

  painter->save();
  painter->setRenderHint(QPainter::TextAntialiasing);
  painter->setRenderHint(QPainter::Antialiasing);

  // chevron right when closed, down when open
  const QRect chevron(area.left(), area.top(), 16, area.height());
  painter->setFont(FluentIcons::font(11));
  painter->setPen(ink);
  painter->drawText(chevron, Qt::AlignCenter,
                    QString(QChar(expanded ? 0xE70D : 0xE76C)));

  const QRect icon(chevron.right() + 6, area.center().y() - 8, 16, 16);
  FluentIcons::glyph(glyph, FluentIcons::Tone::Muted).paint(painter, icon);

  // what the search left of the group, and its size on the right
  const int count    = index.model()->rowCount(first);
  const QString size = index.data(DownloadList::GroupSizeRole).toString();
  const QFont normal = opt.font;
  QFont bold         = opt.font;
  bold.setWeight(QFont::DemiBold);
  const QFontMetrics boldMetrics(bold);
  const QFontMetrics normalMetrics(normal);

  const int sizeWidth  = size.isEmpty() ? 0 : normalMetrics.horizontalAdvance(size);
  const QString counted = QString::number(count);
  const int countWidth  = normalMetrics.horizontalAdvance(counted);
  const int textLeft    = icon.right() + 8;
  const int nameRoom =
      std::max(0, area.right() - textLeft - countWidth - sizeWidth - 32);
  const QString name = boldMetrics.elidedText(
      index.data(DownloadList::GroupNameRole).toString(), Qt::ElideRight, nameRoom);

  painter->setFont(bold);
  painter->setPen(ink);
  const QRect nameRect(textLeft, area.top(), boldMetrics.horizontalAdvance(name),
                       area.height());
  painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, name);

  painter->setFont(normal);
  painter->setPen(muted);
  const QRect countRect(nameRect.right() + 8, area.top(), countWidth, area.height());
  painter->drawText(countRect, Qt::AlignVCenter | Qt::AlignLeft, counted);

  // a quiet line up to the size, as between the groups of a file window
  const int lineLeft  = countRect.right() + 12;
  const int lineRight = area.right() - sizeWidth - (sizeWidth > 0 ? 12 : 0);
  if (lineRight > lineLeft) {
    QColor line = muted;
    line.setAlphaF(0.35);
    painter->setPen(QPen(line, 1));
    const int y = area.center().y();
    painter->drawLine(lineLeft, y, lineRight, y);
  }
  if (sizeWidth > 0) {
    painter->setPen(muted);
    painter->drawText(
        QRect(area.right() - sizeWidth, area.top(), sizeWidth, area.height()),
        Qt::AlignVCenter | Qt::AlignRight, size);
  }
  painter->restore();
}

void DownloadListHeader::compactSections()
{
  if (isSectionHidden(DownloadList::COL_NAME)) {
    return;
  }

  int spare = 0;
  const auto compact = [&](int column, int width) {
    if (!isSectionHidden(column) && sectionSize(column) > width) {
      spare += sectionSize(column) - width;
      resizeSection(column, width);
    }
  };
  compact(DownloadList::COL_STATUS, DownloadListStatusWidth);
  compact(DownloadList::COL_SIZE, DownloadListSizeWidth);
  compact(DownloadList::COL_FILETIME, DownloadListFiletimeWidth);

  if (spare > 0) {
    resizeSection(DownloadList::COL_NAME, sectionSize(DownloadList::COL_NAME) + spare);
  }
}

void DownloadListHeader::ensureReadableSections()
{
  constexpr int statusTargetWidth   = DownloadListStatusWidth;
  constexpr int filetimeTargetWidth = DownloadListFiletimeWidth;
  constexpr int minimumNameWidth    = 180;

  const int statusGrowth = isSectionHidden(DownloadList::COL_STATUS)
                               ? 0
                               : std::max(0, statusTargetWidth -
                                                sectionSize(DownloadList::COL_STATUS));
  const int filetimeGrowth =
      isSectionHidden(DownloadList::COL_FILETIME)
          ? 0
          : std::max(0, filetimeTargetWidth -
                            sectionSize(DownloadList::COL_FILETIME));
  const int availableNameWidth =
      std::max(0, sectionSize(DownloadList::COL_NAME) - minimumNameWidth);

  int remainingGrowth = std::min(statusGrowth + filetimeGrowth, availableNameWidth);
  const int appliedStatusGrowth = std::min(statusGrowth, remainingGrowth);
  remainingGrowth -= appliedStatusGrowth;
  const int appliedFiletimeGrowth = std::min(filetimeGrowth, remainingGrowth);
  const int totalGrowth = appliedStatusGrowth + appliedFiletimeGrowth;

  if (totalGrowth == 0) {
    return;
  }

  resizeSection(DownloadList::COL_NAME,
                sectionSize(DownloadList::COL_NAME) - totalGrowth);
  if (appliedStatusGrowth > 0) {
    resizeSection(DownloadList::COL_STATUS,
                  sectionSize(DownloadList::COL_STATUS) + appliedStatusGrowth);
  }
  if (appliedFiletimeGrowth > 0) {
    resizeSection(DownloadList::COL_FILETIME,
                  sectionSize(DownloadList::COL_FILETIME) +
                      appliedFiletimeGrowth);
  }
}

void DownloadListHeader::mouseReleaseEvent(QMouseEvent* event)
{
  // the window's column sizer keeps the widths in step (and keeps what was
  // dragged); squeezing them here as well made the two fight on every resize
  QHeaderView::mouseReleaseEvent(event);
}

DownloadListView::DownloadListView(QWidget* parent) : QTreeView(parent)
{
  setHeader(new DownloadListHeader(Qt::Horizontal, this));

  header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header()->setSectionsMovable(true);
  header()->setContextMenuPolicy(Qt::CustomContextMenu);
  header()->setCascadingSectionResizes(true);
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(QHeaderView::Interactive);
  header()->setDefaultSectionSize(100);

  setUniformRowHeights(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // several downloads can be moved into a category at once
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  // a group opens and closes with one click on its header
  setRootIsDecorated(false);
  setExpandsOnDoubleClick(false);
  setItemsExpandable(true);
  sortByColumn(1, Qt::DescendingOrder);

  connect(header(), SIGNAL(customContextMenuRequested(QPoint)), this,
          SLOT(onHeaderCustomContextMenu(QPoint)));
  connect(this, SIGNAL(doubleClicked(QModelIndex)), this,
          SLOT(onDoubleClick(QModelIndex)));
  connect(this, SIGNAL(customContextMenuRequested(QPoint)), this,
          SLOT(onCustomContextMenu(QPoint)));
}

DownloadListView::~DownloadListView() {}

void DownloadListView::setManager(DownloadManager* manager)
{
  m_Manager = manager;

  // hide these columns by default
  //
  // note that this is overridden by the ini if MO has been started at least
  // once before, which is handled in MainWindow::processUpdates() for older
  // versions
  header()->hideSection(DownloadList::COL_MODNAME);
  header()->hideSection(DownloadList::COL_VERSION);
  header()->hideSection(DownloadList::COL_ID);
  header()->hideSection(DownloadList::COL_SOURCEGAME);
}

void DownloadListView::onDoubleClick(const QModelIndex& index)
{
  const int row = downloadRow(index);
  // a group, or a pending row (an nxm link still resolving), has no state
  if (row < 0 || row >= m_Manager->numTotalDownloads()) {
    return;
  }
  if (m_Manager->getState(row) >= DownloadManager::STATE_READY) {
    // MO2 Revamped: a double-click on one of several selected installs them
    // all; its first press left only this row selected, so they are selected
    // again as they were
    const QStringList before = std::exchange(m_PressSelection, {});
    const QString archive    = m_Manager->getArchivePath(row);
    if (before.size() > 1 && before.contains(archive, Qt::CaseInsensitive)) {
      const QStringList ready = readyArchives(before);
      if (ready.size() > 1) {
        if (selectionModel() != nullptr) {
          selectionModel()->select(m_PressRanges, QItemSelectionModel::ClearAndSelect);
        }
        m_PressRanges = {};
        emit installDownloads(ready);
        return;
      }
    }
    emit installDownload(row);
  }
  else if ((m_Manager->getState(row) == DownloadManager::STATE_PAUSED) ||
           (m_Manager->getState(row) == DownloadManager::STATE_PAUSING))
    emit resumeDownload(row);
}

void DownloadListView::onHeaderCustomContextMenu(const QPoint& point)
{
  QMenu menu;

  // display a list of all headers as checkboxes
  QAbstractItemModel* model = header()->model();
  for (int i = 1; i < model->columnCount(); ++i) {
    QString columnName  = model->headerData(i, Qt::Horizontal).toString();
    QCheckBox* checkBox = new QCheckBox(&menu);
    checkBox->setText(columnName);
    checkBox->setChecked(!header()->isSectionHidden(i));
    QWidgetAction* checkableAction = new QWidgetAction(&menu);
    checkableAction->setDefaultWidget(checkBox);
    menu.addAction(checkableAction);
  }

  menu.exec(header()->viewport()->mapToGlobal(point));

  // view/hide columns depending on check-state
  int i = 1;
  for (const QAction* action : menu.actions()) {
    const QWidgetAction* widgetAction = qobject_cast<const QWidgetAction*>(action);
    if (widgetAction != nullptr) {
      const QCheckBox* checkBox =
          qobject_cast<const QCheckBox*>(widgetAction->defaultWidget());
      if (checkBox != nullptr) {
        header()->setSectionHidden(i, !checkBox->isChecked());
      }
    }
    ++i;
  }

  qobject_cast<DownloadListHeader*>(header())->ensureReadableSections();
  // a column shown or hidden changes no size the window's column sizer
  // watches: it lays the columns out again from this
  emit header()->geometriesChanged();
}

void DownloadListView::resizeEvent(QResizeEvent* event)
{
  QTreeView::resizeEvent(event);
  auto* listHeader = qobject_cast<DownloadListHeader*>(header());

  // once, when the list first gets a usable width
  if (!listHeader->m_readableChecked && event->size().width() >= 400) {
    listHeader->m_readableChecked = true;
    listHeader->ensureReadableSections();
  }
}

void DownloadListView::onCustomContextMenu(const QPoint& point)
{
  QMenu menu(this);
  QModelIndex index = indexAt(point);
  bool hidden       = false;

  const auto addAction = [](QMenu& target, const QString& text, const QIcon& icon,
                            auto callback) {
    return target.addAction(icon, text, callback);
  };
  const auto svg = [](const QString& path) {
    return FluentIcons::icon(path);
  };

  // the destructive command of the selected download, added last
  std::function<void()> remove;
  // MO2 Revamped: a pending row (an nxm link still asking Nexus) can be taken off
  std::function<void()> removePending;

  try {
    const int sourceRow = downloadRow(index);
    if (sourceRow >= m_Manager->numTotalDownloads()) {
      // found again by its ids: the rows can change while the menu is open
      const auto pending =
          m_Manager->getPendingDownload(sourceRow - m_Manager->numTotalDownloads());
      removePending = [this, pending] {
        m_Manager->removePendingDownload(pending);
      };
    } else if (sourceRow >= 0) {
      const int row = sourceRow;
      DownloadManager::DownloadState state = m_Manager->getState(row);

      hidden = m_Manager->isHidden(row);
      const QString filePath = m_Manager->getFilePath(row);
      // the list is rebuilt when a download finishes, possibly while this menu
      // is open: each command finds its archive again by its file (two
      // downloads can share a name: foo.7z beside a paused foo.7z.unfinished,
      // or a read-only source's), and does nothing once it is gone
      const QString fileName = m_Manager->getFileName(row);
      const QString archive  = m_Manager->getArchivePath(row);
      const auto onRow = [this, archive](void (DownloadListView::*command)(int)) {
        return [this, archive, command] {
          const int current = m_Manager->indexByArchive(archive);
          if (current >= 0) {
            (this->*command)(current);
          }
        };
      };

      // MO2 Revamped: an archive of a read-only source (Nolvus' archives) can
      // be installed and looked at; nothing is written or deleted beside it
      const bool readOnly = m_Manager->isReadOnly(row);
      // MO2 Revamped: on one of several selected, Install takes them all
      const QStringList selected = selectedArchives();
      const int many = selected.contains(archive, Qt::CaseInsensitive)
                           ? static_cast<int>(readyArchives(selected).size())
                           : 0;
      const QString installText =
          many > 1 ? tr("Install %n selected", "", many) : tr("Install");
      if (state >= DownloadManager::STATE_READY && readOnly) {
        setDefaultMenuAction(&menu, 
            addAction(menu, installText, svg(":/MO/gui/mainwindow/install.svg"), onRow(&DownloadListView::issueInstall)));
        if (m_Manager->getModID(row) > 0) {
          addAction(menu, tr("Visit on Nexus"), svg(":/MO/gui/contextmenu/visit.svg"),
                    onRow(&DownloadListView::issueVisitOnNexus));
        }
        addAction(menu, tr("Open"), svg(":/MO/gui/mainwindow/files/archive.svg"), onRow(&DownloadListView::issueOpenFile));
        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), onRow(&DownloadListView::issueOpenInDownloadsFolder));
        addCopyMenu(&menu, {{tr("File name"), QFileInfo(filePath).fileName()},
                            {tr("File path"), nativePath(filePath)}});
      } else if (state >= DownloadManager::STATE_READY) {
        setDefaultMenuAction(&menu, 
            addAction(menu, installText, svg(":/MO/gui/mainwindow/install.svg"), onRow(&DownloadListView::issueInstall)));
        if (m_Manager->isInfoIncomplete(row)) {
          addAction(menu, tr("Query info"), svg(":/MO/gui/contextmenu/information.svg"),
                    onRow(&DownloadListView::issueQueryInfoMd5));
        } else {
          addAction(menu, tr("Visit on Nexus"), svg(":/MO/gui/contextmenu/visit.svg"),
                    onRow(&DownloadListView::issueVisitOnNexus));
        }
        addAction(menu, tr("Open"), svg(":/MO/gui/mainwindow/files/archive.svg"), onRow(&DownloadListView::issueOpenFile));

        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), onRow(&DownloadListView::issueOpenInDownloadsFolder));
        addAction(menu, tr("Open meta file"), menuGlyph(MenuGlyphs::Document), onRow(&DownloadListView::issueOpenMetaFile));
        addCopyMenu(&menu, {{tr("File name"), QFileInfo(filePath).fileName()},
                            {tr("File path"), nativePath(filePath)}});

        menu.addSeparator();
        if (hidden) {
          addAction(menu, tr("Un-hide"), svg(":/MO/gui/contextmenu/visibility-show.svg"),
                    onRow(&DownloadListView::issueRestoreToView));
        } else {
          addAction(menu, tr("Hide"), svg(":/MO/gui/contextmenu/visibility-hide.svg"),
                    onRow(&DownloadListView::issueRemoveFromView));
        }
        remove = onRow(&DownloadListView::issueDelete);
      } else if (state == DownloadManager::STATE_DOWNLOADING) {
        addAction(menu, tr("Pause"), menuGlyph(0xE769), onRow(&DownloadListView::issuePause));
        addAction(menu, tr("Cancel"), menuGlyph(0xE711), onRow(&DownloadListView::issueCancelAsk));
        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), onRow(&DownloadListView::issueOpenInDownloadsFolder));
      } else if ((state == DownloadManager::STATE_PAUSED) ||
                 (state == DownloadManager::STATE_ERROR) ||
                 (state == DownloadManager::STATE_PAUSING)) {
        // a download still pausing keeps its file open until it has paused:
        // it can be resumed, deleted only once paused
        const bool pausing = (state == DownloadManager::STATE_PAUSING);
        setDefaultMenuAction(&menu, addAction(menu, tr("Resume"), menuGlyph(MenuGlyphs::Run), onRow(&DownloadListView::issueResume)));
        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), onRow(&DownloadListView::issueOpenInDownloadsFolder));
        if (!pausing) {
          remove = onRow(&DownloadListView::issueDelete);
        }
      }
    }
  } catch (std::exception&) {
    // this happens when the download index is not found, ignore it and don't
    // display download-specific actions
  }

  // categories and groups
  addGroupActions(menu, index);

  // the whole list: the folder, then hiding and deleting by state
  menu.addSeparator();
  addAction(menu, tr("Open downloads folder"), menuGlyph(MenuGlyphs::Folder), [this] {
    shell::Explore(m_Manager->getOutputDirectory());
  });

  if (!hidden) {
    auto* hide = menu.addMenu(svg(":/MO/gui/contextmenu/visibility-hide.svg"),
                              tr("Hide downloads"));
    addAction(*hide, tr("Installed..."), {}, [=] {
      issueRemoveFromViewCompleted();
    });
    addAction(*hide, tr("Uninstalled..."), {}, [=] {
      issueRemoveFromViewUninstalled();
    });
    addAction(*hide, tr("All..."), {}, [=] {
      issueRemoveFromViewAll();
    });
  } else {
    addAction(menu, tr("Un-hide all"), svg(":/MO/gui/contextmenu/visibility-show.svg"),
              [=] {
                issueRestoreToViewAll();
              });
  }

  auto* cleanup =
      menu.addMenu(svg(":/MO/gui/contextmenu/remove.svg"), tr("Delete downloads"));
  addAction(*cleanup, tr("Installed..."), {}, [=] {
    issueDeleteCompleted();
  });
  addAction(*cleanup, tr("Uninstalled..."), {}, [=] {
    issueDeleteUninstalled();
  });
  addAction(*cleanup, tr("All..."), {}, [=] {
    issueDeleteAll();
  });

  if (remove) {
    menu.addSeparator();
    addAction(menu, tr("Delete..."), svg(":/MO/gui/contextmenu/remove.svg"), remove);
  } else if (removePending) {
    menu.addSeparator();
    addAction(menu, tr("Remove"), svg(":/MO/gui/contextmenu/remove.svg"), removePending);
  }

  menu.exec(viewport()->mapToGlobal(point));
}

void DownloadListView::keyPressEvent(QKeyEvent* event)
{
  if (selectionModel()->hasSelection()) {
    const int row = downloadRow(currentIndex());
    // a pending row (an nxm link still resolving) has no state yet; Delete
    // takes it off the list (MO2 Revamped)
    if (row >= m_Manager->numTotalDownloads() &&
        row < m_Manager->numTotalDownloads() + m_Manager->numPendingDownloads() &&
        event->key() == Qt::Key_Delete) {
      m_Manager->removePendingDownload(
          m_Manager->getPendingDownload(row - m_Manager->numTotalDownloads()));
      event->accept();
      return;
    }
    if (row < 0 || row >= m_Manager->numTotalDownloads()) {
      QTreeView::keyPressEvent(event);
      return;
    }
    auto state = m_Manager->getState(row);
    if (state >= DownloadManager::STATE_READY) {
      if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        issueInstall(row);
      } else if (event->key() == Qt::Key_Delete) {
        issueDelete(row);
      }
    } else if (state == DownloadManager::STATE_DOWNLOADING) {
      if (event->key() == Qt::Key_Delete) {
        // a finished download asks before it goes; one under way too, as
        // cancelling throws away what was downloaded so far
        issueCancelAsk(row);
        event->accept();
        return;
      } else if (event->key() == Qt::Key_Space) {
        issuePause(row);
      }
    } else if (state == DownloadManager::STATE_PAUSED ||
               state == DownloadManager::STATE_ERROR ||
               state == DownloadManager::STATE_PAUSING) {
      // deleting waits until a pausing download has paused (see the menu)
      if (event->key() == Qt::Key_Delete && state != DownloadManager::STATE_PAUSING) {
        issueDelete(row);
      } else if (event->key() == Qt::Key_Space) {
        issueResume(row);
      }
    }
  }
  QTreeView::keyPressEvent(event);
}

void DownloadListView::issueInstall(int index)
{
  // MO2 Revamped: the row is one of several selected: all of them, in turn
  const QStringList selected = selectedArchives();
  if (index >= 0 && index < m_Manager->numTotalDownloads() &&
      selected.contains(m_Manager->getArchivePath(index), Qt::CaseInsensitive)) {
    const QStringList ready = readyArchives(selected);
    if (ready.size() > 1) {
      emit installDownloads(ready);
      return;
    }
  }
  emit installDownload(index);
}

QStringList DownloadListView::readyArchives(const QStringList& archives) const
{
  QStringList ready;
  for (const QString& archive : archives) {
    const int row = m_Manager->indexByArchive(archive);
    if (row >= 0 && m_Manager->getState(row) >= DownloadManager::STATE_READY) {
      ready << archive;
    }
  }
  return ready;
}

QStringList DownloadListView::selectedArchives() const
{
  // the same rows, in the same order, as selectedFileNames()
  QStringList archives;
  for (const int row : selectedDownloadRows()) {
    archives << m_Manager->getArchivePath(row);
  }
  return archives;
}

void DownloadListView::issueQueryInfoMd5(int index)
{
  emit queryInfoMd5(index);
}

void DownloadListView::issueDelete(int index)
{
  // the list is rebuilt when a download finishes, possibly while the question
  // is open; delete the archive that was asked about, found again by its file
  const QString archive = m_Manager->getArchivePath(index);

  const auto r = MOBase::TaskDialog(this, tr("Delete download"))
                     .main(tr("Are you sure you want to delete this download?"))
                     .content(m_Manager->getFilePath(index))
                     .icon(QMessageBox::Question)
                     .button({tr("Move to the Recycle Bin"), QMessageBox::Yes})
                     .button({tr("Cancel"), QMessageBox::Cancel})
                     .exec();

  if (r != QMessageBox::Yes) {
    return;
  }

  const int current = m_Manager->indexByArchive(archive);
  if (current < 0) {
    return;
  }

  emit removeDownload(current, true);
}

void DownloadListView::issueRemoveFromView(int index)
{
  log::debug("removing from view: {}", index);
  emit removeDownload(index, false);
}

void DownloadListView::issueRestoreToView(int index)
{
  emit restoreDownload(index);
}

void DownloadListView::issueRestoreToViewAll()
{
  emit restoreDownload(-1);
}

void DownloadListView::issueVisitOnNexus(int index)
{
  emit visitOnNexus(index);
}

void DownloadListView::issueOpenFile(int index)
{
  emit openFile(index);
}

void DownloadListView::issueOpenMetaFile(int index)
{
  emit openMetaFile(index);
}

void DownloadListView::issueOpenInDownloadsFolder(int index)
{
  emit openInDownloadsFolder(index);
}

void DownloadListView::issueCancel(int index)
{
  emit cancelDownload(index);
}

void DownloadListView::issuePause(int index)
{
  emit pauseDownload(index);
}

void DownloadListView::issueResume(int index)
{
  emit resumeDownload(index);
}

void DownloadListView::issueCancelAsk(int index)
{
  // the menu's Cancel and the Delete key ask the same question
  const QString archive = m_Manager->getArchivePath(index);
  const auto answer      = MOBase::TaskDialog(this, tr("Cancel download"))
                          .main(tr("Cancel this download?"))
                          .content(tr("What was downloaded so far is deleted."))
                          .icon(QMessageBox::Question)
                          .button({tr("Keep downloading"), QMessageBox::Cancel})
                          .button({tr("Cancel download"), QMessageBox::Yes})
                          .exec();
  // the list can change while the question is open
  const int current = m_Manager->indexByArchive(archive);
  if (answer == QMessageBox::Yes && current >= 0) {
    issueCancel(current);
  }
}

QStringList DownloadListView::confirmBulkDelete(int mode, const QString& title)
{
  // the same choice DownloadManager::removeDownload() makes: every download in
  // that state the list holds, rows the search hides included; the archives
  // are kept, as two downloads can share a name
  QStringList names, archives;
  for (int i = 0; i < m_Manager->numTotalDownloads(); ++i) {
    const auto state = m_Manager->getState(i);
    // a read-only source's archives are never deleted
    if (m_Manager->isReadOnly(i)) {
      continue;
    }
    if ((mode == -1 && state >= DownloadManager::STATE_READY) ||
        (mode == -2 && state == DownloadManager::STATE_INSTALLED) ||
        (mode == -3 && state == DownloadManager::STATE_UNINSTALLED)) {
      names << m_Manager->getFileName(i);
      archives << m_Manager->getArchivePath(i);
    }
  }

  if (names.isEmpty()) {
    QMessageBox::information(this, title, tr("There are no downloads to delete."));
    return {};
  }

  QStringList shown = names.mid(0, 10);
  if (names.size() > shown.size()) {
    shown << tr("...and %1 more").arg(names.size() - shown.size());
  }
  // hidden downloads are only in the list (and so only deleted) while they
  // are shown
  QString text;
  if (m_Manager->showsHidden()) {
    text = names.size() == 1
               ? tr("1 download will be moved to the Recycle Bin, even if the search "
                    "or Hide keeps it out of the list:")
               : tr("%1 downloads will be moved to the Recycle Bin, including any the "
                    "search or Hide keeps out of the list:")
                     .arg(names.size());
  } else {
    text = names.size() == 1
               ? tr("1 download will be moved to the Recycle Bin, even if the search "
                    "keeps it out of the list. Hidden downloads are not deleted; "
                    "turn on Show hidden downloads to include them:")
               : tr("%1 downloads will be moved to the Recycle Bin, including any the "
                    "search keeps out of the list. Hidden downloads are not deleted; "
                    "turn on Show hidden downloads to include them:")
                     .arg(names.size());
  }
  text += QStringLiteral("\n\n") + shown.join(QLatin1Char('\n'));

  // only what was named goes, even if another download finishes meanwhile
  if (QMessageBox::warning(this, title, text, QMessageBox::Yes | QMessageBox::No,
                           QMessageBox::No) != QMessageBox::Yes) {
    return {};
  }
  return archives;
}

void DownloadListView::deleteDownloads(const QStringList& archives)
{
  // one removal and one refresh of the list for all of them; each is found
  // again by its archive and only if it is still finished
  if (!archives.isEmpty()) {
    m_Manager->removeDownloads(archives, true);
  }
}

void DownloadListView::issueDeleteAll()
{
  deleteDownloads(confirmBulkDelete(-1, tr("Delete finished downloads?")));
}

void DownloadListView::issueDeleteCompleted()
{
  deleteDownloads(confirmBulkDelete(-2, tr("Delete installed downloads?")));
}

void DownloadListView::issueDeleteUninstalled()
{
  deleteDownloads(confirmBulkDelete(-3, tr("Delete uninstalled downloads?")));
}

void DownloadListView::issueRemoveFromViewAll()
{
  if (QMessageBox::question(this, tr("Hide files?"),
                            tr("This will remove all finished downloads from this list "
                               "(but NOT from disk)."),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-1, false);
  }
}

void DownloadListView::issueRemoveFromViewCompleted()
{
  if (QMessageBox::question(this, tr("Hide files?"),
                            tr("This will remove all installed downloads from this "
                               "list (but NOT from disk)."),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-2, false);
  }
}

void DownloadListView::issueRemoveFromViewUninstalled()
{
  if (QMessageBox::question(this, tr("Hide files?"),
                            tr("This will remove all uninstalled downloads from this "
                               "list (but NOT from disk)."),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-3, false);
  }
}

void DownloadListView::paintEvent(QPaintEvent* event)
{
  QTreeView::paintEvent(event);

  // where downloads dragged here would go
  if (m_DropGroup.isValid()) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    QRect target = visualRect(m_DropGroup);
    target.setLeft(0);
    target.setRight(viewport()->width() - 1);
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(target).adjusted(1, 1, -1, -1), 4, 4);
  }

  // a filter that hides every row is not an empty list, nor are categories
  // still empty
  if (m_Manager == nullptr ||
      m_Manager->numTotalDownloads() + m_Manager->numPendingDownloads() > 0) {
    return;
  }

  // Windows 11 empty state: a muted glyph, a title and one line of help
  QPainter p(viewport());
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);

  const QRect area = viewport()->rect().adjusted(24, 0, -24, 0);
  const int glyph  = 32;
  const int top    = area.center().y() - 56;

  FluentIcons::glyph(0xE896, FluentIcons::Tone::Muted)
      .paint(&p, QRect(area.center().x() - glyph / 2, top, glyph, glyph));

  QFont titleFont = font();
  titleFont.setPointSizeF(titleFont.pointSizeF() + 3);
  titleFont.setWeight(QFont::DemiBold);
  p.setFont(titleFont);
  p.setPen(palette().color(QPalette::Text));
  const QRect title(area.left(), top + glyph + 12, area.width(), 28);
  p.drawText(title, Qt::AlignHCenter | Qt::AlignVCenter, tr("No downloads to show"));

  p.setFont(font());
  p.setPen(FluentIcons::color(FluentIcons::Tone::Muted));
  // as tall as the wrapped text needs, so a narrow panel or a larger font
  // doesn't cut its last line
  const QString helpText =
      tr("Archives you download with Nexus or add with Install appear here. "
         "Turn on Show hidden downloads to see the ones you hid.");
  const QRect needed = p.fontMetrics().boundingRect(
      QRect(area.left(), 0, area.width(), 10000),
      Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, helpText);
  const QRect help(area.left(), title.bottom() + 4, area.width(), needed.height());
  p.drawText(help, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, helpText);
}

// --- MO2 Revamped: groups and categories ------------------------------------

int DownloadListView::downloadRow(const QModelIndex& index) const
{
  if (!index.isValid()) {
    return -1;
  }
  const QVariant row = index.data(DownloadList::DownloadRowRole);
  return row.isValid() ? row.toInt() : -1;
}

void DownloadListView::setGroupsModel(DownloadList* model)
{
  m_Groups = model;
  // the window's layout file turns these off for the flat list it had
  setItemsExpandable(true);
  setRootIsDecorated(false);
  setExpandsOnDoubleClick(false);
  // opening a group must not narrow the list: the columns are fitted to its
  // width, and a scroll bar showing up only then moved every header
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  QAbstractItemModel* shown = this->model();
  if (shown == nullptr) {
    return;
  }

  connect(shown, &QAbstractItemModel::modelReset, this, [this] {
    applyGroupState();
  });
  // a group the search had hidden comes back closed: it is opened again as
  // it was left
  connect(shown, &QAbstractItemModel::rowsInserted, this,
          [this](const QModelIndex& parent, int first, int last) {
            if (!parent.isValid()) {
              applyGroupState(first, last);
            }
          });
  connect(this, &QTreeView::expanded, this, [this](const QModelIndex& index) {
    rememberGroupState(index, true);
  });
  connect(this, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
    rememberGroupState(index, false);
  });

  applyGroupState();
}

void DownloadListView::setSearching(bool searching)
{
  if (m_Searching == searching) {
    return;
  }
  m_Searching = searching;
  applyGroupState();
}

void DownloadListView::applyGroupState(int first, int last)
{
  if (m_Groups == nullptr || model() == nullptr) {
    return;
  }
  const int count = model()->rowCount();
  if (last < 0 || last >= count) {
    last = count - 1;
  }

  m_Restoring = true;
  for (int row = std::max(0, first); row <= last; ++row) {
    const QModelIndex index = model()->index(row, 0);
    const QVariant key      = index.data(DownloadList::GroupKeyRole);
    if (!key.isValid()) {
      continue;  // not grouped: the rows are downloads
    }
    setFirstColumnSpanned(row, QModelIndex(), true);
    const bool open = m_Searching || !m_Groups->groups().isCollapsed(key.toString());
    if (isExpanded(index) != open) {
      setExpanded(index, open);
    }
  }
  m_Restoring = false;
}

void DownloadListView::setAllGroupsExpanded(bool expanded)
{
  // one group at a time: QTreeView::expandAll() says nothing per group, so
  // what it opened wasn't kept and closed again at the next refresh
  if (model() == nullptr) {
    return;
  }
  for (int row = 0; row < model()->rowCount(); ++row) {
    const QModelIndex index = model()->index(row, 0);
    if (index.data(DownloadList::GroupKeyRole).isValid()) {
      if (isExpanded(index) != expanded) {
        setExpanded(index, expanded);
      } else {
        rememberGroupState(index, expanded);
      }
    }
  }
}

void DownloadListView::rememberGroupState(const QModelIndex& index, bool expanded)
{
  // only what the user did; opening every group for a search isn't kept
  if (m_Restoring || m_Searching || m_Groups == nullptr) {
    return;
  }
  const QVariant key = index.data(DownloadList::GroupKeyRole);
  if (key.isValid()) {
    m_Groups->groups().setCollapsed(key.toString(), !expanded);
  }
}

std::vector<int> DownloadListView::selectedDownloadRows() const
{
  if (selectionModel() == nullptr) {
    return {};
  }
  // top to bottom, as the user sees them (the selection keeps the order of
  // the clicks): in the view's model order, group by group. Not by
  // visualRect: rows in a folded group have none, and went first
  const auto path = [](QModelIndex index) {
    std::vector<int> rows;
    for (; index.isValid(); index = index.parent()) {
      rows.insert(rows.begin(), index.row());
    }
    return rows;
  };
  QModelIndexList rows = selectionModel()->selectedRows();
  std::sort(rows.begin(), rows.end(), [&path](const QModelIndex& a, const QModelIndex& b) {
    return path(a) < path(b);
  });
  std::vector<int> downloads;
  for (const QModelIndex& index : rows) {
    const int row = downloadRow(index);
    if (row >= 0 && row < m_Manager->numTotalDownloads()) {
      downloads.push_back(row);
    }
  }
  return downloads;
}

QStringList DownloadListView::selectedFileNames() const
{
  QStringList names;
  for (const int row : selectedDownloadRows()) {
    names << m_Manager->getFileName(row);
  }
  return names;
}

void DownloadListView::newCategory(const QStringList& moveHere)
{
  if (m_Groups == nullptr) {
    return;
  }
  DownloadGroups& groups = m_Groups->groups();
  QString name;
  for (;;) {
    bool ok = false;
    name    = QInputDialog::getText(this, tr("New category"),
                                    tr("Name of the new category:"), QLineEdit::Normal,
                                    name, &ok)
               .trimmed();
    if (!ok || name.isEmpty()) {
      return;
    }
    if (!groups.hasCategory(name)) {
      break;
    }
    QMessageBox::information(this, tr("New category"),
                             tr("There already is a category named \"%1\".").arg(name));
  }

  groups.addCategory(name);
  if (!moveHere.isEmpty()) {
    groups.setCategory(moveHere, name);
  }
  // grouping is what shows a category
  groups.setGrouped(true);
}

void DownloadListView::addGroupActions(QMenu& menu, const QModelIndex& index)
{
  if (m_Groups == nullptr) {
    return;
  }
  DownloadGroups& groups = m_Groups->groups();
  menu.addSeparator();

  // the selected downloads: into a category, or out of one
  const QStringList selected = selectedFileNames();
  if (!selected.isEmpty()) {
    QString current = groups.categoryOf(selected.first());
    for (const QString& name : selected) {
      if (groups.categoryOf(name) != current) {
        current.clear();
        break;
      }
    }
    bool anyInCategory = false;
    for (const QString& name : selected) {
      if (!groups.categoryOf(name).isEmpty()) {
        anyInCategory = true;
        break;
      }
    }

    auto* move = menu.addMenu(menuGlyph(0xE8DE),
                              selected.size() == 1
                                  ? tr("Move to category")
                                  : tr("Move %1 downloads to category").arg(selected.size()));
    for (const QString& category : groups.categories()) {
      QAction* action = move->addAction(category, [this, selected, category] {
        m_Groups->groups().setCategory(selected, category);
        m_Groups->groups().setGrouped(true);
      });
      action->setCheckable(true);
      action->setChecked(category == current);
    }
    if (!groups.categories().isEmpty()) {
      move->addSeparator();
    }
    move->addAction(tr("New category..."), [this, selected] {
      newCategory(selected);
    });
    if (anyInCategory) {
      move->addSeparator();
      move->addAction(tr("Back to where it came from"), [this, selected] {
        m_Groups->groups().setCategory(selected, QString());
      });
    }
  }

  // a category's own header
  const QVariant kind = index.data(DownloadList::GroupKindRole);
  if (kind.isValid() && kind.toInt() == DownloadList::GROUP_CATEGORY) {
    const QString name = index.data(DownloadList::GroupNameRole).toString();
    menu.addAction(menuGlyph(0xE8AC), tr("Rename category..."), [this, name] {
      bool ok = false;
      const QString to =
          QInputDialog::getText(this, tr("Rename category"), tr("New name:"),
                                QLineEdit::Normal, name, &ok)
              .trimmed();
      if (ok && !to.isEmpty() && to != name &&
          !m_Groups->groups().renameCategory(name, to)) {
        QMessageBox::information(this, tr("Rename category"),
                                 tr("There already is a category named \"%1\".").arg(to));
      }
    });
    menu.addAction(menuGlyph(0xE74D), tr("Remove category"), [this, name] {
      // nothing is deleted: its downloads go back to where they came from
      m_Groups->groups().removeCategory(name);
    });
  } else if (selected.isEmpty()) {
    menu.addAction(menuGlyph(0xE8F4), tr("New category..."), [this] {
      newCategory({});
    });
  }

  QAction* grouped = menu.addAction(tr("Group by origin"), [this](bool on) {
    m_Groups->groups().setGrouped(on);
  });
  grouped->setCheckable(true);
  grouped->setChecked(groups.grouped());

  if (groups.grouped() && model() != nullptr && model()->rowCount() > 1) {
    menu.addAction(tr("Expand all groups"), [this] {
      setAllGroupsExpanded(true);
    });
    menu.addAction(tr("Collapse all groups"), [this] {
      setAllGroupsExpanded(false);
    });
  }
}

void DownloadListView::mouseMoveEvent(QMouseEvent* event)
{
  QTreeView::mouseMoveEvent(event);
  if (!m_MoveLogged && (event->buttons() & Qt::LeftButton)) {
    m_MoveLogged = true;
    log::debug("downloads: first move with the button held, state {}, drag {}",
              static_cast<int>(state()), dragEnabled());
  }
}

void DownloadListView::mousePressEvent(QMouseEvent* event)
{
  {
    const QModelIndex at = indexAt(event->position().toPoint());
    log::debug("downloads: press, button {}, on {} (row {})",
              static_cast<int>(event->button()),
              !at.isValid()                                         ? "nothing"
              : at.data(DownloadList::GroupKeyRole).isValid()       ? "a group"
                                                                     : "a download",
              downloadRow(at));
    m_MoveLogged = false;
  }
  // what was selected before this press (see onDoubleClick())
  m_PressSelection = selectedArchives();
  m_PressRanges    = selectionModel() != nullptr ? selectionModel()->selection()
                                                 : QItemSelection();
  // one click on a group's header opens or closes it
  if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
    const QModelIndex index = indexAt(event->position().toPoint());
    if (index.isValid() && index.data(DownloadList::GroupKeyRole).isValid()) {
      const QModelIndex first = index.siblingAtColumn(0);
      setExpanded(first, !isExpanded(first));
      setCurrentIndex(first);
      event->accept();
      return;
    }
  }
  QTreeView::mousePressEvent(event);
}

void DownloadListView::mouseDoubleClickEvent(QMouseEvent* event)
{
  // a quick second click on a header toggles it back, as a click does
  const QModelIndex index = indexAt(event->position().toPoint());
  if (event->button() == Qt::LeftButton && index.isValid() &&
      index.data(DownloadList::GroupKeyRole).isValid()) {
    mousePressEvent(event);
    return;
  }
  QTreeView::mouseDoubleClickEvent(event);
}

QStringList DownloadListView::draggedFiles(const QMimeData* mime)
{
  if (mime == nullptr || !mime->hasFormat(DownloadList::namesMimeType())) {
    return {};
  }
  return QString::fromUtf8(mime->data(DownloadList::namesMimeType()))
      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QModelIndex DownloadListView::dropGroupAt(const QPoint& pos,
                                          const QStringList& files) const
{
  if (m_Groups == nullptr || !m_Groups->groups().grouped() || files.isEmpty()) {
    return {};
  }
  QModelIndex index = indexAt(pos);
  if (!index.isValid()) {
    return {};
  }
  if (!index.data(DownloadList::GroupKeyRole).isValid()) {
    index = index.parent();
  }
  if (!index.isValid()) {
    return {};
  }
  index = index.siblingAtColumn(0);

  // into a category; or, onto where they came from, out of their category
  const DownloadGroups& groups = m_Groups->groups();
  if (index.data(DownloadList::GroupKindRole).toInt() == DownloadList::GROUP_CATEGORY) {
    const QString name = index.data(DownloadList::GroupNameRole).toString();
    for (const QString& file : files) {
      if (groups.categoryOf(file) != name) {
        return index;
      }
    }
    return {};
  }
  // only back to the group each of them comes from: dropped on another, they
  // would land somewhere else than where they were dropped
  const QString key = index.data(DownloadList::GroupKeyRole).toString();
  bool any          = false;
  for (const QString& file : files) {
    if (groups.categoryOf(file).isEmpty()) {
      continue;
    }
    if (m_Groups->originKey(m_Manager->indexByName(file)) != key) {
      return {};
    }
    any = true;
  }
  return any ? index : QModelIndex();
}

void DownloadListView::startDrag(Qt::DropActions supportedActions)
{
  // MO2 Revamped: the dragged downloads only; no picture of every column of
  // every row, and only ever copied (nothing is taken out of the list)
  QModelIndexList indexes;
  if (selectionModel() != nullptr) {
    for (const QModelIndex& index : selectionModel()->selectedIndexes()) {
      if (model()->flags(index) & Qt::ItemIsDragEnabled) {
        indexes << index;
      }
    }
  }
  if (indexes.isEmpty()) {
    log::debug("downloads: nothing to drag");
    return;
  }
  QMimeData* data = model()->mimeData(indexes);
  if (data == nullptr) {
    return;
  }

  const int count = static_cast<int>(draggedFiles(data).size());
  auto* drag      = new QDrag(this);
  drag->setMimeData(data);
  drag->setPixmap(FluentIcons::file(QStringLiteral("archive.zip"), false)
                      .pixmap(QSize(32, 32), devicePixelRatioF()));
  drag->setHotSpot(QPoint(-8, -8));

  // Windows ends a drag at once when it doesn't see the button held
  log::info("downloads: dragging {} download(s), left button held for Windows: {}",
             count, (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON
                                                                      : VK_LBUTTON) &
                     0x8000) != 0);
  const Qt::DropAction action =
      drag->exec(supportedActions | Qt::CopyAction, Qt::CopyAction);
  log::info("downloads: drag ended ({})", static_cast<int>(action));
}

void DownloadListView::dragEnterEvent(QDragEnterEvent* event)
{
  log::info("downloads: drag entered, {} download(s)",
             draggedFiles(event->mimeData()).size());
  // a drag of this list's downloads (the names say so; the source is only
  // known while a real drag runs)
  if (!draggedFiles(event->mimeData()).isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    return;
  }
  // MO2 Revamped: files from Explorer go to the main window, which copies
  // them into the downloads folder (it knows where that is)
  if (event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
    event->ignore();
    return;
  }
  QTreeView::dragEnterEvent(event);
}

void DownloadListView::dragMoveEvent(QDragMoveEvent* event)
{
  const QStringList files = draggedFiles(event->mimeData());
  if (files.isEmpty()) {
    QTreeView::dragMoveEvent(event);
    return;
  }
  const QModelIndex target = dropGroupAt(event->position().toPoint(), files);
  if (QPersistentModelIndex(target) != m_DropGroup) {
    m_DropGroup = target;
    viewport()->update();
  }
  // near the edges the list scrolls, as for any drag; the answer is ours
  QAbstractItemView::dragMoveEvent(event);
  if (target.isValid()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->ignore();
  }
}

void DownloadListView::dragLeaveEvent(QDragLeaveEvent* event)
{
  m_DropGroup = QPersistentModelIndex();
  viewport()->update();
  QTreeView::dragLeaveEvent(event);
}

void DownloadListView::dropEvent(QDropEvent* event)
{
  m_DropGroup = QPersistentModelIndex();
  viewport()->update();
  const QStringList files = draggedFiles(event->mimeData());
  log::info("downloads: drop of {} download(s)", files.size());
  if (files.isEmpty()) {
    QTreeView::dropEvent(event);
    return;
  }

  const QModelIndex target = dropGroupAt(event->position().toPoint(), files);
  if (!target.isValid()) {
    event->ignore();
    return;
  }
  const bool category =
      target.data(DownloadList::GroupKindRole).toInt() == DownloadList::GROUP_CATEGORY;
  // copy: the view must not take the rows out of the model after the drop
  event->setDropAction(Qt::CopyAction);
  event->accept();
  const QString to = category ? target.data(DownloadList::GroupNameRole).toString()
                              : QString();
  // after the drag has ended: moving regroups the list
  QTimer::singleShot(0, this, [this, files, to] {
    if (m_Groups != nullptr) {
      m_Groups->groups().setCategory(files, to);
    }
  });
}
