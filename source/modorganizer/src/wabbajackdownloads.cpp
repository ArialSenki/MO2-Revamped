#include "wabbajackdownloads.h"

#include "nexusinterface.h"

#include <log.h>
#include <utility.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>

#include <zlib.h>

using namespace MOBase;

namespace Wabbajack
{

namespace
{

// at most this many archives download at the same time
constexpr int MaxParallel = 3;
// tries of an archive after a failure that may pass (the network), in one run
constexpr int MaxRetries = 3;
// tries of one part of a file from Wabbajack's servers
constexpr int MaxPartRetries = 4;
// a Nexus link not answered in this time is given up (the request was lost)
constexpr qint64 NexusAnswerMs = 180000;

constexpr char PartSuffix[]  = ".mo2r-part";
constexpr char StateFolder[] = ".mo2r";

QString stateFolder(const QString& folder)
{
  return QDir(folder).filePath(QString::fromLatin1(StateFolder));
}

// why a name from the list can't be a file of the downloads folder; empty when
// it can: no folders, nothing that leads out, nothing Windows refuses, none of
// the names this folder uses for itself
QString nameProblem(const QString& name)
{
  static const QRegularExpression device(
      QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
      QRegularExpression::CaseInsensitiveOption);
  static const QString forbidden = QStringLiteral("/\\:<>\"|?*");
  bool bad = name.isEmpty() || name == u"." || name == u".." ||
             name.endsWith(u'.') || name.endsWith(u' ') ||
             name.compare(QString::fromLatin1(StateFolder), Qt::CaseInsensitive) == 0 ||
             name.endsWith(QString::fromLatin1(PartSuffix), Qt::CaseInsensitive) ||
             device.match(name).hasMatch();
  for (const QChar c : name) {
    if (bad) {
      break;
    }
    bad = c.unicode() < 32 || forbidden.contains(c);
  }
  return bad ? QCoreApplication::translate(
                   "Wabbajack::DownloadQueue",
                   "The list gives this archive a name that can't be a file.")
             : QString();
}

// Wabbajack's old CDN host names, as its own downloader maps them
QUrl cdnUrl(const QString& url)
{
  QUrl u(url, QUrl::TolerantMode);
  static const QHash<QString, QString> remaps = {
      {"wabbajack.b-cdn.net", "authored-files.wabbajack.org"},
      {"wabbajack-mirror.b-cdn.net", "mirror.wabbajack.org"}};
  const QString host = u.host().toLower();
  if (remaps.contains(host)) {
    u.setHost(remaps.value(host));
  }
  return u;
}

bool isWabbajackCdnHost(const QUrl& url)
{
  const QString host = url.host().toLower();
  return url.scheme() == u"https" &&
         (host == u"authored-files.wabbajack.org" || host == u"mirror.wabbajack.org");
}

// a gzip stream, whole; nullopt when damaged or larger than a definition can be
std::optional<QByteArray> gunzip(const QByteArray& data)
{
  z_stream z{};
  if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) {
    return std::nullopt;
  }
  QByteArray out;
  char buffer[64 * 1024];
  z.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
  z.avail_in = uInt(data.size());
  int r      = Z_OK;
  while (r == Z_OK) {
    z.next_out  = reinterpret_cast<Bytef*>(buffer);
    z.avail_out = sizeof(buffer);
    r           = inflate(&z, Z_NO_FLUSH);
    if (r != Z_OK && r != Z_STREAM_END) {
      inflateEnd(&z);
      return std::nullopt;
    }
    out.append(buffer, qsizetype(sizeof(buffer) - z.avail_out));
    if (out.size() > 64 * 1024 * 1024) {  // a definition is a few KB
      inflateEnd(&z);
      return std::nullopt;
    }
  }
  inflateEnd(&z);
  return out;
}

// why a request failed, in the user's language: Qt's own texts are English
// only ("Operation canceled", "Host ... not found"); its words go to the log
QString networkErrorText(int httpStatus, QNetworkReply::NetworkError error,
                         const QString& raw)
{
  auto tr = [](const char* text) {
    return QCoreApplication::translate("Wabbajack::DownloadQueue", text);
  };
  log::debug("wabbajack: request failed: HTTP {}, error {}: {}", httpStatus, int(error), raw);
  if (httpStatus == 401 || httpStatus == 403) {
    return tr("The server refused the download (HTTP %1).").arg(httpStatus);
  }
  if (httpStatus == 404 || httpStatus == 410) {
    return tr("The file isn't on the server any more (HTTP %1).").arg(httpStatus);
  }
  if (httpStatus == 429) {
    return tr("The server asks to wait: too many requests.");
  }
  if (httpStatus >= 500 && httpStatus < 600) {
    return tr("The server had a problem (HTTP %1).").arg(httpStatus);
  }
  switch (error) {
  case QNetworkReply::OperationCanceledError:
  case QNetworkReply::TimeoutError:
    return tr("The server stopped answering.");
  case QNetworkReply::ConnectionRefusedError:
  case QNetworkReply::RemoteHostClosedError:
  case QNetworkReply::HostNotFoundError:
  case QNetworkReply::TemporaryNetworkFailureError:
  case QNetworkReply::NetworkSessionFailedError:
  case QNetworkReply::UnknownNetworkError:
    return tr("The connection failed: no network, or the server can't be reached.");
  case QNetworkReply::SslHandshakeFailedError:
    return tr("The secure connection failed (a certificate problem).");
  default:
    break;
  }
  if (httpStatus >= 400) {
    return tr("The server answered with an error (HTTP %1).").arg(httpStatus);
  }
  return tr("Network error: %1").arg(raw);
}

QString networkErrorText(const QNetworkReply* reply)
{
  return networkErrorText(
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->error(),
      reply->errorString());
}

QString speedText(qint64 bytesPerSecond)
{
  return QCoreApplication::translate("Wabbajack::DownloadQueue", "%1/s")
      .arg(localizedByteSize(qMax<qint64>(0, bytesPerSecond)));
}

// a header a direct download may send: a plain name, none that would carry
// credentials or change how the transfer works
bool isAllowedHeader(const QString& name)
{
  static const QRegularExpression token(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
  static const QSet<QString> refused = {
      "cookie",     "authorization",     "proxy-authorization", "host",
      "range",      "content-length",    "accept-encoding",     "connection",
      "transfer-encoding", "te",         "expect",              "upgrade",
      "keep-alive", "proxy-connection"};
  const QString lower = name.toLower();
  return token.match(name).hasMatch() && !refused.contains(lower) &&
         !lower.startsWith(u"if-") && !lower.startsWith(u"sec-") &&
         !lower.startsWith(u"proxy-");
}

}  // namespace

// ---- the hash cache -------------------------------------------------------

HashCache::HashCache(const QString& folder)
{
  if (folder.isEmpty()) {
    return;
  }
  m_file = QDir(stateFolder(folder)).filePath("hashes.json");
  QFile f(m_file);
  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject all = QJsonDocument::fromJson(f.readAll()).object();
  for (auto it = all.begin(); it != all.end(); ++it) {
    const QJsonObject o = it.value().toObject();
    bool ok             = false;
    const uint64_t hash = hashFromBase64(o.value("h").toString(), &ok);
    if (ok) {
      m_known.insert(it.key(), Known{qint64(o.value("s").toDouble()),
                                     qint64(o.value("m").toDouble()), hash});
    }
  }
}

std::optional<uint64_t> HashCache::find(const QString& path) const
{
  const auto it = m_known.constFind(QDir::cleanPath(path).toLower());
  if (it == m_known.constEnd()) {
    return std::nullopt;
  }
  const QFileInfo fi(path);
  if (!fi.isFile() || fi.size() != it->size ||
      fi.lastModified().toMSecsSinceEpoch() != it->modified) {
    return std::nullopt;
  }
  return it->hash;
}

void HashCache::store(const QString& path, uint64_t hash)
{
  const QFileInfo fi(path);
  if (!fi.isFile() || m_file.isEmpty()) {
    return;
  }
  m_known.insert(QDir::cleanPath(path).toLower(),
                 Known{fi.size(), fi.lastModified().toMSecsSinceEpoch(), hash});
  m_dirty = true;
}

void HashCache::save()
{
  if (!m_dirty || m_file.isEmpty()) {
    return;
  }
  QJsonObject all;
  for (auto it = m_known.constBegin(); it != m_known.constEnd(); ++it) {
    // files that are gone are left out
    if (!QFileInfo::exists(it.key())) {
      continue;
    }
    all.insert(it.key(), QJsonObject{{"s", double(it->size)},
                                     {"m", double(it->modified)},
                                     {"h", hashToBase64(it->hash)}});
  }
  QDir().mkpath(QFileInfo(m_file).absolutePath());
  QSaveFile f(m_file);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
    if (f.commit()) {
      m_dirty = false;
    }
  }
}

// ---- a download -----------------------------------------------------------

struct DownloadQueue::Job
{
  // every job has its own id: a reply, a timer or a hash that finishes after
  // its job was replaced by another one for the same archive finds out so
  quint64 id = 0;
  QPointer<QNetworkReply> reply;
  QFile file;
  qint64 offset = 0;  // bytes in the part file when this request began
  QElapsedTimer shown;    // the last progress shown
  QElapsedTimer speedAt;  // the speed, measured over a second or so
  qint64 speedFrom = 0;
  qint64 speed     = 0;

  // Wabbajack's servers: the file's parts
  QString cdnBase;
  struct Part
  {
    int index;
    qint64 offset;
    qint64 size;
    uint64_t hash;
    bool done;
  };
  std::vector<Part> parts;
  std::size_t part = 0;  // the part being downloaded
  int partRetries  = 0;
  QByteArray partData;

  // its hashing off the window's thread stops with the job (a pause, then
  // Download again, doesn't leave the old one reading the same file)
  std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
  ~Job() { cancel->store(true); }
};

// ---- the queue ------------------------------------------------------------

DownloadQueue::DownloadQueue(std::shared_ptr<const ModList> list, QString gameDirectory,
                             QObject* parent)
    : QObject(parent), m_list(std::move(list)), m_gameDirectory(std::move(gameDirectory)),
      m_entries(m_list->archives.size()), m_badName(m_list->archives.size()),
      m_cache(std::make_unique<HashCache>()),
      m_cancelHash(std::make_shared<std::atomic_bool>(false)),
      m_cancelWork(std::make_shared<std::atomic_bool>(false))
{
  // the names: each one a file of the folder, and two archives never one file
  QSet<QString> seen;
  for (std::size_t n = 0; n < m_list->archives.size(); ++n) {
    const Archive& a = m_list->archives[n];
    if (a.source == Source::GameFile) {
      continue;
    }
    m_badName[n] = nameProblem(a.name);
    if (m_badName[n].isEmpty() && !a.isModOrganizer) {
      const QString key = a.name.toLower();
      if (seen.contains(key)) {
        m_badName[n] = tr("Another archive of the list has the same name.");
      }
      seen.insert(key);
    }
  }

  m_network.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
  m_network.setTransferTimeout(60000);

  m_pump.setInterval(1000);
  connect(&m_pump, &QTimer::timeout, this, &DownloadQueue::pumpQueue);
  m_saveCache.setInterval(5000);
  m_saveCache.setSingleShot(true);
  connect(&m_saveCache, &QTimer::timeout, this, [this] {
    m_cache->save();
  });

  auto& nexus = NexusInterface::instance();
  connect(&nexus, &NexusInterface::nxmApiV2Available, this, &DownloadQueue::onApiResult);
  connect(&nexus, &NexusInterface::nxmRequestFailed, this, &DownloadQueue::onApiFailed);
  // a request dropped at the rate limit never gets an answer of its own
  connect(&nexus, &NexusInterface::nxmRequestDropped, this, &DownloadQueue::onApiFailed);
}

DownloadQueue::~DownloadQueue()
{
  m_cancelHash->store(true);
  m_cancelWork->store(true);
  m_watch.stop();
  m_cancelAdopt->store(true);
  if (m_hasher != nullptr) {
    m_hasher->waitForFinished();
  }
  for (auto& [i, job] : m_jobs) {
    if (job->reply) {
      job->reply->disconnect(this);
      job->reply->abort();
    }
  }
  m_jobs.clear();
  m_cache->save();
}

DownloadQueue::Job* DownloadQueue::jobOf(int i, quint64 id) const
{
  const auto it = m_jobs.find(i);
  return it != m_jobs.end() && it->second->id == id ? it->second.get() : nullptr;
}

QString DownloadQueue::pathOf(int i) const
{
  const Archive& a = m_list->archives[std::size_t(i)];
  if (a.source == Source::GameFile) {
    const QString relative = QDir::fromNativeSeparators(a.gameFile);
    if (m_gameDirectory.isEmpty() || relative.isEmpty() || QDir::isAbsolutePath(relative) ||
        relative.contains(u':') || relative.split(u'/').contains(QStringLiteral(".."))) {
      return {};
    }
    return QDir(m_gameDirectory).filePath(relative);
  }
  if (m_folder.isEmpty() || !m_badName[std::size_t(i)].isEmpty()) {
    return {};
  }
  return QDir(m_folder).filePath(a.name);
}

QString DownloadQueue::partPath(int i) const
{
  const QString path = pathOf(i);
  return path.isEmpty() ? QString() : path + QString::fromLatin1(PartSuffix);
}

bool DownloadQueue::setFolder(const QString& folder)
{
  if (m_running || isChecking() || m_adopting || !m_adoptQueue.empty()) {
    return false;
  }
  const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(folder.trimmed()));
  if (clean.compare(m_folder, Qt::CaseInsensitive) != 0) {
    m_cache->save();
    m_folder = clean;
    m_cache  = std::make_unique<HashCache>(m_folder);
    for (Entry& e : m_entries) {
      e = Entry{};
    }
  }
  scanFolder();
  return true;
}

void DownloadQueue::setStatus(int i, Status s, const QString& reason)
{
  Entry& e = m_entries[std::size_t(i)];
  e.status = s;
  e.reason = reason;
  emit entryChanged(i);
}

bool DownloadQueue::isAutomaticSource(const Archive& a) const
{
  return a.source == Source::Nexus || a.source == Source::Http ||
         a.source == Source::WabbajackCdn;
}

bool DownloadQueue::canDownload(int i) const
{
  const Archive& a = m_list->archives[std::size_t(i)];
  if (a.isModOrganizer || !m_badName[std::size_t(i)].isEmpty()) {
    return false;
  }
  switch (a.source) {
  case Source::Nexus:
    return a.modId > 0 && a.fileId > 0;
  case Source::Http: {
    const QUrl url(a.url);
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == u"http" || scheme == u"https") &&
           !url.host().isEmpty();
  }
  case Source::WabbajackCdn:
    return isWabbajackCdnHost(cdnUrl(a.url));
  default:
    return false;
  }
}

void DownloadQueue::scanFolder()
{
  QHash<QString, qint64> present;
  const QDir dir(m_folder);
  if (!m_folder.isEmpty() && dir.exists()) {
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Hidden)) {
      present.insert(fi.fileName().toLower(), fi.size());
    }
  }

  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    const int i      = int(n);
    const Archive& a = m_list->archives[n];
    Entry& e         = m_entries[n];
    // a run's own entries stay as they are while it runs
    if (m_jobs.count(i) > 0 ||
        (m_running && (e.status == Status::Queued || e.status == Status::NeedsClick ||
                       e.status == Status::Requesting || e.status == Status::Failed))) {
      continue;
    }
    e.total = a.size;
    e.reason.clear();

    if (a.isModOrganizer) {
      e.status = Status::NotNeeded;
      continue;
    }
    if (a.source == Source::GameFile) {
      const QString path = pathOf(i);
      const QFileInfo fi(path);
      if (path.isEmpty() || !fi.isFile()) {
        e.status = Status::GameMissing;
      } else if (a.size > 0 && fi.size() != a.size) {
        e.status = Status::GameDiffers;
      } else if (const auto h = m_cache->find(path)) {
        e.status = *h == a.hash ? Status::GameFound : Status::GameDiffers;
      } else if (e.status != Status::GameFound && e.status != Status::GameDiffers) {
        e.status = Status::Unknown;
      }
      continue;
    }
    if (!m_badName[n].isEmpty()) {
      e.status = Status::Unsupported;
      e.reason = m_badName[n];
      continue;
    }

    const auto it = present.constFind(a.name.toLower());
    if (it != present.constEnd() && (a.size <= 0 || it.value() == a.size)) {
      if (const auto h = m_cache->find(pathOf(i))) {
        e.status = *h == a.hash ? Status::Verified : Status::WrongHash;
      } else if (e.status != Status::Verified && e.status != Status::WrongHash) {
        e.status = Status::Present;
      }
    } else if (a.source == Source::Unknown) {
      e.status = Status::Unsupported;
    } else if (!canDownload(i)) {
      if (isAutomaticSource(a)) {
        // a source MO2 Revamped downloads from, but not this address
        e.status = Status::Unsupported;
        e.reason = tr("MO2 Revamped doesn't download from this address.");
      } else {
        e.status = Status::ByHand;
      }
    } else {
      e.status = Status::Missing;
      // what a stopped download left (a Wabbajack-server one is made at its
      // full size: its parts say what is there, when it goes on)
      const QFileInfo part(partPath(i));
      e.received = part.isFile() && a.source != Source::WabbajackCdn
                       ? qMin(part.size(), a.size)
                       : 0;
    }
  }
  emit changed();
}

void DownloadQueue::stopCheck()
{
  m_cancelHash->store(true);
}

bool DownloadQueue::isChecking() const
{
  return m_hasher != nullptr && m_hasher->isRunning();
}

void DownloadQueue::checkHashes()
{
  if (isChecking() || m_running) {
    return;
  }
  scanFolder();
  std::vector<std::pair<int, QString>> jobs;
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    const Status s = m_entries[n].status;
    if (s == Status::Present ||
        (s == Status::Unknown && m_list->archives[n].source == Source::GameFile)) {
      const QString path = pathOf(int(n));
      if (!path.isEmpty()) {
        jobs.emplace_back(int(n), path);
      }
    }
  }
  if (jobs.empty()) {
    emit message(tr("Nothing to check: no archive of the list is in the folder."));
    return;
  }
  hashJobs(std::move(jobs), false);
}

void DownloadQueue::hashJobs(std::vector<std::pair<int, QString>> jobs, bool thenDownload)
{
  qint64 total = 0;
  for (const auto& [i, path] : jobs) {
    total += m_list->archives[std::size_t(i)].size;
  }
  m_cancelHash->store(false);
  emit message(tr("Checking %n files…", nullptr, int(jobs.size())));

  auto cancel   = m_cancelHash;
  auto* watcher = new QFutureWatcher<void>(this);
  m_hasher      = watcher;
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, thenDownload] {
    watcher->deleteLater();
    if (m_hasher != watcher) {
      return;
    }
    m_hasher = nullptr;
    m_saveCache.start();
    int good = 0, wrong = 0;
    for (const Entry& e : m_entries) {
      if (e.status == Status::Verified || e.status == Status::GameFound) {
        ++good;
      } else if (e.status == Status::WrongHash || e.status == Status::GameDiffers) {
        ++wrong;
      }
    }
    if (m_cancelHash->load()) {
      emit message(tr("Check stopped."));
    } else if (wrong > 0) {
      emit message(tr("%n files verified.", nullptr, good) + QStringLiteral(" ") +
                   tr("%n don't match the list.", nullptr, wrong));
    } else {
      emit message(tr("%n files verified.", nullptr, good));
    }
    emit changed();
    if (thenDownload && !m_cancelHash->load()) {
      start();
    }
  });
  emit changed();

  // the queue waits for this before it goes, so the pointer stays valid
  DownloadQueue* queue = this;
  watcher->setFuture(QtConcurrent::run([jobs = std::move(jobs), cancel, queue, total] {
    const LowPriority low(LowPriority::Mode::Background);
    qint64 done = 0;
    for (const auto& [row, path] : jobs) {
      if (cancel->load()) {
        return;
      }
      std::optional<uint64_t> hash;
      try {
        hash = hashFile(path, cancel.get());
      } catch (...) {
        hash.reset();
      }
      if (cancel->load()) {
        return;
      }
      done += queue->m_list->archives[std::size_t(row)].size;
      const int percent = total > 0 ? int(done * 100 / total) : 100;
      QMetaObject::invokeMethod(
          queue,
          [queue, row, path, hash, percent] {
            if (queue->m_entries[std::size_t(row)].status == Status::Verified &&
                queue->m_list->archives[std::size_t(row)].source != Source::GameFile) {
              return;  // put in place meanwhile (a file taken in): this read was the old one
            }
            const Archive& a = queue->m_list->archives[std::size_t(row)];
            const bool game  = a.source == Source::GameFile;
            Status s;
            if (!hash) {
              s = game ? Status::GameMissing : Status::Missing;
            } else {
              queue->m_cache->store(path, *hash);
              s = *hash == a.hash ? (game ? Status::GameFound : Status::Verified)
                                  : (game ? Status::GameDiffers : Status::WrongHash);
            }
            queue->setStatus(row, s);
            emit queue->message(tr("Checking… %1%").arg(percent));
          },
          Qt::QueuedConnection);
    }
  }));
}

// ---- running --------------------------------------------------------------

bool DownloadQueue::downloadsNexusByThemselves() const
{
  // an account not known yet (the sign-in at MO2's start) is tried as Premium,
  // as the collections do: the API's answer says if it isn't, and the file
  // then waits for a click
  const auto account = NexusInterface::instance().getAPIUserAccount();
  return !account.isValid() || account.type() == APIUserAccountTypes::Premium;
}

bool DownloadQueue::belongsInFolder(int i) const
{
  // what the downloads folder has to hold: downloaded here or in the browser
  const Archive& a = m_list->archives[std::size_t(i)];
  return !a.isModOrganizer && a.source != Source::GameFile && a.source != Source::Unknown &&
         m_badName[std::size_t(i)].isEmpty();
}

qint64 DownloadQueue::remaining() const
{
  qint64 left = 0;
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    const Entry& e = m_entries[n];
    if (belongsInFolder(int(n)) && e.status != Status::Verified) {
      left += qMax<qint64>(0, m_list->archives[n].size - e.received);
    }
  }
  return left;
}

qint64 DownloadQueue::freeSpace() const
{
  if (m_folder.isEmpty()) {
    return -1;
  }
  // the folder may not exist yet: its nearest parent that does
  QString existing = m_folder;
  while (!QFileInfo(existing).isDir() && QFileInfo(existing).absolutePath() != existing) {
    existing = QFileInfo(existing).absolutePath();
  }
  const QStorageInfo info(existing);
  return info.isValid() ? info.bytesAvailable() : -1;
}

int DownloadQueue::countOf(Status s) const
{
  int n = 0;
  for (const Entry& e : m_entries) {
    if (e.status == s) {
      ++n;
    }
  }
  return n;
}

int DownloadQueue::downloadable() const
{
  int n = 0;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    if (belongsInFolder(int(i))) {
      ++n;
    }
  }
  return n;
}

int DownloadQueue::done() const
{
  int n = 0;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    if (belongsInFolder(int(i)) && m_entries[i].status == Status::Verified) {
      ++n;
    }
  }
  return n;
}

void DownloadQueue::start()
{
  if (m_folder.isEmpty()) {
    emit message(tr("Choose a downloads folder first."));
    return;
  }
  if (isChecking() || m_running) {
    return;
  }

  // what is in the folder now: a file the user put there since counts
  scanFolder();

  // files found by name and size are read first: they may be the right ones
  std::vector<std::pair<int, QString>> unchecked;
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    if (m_entries[n].status == Status::Present && canDownload(int(n))) {
      unchecked.emplace_back(int(n), pathOf(int(n)));
    }
  }
  if (!unchecked.empty()) {
    hashJobs(std::move(unchecked), true);
    return;
  }

  if (!QDir().mkpath(m_folder)) {
    emit message(tr("The downloads folder can't be made: %1")
                     .arg(QDir::toNativeSeparators(m_folder)));
    return;
  }

  const qint64 free   = freeSpace();
  const qint64 needed = remaining();
  if (free >= 0 && needed > free) {
    emit message(tr("Not enough free space: the downloads need %1 and the drive has %2.")
                     .arg(localizedByteSize(needed))
                     .arg(localizedByteSize(free)));
    return;
  }

  const bool premium = downloadsNexusByThemselves();
  int queued         = 0;
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    Entry& e         = m_entries[n];
    const Archive& a = m_list->archives[n];
    if (!canDownload(int(n)) || e.status == Status::Verified || m_jobs.count(int(n)) > 0) {
      continue;
    }
    e.retries = 0;
    e.reason.clear();
    e.status = (a.source == Source::Nexus && !premium) ? Status::NeedsClick : Status::Queued;
    ++queued;
    emit entryChanged(int(n));
  }

  if (queued == 0) {
    emit message(tr("Every archive MO2 Revamped can download is here."));
    emit changed();
    return;
  }
  m_running = true;
  m_cancelWork->store(false);
  m_pump.start();
  log::info("wabbajack: downloading {} archives of '{}' to {}", queued, m_list->name,
            m_folder);
  emit changed();
  pumpQueue();
}

void DownloadQueue::pause()
{
  const bool wasRunning = m_running;
  m_running             = false;
  m_pump.stop();
  m_cancelHash->store(true);
  m_cancelWork->store(true);
  std::vector<int> busy;
  for (auto& [i, job] : m_jobs) {
    busy.push_back(i);
  }
  for (int i : busy) {
    cancelJob(i);
  }
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    Entry& e = m_entries[n];
    if (e.status == Status::Queued || e.status == Status::NeedsClick ||
        e.status == Status::Requesting || e.status == Status::Downloading ||
        e.status == Status::Verifying) {
      e.status = Status::Missing;
      const QFileInfo part(partPath(int(n)));
      e.received = part.isFile() && m_list->archives[n].source != Source::WabbajackCdn
                       ? qMin(part.size(), m_list->archives[n].size)
                       : 0;
      emit entryChanged(int(n));
    }
  }
  m_linkRequests.clear();
  m_cache->save();
  if (wasRunning) {
    emit message(tr("Paused. The downloads go on from where they were."));
  }
  emit changed();
}

void DownloadQueue::retryFailed()
{
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    Entry& e = m_entries[n];
    if (e.status == Status::Failed) {
      e.status  = Status::Missing;
      e.retries = 0;
      e.reason.clear();
      emit entryChanged(int(n));
    }
  }
  start();
}

void DownloadQueue::pumpQueue()
{
  if (!m_running) {
    return;
  }

  // Nexus links that never came back (a request lost on the way)
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    Entry& e = m_entries[n];
    if (e.status == Status::Requesting && m_jobs.count(int(n)) == 0 &&
        e.requestedAt.isValid() && e.requestedAt.elapsed() > NexusAnswerMs) {
      for (auto it = m_linkRequests.begin(); it != m_linkRequests.end();) {
        it = it.value() == int(n) ? m_linkRequests.erase(it) : std::next(it);
      }
      failDownload(int(n), tr("Nexus didn't answer."));
      if (!m_running) {
        return;
      }
    }
  }

  // what runs: downloads, hashes, the definitions of Wabbajack's files and
  // the Nexus links asked for (each becomes a download): a list of thousands
  // of Nexus files asks for a few links at a time, not all of them at once
  int running = 0;
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    const Status s = m_entries[n].status;
    if (s == Status::Downloading || s == Status::Verifying || s == Status::Requesting) {
      ++running;
    }
  }

  for (std::size_t n = 0; n < m_entries.size() && running < MaxParallel && m_running; ++n) {
    Entry& e = m_entries[n];
    if (e.status != Status::Queued ||
        (e.retryAt.isValid() && e.retryAt.elapsed() < 5000LL * e.retries)) {
      continue;
    }
    const Archive& a = m_list->archives[n];
    const int i      = int(n);
    switch (a.source) {
    case Source::Nexus:
      setStatus(i, Status::Requesting);
      requestNexusLink(i, {});
      ++running;
      break;
    case Source::Http:
      downloadUrl(i, QUrl(a.url));
      ++running;
      break;
    case Source::WabbajackCdn:
      downloadCdn(i);
      ++running;
      break;
    default:
      setStatus(i, Status::ByHand);
      break;
    }
  }
  if (!m_running) {
    return;  // stopped on the way (the request limit): its message stays
  }

  // the end of the run: nothing waits, nothing runs
  for (const Entry& e : m_entries) {
    if (e.status == Status::Queued || e.status == Status::NeedsClick ||
        e.status == Status::Requesting || e.status == Status::Downloading ||
        e.status == Status::Verifying) {
      return;
    }
  }
  m_running = false;
  m_pump.stop();
  m_cache->save();
  const int failed = countOf(Status::Failed);
  if (failed > 0) {
    emit message(tr("Downloads finished; %n failed.", nullptr, failed));
  } else {
    emit message(tr("Every archive MO2 Revamped can download is here."));
  }
  emit changed();
  emit runFinished();
}

// ---- Nexus ----------------------------------------------------------------

namespace
{
QString nexusDomain(const Archive& a)
{
  const auto game = gameInfo(a.nexusGame);
  return game && !game->nexusDomain.isEmpty() ? game->nexusDomain : a.nexusGame.toLower();
}
}  // namespace

bool DownloadQueue::openNext()
{
  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    if (m_entries[n].status == Status::NeedsClick) {
      openPage(int(n));
      return true;
    }
  }
  return false;
}

void DownloadQueue::openPage(int i) const
{
  const Archive& a = m_list->archives[std::size_t(i)];
  if (a.source == Source::Nexus) {
    const QString domain = nexusDomain(a);
    if (a.modId <= 0 || domain.isEmpty()) {
      return;
    }
    // the file's own page: "Slow download" there sends its nxm link here
    shell::Open(QUrl(QStringLiteral("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3&nmm=1")
                         .arg(QString::fromUtf8(QUrl::toPercentEncoding(domain)))
                         .arg(a.modId)
                         .arg(a.fileId)));
  } else if (a.source == Source::GoogleDrive) {
    if (!a.url.isEmpty()) {
      shell::Open(QUrl(QStringLiteral("https://drive.google.com/file/d/%1/view")
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(a.url)))));
    }
  } else {
    const QUrl url(a.url);
    const QString scheme = url.scheme().toLower();
    if (url.isValid() && (scheme == u"http" || scheme == u"https")) {
      shell::Open(url);
    }
  }
}

bool DownloadQueue::claimNxm(const QString& url)
{
  static const QRegularExpression exp(
      QStringLiteral("^nxm://([^/]+)/mods/(\\d+)/files/(\\d+)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto m = exp.match(url.trimmed());
  if (!m.hasMatch() || !m_running) {
    return false;
  }
  const QString domain = m.captured(1).toLower();
  const qint64 modId   = m.captured(2).toLongLong();
  const qint64 fileId  = m.captured(3).toLongLong();
  const QUrlQuery query(QUrl(url.trimmed()).query());
  const QString key     = query.queryItemValue("key");
  const QString expires = query.queryItemValue("expires");

  for (std::size_t n = 0; n < m_entries.size(); ++n) {
    const Archive& a = m_list->archives[n];
    Entry& e         = m_entries[n];
    if (a.source != Source::Nexus || a.modId != modId || a.fileId != fileId ||
        nexusDomain(a) != domain) {
      continue;
    }
    if (e.status == Status::Requesting || e.status == Status::Downloading ||
        e.status == Status::Verifying) {
      return true;  // a second click on a file already coming
    }
    if (e.status != Status::NeedsClick && e.status != Status::Queued) {
      continue;
    }
    QString q;
    if (!key.isEmpty() && !expires.isEmpty()) {
      QUrlQuery free;
      free.addQueryItem("key", key);
      free.addQueryItem("expires", expires);
      q = QStringLiteral("?") + free.toString(QUrl::FullyEncoded);
    }
    // Premium's links carry no key: the API gives the file all the same
    setStatus(int(n), Status::Requesting);
    requestNexusLink(int(n), q);
    emit changed();
    return true;
  }
  return false;
}

void DownloadQueue::requestNexusLink(int i, const QString& query)
{
  const Archive& a = m_list->archives[std::size_t(i)];
  // only the file the list names, on its own game's site: one request
  const QString path =
      QStringLiteral("/v1/games/%1/mods/%2/files/%3/download_link.json%4")
          .arg(QString::fromUtf8(QUrl::toPercentEncoding(nexusDomain(a))))
          .arg(a.modId)
          .arg(a.fileId)
          .arg(query);
  const int id = NexusInterface::instance().requestApiPath(path, QVariant(i),
                                                           QStringLiteral("wabbajack"));
  if (id < 0) {
    // the request limit: everything that would ask Nexus waits for the user
    failDownload(i,
                 tr("Nexus's request limit was reached. Continue later; nothing is "
                    "lost."),
                 false);
    pause();
    emit message(tr("Nexus's request limit was reached. Continue later; nothing is "
                    "lost."));
    return;
  }
  m_entries[std::size_t(i)].requestedAt.start();
  m_linkRequests.insert(id, i);
}

void DownloadQueue::onApiResult(QVariant, QVariant resultData, int requestId)
{
  const auto it = m_linkRequests.find(requestId);
  if (it == m_linkRequests.end()) {
    return;  // a request of someone else
  }
  const int i = it.value();
  m_linkRequests.erase(it);
  if (m_entries[std::size_t(i)].status != Status::Requesting || m_jobs.count(i) > 0) {
    return;  // paused meanwhile
  }

  // [{"name": ..., "short_name": ..., "URI": ...}, ...], best server first
  QString link;
  for (const QVariant& v : resultData.toList()) {
    const QVariantMap map = v.toMap();
    link                  = map.value("URI", map.value("uri")).toString();
    if (!link.isEmpty()) {
      break;
    }
  }
  const QUrl url(link);
  if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
    failDownload(i, tr("Nexus gave no link to download it."), false);
    return;
  }
  downloadUrl(i, url);
}

void DownloadQueue::onApiFailed(QString, int, int, QVariant, int requestId, int errorCode,
                                const QString& errorString)
{
  const auto it = m_linkRequests.find(requestId);
  if (it == m_linkRequests.end()) {
    return;
  }
  const int i = it.value();
  m_linkRequests.erase(it);
  if (m_entries[std::size_t(i)].status != Status::Requesting) {
    return;
  }
  log::warn("wabbajack: Nexus link for '{}' failed ({}): {}",
            m_list->archives[std::size_t(i)].name, errorCode, errorString);

  if (errorCode == 429) {
    // the request limit: nothing more is asked until the user goes on
    failDownload(i, networkErrorText(errorCode, QNetworkReply::NoError, errorString), false);
    pause();
    emit message(tr("Nexus's request limit was reached. Continue later; nothing is "
                    "lost."));
    return;
  }
  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.isValid() && account.type() != APIUserAccountTypes::Premium) {
    // a free link that expired, or another account's: one more click
    setStatus(i, Status::NeedsClick,
              networkErrorText(errorCode, QNetworkReply::NoError, errorString));
    emit changed();
    return;
  }
  // 401/403: the key or the file isn't allowed; 404: the file is gone. Asking
  // again only spends requests
  const bool final = errorCode == 401 || errorCode == 403 || errorCode == 404 ||
                     errorCode == 410;
  failDownload(i, networkErrorText(errorCode, QNetworkReply::NoError, errorString), !final);
}

// ---- HTTP -----------------------------------------------------------------

QNetworkRequest DownloadQueue::request(const QUrl& url) const
{
  QNetworkRequest r(url);
  r.setHeader(QNetworkRequest::UserAgentHeader,
              QStringLiteral("MO2-Revamped/%1").arg(QApplication::applicationVersion()));
  r.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                 QNetworkRequest::AlwaysNetwork);
  r.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  // the bytes as they are: a server that compresses (GitHub does) would send a
  // resumed download as a piece of a gzip stream, which can't be unpacked
  r.setRawHeader("Accept-Encoding", "identity");
  return r;
}

void DownloadQueue::downloadUrl(int i, const QUrl& url)
{
  const Archive& a   = m_list->archives[std::size_t(i)];
  const QString part = partPath(i);
  if (part.isEmpty()) {
    failDownload(i, tr("The list gives this archive a name that can't be a file."), false);
    return;
  }

  auto job = std::make_unique<Job>();
  job->id  = m_nextJob++;
  job->file.setFileName(part);
  qint64 have = job->file.exists() ? job->file.size() : 0;
  if (a.size > 0 && have > a.size) {
    have = 0;  // not this file
  }
  if (!job->file.open(have > 0 ? QIODevice::Append : QIODevice::WriteOnly)) {
    failDownload(i, tr("%1 can't be written.").arg(QDir::toNativeSeparators(part)), false);
    return;
  }
  if (have == 0) {
    job->file.resize(0);
  }
  job->offset = have;

  Entry& e   = m_entries[std::size_t(i)];
  e.received = have;
  e.total    = a.size;

  if (a.size > 0 && have == a.size) {
    // all here already: only its hash is left
    job->file.close();
    m_jobs[i] = std::move(job);
    verifyDownload(i);
    return;
  }

  QNetworkRequest r = request(url);
  // a retry goes over HTTP/1.1, a connection of its own: a reply cut short
  // comes from a shared HTTP/2 one that broke
  if (e.retries > 0) {
    r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
  }
  // a direct download's own headers ("Name: value", a referrer or an agent some
  // sites want); the links Nexus gives get none
  if (a.source == Source::Http) {
    for (const QString& h : a.headers) {
      const int colon = h.indexOf(u':');
      if (colon <= 0) {
        continue;
      }
      const QString name  = h.left(colon).trimmed();
      const QString value = h.mid(colon + 1).trimmed();
      if (isAllowedHeader(name) && !value.contains(u'\n') && !value.contains(u'\r')) {
        r.setRawHeader(name.toLatin1(), value.toUtf8());
      }
    }
  }
  if (have > 0) {
    r.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(have) + "-");
  }

  const quint64 id     = job->id;
  QNetworkReply* reply = m_network.get(r);
  job->reply           = reply;
  job->shown.start();
  job->speedAt.start();
  job->speedFrom = have;
  m_jobs[i]      = std::move(job);
  setStatus(i, Status::Downloading);

  connect(reply, &QNetworkReply::metaDataChanged, this, [this, i, id, reply] {
    Job* j = jobOf(i, id);
    if (j == nullptr || j->reply != reply) {
      return;
    }
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (j->offset > 0 && code == 200) {
      // the server sends the whole file again: start over
      j->file.resize(0);
      j->file.seek(0);
      j->offset                          = 0;
      j->speedFrom                       = 0;
      m_entries[std::size_t(i)].received = 0;
    } else if (code == 206) {
      // the rest from where the part file ends, or nothing of it
      static const QRegularExpression range(QStringLiteral("^bytes\\s+(\\d+)-"));
      const auto m = range.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
      if (!m.hasMatch() || m.captured(1).toLongLong() != j->offset) {
        QFile::remove(j->file.fileName());
        m_entries[std::size_t(i)].received = 0;
        failDownload(i, tr("The server sent another part of the file than the one asked for."));
      }
    }
  });

  connect(reply, &QNetworkReply::readyRead, this, [this, i, id, reply] {
    Job* j = jobOf(i, id);
    if (j == nullptr || j->reply != reply) {
      return;
    }
    const QByteArray chunk = reply->readAll();
    // an error's page (a 403 of an expired link, a 503) isn't the file: it
    // stays out of the part file, which resumes from what it has
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code != 200 && code != 206) {
      return;
    }
    Entry& e = m_entries[std::size_t(i)];
    const qint64 size      = m_list->archives[std::size_t(i)].size;
    if (size > 0 && e.received + chunk.size() > size) {
      // more than the file the list names: not it
      failDownload(i, tr("The download has another size than the list's (%1 instead of %2).")
                          .arg(localizedByteSize(e.received + chunk.size()))
                          .arg(localizedByteSize(size)),
                   false);
      return;
    }
    if (j->file.write(chunk) != chunk.size()) {
      failDownload(i, tr("The disk is full, or %1 can't be written.")
                          .arg(QDir::toNativeSeparators(j->file.fileName())),
                   false);
      return;
    }
    e.received += chunk.size();
    if (j->speedAt.elapsed() >= 1000) {
      j->speed     = (e.received - j->speedFrom) * 1000 / qMax<qint64>(1, j->speedAt.elapsed());
      j->speedFrom = e.received;
      j->speedAt.restart();
    }
    if (j->shown.elapsed() >= 300) {
      j->shown.restart();
      emit entryChanged(i);
    }
  });

  connect(reply, &QNetworkReply::finished, this, [this, i, id, reply] {
    reply->deleteLater();
    Job* j = jobOf(i, id);
    if (j == nullptr || j->reply != reply) {
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      j->file.close();
      if (code == 416 && j->offset > 0) {
        // the part is complete, or the server has another file: its hash says
        verifyDownload(i);
        return;
      }
      failDownload(i, networkErrorText(reply),
                   code != 401 && code != 403 && code != 404 && code != 410);
      return;
    }
    j->file.close();
    const Archive& a   = m_list->archives[std::size_t(i)];
    const qint64 size  = j->file.size();
    const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (a.size > 0 && size < a.size && !type.startsWith(u"text/html")) {
      // the connection ended before the end, "without error" (a reset HTTP/2
      // stream): what came stays, and the next try goes on from there
      log::warn("wabbajack: '{}' ended at {} of {} bytes (HTTP/2 {})", a.name, size, a.size,
                reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool());
      m_entries[std::size_t(i)].received = size;
      failDownload(i, tr("The connection ended before the whole file came."));
      return;
    }
    if (a.size > 0 && size != a.size) {
      QFile::remove(j->file.fileName());
      m_entries[std::size_t(i)].received = 0;
      // what came isn't the file: asking again brings the same
      failDownload(i, type.startsWith(u"text/html")
                          ? tr("The site sent a web page instead of the file: download "
                               "it in the browser.")
                          : tr("The download has another size than the list's (%1 "
                               "instead of %2).")
                                .arg(localizedByteSize(size))
                                .arg(localizedByteSize(a.size)),
                   false);
      return;
    }
    verifyDownload(i);
  });
}

// ---- Wabbajack's servers --------------------------------------------------

void DownloadQueue::downloadCdn(int i)
{
  const Archive& a   = m_list->archives[std::size_t(i)];
  const QUrl base    = cdnUrl(a.url);
  const QString part = partPath(i);
  if (part.isEmpty() || !isWabbajackCdnHost(base)) {
    failDownload(i, tr("The list gives this archive an address that isn't Wabbajack's."),
                 false);
    return;
  }

  auto job     = std::make_unique<Job>();
  job->id      = m_nextJob++;
  job->cdnBase = base.toString(QUrl::FullyEncoded);
  job->file.setFileName(part);
  job->shown.start();
  job->speedAt.start();
  const quint64 id = job->id;
  QNetworkReply* reply =
      m_network.get(request(QUrl(job->cdnBase + QStringLiteral("/definition.json.gz"))));
  job->reply = reply;
  m_jobs[i]  = std::move(job);
  setStatus(i, Status::Requesting);

  connect(reply, &QNetworkReply::readyRead, this, [this, i, id, reply] {
    // a definition is a few KB: anything far larger isn't one
    if (jobOf(i, id) != nullptr && reply->bytesAvailable() > 16 * 1024 * 1024) {
      failDownload(i, tr("Wabbajack's servers describe another file than the list's."),
                   false);
    }
  });

  connect(reply, &QNetworkReply::finished, this, [this, i, id, reply] {
    reply->deleteLater();
    Job* j = jobOf(i, id);
    if (j == nullptr || j->reply != reply) {
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      failDownload(i, networkErrorText(reply));
      return;
    }
    const auto json     = gunzip(reply->readAll());
    const QJsonObject d = json ? QJsonDocument::fromJson(*json).object() : QJsonObject();
    const Archive& a    = m_list->archives[std::size_t(i)];
    const qint64 size   = qint64(d.value("Size").toDouble());
    const QString wrong = tr("Wabbajack's servers describe another file than the list's.");
    if (d.isEmpty() || size <= 0 || (a.size > 0 && size != a.size) ||
        hashFromBase64(d.value("Hash").toString()) != a.hash) {
      failDownload(i, wrong, false);
      return;
    }
    j->parts.clear();
    qint64 covered = 0;
    for (const QJsonValue& v : d.value("Parts").toArray()) {
      const QJsonObject p = v.toObject();
      const qint64 offset = qint64(p.value("Offset").toDouble());
      const qint64 psize  = qint64(p.value("Size").toDouble());
      if (offset < 0 || psize <= 0 || psize > 256LL * 1024 * 1024 || offset + psize > size) {
        failDownload(i, wrong, false);
        return;
      }
      covered += psize;
      j->parts.push_back({p.value("Index").toInt(), offset, psize,
                          hashFromBase64(p.value("Hash").toString()), false});
    }
    if (j->parts.empty() || covered != size) {
      failDownload(i, wrong, false);
      return;
    }
    startCdnParts(i, id);
  });
}

void DownloadQueue::startCdnParts(int i, quint64 id)
{
  Job* j = jobOf(i, id);
  if (j == nullptr) {
    return;
  }
  j->reply.clear();
  const Archive& a   = m_list->archives[std::size_t(i)];
  const bool existed = j->file.exists() && j->file.size() == a.size;
  Entry& e           = m_entries[std::size_t(i)];
  e.received         = 0;
  e.total            = a.size;
  setStatus(i, Status::Downloading);

  auto begin = [this, i, id] {
    Job* j = jobOf(i, id);
    if (j == nullptr) {
      return;
    }
    const Archive& a = m_list->archives[std::size_t(i)];
    if (!j->file.open(QIODevice::ReadWrite)) {
      failDownload(i, tr("%1 can't be written.").arg(QDir::toNativeSeparators(j->file.fileName())),
                   false);
      return;
    }
    if (j->file.size() != a.size && !j->file.resize(a.size)) {
      failDownload(i, tr("The disk is full, or %1 can't be written.")
                          .arg(QDir::toNativeSeparators(j->file.fileName())),
                   false);
      return;
    }
    j->speedFrom = m_entries[std::size_t(i)].received;
    j->speedAt.restart();
    j->part = 0;
    nextCdnPart(i, id);
  };

  if (!existed) {
    QFile::remove(j->file.fileName());  // ours, of another size: not this file
    begin();
    return;
  }

  // the parts a stopped download wrote already: read back and checked, off
  // the window's thread (a file can be gigabytes)
  struct Range
  {
    qint64 offset;
    qint64 size;
    uint64_t hash;
  };
  std::vector<Range> ranges;
  for (const Job::Part& p : j->parts) {
    ranges.push_back({p.offset, p.size, p.hash});
  }
  const QString path = j->file.fileName();
  auto cancel        = j->cancel;
  auto* watcher      = new QFutureWatcher<std::vector<bool>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, i, id, watcher, begin] {
    watcher->deleteLater();
    Job* j = jobOf(i, id);
    if (j == nullptr) {
      return;
    }
    const std::vector<bool> good = watcher->result();
    Entry& e                     = m_entries[std::size_t(i)];
    for (std::size_t n = 0; n < j->parts.size() && n < good.size(); ++n) {
      j->parts[n].done = good[n];
      if (good[n]) {
        e.received += j->parts[n].size;
      }
    }
    emit entryChanged(i);
    begin();
  });
  watcher->setFuture(QtConcurrent::run([path, ranges, cancel] {
    const LowPriority low(LowPriority::Mode::Background);
    std::vector<bool> good(ranges.size(), false);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      return good;
    }
    for (std::size_t n = 0; n < ranges.size() && !cancel->load(); ++n) {
      if (!f.seek(ranges[n].offset)) {
        continue;
      }
      const QByteArray data = f.read(ranges[n].size);
      good[n]               = data.size() == ranges[n].size &&
                XxHash64::of(data.constData(), std::size_t(data.size())) == ranges[n].hash;
    }
    return good;
  }));
}

void DownloadQueue::nextCdnPart(int i, quint64 id)
{
  Job* j = jobOf(i, id);
  if (j == nullptr || !m_running) {
    return;
  }
  while (j->part < j->parts.size() && j->parts[j->part].done) {
    ++j->part;
  }
  if (j->part >= j->parts.size()) {
    j->file.close();
    verifyDownload(i);
    return;
  }

  const Job::Part& p = j->parts[j->part];
  QNetworkReply* reply =
      m_network.get(request(QUrl(j->cdnBase + QStringLiteral("/parts/%1").arg(p.index))));
  j->reply = reply;
  j->partData.clear();

  connect(reply, &QNetworkReply::readyRead, this, [this, i, id, reply] {
    Job* j = jobOf(i, id);
    if (j == nullptr || j->reply != reply) {
      return;
    }
    const QByteArray chunk = reply->readAll();
    if (j->partData.size() + chunk.size() > j->parts[j->part].size) {
      // more than the part: not it; the finished handler retries it
      reply->abort();
      return;
    }
    j->partData.append(chunk);
    Entry& e = m_entries[std::size_t(i)];
    e.received += chunk.size();
    if (j->speedAt.elapsed() >= 1000) {
      j->speed     = (e.received - j->speedFrom) * 1000 / qMax<qint64>(1, j->speedAt.elapsed());
      j->speedFrom = e.received;
      j->speedAt.restart();
    }
    if (j->shown.elapsed() >= 300) {
      j->shown.restart();
      emit entryChanged(i);
    }
  });

  connect(reply, &QNetworkReply::finished, this, [this, i, id, reply] {
    reply->deleteLater();
    Job* j = jobOf(i, id);
    if (j == nullptr || j->reply != reply) {
      return;
    }
    Job::Part& p    = j->parts[j->part];
    Entry& e        = m_entries[std::size_t(i)];
    const bool good = reply->error() == QNetworkReply::NoError &&
                      j->partData.size() == p.size &&
                      XxHash64::of(j->partData.constData(),
                                   std::size_t(j->partData.size())) == p.hash;
    if (!good) {
      e.received -= j->partData.size();
      j->partData.clear();
      if (++j->partRetries > MaxPartRetries) {
        j->file.close();
        failDownload(i, reply->error() != QNetworkReply::NoError
                            ? networkErrorText(reply)
                            : tr("A part of the file kept arriving damaged."));
        return;
      }
      // the same part again, a little later, if this job is still the one
      QTimer::singleShot(2000 * j->partRetries, this, [this, i, id] {
        nextCdnPart(i, id);
      });
      return;
    }
    j->file.seek(p.offset);
    if (j->file.write(j->partData) != p.size) {
      j->file.close();
      failDownload(i, tr("The disk is full, or %1 can't be written.")
                          .arg(QDir::toNativeSeparators(j->file.fileName())),
                   false);
      return;
    }
    p.done         = true;
    j->partRetries = 0;
    j->partData.clear();
    nextCdnPart(i, id);
  });
}

// ---- the end of a download ------------------------------------------------

void DownloadQueue::verifyDownload(int i)
{
  auto it = m_jobs.find(i);
  if (it == m_jobs.end()) {
    return;
  }
  Job& j           = *it->second;
  const quint64 id = j.id;
  j.reply.clear();
  if (j.file.isOpen()) {
    j.file.close();
  }
  setStatus(i, Status::Verifying);

  const QString part = j.file.fileName();
  auto cancel        = j.cancel;
  auto* watcher      = new QFutureWatcher<std::optional<uint64_t>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, i, id, watcher, part] {
    watcher->deleteLater();
    if (jobOf(i, id) == nullptr || m_entries[std::size_t(i)].status != Status::Verifying) {
      return;  // paused meanwhile, or another run's
    }
    const auto hash  = watcher->result();
    const Archive& a = m_list->archives[std::size_t(i)];
    if (!hash) {
      failDownload(i, tr("The downloaded file can't be read."));
      return;
    }
    if (*hash != a.hash) {
      QFile::remove(part);
      m_entries[std::size_t(i)].received = 0;
      // the source has another file than the list's: asking again brings it again
      failDownload(i, tr("The downloaded file isn't the list's (its hash differs)."), false);
      return;
    }
    finishDownload(i);
  });
  watcher->setFuture(QtConcurrent::run([part, cancel]() -> std::optional<uint64_t> {
    const LowPriority low(LowPriority::Mode::Background);
    try {
      return hashFile(part, cancel.get());
    } catch (...) {
      return std::nullopt;
    }
  }));
}

void DownloadQueue::finishDownload(int i)
{
  m_jobs.erase(i);
  const QString problem = putInPlace(i);
  if (!problem.isEmpty()) {
    failDownload(i, problem, false);
    return;
  }
  QTimer::singleShot(0, this, &DownloadQueue::pumpQueue);
}

QString DownloadQueue::putInPlace(int i)
{
  const Archive& a     = m_list->archives[std::size_t(i)];
  const QString target = pathOf(i);
  const QString part   = partPath(i);

  // a file already under that name is another one: kept aside with its .meta,
  // never deleted
  const QString meta = target + QStringLiteral(".meta");
  if (QFileInfo::exists(target)) {
    const QString aside = QDir(stateFolder(m_folder)).filePath("replaced");
    QDir().mkpath(aside);
    QString moved = QDir(aside).filePath(a.name);
    for (int n = 2; QFileInfo::exists(moved); ++n) {
      moved = QDir(aside).filePath(QStringLiteral("%1 (%2)").arg(a.name).arg(n));
    }
    if (!QFile::rename(target, moved)) {
      return tr("%1 is in the way and can't be moved aside.")
          .arg(QDir::toNativeSeparators(target));
    }
    if (QFileInfo::exists(meta)) {
      QFile::rename(meta, moved + QStringLiteral(".meta"));
    }
    log::info("wabbajack: '{}' had another hash, moved to {}", target, moved);
  }
  if (!QFile::rename(part, target)) {
    return tr("%1 can't be renamed.").arg(QDir::toNativeSeparators(part));
  }
  m_cache->store(target, a.hash);
  m_saveCache.start();

  // the .meta MO2 reads to know where an archive came from, as the list has it
  if (!a.meta.isEmpty() && !QFileInfo::exists(meta)) {
    QSaveFile f(meta);
    if (f.open(QIODevice::WriteOnly)) {
      f.write(a.meta.toUtf8());
      f.commit();
    }
  }

  Entry& e   = m_entries[std::size_t(i)];
  e.received = a.size;
  setStatus(i, Status::Verified);
  emit changed();
  return {};
}

// ---- files downloaded by hand -----------------------------------------------

bool DownloadQueue::needsByHand(int i) const
{
  const Archive& a = m_list->archives[std::size_t(i)];
  const Status s   = m_entries[std::size_t(i)].status;
  if (!belongsInFolder(i)) {
    return false;
  }
  // only a browser gets it, or MO2 Revamped's own try failed
  return s == Status::ByHand || s == Status::Failed || s == Status::WrongHash ||
         (s == Status::Unsupported && a.source != Source::Unknown);
}

bool DownloadQueue::canAdopt(int i) const
{
  return belongsInFolder(i) && m_entries[std::size_t(i)].status != Status::Verified &&
         m_jobs.count(i) == 0;
}

int DownloadQueue::byHandCount() const
{
  int n = 0;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    if (needsByHand(int(i))) {
      ++n;
    }
  }
  return n;
}

bool DownloadQueue::isWatching() const
{
  if (m_watchFolder.isEmpty() || m_folder.isEmpty()) {
    return false;
  }
  // the same folder under another spelling (a junction, subst) is the same
  const QString watch     = QFileInfo(m_watchFolder).canonicalFilePath();
  const QString downloads = QFileInfo(m_folder).canonicalFilePath();
  return !watch.isEmpty() &&
         watch.compare(downloads.isEmpty() ? m_folder : downloads, Qt::CaseInsensitive) != 0;
}

void DownloadQueue::setWatchFolder(const QString& folder)
{
  m_watchFolder = QDir::cleanPath(QDir::fromNativeSeparators(folder));
  m_seen.clear();
  m_settling.clear();
  if (!m_watch.isActive()) {
    m_watch.setInterval(4000);
    connect(&m_watch, &QTimer::timeout, this, &DownloadQueue::watchTick,
            Qt::UniqueConnection);
    m_watch.start();
  }
  watchTick();
}

void DownloadQueue::watchTick()
{
  if (!isWatching()) {
    return;
  }
  // the sizes of the files waited for: a file of another size isn't one
  QHash<qint64, std::vector<int>> wanted;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    if (needsByHand(int(i)) && m_list->archives[i].size > 0) {
      wanted[m_list->archives[i].size].push_back(int(i));
    }
  }
  if (wanted.isEmpty()) {
    m_settling.clear();
    return;
  }

  // what browsers write while downloading: never touched
  static const QStringList unfinished = {".crdownload", ".part", ".partial", ".download",
                                         ".opdownload", ".tmp", ".!ut"};
  QHash<QString, QPair<qint64, qint64>> settling;
  for (const QFileInfo& fi : QDir(m_watchFolder).entryInfoList(QDir::Files)) {
    const QString name = fi.fileName().toLower();
    if (std::any_of(unfinished.begin(), unfinished.end(), [&](const QString& s) {
          return name.endsWith(s);
        })) {
      continue;
    }
    const auto it = wanted.constFind(fi.size());
    if (it == wanted.constEnd()) {
      continue;
    }
    const QString path    = fi.absoluteFilePath();
    const qint64 modified = fi.lastModified().toMSecsSinceEpoch();
    if (m_seen.value(path, -1) == modified) {
      continue;
    }
    // taken only once it hasn't changed for a whole tick: a browser done with it
    const auto now = qMakePair(fi.size(), modified);
    settling.insert(path, now);
    if (m_settling.value(path) != now) {
      continue;
    }
    m_seen.insert(path, modified);
    m_adoptQueue.push_back(Adoption{path, it.value(), false});
  }
  m_settling = settling;
  nextAdoption();
}

void DownloadQueue::adoptFile(int i, const QString& path)
{
  if (!canAdopt(i)) {
    return;
  }
  // the archive's own file, or MO2 Revamped's unfinished download of it
  const QString canonical = QFileInfo(path).canonicalFilePath();
  const QString target    = QFileInfo(pathOf(i)).canonicalFilePath();
  const QString part      = QFileInfo(partPath(i)).canonicalFilePath();
  if (!canonical.isEmpty() && canonical.compare(part, Qt::CaseInsensitive) == 0) {
    emit message(tr("That is MO2 Revamped's own unfinished download of %1.")
                     .arg(m_list->archives[std::size_t(i)].name));
    return;
  }
  if (!canonical.isEmpty() && canonical.compare(target, Qt::CaseInsensitive) == 0) {
    // already where it goes: only its hash is read
    if (!isChecking()) {
      hashJobs({{i, pathOf(i)}}, false);
    }
    return;
  }
  m_adoptQueue.push_back(Adoption{path, {i}, true});
  nextAdoption();
}

void DownloadQueue::nextAdoption()
{
  if (m_adopting || m_adoptQueue.empty()) {
    return;
  }
  // a hash check or a download writes the same entries: it goes first
  if (isChecking()) {
    QTimer::singleShot(1000, this, &DownloadQueue::nextAdoption);
    return;
  }
  Adoption job = m_adoptQueue.front();
  m_adoptQueue.erase(m_adoptQueue.begin());
  job.rows.erase(std::remove_if(job.rows.begin(), job.rows.end(),
                                [&](int i) {
                                  return !canAdopt(i);
                                }),
                 job.rows.end());
  if (job.rows.empty() || m_folder.isEmpty()) {
    QTimer::singleShot(0, this, &DownloadQueue::nextAdoption);
    return;
  }

  // copied into the downloads folder's own place first, then that copy is
  // hashed: off the window's thread, and what is hashed is what is kept. The
  // user's file is only read
  const QString aside = QDir(stateFolder(m_folder)).filePath("adopting");
  QDir().mkpath(aside);
  const QString copy = QDir(aside).filePath(QString::number(++m_adopted));
  QFile::remove(copy);

  m_adopting    = true;
  auto cancel   = m_cancelAdopt;
  auto* watcher = new QFutureWatcher<std::optional<uint64_t>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, job, copy] {
    watcher->deleteLater();
    m_adopting      = false;
    const auto hash = watcher->result();
    if (!hash) {
      QFile::remove(copy);
      // read again at the next tick (the browser or an antivirus held it)
      m_seen.remove(job.path);
      if (job.byUser) {
        emit message(tr("%1 can't be read.").arg(QDir::toNativeSeparators(job.path)));
      }
      nextAdoption();
      return;
    }

    int matched = -1;
    bool taken  = false;  // the right file, but its entry was filled meanwhile
    for (int i : job.rows) {
      if (m_list->archives[std::size_t(i)].hash == *hash) {
        if (isChecking()) {
          // a hash check began meanwhile: the file is taken at a later tick
          m_seen.remove(job.path);
          taken = true;
        } else if (canAdopt(i)) {
          matched = i;
        } else {
          taken = true;
        }
        break;
      }
    }
    if (matched < 0) {
      QFile::remove(copy);
      if (job.byUser && !taken) {
        emit message(tr("%1 isn't the list's %2 (its hash differs).")
                         .arg(QFileInfo(job.path).fileName(),
                              m_list->archives[std::size_t(job.rows.front())].name));
      }
      emit changed();
      nextAdoption();
      return;
    }

    const Archive& a   = m_list->archives[std::size_t(matched)];
    const QString part = partPath(matched);
    QFile::remove(part);  // ours: an unfinished download of the same archive
    QString problem;
    if (!QFile::rename(copy, part)) {
      QFile::remove(copy);
      problem = tr("%1 can't be written.").arg(QDir::toNativeSeparators(part));
    } else {
      problem = putInPlace(matched);
    }
    if (problem.isEmpty()) {
      emit message(tr("%1 was taken from %2.")
                       .arg(a.name, QDir::toNativeSeparators(QFileInfo(job.path).absolutePath())));
    } else {
      setStatus(matched, Status::Failed, problem);
    }
    emit changed();
    nextAdoption();
  });
  const QString source = job.path;
  watcher->setFuture(QtConcurrent::run([source, copy, cancel]() -> std::optional<uint64_t> {
    const LowPriority low(LowPriority::Mode::Background);
    try {
      const auto hash = copyFile(source, copy, cancel.get());
      if (!hash) {
        QFile::remove(copy);
      }
      return hash;
    } catch (...) {
      QFile::remove(copy);
      return std::nullopt;
    }
  }));
}

void DownloadQueue::cancelJob(int i)
{
  auto it = m_jobs.find(i);
  if (it == m_jobs.end()) {
    return;
  }
  Job& j = *it->second;
  if (j.reply) {
    j.reply->disconnect(this);
    j.reply->abort();
    j.reply->deleteLater();
  }
  if (j.file.isOpen()) {
    j.file.close();
  }
  m_jobs.erase(it);
}

void DownloadQueue::failDownload(int i, const QString& reason, bool retry)
{
  cancelJob(i);
  Entry& e = m_entries[std::size_t(i)];
  log::warn("wabbajack: '{}' failed: {}", m_list->archives[std::size_t(i)].name, reason);
  if (retry && m_running && e.retries < MaxRetries) {
    ++e.retries;
    e.retryAt.start();
    setStatus(i, Status::Queued, reason);
  } else {
    setStatus(i, Status::Failed, reason);
  }
  emit changed();
  QTimer::singleShot(0, this, &DownloadQueue::pumpQueue);
}

// ---- text -----------------------------------------------------------------

QString DownloadQueue::statusText(int i) const
{
  const Entry& e = m_entries[std::size_t(i)];
  auto progress  = [&] {
    const int percent = e.total > 0 ? int(e.received * 100 / e.total) : 0;
    QString text      = tr("Downloading %1%").arg(percent);
    const auto it     = m_jobs.find(i);
    if (it != m_jobs.end() && it->second->speed > 0) {
      text += QStringLiteral(" · ") + speedText(it->second->speed);
    }
    return text;
  };

  switch (e.status) {
  case Status::Unknown:
    return {};
  case Status::Missing:
    return e.received > 0 ? tr("Not downloaded (%1 already)").arg(localizedByteSize(e.received))
                          : tr("Not downloaded");
  case Status::Present:
    return tr("In the folder");
  case Status::Verified:
    return tr("In the folder, verified");
  case Status::WrongHash:
    return tr("In the folder, but a different file");
  case Status::GameFound:
    return tr("In the game, verified");
  case Status::GameDiffers:
    return tr("In the game, but another version");
  case Status::GameMissing:
    return tr("Not in the game folder");
  case Status::Queued:
    return e.retries > 0 ? tr("Waiting to try again (%1)").arg(e.reason) : tr("Waiting");
  case Status::NeedsClick:
    return tr("Press Slow download on Nexus");
  case Status::Requesting:
    return tr("Asking for the file…");
  case Status::Downloading:
    return progress();
  case Status::Verifying:
    return tr("Checking its hash…");
  case Status::Failed:
    return tr("Failed: %1").arg(e.reason);
  case Status::ByHand:
    return tr("Download it in the browser (double-click)");
  case Status::NotNeeded:
    return tr("Not needed");
  case Status::Unsupported:
    return e.reason.isEmpty() ? tr("Unknown source") : e.reason;
  }
  return {};
}

}  // namespace Wabbajack
