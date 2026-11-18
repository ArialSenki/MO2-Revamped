#include "revampedtheme.h"
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

#include "downloadlist.h"
#include "downloadgroups.h"
#include "fluenticons.h"
#include "downloadmanager.h"
#include "modlistdropinfo.h"
#include "organizercore.h"
#include "settings.h"
#include <QColor>
#include <algorithm>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QMimeData>
#include <QDataStream>
#include <QSortFilterProxyModel>
#include <log.h>
#include <utility.h>

using namespace MOBase;

namespace
{
constexpr quintptr GroupFlag = quintptr(1) << (sizeof(quintptr) * 8 - 1);
}

DownloadList::DownloadList(OrganizerCore& core, QObject* parent)
    : QAbstractItemModel(parent), m_manager(*core.downloadManager()),
      m_settings(core.settings()), m_groups(new DownloadGroups(this))
{
  connect(&m_manager, SIGNAL(update(int)), this, SLOT(update(int)));
  connect(&m_manager, SIGNAL(aboutToUpdate()), this, SLOT(aboutToUpdate()));

  m_groups->setInstanceFolder(m_settings.paths().base());
  connect(m_groups, &DownloadGroups::changed, this, &DownloadList::regroup);
  buildGroups();
}

bool DownloadList::isGroup(const QModelIndex& index)
{
  return index.isValid() && (index.internalId() & GroupFlag) != 0;
}

int DownloadList::downloadRow(const QModelIndex& index) const
{
  if (!index.isValid() || isGroup(index)) {
    return -1;
  }
  return static_cast<int>(index.internalId());
}

void DownloadList::regroup()
{
  if (!m_resetting) {
    beginResetModel();
  }
  m_resetting = false;
  buildGroups();
  endResetModel();
}

void DownloadList::buildGroups()
{
  m_groupList.clear();
  m_groupOf.clear();
  m_placeOf.clear();

  // the instance can change without the model being made again
  m_groups->setInstanceFolder(m_settings.paths().base());
  m_grouped = m_groups->grouped();
  if (!m_grouped) {
    return;
  }

  TimeThis tt("DownloadList::buildGroups()");
  const int total   = m_manager.numTotalDownloads();
  const int pending = m_manager.numPendingDownloads();
  m_groupOf.assign(total + pending, -1);
  m_placeOf.assign(total + pending, -1);
  m_groups->refreshLists();

  // the user's categories first, in their order, even when empty: a
  // download can be moved into them
  QHash<QString, int> byKey;
  for (const QString& name : m_groups->categories()) {
    byKey.insert(QStringLiteral("cat:") + name, static_cast<int>(m_groupList.size()));
    m_groupList.push_back({QStringLiteral("cat:") + name, name, GROUP_CATEGORY, {}});
  }

  // then by origin; the lists are only known once their downloads are seen
  std::vector<Group> lists;
  QHash<QString, int> listByKey;
  Group nexus{QStringLiteral("nexus"), tr("Nexus Mods"), GROUP_NEXUS, {}};
  Group other{QStringLiteral("other"), tr("Other sites and manual"), GROUP_OTHER, {}};

  const auto add = [](Group& g, int row, qint64 size) {
    g.rows.push_back(row);
    g.size += size;
  };

  for (int row = 0; row < total; ++row) {
    const QString fileName = m_manager.getFileName(row);
    const qint64 size      = m_manager.getFileSize(row);

    const QString category = m_groups->categoryOf(fileName);
    if (!category.isEmpty()) {
      const auto it = byKey.constFind(QStringLiteral("cat:") + category);
      if (it != byKey.constEnd()) {
        add(m_groupList[it.value()], row, size);
        continue;
      }
    }

    const MOBase::ModRepositoryFileInfo* info = m_manager.getFileInfo(row);
    QString list = m_manager.getSourceList(row);
    if (list.isEmpty() && info != nullptr) {
      list = m_groups->listOf(info->modID, info->fileID, fileName);
    }
    if (!list.isEmpty()) {
      const QString key = QStringLiteral("list:") + list;
      auto it           = listByKey.constFind(key);
      if (it == listByKey.constEnd()) {
        it = listByKey.insert(key, static_cast<int>(lists.size()));
        lists.push_back({key, list, GROUP_LIST, {}});
      }
      add(lists[it.value()], row, size);
      continue;
    }

    // a Nexus download knows its mod; one from elsewhere or added by hand
    // doesn't
    const bool fromNexus =
        info != nullptr && info->modID > 0 &&
        (info->repository.isEmpty() ||
         info->repository.compare(QStringLiteral("Nexus"), Qt::CaseInsensitive) == 0);
    add(fromNexus ? nexus : other, row, size);
  }

  // a link still asking Nexus for its file
  for (int i = 0; i < pending; ++i) {
    add(nexus, total + i, 0);
  }

  std::sort(lists.begin(), lists.end(), [](const Group& a, const Group& b) {
    return a.name.localeAwareCompare(b.name) < 0;
  });
  for (auto& g : lists) {
    m_groupList.push_back(std::move(g));
  }
  if (!nexus.rows.empty()) {
    m_groupList.push_back(std::move(nexus));
  }
  if (!other.rows.empty()) {
    m_groupList.push_back(std::move(other));
  }

  for (int g = 0; g < static_cast<int>(m_groupList.size()); ++g) {
    const auto& rows = m_groupList[g].rows;
    for (int place = 0; place < static_cast<int>(rows.size()); ++place) {
      m_groupOf[rows[place]] = g;
      m_placeOf[rows[place]] = place;
    }
  }
}

int DownloadList::rowCount(const QModelIndex& parent) const
{
  if (!m_grouped) {
    if (!parent.isValid()) {
      // root item
      return m_manager.numTotalDownloads() + m_manager.numPendingDownloads();
    }
    return 0;
  }

  if (!parent.isValid()) {
    return static_cast<int>(m_groupList.size());
  }
  if (isGroup(parent) && parent.column() == 0) {
    const auto g = parent.internalId() & ~GroupFlag;
    if (g < m_groupList.size()) {
      return static_cast<int>(m_groupList[g].rows.size());
    }
  }
  return 0;
}

int DownloadList::columnCount(const QModelIndex&) const
{
  return COL_COUNT;
}

QModelIndex DownloadList::index(int row, int column, const QModelIndex& parent) const
{
  if (row < 0 || column < 0 || column >= COL_COUNT) {
    return {};
  }
  if (!m_grouped) {
    if (parent.isValid()) {
      return {};
    }
    return createIndex(row, column, quintptr(row));
  }

  if (!parent.isValid()) {
    if (row >= static_cast<int>(m_groupList.size())) {
      return {};
    }
    return createIndex(row, column, GroupFlag | quintptr(row));
  }
  if (!isGroup(parent)) {
    return {};
  }
  const auto g = parent.internalId() & ~GroupFlag;
  if (g >= m_groupList.size() || row >= static_cast<int>(m_groupList[g].rows.size())) {
    return {};
  }
  return createIndex(row, column, quintptr(m_groupList[g].rows[row]));
}

QModelIndex DownloadList::parent(const QModelIndex& child) const
{
  if (!m_grouped || !child.isValid() || isGroup(child)) {
    return {};
  }
  const auto row = child.internalId();
  if (row >= m_groupOf.size() || m_groupOf[row] < 0) {
    return {};
  }
  const int g = m_groupOf[row];
  return createIndex(g, 0, GroupFlag | quintptr(g));
}

QVariant DownloadList::headerData(int section, Qt::Orientation orientation,
                                  int role) const
{
  if ((role == Qt::DisplayRole) && (orientation == Qt::Horizontal)) {
    switch (section) {
    case COL_NAME:
      return tr("Name");
    case COL_MODNAME:
      return tr("Mod name");
    case COL_VERSION:
      return tr("Version");
    case COL_ID:
      return tr("Nexus ID");
    case COL_SIZE:
      return tr("Size");
    case COL_STATUS:
      return tr("Status");
    case COL_FILETIME:
      // the date the archive was downloaded (its file's creation time)
      return tr("Date downloaded");
    case COL_SOURCEGAME:
      return tr("Source Game");
    default:
      return QVariant();
    }
  } else {
    return QAbstractItemModel::headerData(section, orientation, role);
  }
}

Qt::ItemFlags DownloadList::flags(const QModelIndex& idx) const
{
  if (isGroup(idx)) {
    // a group takes downloads dropped on it; it is not one to install
    return Qt::ItemIsEnabled | Qt::ItemIsDropEnabled;
  }
  return QAbstractItemModel::flags(idx) | Qt::ItemIsDragEnabled;
}

QMimeData* DownloadList::mimeData(const QModelIndexList& indexes) const
{
  // the rows are the downloads' own (the mod list installs the one dropped),
  // not their places inside a group
  QByteArray encoded;
  QDataStream stream(&encoded, QIODevice::WriteOnly);
  for (const QModelIndex& index : indexes) {
    const int row = downloadRow(index);
    if (row < 0) {
      continue;
    }
    QMap<int, QVariant> roles;
    roles.insert(Qt::DisplayRole, index.data(Qt::DisplayRole));
    stream << row << index.column() << roles;
  }

  QMimeData* result = new QMimeData;
  result->setData("application/x-qabstractitemmodeldatalist", encoded);
  result->setData("text/plain", ModListDropInfo::DownloadText);
  return result;
}

QStringList DownloadList::mimeTypes() const
{
  return {QStringLiteral("application/x-qabstractitemmodeldatalist"),
          QStringLiteral("text/plain")};
}

QVariant DownloadList::groupData(const Group& group, const QModelIndex& index,
                                 int role) const
{
  switch (role) {
  case Qt::DisplayRole:
    if (index.column() == COL_NAME) {
      return QStringLiteral("%1 (%2)").arg(group.name).arg(group.rows.size());
    }
    return {};
  case Qt::ToolTipRole:
    switch (group.kind) {
    case GROUP_CATEGORY:
      return tr("Your category. Drag downloads here, or use \"Move to category\" in "
                "their menu.");
    case GROUP_LIST:
      return tr("Downloaded for the collection or list \"%1\".").arg(group.name);
    case GROUP_NEXUS:
      return tr("Downloaded from Nexus Mods.");
    case GROUP_OTHER:
      return tr("Downloaded from other sites, or added by hand.");
    }
    return {};
  case Qt::FontRole: {
    QFont font;
    font.setWeight(QFont::DemiBold);
    return font;
  }
  case GroupKeyRole:
    return group.key;
  case GroupKindRole:
    return static_cast<int>(group.kind);
  case GroupNameRole:
    return group.name;
  case GroupSizeRole:
    return MOBase::localizedByteSize(group.size);
  case DownloadRowRole:
    return -1;
  case Qt::TextAlignmentRole:
    return QVariant(Qt::AlignVCenter | Qt::AlignLeft);
  }
  return {};
}

QVariant DownloadList::data(const QModelIndex& index, int role) const
{
  if (isGroup(index)) {
    const auto g = index.internalId() & ~GroupFlag;
    return g < m_groupList.size() ? groupData(m_groupList[g], index, role) : QVariant();
  }

  const int row = downloadRow(index);
  if (row < 0 || row >= m_manager.numTotalDownloads() + m_manager.numPendingDownloads()) {
    return {};
  }
  if (role == DownloadRowRole) {
    return row;
  }

  bool pendingDownload = row >= m_manager.numTotalDownloads();
  if (role == Qt::DisplayRole) {
    if (pendingDownload) {
      std::tuple<QString, int, int> nexusids =
          m_manager.getPendingDownload(row - m_manager.numTotalDownloads());
      switch (index.column()) {
      case COL_NAME:
        return tr("Game %1, mod %2, file %3")
            .arg(std::get<0>(nexusids))
            .arg(std::get<1>(nexusids))
            .arg(std::get<2>(nexusids));
      case COL_SIZE:
        return tr("Unknown");
      case COL_STATUS:
        return tr("Pending");
      }
    } else {
      switch (index.column()) {
      case COL_NAME:
        return m_settings.interface().metaDownloads()
                   ? m_manager.getDisplayName(row)
                   : m_manager.getFileName(row);
      case COL_MODNAME: {
        if (m_manager.isInfoIncomplete(row)) {
          return {};
        } else {
          const MOBase::ModRepositoryFileInfo* info =
              m_manager.getFileInfo(row);
          return info->modName;
        }
      }
      case COL_VERSION: {
        if (m_manager.isInfoIncomplete(row)) {
          return {};
        } else {
          const MOBase::ModRepositoryFileInfo* info =
              m_manager.getFileInfo(row);
          return info->version.canonicalString();
        }
      }
      case COL_ID: {
        if (m_manager.isInfoIncomplete(row)) {
          return {};
        } else {
          return QString("%1").arg(m_manager.getModID(row));
        }
      }
      case COL_SOURCEGAME: {
        if (m_manager.isInfoIncomplete(row)) {
          return {};
        } else {
          return QString("%1").arg(m_manager.getDisplayGameName(row));
        }
      }
      case COL_SIZE:
        return MOBase::localizedByteSize(m_manager.getFileSize(row));
      case COL_FILETIME:
        return m_manager.getFileTime(row);
      case COL_STATUS:
        switch (m_manager.getState(row)) {
        // STATE_DOWNLOADING handled by DownloadProgressDelegate
        case DownloadManager::STATE_STARTED:
          return tr("Started");
        case DownloadManager::STATE_CANCELING:
          return tr("Canceling");
        case DownloadManager::STATE_PAUSING:
          return tr("Pausing");
        case DownloadManager::STATE_CANCELED:
          return tr("Canceled");
        case DownloadManager::STATE_PAUSED:
          return tr("Paused");
        case DownloadManager::STATE_ERROR:
          return tr("Error");
        case DownloadManager::STATE_FETCHINGMODINFO:
          return tr("Fetching Info");
        case DownloadManager::STATE_FETCHINGFILEINFO:
          return tr("Fetching Info");
        case DownloadManager::STATE_FETCHINGMODINFO_MD5:
          return tr("Fetching Info");
        // MO2 Revamped: a download from outside Nexus, done, on its way to ready
        case DownloadManager::STATE_NOFETCH:
        case DownloadManager::STATE_READY:
          return tr("Downloaded");
        case DownloadManager::STATE_INSTALLED:
          return tr("Installed");
        case DownloadManager::STATE_UNINSTALLED:
          return tr("Uninstalled");
        }
      }
    }
  } else if (role == Qt::ForegroundRole && index.column() == COL_STATUS) {
    if (pendingDownload) {
      return MOBase::RevampedTheme::infoText();
    } else {
      DownloadManager::DownloadState state = m_manager.getState(row);
      if (state == DownloadManager::STATE_READY)
        return MOBase::RevampedTheme::successText();
      else if (state == DownloadManager::STATE_UNINSTALLED)
        return MOBase::RevampedTheme::cautionText();
      // a pause is the user's, not a fault: amber; a failed download is red
      else if (state == DownloadManager::STATE_PAUSED)
        return MOBase::RevampedTheme::cautionText();
      else if (state == DownloadManager::STATE_ERROR)
        return MOBase::RevampedTheme::criticalText();
    }
  } else if (role == Qt::ToolTipRole) {
    if (pendingDownload) {
      return tr("Pending download");
    } else {
      // MO2 Revamped: an "Error" row says why it failed, and so does one paused
      // once its retries ran out
      QString failure;
      const auto rowState = m_manager.getState(row);
      if (rowState == DownloadManager::STATE_ERROR ||
          rowState == DownloadManager::STATE_PAUSED) {
        const QString lastError = m_manager.getLastError(row);
        if (!lastError.isEmpty()) {
          failure = tr("Download failed: %1").arg(lastError);
        }
      }

      QString text = m_manager.getFileName(row) + "\n";
      if (m_manager.isInfoIncomplete(row)) {
        text += tr("Information missing, please select \"Query Info\" from the context "
                   "menu to re-retrieve.");
      } else {
        const MOBase::ModRepositoryFileInfo* info = m_manager.getFileInfo(row);
        // the tool tip is rich text: the name is shown as written
        // one arg() call: a "%2" in the mod's name stays as written
        QString richText =
            QString("%1 (ID %2) %3<br><span>%4</span>")
                .arg(info->modName.toHtmlEscaped(),
                     QString::number(m_manager.getModID(row)),
                     info->version.canonicalString(), info->description.mid(0, 4096));
        if (!failure.isEmpty()) {
          richText += "<br><span>" + failure.toHtmlEscaped() + "</span>";
        }
        return richText;
      }
      if (!failure.isEmpty()) {
        text += "\n" + failure;
      }
      return text;
    }
  } else if (role == Qt::DecorationRole && index.column() == COL_NAME) {
    if (!pendingDownload &&
        m_manager.getState(row) >= DownloadManager::STATE_READY &&
        m_manager.isInfoIncomplete(row))
      return FluentIcons::icon(":/MO/gui/mainwindow/status/warning.svg");
    // Fluent: every archive gets its file icon, so the names line up
    return FluentIcons::file(QStringLiteral("archive.zip"), false);

  } else if (role == Qt::TextAlignmentRole) {
    return QVariant(Qt::AlignVCenter | Qt::AlignLeft);
  }
  return QVariant();
}

void DownloadList::aboutToUpdate()
{
  if (!m_resetting) {
    beginResetModel();
    m_resetting = true;
  }
}

void DownloadList::update(int row)
{
  // MO2 Revamped: the download manager also asks for a full update without
  // announcing it first; a reset is only ended once it began (Qt 6.8 and later
  // warn, and views can lose track of the rows otherwise)
  if (row < 0) {
    if (!m_resetting) {
      beginResetModel();
    }
    m_resetting = false;
    buildGroups();
    endResetModel();
    return;
  }

  const int total = m_manager.numTotalDownloads() + m_manager.numPendingDownloads();
  if (row >= total) {
    log::error("invalid row {} in download list, update failed", row);
    return;
  }

  if (!m_grouped) {
    emit dataChanged(this->index(row, 0, QModelIndex()),
                     this->index(row, COL_COUNT - 1, QModelIndex()));
    return;
  }

  // a download the groups don't know yet (added without a full update)
  if (row >= static_cast<int>(m_groupOf.size()) || m_groupOf[row] < 0) {
    regroup();
    return;
  }
  emit dataChanged(createIndex(m_placeOf[row], 0, quintptr(row)),
                   createIndex(m_placeOf[row], COL_COUNT - 1, quintptr(row)));
}

bool DownloadList::lessThanPredicate(const QModelIndex& left, const QModelIndex& right,
                                     Qt::SortOrder order)
{
  // groups stay in their order: the proxy turns the comparison around when
  // sorting down, so it is turned around here too
  if (isGroup(left) || isGroup(right)) {
    const bool less = left.row() < right.row();
    return order == Qt::AscendingOrder ? less : !less;
  }

  int leftIndex  = downloadRow(left);
  int rightIndex = downloadRow(right);
  if ((leftIndex < m_manager.numTotalDownloads()) &&
      (rightIndex < m_manager.numTotalDownloads())) {
    if (left.column() == DownloadList::COL_NAME) {
      return left.data(Qt::DisplayRole)
                 .toString()
                 .compare(right.data(Qt::DisplayRole).toString(), Qt::CaseInsensitive) <
             0;
    } else if (left.column() == DownloadList::COL_MODNAME) {
      QString leftName, rightName;

      if (!m_manager.isInfoIncomplete(leftIndex)) {
        leftName = m_manager.getFileInfo(leftIndex)->modName;
      }
      if (!m_manager.isInfoIncomplete(rightIndex)) {
        rightName = m_manager.getFileInfo(rightIndex)->modName;
      }

      return leftName.compare(rightName, Qt::CaseInsensitive) < 0;
    } else if (left.column() == DownloadList::COL_VERSION) {
      MOBase::VersionInfo versionLeft, versionRight;

      if (!m_manager.isInfoIncomplete(leftIndex)) {
        versionLeft = m_manager.getFileInfo(leftIndex)->version;
      }
      if (!m_manager.isInfoIncomplete(rightIndex)) {
        versionRight = m_manager.getFileInfo(rightIndex)->version;
      }

      return versionLeft < versionRight;
    } else if (left.column() == DownloadList::COL_ID) {
      int leftID = 0, rightID = 0;

      if (!m_manager.isInfoIncomplete(leftIndex)) {
        leftID = m_manager.getFileInfo(leftIndex)->modID;
      }
      if (!m_manager.isInfoIncomplete(rightIndex)) {
        rightID = m_manager.getFileInfo(rightIndex)->modID;
      }

      return leftID < rightID;
    } else if (left.column() == DownloadList::COL_STATUS) {
      DownloadManager::DownloadState leftState  = m_manager.getState(leftIndex);
      DownloadManager::DownloadState rightState = m_manager.getState(rightIndex);
      if (leftState == rightState)
        return m_manager.getFileTime(leftIndex) > m_manager.getFileTime(rightIndex);
      else
        return leftState < rightState;
    } else if (left.column() == DownloadList::COL_SIZE) {
      return m_manager.getFileSize(leftIndex) < m_manager.getFileSize(rightIndex);
    } else if (left.column() == DownloadList::COL_FILETIME) {
      return m_manager.getFileTime(leftIndex) < m_manager.getFileTime(rightIndex);
    } else if (left.column() == DownloadList::COL_SOURCEGAME) {
      return m_manager.getDisplayGameName(leftIndex) <
             m_manager.getDisplayGameName(rightIndex);
    } else {
      return leftIndex < rightIndex;
    }
  } else {
    return leftIndex < rightIndex;
  }
}
