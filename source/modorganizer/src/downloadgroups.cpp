#include "downloadgroups.h"

#include <QDir>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <log.h>

using namespace MOBase;

namespace
{
quint64 idsKey(int modId, int fileId)
{
  return (quint64(quint32(modId)) << 32) | quint32(fileId);
}

QString nameKey(const QString& fileName)
{
  return fileName.toCaseFolded();
}
}  // namespace

DownloadGroups::DownloadGroups(QObject* parent) : QObject(parent)
{
  // a burst of moves or collapses is one write
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(500);
  connect(&m_saveTimer, &QTimer::timeout, this, [this] {
    save();
  });
}

DownloadGroups::~DownloadGroups()
{
  flush();
}

void DownloadGroups::setInstanceFolder(const QString& folder)
{
  const QString clean = QDir::cleanPath(folder);
  if (clean.compare(m_folder, Qt::CaseInsensitive) == 0) {
    return;
  }
  flush();
  m_folder = clean;
  m_listFiles.clear();
  m_listByIds.clear();
  m_listByName.clear();
  load();
}

QString DownloadGroups::sidecarPath() const
{
  if (m_folder.isEmpty()) {
    return {};
  }
  return QDir(m_folder).filePath(QStringLiteral("downloadgroups.json"));
}

void DownloadGroups::load()
{
  m_grouped = true;
  m_categories.clear();
  m_assigned.clear();
  m_collapsed.clear();
  m_dirty = false;

  const QString path = sidecarPath();
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return;  // none yet
  }
  QJsonParseError error;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &error).object();
  if (error.error != QJsonParseError::NoError) {
    log::warn("{} couldn't be read: {}", path, error.errorString());
    return;
  }

  m_grouped = o.value("grouped").toBool(true);
  for (const auto& v : o.value("categories").toArray()) {
    const QString name = v.toString().trimmed();
    if (!name.isEmpty() && !hasCategory(name)) {
      m_categories.append(name);
    }
  }
  const QJsonObject assigned = o.value("assigned").toObject();
  for (auto it = assigned.begin(); it != assigned.end(); ++it) {
    const QString category = it.value().toString();
    if (hasCategory(category)) {
      m_assigned.insert(nameKey(it.key()), category);
    }
  }
  for (const auto& v : o.value("collapsed").toArray()) {
    m_collapsed.insert(v.toString());
  }
}

void DownloadGroups::markDirty()
{
  m_dirty = true;
  m_saveTimer.start();
}

void DownloadGroups::flush()
{
  m_saveTimer.stop();
  if (m_dirty) {
    save();
  }
}

void DownloadGroups::save()
{
  m_dirty = false;
  const QString path = sidecarPath();
  if (path.isEmpty()) {
    return;
  }

  QJsonObject o;
  o.insert("version", 1);
  o.insert("grouped", m_grouped);
  o.insert("categories", QJsonArray::fromStringList(m_categories));
  QJsonObject assigned;
  for (auto it = m_assigned.constBegin(); it != m_assigned.constEnd(); ++it) {
    assigned.insert(it.key(), it.value());
  }
  o.insert("assigned", assigned);
  QStringList collapsed(m_collapsed.begin(), m_collapsed.end());
  collapsed.sort();
  o.insert("collapsed", QJsonArray::fromStringList(collapsed));

  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly) ||
      f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
    log::warn("the download groups couldn't be saved to {}", path);
  }
}

void DownloadGroups::setGrouped(bool grouped)
{
  if (m_grouped == grouped) {
    return;
  }
  m_grouped = grouped;
  markDirty();
  emit changed();
}

bool DownloadGroups::hasCategory(const QString& name) const
{
  return m_categories.contains(name, Qt::CaseInsensitive);
}

bool DownloadGroups::addCategory(const QString& name)
{
  const QString clean = name.trimmed();
  if (clean.isEmpty() || hasCategory(clean)) {
    return false;
  }
  m_categories.append(clean);
  markDirty();
  emit changed();
  return true;
}

bool DownloadGroups::renameCategory(const QString& from, const QString& to)
{
  const QString clean = to.trimmed();
  const qsizetype at  = m_categories.indexOf(from);
  if (at < 0 || clean.isEmpty()) {
    return false;
  }
  // a new name may differ from the old one only in case
  if (clean.compare(from, Qt::CaseInsensitive) != 0 && hasCategory(clean)) {
    return false;
  }
  m_categories[at] = clean;
  for (auto it = m_assigned.begin(); it != m_assigned.end(); ++it) {
    if (it.value() == from) {
      it.value() = clean;
    }
  }
  if (m_collapsed.remove(QStringLiteral("cat:") + from)) {
    m_collapsed.insert(QStringLiteral("cat:") + clean);
  }
  markDirty();
  emit changed();
  return true;
}

void DownloadGroups::removeCategory(const QString& name)
{
  if (!m_categories.removeOne(name)) {
    return;
  }
  // its downloads go back to where they came from
  for (auto it = m_assigned.begin(); it != m_assigned.end();) {
    if (it.value() == name) {
      it = m_assigned.erase(it);
    } else {
      ++it;
    }
  }
  m_collapsed.remove(QStringLiteral("cat:") + name);
  markDirty();
  emit changed();
}

QString DownloadGroups::categoryOf(const QString& fileName) const
{
  if (m_assigned.isEmpty()) {
    return {};
  }
  return m_assigned.value(nameKey(fileName));
}

void DownloadGroups::setCategory(const QStringList& fileNames, const QString& category)
{
  if (!category.isEmpty() && !hasCategory(category)) {
    return;
  }
  bool any = false;
  for (const QString& name : fileNames) {
    const QString key = nameKey(name);
    if (category.isEmpty()) {
      any |= m_assigned.remove(key) > 0;
    } else if (m_assigned.value(key) != category) {
      m_assigned.insert(key, category);
      any = true;
    }
  }
  if (any) {
    markDirty();
    emit changed();
  }
}

bool DownloadGroups::isCollapsed(const QString& groupKey) const
{
  return m_collapsed.contains(groupKey);
}

void DownloadGroups::setCollapsed(const QString& groupKey, bool collapsed)
{
  const bool was = m_collapsed.contains(groupKey);
  if (was == collapsed) {
    return;
  }
  if (collapsed) {
    m_collapsed.insert(groupKey);
  } else {
    m_collapsed.remove(groupKey);
  }
  markDirty();
}

void DownloadGroups::refreshLists()
{
  if (m_folder.isEmpty()) {
    return;
  }
  const QDir dir(QDir(m_folder).filePath(QStringLiteral("collections")));
  QHash<QString, QDateTime> files;
  if (dir.exists()) {
    for (const QFileInfo& fi :
         dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
      if (fi.fileName().compare(QStringLiteral("pending.json"), Qt::CaseInsensitive) ==
          0) {
        continue;
      }
      files.insert(fi.absoluteFilePath(), fi.lastModified());
    }
  }
  // read again only when a collection was saved since
  if (files == m_listFiles) {
    return;
  }
  m_listFiles = files;
  m_listByIds.clear();
  m_listByName.clear();

  // oldest first: a file in two collections ends up in the newest
  QList<QString> paths = files.keys();
  std::sort(paths.begin(), paths.end(), [&](const QString& a, const QString& b) {
    return files.value(a) < files.value(b);
  });
  for (const QString& path : paths) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QString name        = o.value("name").toString().trimmed();
    if (name.isEmpty()) {
      name = o.value("slug").toString();
    }
    if (name.isEmpty()) {
      continue;
    }
    for (const auto& v : o.value("entries").toArray()) {
      const QJsonObject e = v.toObject();
      const int modId     = e.value("modId").toInt();
      const int fileId    = e.value("fileId").toInt();
      if (modId > 0 && fileId > 0) {
        m_listByIds.insert(idsKey(modId, fileId), name);
      }
      const QString archive = e.value("archive").toString();
      if (!archive.isEmpty()) {
        m_listByName.insert(nameKey(QFileInfo(archive).fileName()), name);
      }
    }
  }
}

QString DownloadGroups::listOf(int modId, int fileId, const QString& fileName) const
{
  if (modId > 0 && fileId > 0) {
    const auto it = m_listByIds.constFind(idsKey(modId, fileId));
    if (it != m_listByIds.constEnd()) {
      return it.value();
    }
  }
  if (!m_listByName.isEmpty()) {
    const auto it = m_listByName.constFind(nameKey(fileName));
    if (it != m_listByName.constEnd()) {
      return it.value();
    }
  }
  return {};
}
