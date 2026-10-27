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
#include "startupdiagnostics.h"
#include <QApplication>
#include <QCheckBox>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QSortFilterProxyModel>
#include <QWidgetAction>
#include <functional>
#include <log.h>
#include <report.h>

using namespace MOBase;

DownloadProgressDelegate::DownloadProgressDelegate(DownloadManager* manager,
                                                   DownloadListView* list)
    : QStyledItemDelegate(list), m_Manager(manager), m_List(list)
{}

void DownloadProgressDelegate::paint(QPainter* painter,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
  QModelIndex sourceIndex;

  if (auto* proxy = dynamic_cast<QSortFilterProxyModel*>(m_List->model())) {
    sourceIndex = proxy->mapToSource(index);
  } else {
    sourceIndex = index;
  }

  bool pendingDownload = (sourceIndex.row() >= m_Manager->numTotalDownloads());
  if (sourceIndex.column() == DownloadList::COL_STATUS && !pendingDownload &&
      m_Manager->getState(sourceIndex.row()) == DownloadManager::STATE_DOWNLOADING) {
    QProgressBar progressBar;
    progressBar.setProperty("downloadView", option.widget->property("downloadView"));
    progressBar.setProperty("downloadProgress", true);
    progressBar.resize(option.rect.width(), option.rect.height());
    progressBar.setTextVisible(true);
    progressBar.setAlignment(Qt::AlignCenter);
    progressBar.setMinimum(0);
    progressBar.setMaximum(100);
    progressBar.setValue(m_Manager->getProgress(sourceIndex.row()).first);
    progressBar.setFormat(m_Manager->getProgress(sourceIndex.row()).second);
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

void DownloadListHeader::customResizeSections()
{
  // find the rightmost column that is not hidden
  int rightVisible = count() - 1;
  while (isSectionHidden(rightVisible) && rightVisible > 0)
    rightVisible--;

  // if that column is already squashed, squash others to the right side --
  // otherwise to the left
  if (sectionSize(rightVisible) == minimumSectionSize()) {
    for (int idx = rightVisible; idx >= 0; idx--) {
      if (!isSectionHidden(idx)) {
        if (length() != width()) {
          setStartupDiagnosticPhase(
              "download_list_header.resize_section.right_to_left");
          resizeSection(idx, std::max(sectionSize(idx) + width() - length(),
                                      minimumSectionSize()));
        } else {
          break;
        }
      }
    }
  } else {
    for (int idx = 0; idx <= rightVisible; idx++) {
      if (!isSectionHidden(idx)) {
        if (length() != width()) {
          setStartupDiagnosticPhase(
              "download_list_header.resize_section.left_to_right");
          resizeSection(idx, std::max(sectionSize(idx) + width() - length(),
                                      minimumSectionSize()));
        } else {
          break;
        }
      }
    }
  }
}

void DownloadListHeader::ensureReadableSections()
{
  constexpr int statusTargetWidth   = 120;
  constexpr int filetimeTargetWidth = 155;
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
  QHeaderView::mouseReleaseEvent(event);
  customResizeSections();
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

void DownloadListView::setSourceModel(DownloadList* sourceModel)
{
  m_SourceModel = sourceModel;
}

void DownloadListView::onDoubleClick(const QModelIndex& index)
{
  QModelIndex sourceIndex =
      qobject_cast<QSortFilterProxyModel*>(model())->mapToSource(index);
  if (m_Manager->getState(sourceIndex.row()) >= DownloadManager::STATE_READY)
    emit installDownload(sourceIndex.row());
  else if ((m_Manager->getState(sourceIndex.row()) == DownloadManager::STATE_PAUSED) ||
           (m_Manager->getState(sourceIndex.row()) == DownloadManager::STATE_PAUSING))
    emit resumeDownload(sourceIndex.row());
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

  qobject_cast<DownloadListHeader*>(header())->customResizeSections();
  qobject_cast<DownloadListHeader*>(header())->ensureReadableSections();
}

void DownloadListView::resizeEvent(QResizeEvent* event)
{
  QTreeView::resizeEvent(event);
  auto* listHeader = qobject_cast<DownloadListHeader*>(header());
  listHeader->customResizeSections();

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

  try {
    if (index.row() >= 0) {
      const int row =
          qobject_cast<QSortFilterProxyModel*>(model())->mapToSource(index).row();
      DownloadManager::DownloadState state = m_Manager->getState(row);

      hidden = m_Manager->isHidden(row);
      const QString filePath = m_Manager->getFilePath(row);

      if (state >= DownloadManager::STATE_READY) {
        setDefaultMenuAction(&menu, 
            addAction(menu, tr("Install"), svg(":/MO/gui/mainwindow/install.svg"), [=] {
              issueInstall(row);
            }));
        if (m_Manager->isInfoIncomplete(row)) {
          addAction(menu, tr("Query info"), svg(":/MO/gui/contextmenu/information.svg"),
                    [=] {
                      issueQueryInfoMd5(row);
                    });
        } else {
          addAction(menu, tr("Visit on Nexus"), svg(":/MO/gui/contextmenu/visit.svg"),
                    [=] {
                      issueVisitOnNexus(row);
                    });
        }
        addAction(menu, tr("Open"), svg(":/MO/gui/mainwindow/files/archive.svg"), [=] {
          issueOpenFile(row);
        });

        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), [=] {
          issueOpenInDownloadsFolder(row);
        });
        addAction(menu, tr("Open meta file"), menuGlyph(MenuGlyphs::Document), [=] {
          issueOpenMetaFile(row);
        });
        addCopyMenu(&menu, {{tr("File name"), QFileInfo(filePath).fileName()},
                            {tr("File path"), nativePath(filePath)}});

        menu.addSeparator();
        if (hidden) {
          addAction(menu, tr("Un-hide"), svg(":/MO/gui/contextmenu/visibility-show.svg"),
                    [=] {
                      issueRestoreToView(row);
                    });
        } else {
          addAction(menu, tr("Hide"), svg(":/MO/gui/contextmenu/visibility-hide.svg"),
                    [=] {
                      issueRemoveFromView(row);
                    });
        }
        remove = [=] {
          issueDelete(row);
        };
      } else if (state == DownloadManager::STATE_DOWNLOADING) {
        addAction(menu, tr("Pause"), menuGlyph(0xE769), [=] {
          issuePause(row);
        });
        addAction(menu, tr("Cancel"), menuGlyph(0xE711), [=] {
          issueCancel(row);
        });
        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), [=] {
          issueOpenInDownloadsFolder(row);
        });
      } else if ((state == DownloadManager::STATE_PAUSED) ||
                 (state == DownloadManager::STATE_ERROR) ||
                 (state == DownloadManager::STATE_PAUSING)) {
        setDefaultMenuAction(&menu, addAction(menu, tr("Resume"), menuGlyph(MenuGlyphs::Run), [=] {
          issueResume(row);
        }));
        menu.addSeparator();
        addAction(menu, tr("Show in folder"), menuGlyph(MenuGlyphs::Folder), [=] {
          issueOpenInDownloadsFolder(row);
        });
        remove = [=] {
          issueDelete(row);
        };
      }
    }
  } catch (std::exception&) {
    // this happens when the download index is not found, ignore it and don't
    // display download-specific actions
  }

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
  }

  menu.exec(viewport()->mapToGlobal(point));
}

void DownloadListView::keyPressEvent(QKeyEvent* event)
{
  if (selectionModel()->hasSelection()) {
    const int row = qobject_cast<QSortFilterProxyModel*>(model())
                        ->mapToSource(currentIndex())
                        .row();
    // a pending row (an nxm link still resolving) has no state yet
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
        issueCancel(row);
      } else if (event->key() == Qt::Key_Space) {
        issuePause(row);
      }
    } else if (state == DownloadManager::STATE_PAUSED ||
               state == DownloadManager::STATE_ERROR ||
               state == DownloadManager::STATE_PAUSING) {
      if (event->key() == Qt::Key_Delete) {
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
  emit installDownload(index);
}

void DownloadListView::issueQueryInfo(int index)
{
  emit queryInfo(index);
}

void DownloadListView::issueQueryInfoMd5(int index)
{
  emit queryInfoMd5(index);
}

void DownloadListView::issueDelete(int index)
{
  // the list is rebuilt when a download finishes, possibly while the question
  // is open; delete the archive that was asked about, found again by name
  const QString fileName = m_Manager->getFileName(index);

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

  const int current = m_Manager->indexByName(fileName);
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

void DownloadListView::issueDeleteAll()
{
  if (QMessageBox::warning(
          nullptr, tr("Delete Files?"),
          tr("This will remove all finished downloads from this list and from "
             "disk.\n\nAre you absolutely sure you want to proceed?"),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-1, true);
  }
}

void DownloadListView::issueDeleteCompleted()
{
  if (QMessageBox::warning(
          nullptr, tr("Delete Files?"),
          tr("This will remove all installed downloads from this list and from "
             "disk.\n\nAre you absolutely sure you want to proceed?"),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-2, true);
  }
}

void DownloadListView::issueDeleteUninstalled()
{
  if (QMessageBox::warning(
          nullptr, tr("Delete Files?"),
          tr("This will remove all uninstalled downloads from this list and from "
             "disk.\n\nAre you absolutely sure you want to proceed?"),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-3, true);
  }
}

void DownloadListView::issueRemoveFromViewAll()
{
  if (QMessageBox::question(nullptr, tr("Hide Files?"),
                            tr("This will remove all finished downloads from this list "
                               "(but NOT from disk)."),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-1, false);
  }
}

void DownloadListView::issueRemoveFromViewCompleted()
{
  if (QMessageBox::question(nullptr, tr("Hide Files?"),
                            tr("This will remove all installed downloads from this "
                               "list (but NOT from disk)."),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-2, false);
  }
}

void DownloadListView::issueRemoveFromViewUninstalled()
{
  if (QMessageBox::question(nullptr, tr("Hide Files?"),
                            tr("This will remove all uninstalled downloads from this "
                               "list (but NOT from disk)."),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    emit removeDownload(-3, false);
  }
}

void DownloadListView::paintEvent(QPaintEvent* event)
{
  QTreeView::paintEvent(event);

  // a filter that hides every row is not an empty list
  const QAbstractItemModel* source = model();
  if (auto* proxy = qobject_cast<const QAbstractProxyModel*>(source)) {
    source = proxy->sourceModel();
  }
  if (source == nullptr || source->rowCount() > 0) {
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
  const QRect help(area.left(), title.bottom() + 4, area.width(), 44);
  p.drawText(help, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
             tr("Archives you download with Nexus or add with Install appear here. "
                "Hidden downloads appear when hidden downloads are shown."));
}
