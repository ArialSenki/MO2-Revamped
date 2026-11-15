#ifndef WABBAJACKDOWNLOADS_H
#define WABBAJACKDOWNLOADS_H

// MO2 Revamped: getting the archives of a Wabbajack list into a downloads
// folder, and knowing which of them are there already.
//
// Every archive is recognised by its xxHash64, so a folder used by Wabbajack
// or by another list serves again; the hashes of the files read are kept in
// <folder>/.mo2r/hashes.json so they are read once.
//
// Downloads go to "<name>.mo2r-part" beside the final file and are renamed when
// their hash is the list's; a stopped download goes on from where it was.
//  - Nexus: with Premium the API gives the link (one request per file, nothing
//    else is asked: the list says what the file is). Without Premium the user
//    presses "Slow download" on the file's page and the nxm link comes back
//    here, as Nexus requires.
//  - Direct downloads (HTTP): http and https only, with the list's headers.
//  - Wabbajack's servers: through the file's definition and its parts, each
//    part checked by hash, as Wabbajack does; never the direct links of the
//    authored files page.
// A file already in the folder under the archive's name but with another hash
// is moved to <folder>/.mo2r/replaced (with its .meta), never deleted.

#include "wabbajacklist.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QNetworkReply;

namespace Wabbajack
{

// the hashes of the files read in a folder, by name, size and time
class HashCache
{
public:
  explicit HashCache(const QString& folder = {});

  std::optional<uint64_t> find(const QString& path) const;
  void store(const QString& path, uint64_t hash);
  void save();

private:
  QString m_file;
  struct Known
  {
    qint64 size     = 0;
    qint64 modified = 0;
    uint64_t hash   = 0;
  };
  QHash<QString, Known> m_known;  // lowercase path
  bool m_dirty = false;
};

class DownloadQueue : public QObject
{
  Q_OBJECT

public:
  enum class Status
  {
    Unknown,      // not looked at yet
    Missing,      // not in the folder
    Present,      // a file of that name and size is there
    Verified,     // and its hash is the list's
    WrongHash,    // a file of that name, but not that file
    GameFound,    // a game file, with the list's hash
    GameDiffers,  // a game file, but another version of it
    GameMissing,  // the game file isn't there
    Queued,       // to download, its turn hasn't come
    NeedsClick,   // a free account: the user presses Slow download on Nexus
    Requesting,   // asking for its link (or the definition of its parts)
    Downloading,
    Verifying,    // downloaded, its hash being read
    Failed,
    ByHand,       // only in a browser (Mega, Google Drive...): a later version
    NotNeeded,    // Mod Organizer's own archive
    Unsupported   // a source, an address or a name MO2 Revamped doesn't take
  };

  struct Entry
  {
    Status status   = Status::Unknown;
    qint64 received = 0;
    qint64 total    = 0;
    QString reason;    // why it failed, or a note
    int retries = 0;   // tries after a failure, in this run
    QElapsedTimer retryAt;
    QElapsedTimer requestedAt;  // a Nexus link asked for
  };

  DownloadQueue(std::shared_ptr<const ModList> list, QString gameDirectory,
                QObject* parent = nullptr);
  ~DownloadQueue() override;

  const ModList& list() const { return *m_list; }
  const Entry& entry(int i) const { return m_entries[std::size_t(i)]; }
  int count() const { return int(m_entries.size()); }

  QString folder() const { return m_folder; }
  // looks at the new folder by name and size at once (hashes known from
  // before count); while hashing or downloading the folder can't change
  bool setFolder(const QString& folder);

  // reads the hashes of the files found by name and size, and of the game's
  // files; downloads wait while it runs
  void checkHashes();
  bool isChecking() const;
  void stopCheck();

  // downloads every archive it can that isn't verified yet
  void start();
  void pause();
  bool isRunning() const { return m_running; }
  void retryFailed();

  // a free account: opens the page of the next Nexus file waiting for a click
  bool openNext();
  void openPage(int i) const;
  // an nxm link from the browser for one of the files waited for; false when it
  // isn't one of them (the link then goes to the download manager as always)
  bool claimNxm(const QString& url);

  bool downloadsNexusByThemselves() const;
  // what is free in the folder's drive, -1 when not known
  qint64 freeSpace() const;
  // what is left to download, in bytes
  qint64 remaining() const;
  int countOf(Status s) const;
  // the archives the downloads folder has to hold, and how many are there
  int downloadable() const;
  int done() const;

  QString statusText(int i) const;

  // files only a browser gets (Google Drive, Mega, a page that wants a click):
  // the user downloads them, and they are taken from the folder watched (the
  // browser's Downloads) or from the file the user chooses, by their hash,
  // copied into the downloads folder
  void adoptFile(int i, const QString& path);
  void setWatchFolder(const QString& folder);
  QString watchFolder() const { return m_watchFolder; }
  // the archives the user has to download in the browser, not here yet
  int byHandCount() const;
  // whether a file can be chosen for this archive now
  bool canAdopt(int i) const;
  // the watched folder is looked at (not the downloads folder itself)
  bool isWatching() const;

  // the file of an archive in the folder; the game's file for a game file
  QString pathOf(int i) const;

signals:
  void changed();
  void entryChanged(int i);
  // a line for the window's footer
  void message(const QString& text);

private:
  struct Job;

  std::shared_ptr<const ModList> m_list;
  QString m_gameDirectory;
  QString m_folder;
  std::vector<Entry> m_entries;
  // why an archive can't be a file of the folder (its name), empty when it can
  std::vector<QString> m_badName;
  std::unique_ptr<HashCache> m_cache;
  QNetworkAccessManager m_network;
  std::map<int, std::unique_ptr<Job>> m_jobs;  // by archive
  quint64 m_nextJob = 1;
  bool m_running    = false;

  QFutureWatcher<void>* m_hasher = nullptr;
  std::shared_ptr<std::atomic_bool> m_cancelHash;
  // the hashes of downloads and of resumed parts: stopped on pause and exit
  std::shared_ptr<std::atomic_bool> m_cancelWork;
  // the Nexus API requests waited for: request id -> archive
  QHash<int, int> m_linkRequests;
  QTimer m_pump;
  QTimer m_saveCache;

  // the files to take in, one hashed at a time
  QTimer m_watch;
  QString m_watchFolder;
  QHash<QString, qint64> m_seen;  // files of the watched folder looked at, and when changed
  // size and time of the candidates at the last tick: taken when unchanged
  QHash<QString, QPair<qint64, qint64>> m_settling;
  struct Adoption
  {
    QString path;
    std::vector<int> rows;  // the archives of its size, one hash for all
    bool byUser = false;
  };
  std::vector<Adoption> m_adoptQueue;
  bool m_adopting = false;
  int m_adopted   = 0;
  // stops the hash of a file taken in: only when the queue goes
  std::shared_ptr<std::atomic_bool> m_cancelAdopt =
      std::make_shared<std::atomic_bool>(false);

  void scanFolder();
  void hashJobs(std::vector<std::pair<int, QString>> jobs, bool thenDownload);
  void pumpQueue();
  bool canDownload(int i) const;
  bool isAutomaticSource(const Archive& a) const;
  void setStatus(int i, Status s, const QString& reason = {});
  // the job of an archive when it is still the one with that id
  Job* jobOf(int i, quint64 id) const;

  void requestNexusLink(int i, const QString& query);
  void onApiResult(QVariant userData, QVariant resultData, int requestId);
  void onApiFailed(QString gameName, int modId, int fileId, QVariant userData,
                   int requestId, int errorCode, const QString& errorString);

  void downloadUrl(int i, const QUrl& url);
  void downloadCdn(int i);
  void startCdnParts(int i, quint64 id);
  void nextCdnPart(int i, quint64 id);
  void finishDownload(int i);
  // the part file of an archive becomes its file (another one in the way is
  // moved aside); the reason when it can't
  QString putInPlace(int i);
  bool needsByHand(int i) const;
  bool belongsInFolder(int i) const;
  void watchTick();
  void nextAdoption();
  void failDownload(int i, const QString& reason, bool retry = true);
  void verifyDownload(int i);
  void cancelJob(int i);
  QString partPath(int i) const;
  QNetworkRequest request(const QUrl& url) const;
};

}  // namespace Wabbajack

#endif  // WABBAJACKDOWNLOADS_H
