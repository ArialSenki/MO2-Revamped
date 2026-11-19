#ifndef DOWNLOADGROUPS_H
#define DOWNLOADGROUPS_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

// MO2 Revamped: what the Downloads tab groups by, per instance.
//
// The groups by origin come from what each download already records: its
// .meta (a Nexus mod, or a list name a list installer wrote as "sourceList"),
// or the instance's saved Nexus collections (collections/*.json list the
// files each one downloaded). The user's own categories, which downloads are
// in them and which groups are collapsed live in a small sidecar,
// downloadgroups.json, in the instance's folder: moving a download is one
// write, not one per .meta.
class DownloadGroups : public QObject
{
  Q_OBJECT

public:
  explicit DownloadGroups(QObject* parent = nullptr);
  ~DownloadGroups() override;

  // the instance whose sidecar and collections are used; reloads when it
  // changed
  void setInstanceFolder(const QString& folder);

  // whether the list is grouped at all
  bool grouped() const { return m_grouped; }
  void setGrouped(bool grouped);

  // the user's categories, in their order
  const QStringList& categories() const { return m_categories; }
  bool hasCategory(const QString& name) const;
  // false when the name is empty or taken
  bool addCategory(const QString& name);
  bool renameCategory(const QString& from, const QString& to);
  void removeCategory(const QString& name);

  // the category a download (by file name) was moved to, or empty
  QString categoryOf(const QString& fileName) const;
  // an empty category takes the download out of its category
  void setCategory(const QStringList& fileNames, const QString& category);

  // a download was deleted: it is in no category any more, so a new one of
  // that name starts where it comes from. No changed(): the list is about to
  // be rebuilt by the deletion itself
  void forget(const QString& fileName);

  bool isCollapsed(const QString& groupKey) const;
  void setCollapsed(const QString& groupKey, bool collapsed);

  // the Nexus collection a file came from, by its Nexus ids or archive name,
  // or empty; refreshLists() first reads the instance's saved collections
  // again when they changed on disk
  void refreshLists();
  QString listOf(int modId, int fileId, const QString& fileName) const;

  // writes the sidecar now if something changed
  void flush();

signals:
  // categories or assignments changed: the list is grouped again
  void changed();

private:
  QString m_folder;
  bool m_grouped = true;
  QStringList m_categories;
  // case-folded file name -> category
  QHash<QString, QString> m_assigned;
  QSet<QString> m_collapsed;

  // the saved collections, by file and their state files' times
  QHash<quint64, QString> m_listByIds;
  QHash<QString, QString> m_listByName;
  QHash<QString, QDateTime> m_listFiles;

  QTimer m_saveTimer;
  bool m_dirty = false;

  void load();
  void markDirty();
  void save();
  QString sidecarPath() const;
};

#endif  // DOWNLOADGROUPS_H
