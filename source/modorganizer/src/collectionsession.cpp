#include "collectionsession.h"

#include "downloadmanager.h"
#include "fluentdialogs.h"
#include "installationmanager.h"
#include "modinfo.h"
#include "modlist.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "pluginlist.h"
#include "profile.h"
#include "settings.h"

#include <archive.h>
#include <filesystemutilities.h>
#include <gameplugins.h>
#include <guessedvalue.h>
#include <imodinterface.h>
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <system_error>

using namespace MOBase;
using namespace Collections;

namespace
{

// how many Premium downloads run at once
constexpr int PremiumParallel = 3;

// a queued Premium download that hasn't shown up after this long failed
constexpr qint64 QueueTimeoutMs = 120000;

const char* PresetProperty       = "mo2r.fomodPreset";
const char* PresetResultProperty = "mo2r.fomodPresetResult";

QString statusToString(CollectionSession::Status s)
{
  switch (s) {
  case CollectionSession::Status::Pending:
    return QStringLiteral("pending");
  case CollectionSession::Status::WaitingForClick:
    return QStringLiteral("waiting");
  case CollectionSession::Status::Downloading:
    return QStringLiteral("downloading");
  case CollectionSession::Status::Downloaded:
    return QStringLiteral("downloaded");
  case CollectionSession::Status::Installed:
    return QStringLiteral("installed");
  case CollectionSession::Status::Skipped:
    return QStringLiteral("skipped");
  case CollectionSession::Status::Failed:
    return QStringLiteral("failed");
  }
  return QStringLiteral("pending");
}

CollectionSession::Status statusFromString(const QString& s)
{
  // a download or a click in progress when MO2 closed is looked for again
  if (s == "downloaded") {
    return CollectionSession::Status::Downloaded;
  } else if (s == "installed") {
    return CollectionSession::Status::Installed;
  } else if (s == "skipped") {
    return CollectionSession::Status::Skipped;
  } else if (s == "failed") {
    return CollectionSession::Status::Failed;
  }
  return CollectionSession::Status::Pending;
}

bool sameName(const QString& a, const QString& b)
{
  return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

bool isDownloadComplete(DownloadManager::DownloadState s)
{
  return s == DownloadManager::STATE_READY || s == DownloadManager::STATE_INSTALLED ||
         s == DownloadManager::STATE_UNINSTALLED || s == DownloadManager::STATE_NOFETCH;
}

bool isDownloadRunning(DownloadManager::DownloadState s)
{
  return s == DownloadManager::STATE_STARTED || s == DownloadManager::STATE_DOWNLOADING ||
         s == DownloadManager::STATE_PAUSING || s == DownloadManager::STATE_PAUSED ||
         s == DownloadManager::STATE_FETCHINGMODINFO ||
         s == DownloadManager::STATE_FETCHINGFILEINFO ||
         s == DownloadManager::STATE_FETCHINGMODINFO_MD5;
}

// GraphQL numbers that may come as strings (BigInt)
qint64 toInt64(const QVariant& v)
{
  return v.toString().toLongLong();
}

QString graphQLError(const QVariantMap& result)
{
  const QVariantList errors = result.value("errors").toList();
  if (errors.isEmpty()) {
    return {};
  }
  return errors.first().toMap().value("message").toString();
}

const char* MetaQuery = R"(query ($slug: String!) {
  collection(slug: $slug, viewAdultContent: true) {
    name
    user { name }
    game { domainName }
    latestPublishedRevision { revisionNumber }
  }
})";

const char* FilesQuery = R"(query ($slug: String!, $revision: Int) {
  collectionRevision(slug: $slug, revision: $revision, viewAdultContent: true) {
    revisionNumber
    downloadLink
    collection { name user { name } game { domainName } }
    modFiles {
      optional
      fileId
      file { modId name version sizeInBytes uri mod { name } }
    }
  }
})";

}  // namespace

CollectionSession::CollectionSession(OrganizerCore& core, const CollectionLink& link,
                                     QObject* parent)
    : QObject(parent), m_core(core), m_link(link), m_revision(link.revision)
{
  m_scanTimer.setSingleShot(true);
  m_scanTimer.setInterval(250);
  connect(&m_scanTimer, &QTimer::timeout, this, &CollectionSession::scanDownloads);

  m_watchdog.setInterval(5000);
  connect(&m_watchdog, &QTimer::timeout, this, &CollectionSession::scheduleScan);

  auto& nexus = NexusInterface::instance();
  connect(&nexus, &NexusInterface::nxmApiV2Available, this,
          &CollectionSession::onApiResult);
  connect(&nexus, &NexusInterface::nxmRequestFailed, this,
          &CollectionSession::onApiFailed);

  DownloadManager* dm = m_core.downloadManager();
  connect(dm, &DownloadManager::update, this, &CollectionSession::scheduleScan);
  connect(dm, &DownloadManager::stateChanged, this, &CollectionSession::scheduleScan);
  connect(dm, &DownloadManager::downloadAdded, this, &CollectionSession::scheduleScan);
  m_connections.push_back(dm->onDownloadComplete([this](int) {
    scheduleScan();
  }));
  m_connections.push_back(dm->onDownloadFailed([this](int) {
    scheduleScan();
  }));
  m_connections.push_back(dm->onDownloadRemoved([this](int) {
    scheduleScan();
  }));
}

CollectionSession::~CollectionSession()
{
  for (auto& c : m_connections) {
    c.disconnect();
  }
  if (m_archiveReply) {
    m_archiveReply->disconnect(this);
    m_archiveReply->abort();
    m_archiveReply->deleteLater();
    m_archiveFile.close();
    m_archiveFile.remove();
  }
  if (m_started) {
    saveState();
  }
}

void CollectionSession::setSwitchProfile(std::function<bool(const QString&)> f)
{
  m_switchProfile = std::move(f);
}

QString CollectionSession::statePath(const QString& slug, int revision)
{
  // the slug names files: nothing else than letters, digits, "_" and "-"
  if (!isValidSlug(slug) || revision < 0) {
    return {};
  }
  return QDir(Settings::instance().paths().base())
      .filePath(QStringLiteral("collections/%1-%2.json").arg(slug).arg(revision));
}

// ---------------------------------------------------------------------------
// what the collection is

QString CollectionSession::displayName() const
{
  if (!m_name.isEmpty()) {
    return m_name;
  }
  if (!m_manifest.name.isEmpty()) {
    return m_manifest.name;
  }
  return m_link.slug;
}

QString CollectionSession::author() const
{
  return !m_author.isEmpty() ? m_author : m_manifest.author;
}

QString CollectionSession::gameName() const
{
  const QString domain = !m_domain.isEmpty() ? m_domain : m_link.game;
  for (IPluginGame* game : m_core.pluginContainer().plugins<IPluginGame>()) {
    if (game != nullptr && sameName(game->gameNexusName(), domain)) {
      return game->displayGameName();
    }
  }
  return domain;
}

bool CollectionSession::isPremium() const
{
  return NexusInterface::instance().getAPIUserAccount().type() ==
         APIUserAccountTypes::Premium;
}

bool CollectionSession::isBethesda() const
{
  return m_core.gameFeatures().gameFeature<GamePlugins>() != nullptr;
}

const CollectionMod& CollectionSession::modOf(const Entry& e) const
{
  return m_manifest.mods[e.mod];
}

qint64 CollectionSession::totalSize() const
{
  qint64 total = 0;
  for (const auto& e : m_entries) {
    if (e.selected) {
      total += e.size;
    }
  }
  return total;
}

qint64 CollectionSession::remainingSize() const
{
  qint64 total = 0;
  for (const auto& e : m_entries) {
    if (e.selected && modOf(e).type != SourceType::Bundle &&
        (e.status == Status::Pending || e.status == Status::WaitingForClick ||
         e.status == Status::Downloading)) {
      total += e.size;
    }
  }
  return total;
}

int CollectionSession::count(Status s) const
{
  return static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                        [s](const Entry& e) {
                                          return e.status == s;
                                        }));
}

bool CollectionSession::isFinished(const Entry& e) const
{
  return !e.selected || e.status == Status::Installed || e.status == Status::Skipped ||
         e.status == Status::Failed;
}

QString CollectionSession::domainOf(const Entry& e) const
{
  const QString d = modOf(e).domainName;
  return !d.isEmpty() ? d : m_domain;
}

QString CollectionSession::nexusPage(const Entry& e) const
{
  // another game's file is downloaded by hand: no "mod manager" link, which
  // this instance would refuse
  const auto& m = modOf(e);
  return QStringLiteral("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3%4")
      .arg(QString::fromUtf8(QUrl::toPercentEncoding(domainOf(e))))
      .arg(m.modId)
      .arg(m.fileId)
      .arg(e.byHand ? QString() : QStringLiteral("&nmm=1"));
}

// ---------------------------------------------------------------------------
// options

void CollectionSession::setNewProfile(bool b)
{
  if (m_profileMade) {
    return;
  }
  m_newProfile = b;
  saveState();
}

void CollectionSession::setProfileName(const QString& name)
{
  if (m_profileMade) {
    return;
  }
  m_profileName = name;
  saveState();
}

void CollectionSession::setSeparatorName(const QString& name)
{
  m_separatorName = name;
  saveState();
}

void CollectionSession::setAutoOpenNext(bool b)
{
  m_autoOpen = b;
  saveState();
}

void CollectionSession::setSelected(int row, bool selected)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  Entry& e = m_entries[row];
  if (!modOf(e).optional || e.selected == selected) {
    return;
  }
  e.selected = selected;
  if (!selected && e.status != Status::Installed) {
    setStatus(row, Status::Skipped, tr("Not selected"));
  } else if (selected && e.status == Status::Skipped) {
    setStatus(row, e.archive.isEmpty() ? Status::Pending : Status::Downloaded);
  }
  saveState();
  emit changed();
  scheduleScan();
}

// ---------------------------------------------------------------------------
// loading

void CollectionSession::setStage(Stage s)
{
  m_stage = s;
  if (s == Stage::Running) {
    m_watchdog.start();
  } else {
    m_watchdog.stop();
  }
  emit changed();
}

void CollectionSession::fail(const QString& message)
{
  log::warn("collection {}: {}", m_link.slug, message);
  m_error = message;
  setStage(Stage::Error);
}

void CollectionSession::setLoading(const QString& text)
{
  m_loadingText = text;
  emit changed();
}

void CollectionSession::load()
{
  // the slug names the state file and the archive
  if (!isValidSlug(m_link.slug)) {
    fail(tr("This isn't the address of a collection."));
    return;
  }

  if (!m_link.game.isEmpty() && !checkGame(m_link.game)) {
    return;
  }

  if (m_revision >= 0) {
    loadState();
    requestFiles();
  } else {
    requestMetadata();
  }

  // requests wait for the Nexus sign-in; without one they wait for good
  QPointer<CollectionSession> self(this);
  QTimer::singleShot(15000, this, [self] {
    if (self && self->m_stage == Stage::Loading &&
        (self->m_metaRequest >= 0 || self->m_filesRequest >= 0) &&
        !NexusInterface::instance().getAPIUserAccount().isValid()) {
      self->setLoading(tr("Waiting for the Nexus sign-in. If Mod Organizer isn't "
                          "signed in, sign in under Settings > Nexus."));
    }
  });
}

bool CollectionSession::gameMatches(const QString& domain) const
{
  const IPluginGame* game = m_core.managedGame();
  if (game == nullptr) {
    return false;
  }

  // the games the download manager takes links for
  if (sameName(domain, game->gameNexusName()) ||
      sameName(domain, game->gameShortName())) {
    return true;
  }
  for (const QString& name : game->validShortNames()) {
    if (const IPluginGame* other = m_core.getGame(name)) {
      if (sameName(domain, other->gameNexusName()) ||
          sameName(domain, other->gameShortName())) {
        return true;
      }
    }
  }
  return false;
}

bool CollectionSession::checkGame(const QString& domain)
{
  const IPluginGame* game = m_core.managedGame();
  if (game == nullptr || domain.isEmpty() || gameMatches(domain)) {
    return true;
  }

  QString linkGame = domain;
  for (IPluginGame* g : m_core.pluginContainer().plugins<IPluginGame>()) {
    if (g != nullptr && sameName(domain, g->gameNexusName())) {
      linkGame = g->displayGameName();
      break;
    }
  }
  fail(tr("This collection is for %1, but this instance manages %2. Open it from an "
          "instance for %1.")
           .arg(linkGame)
           .arg(game->displayGameName()));
  return false;
}

void CollectionSession::requestMetadata()
{
  setLoading(tr("Asking Nexus about the collection…"));
  QJsonObject vars;
  vars.insert("slug", m_link.slug);
  m_metaRequest = NexusInterface::instance().requestGraphQL(
      QString::fromUtf8(MetaQuery), vars, QVariant(), QStringLiteral("collections"));
  if (m_metaRequest < 0) {
    fail(tr("Nexus can't be reached right now: the request limit was reached. "
            "Try again later."));
  }
}

void CollectionSession::requestFiles()
{
  setLoading(tr("Reading the collection's mod list…"));
  QJsonObject vars;
  vars.insert("slug", m_link.slug);
  vars.insert("revision", m_revision);
  m_filesRequest = NexusInterface::instance().requestGraphQL(
      QString::fromUtf8(FilesQuery), vars, QVariant(), QStringLiteral("collections"));
  if (m_filesRequest < 0) {
    fail(tr("Nexus can't be reached right now: the request limit was reached. "
            "Try again later."));
  }
}

void CollectionSession::onApiFailed(QString, int, int, QVariant, int id, int code,
                                    const QString& error)
{
  if (id < 0 || (id != m_metaRequest && id != m_filesRequest && id != m_linkRequest)) {
    return;
  }
  m_metaRequest = m_filesRequest = m_linkRequest = -1;

  if (code == 401 || code == 403 ||
      !NexusInterface::instance().getAPIUserAccount().isValid()) {
    fail(tr("Nexus refused the request (%1). Sign in to Nexus in Settings, then open "
            "the collection again.")
             .arg(error));
  } else {
    fail(tr("Nexus didn't answer: %1").arg(error));
  }
}

void CollectionSession::onApiResult(QVariant, QVariant result, int id)
{
  if (id < 0) {
    return;
  }

  const QVariantMap map = result.toMap();

  if (id == m_metaRequest) {
    m_metaRequest = -1;
    const QVariantMap c = map.value("data").toMap().value("collection").toMap();
    if (c.isEmpty()) {
      const QString e = graphQLError(map);
      fail(e.isEmpty() ? tr("This collection wasn't found on Nexus.")
                       : tr("This collection wasn't found on Nexus: %1").arg(e));
      return;
    }
    m_name   = c.value("name").toString();
    m_author = c.value("user").toMap().value("name").toString();
    m_domain = c.value("game").toMap().value("domainName").toString();
    if (!checkGame(m_domain)) {
      return;
    }
    m_revision =
        c.value("latestPublishedRevision").toMap().value("revisionNumber").toInt();
    if (m_revision <= 0) {
      fail(tr("This collection has no published revision."));
      return;
    }
    loadState();
    requestFiles();
  } else if (id == m_filesRequest) {
    m_filesRequest = -1;
    const QVariantMap r = map.value("data").toMap().value("collectionRevision").toMap();
    if (r.isEmpty()) {
      const QString e = graphQLError(map);
      fail(e.isEmpty() ? tr("Revision %1 of this collection wasn't found on Nexus.")
                             .arg(m_revision)
                       : tr("Revision %1 of this collection wasn't found on Nexus: %2")
                             .arg(m_revision)
                             .arg(e));
      return;
    }
    const QVariantMap c = r.value("collection").toMap();
    if (m_name.isEmpty()) {
      m_name = c.value("name").toString();
    }
    if (m_author.isEmpty()) {
      m_author = c.value("user").toMap().value("name").toString();
    }
    m_domain = c.value("game").toMap().value("domainName").toString();
    if (!checkGame(m_domain)) {
      return;
    }
    m_downloadLink = r.value("downloadLink").toString();

    m_files.clear();
    for (const QVariant& v : r.value("modFiles").toList()) {
      const QVariantMap mf   = v.toMap();
      const QVariantMap file = mf.value("file").toMap();
      NexusFile f;
      f.modId    = file.value("modId").toInt();
      f.name     = file.value("name").toString();
      f.version  = file.value("version").toString();
      f.uri      = file.value("uri").toString();
      f.size     = toInt64(file.value("sizeInBytes"));
      f.modName  = file.value("mod").toMap().value("name").toString();
      f.optional = mf.value("optional").toBool();
      m_files[mf.value("fileId").toInt()] = f;
    }
    findOrFetchArchive();
  } else if (id == m_linkRequest) {
    m_linkRequest      = -1;
    // {"download_links": [{"URI": ..., "name": ..., "short_name": ...}]}, the
    // first being Nexus's preferred server
    QString link;
    for (const QVariant& v : map.value("download_links").toList()) {
      const QVariantMap m = v.toMap();
      link = m.value("URI", m.value("uri")).toString();
      if (!link.isEmpty()) {
        break;
      }
    }
    if (link.isEmpty()) {
      fail(tr("Nexus gave no link to download the collection."));
      return;
    }
    downloadArchive(link);
  }
}

void CollectionSession::findOrFetchArchive()
{
  const QString base =
      QStringLiteral("%1-%2").arg(m_link.slug).arg(m_revision);
  QDir dir(QDir(m_core.downloadManager()->getOutputDirectory()).filePath("collections"));

  // an archive fetched before
  if (dir.exists()) {
    for (const QFileInfo& fi :
         dir.entryInfoList({base + ".*"}, QDir::Files, QDir::Name)) {
      if (fi.suffix().compare("part", Qt::CaseInsensitive) != 0 && fi.size() > 0) {
        m_archivePath = fi.absoluteFilePath();
        readManifest();
        return;
      }
    }
  }

  if (m_downloadLink.isEmpty()) {
    fail(tr("Nexus gave no link to download the collection."));
    return;
  }

  setLoading(tr("Asking Nexus for the collection's archive…"));
  m_linkRequest = NexusInterface::instance().requestApiPath(
      m_downloadLink, QVariant(), QStringLiteral("collections"));
  if (m_linkRequest < 0) {
    fail(tr("Nexus can't be reached right now: the request limit was reached. "
            "Try again later."));
  }
}

void CollectionSession::downloadArchive(const QString& link)
{
  const QUrl url(link);
  if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
    fail(tr("Nexus gave an invalid link to download the collection."));
    return;
  }

  QString ext = QFileInfo(url.path()).suffix().toLower();
  if (ext != "7z" && ext != "zip" && ext != "rar") {
    ext = QStringLiteral("7z");
  }

  QDir dir(QDir(m_core.downloadManager()->getOutputDirectory()).filePath("collections"));
  if (!dir.mkpath(".")) {
    fail(tr("The folder %1 couldn't be created.").arg(QDir::toNativeSeparators(dir.path())));
    return;
  }
  m_archivePath =
      dir.filePath(QStringLiteral("%1-%2.%3").arg(m_link.slug).arg(m_revision).arg(ext));
  m_archiveFile.setFileName(m_archivePath + ".part");
  if (!m_archiveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    fail(tr("%1 couldn't be written.").arg(QDir::toNativeSeparators(m_archivePath)));
    return;
  }

  setLoading(tr("Downloading the collection…"));
  auto* am = NexusInterface::instance().getAccessManager();
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, am->userAgent("collections"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  m_archiveReply = am->get(request);

  connect(m_archiveReply, &QNetworkReply::readyRead, this, [this] {
    if (m_archiveReply) {
      m_archiveFile.write(m_archiveReply->readAll());
    }
  });
  connect(m_archiveReply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            if (total > 0) {
              setLoading(tr("Downloading the collection… %1%")
                             .arg(received * 100 / total));
            }
          });
  connect(m_archiveReply, &QNetworkReply::finished, this,
          &CollectionSession::archiveDownloaded);
}

void CollectionSession::archiveDownloaded()
{
  QNetworkReply* reply = m_archiveReply;
  if (reply == nullptr) {
    return;
  }
  reply->deleteLater();
  m_archiveReply = nullptr;

  m_archiveFile.write(reply->readAll());
  m_archiveFile.close();

  if (reply->error() != QNetworkReply::NoError) {
    m_archiveFile.remove();
    // the link's key stays out of the message
    QString message = reply->errorString();
    message.replace(QRegularExpression(QStringLiteral("\\?\\S*")), QString());
    fail(tr("The collection couldn't be downloaded: %1").arg(message));
    return;
  }

  QFile::remove(m_archivePath);
  if (!m_archiveFile.rename(m_archivePath)) {
    fail(tr("%1 couldn't be written.").arg(QDir::toNativeSeparators(m_archivePath)));
    return;
  }
  readManifest();
}

void CollectionSession::readManifest()
{
  setLoading(tr("Reading the collection…"));

  QString problem;
  std::unique_ptr<Archive> archive = CreateArchive();
  if (!archive->isValid()) {
    problem = tr("the archive library couldn't be loaded");
  } else if (!archive->open(m_archivePath.toStdWString(), {})) {
    problem = tr("the archive couldn't be opened");
  } else {
    FileData* manifestFile = nullptr;
    for (FileData* f : archive->getFileList()) {
      const QString path = QString::fromStdWString(f->getArchiveFilePath());
      if (!f->isDirectory() &&
          path.compare("collection.json", Qt::CaseInsensitive) == 0) {
        manifestFile = f;
        break;
      }
    }

    QTemporaryDir temp;
    if (manifestFile == nullptr) {
      problem = tr("it has no collection.json");
    } else if (!temp.isValid()) {
      problem = tr("no temporary folder");
    } else {
      manifestFile->addOutputFilePath(L"collection.json");
      if (!archive->extract(QDir::toNativeSeparators(temp.path()).toStdWString(), {},
                            {}, {})) {
        problem = tr("collection.json couldn't be extracted");
      } else {
        QFile f(QDir(temp.path()).filePath("collection.json"));
        if (!f.open(QIODevice::ReadOnly)) {
          problem = tr("collection.json couldn't be read");
        } else {
          QString error;
          auto m = CollectionManifest::parse(f.readAll(), &error);
          if (!m) {
            problem = tr("collection.json is not valid: %1").arg(error);
          } else {
            m_manifest     = std::move(*m);
            m_haveManifest = true;
          }
        }
      }
    }
    archive->close();
  }

  if (!m_haveManifest) {
    log::warn("collection {}: {}", m_link.slug, problem);

    // a damaged archive (a cut download, a disk error) is removed and
    // fetched again, once
    if (!m_refetched && !m_downloadLink.isEmpty()) {
      m_refetched = true;
      if (!QFile::remove(m_archivePath)) {
        log::warn("collection {}: the damaged archive couldn't be removed", m_link.slug);
      }
      m_archivePath.clear();
      findOrFetchArchive();
      return;
    }

    if (m_files.empty()) {
      fail(tr("The collection couldn't be read: %1.").arg(problem));
      return;
    }

    // the mod list from Nexus, without installer choices or rules
    m_manifest      = {};
    m_manifest.name = m_name;
    int i           = 0;
    for (const auto& [fileId, f] : m_files) {
      CollectionMod mod;
      mod.index    = i++;
      mod.name     = !f.modName.isEmpty() ? f.modName : f.name;
      mod.version  = f.version;
      mod.optional = f.optional;
      mod.type     = SourceType::Nexus;
      mod.modId    = f.modId;
      mod.fileId   = fileId;
      mod.fileSize = f.size;
      m_manifest.mods.push_back(mod);
    }
    m_haveManifest = true;
    m_reduced      = true;
    m_archivePath.clear();
  }

  buildEntries();
}

QString CollectionSession::uniqueModName(int row) const
{
  // the mod's name on Nexus when it comes from there: the collection keeps the
  // name its author's copy had, often with an older version in it ("CET 1.37.1
  // - Scripting fixes" for 1.38.1)
  const CollectionMod& mod = modOf(m_entries[row]);
  QString base             = mod.name.trimmed();
  if (mod.type == SourceType::Nexus) {
    const auto file = m_files.find(mod.fileId);
    if (file != m_files.end() && !file->second.modName.trimmed().isEmpty()) {
      base = file->second.modName.trimmed();
    }
  }
  fixDirectoryName(base);
  if (base.isEmpty()) {
    base = QStringLiteral("Mod %1").arg(row + 1);
  }

  auto taken = [&](const QString& name) {
    for (int i = 0; i < row; ++i) {
      if (m_entries[i].modName.compare(name, Qt::CaseInsensitive) == 0) {
        return true;
      }
    }
    return false;
  };

  QString name = base;
  for (int n = 2; taken(name); ++n) {
    name = QStringLiteral("%1 (%2)").arg(base).arg(n);
  }
  return name;
}

void CollectionSession::buildEntries()
{
  m_entries.clear();
  for (int index : m_manifest.installOrder()) {
    Entry e;
    e.mod             = index;
    const auto& mod   = m_manifest.mods[index];
    e.selected        = true;
    e.fileVersion     = mod.version;
    e.size            = mod.fileSize;
    // a file of another game on Nexus: this instance takes no links for it
    e.byHand = mod.type == SourceType::Nexus && !gameMatches(domainOf(e));
    if (mod.type == SourceType::Nexus) {
      auto it = m_files.find(mod.fileId);
      if (it != m_files.end()) {
        if (!it->second.uri.contains('/')) {
          e.fileName = it->second.uri;
        }
        if (it->second.size > 0) {
          e.size = it->second.size;
        }
        if (e.fileVersion.isEmpty()) {
          e.fileVersion = it->second.version;
        }
      }
    }
    m_entries.push_back(e);
  }

  // what was done before
  const QJsonArray saved = m_saved.value("entries").toArray();
  for (const auto& v : saved) {
    const QJsonObject o = v.toObject();
    const int mod       = o.value("mod").toInt(-1);
    for (auto& e : m_entries) {
      if (e.mod != mod || m_manifest.mods[mod].fileId != o.value("fileId").toInt()) {
        continue;
      }
      e.status       = statusFromString(o.value("status").toString());
      e.reason       = o.value("reason").toString();
      e.archive      = o.value("archive").toString();
      e.modName      = o.value("modName").toString();
      e.selected     = o.value("selected").toBool(true) || !m_manifest.mods[mod].optional;
      e.presetResult = o.value("preset").toString();
      e.keptExisting = o.value("kept").toBool();
      if (e.status == Status::Downloaded && !QFile::exists(e.archive)) {
        e.status = Status::Pending;
        e.archive.clear();
      }
      break;
    }
  }

  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    if (m_entries[row].modName.isEmpty()) {
      m_entries[row].modName = uniqueModName(row);
    }
  }

  if (m_profileName.isEmpty()) {
    m_profileName = displayName();
    fixDirectoryName(m_profileName);
  }
  if (m_separatorName.isEmpty()) {
    m_separatorName = displayName();
    fixDirectoryName(m_separatorName);
  }

  m_loadingText.clear();
  setStage(m_saved.value("finished").toBool() ? Stage::Done
           : m_started                        ? Stage::Paused
                                              : Stage::Ready);
  scanDownloads();
}

// ---------------------------------------------------------------------------
// the state file

void CollectionSession::loadState()
{
  const QString path = statePath(m_link.slug, m_revision);
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  if (o.isEmpty()) {
    return;
  }
  m_saved         = o;
  m_started       = o.value("started").toBool();
  m_newProfile    = o.value("newProfile").toBool(true);
  m_profileName   = o.value("profile").toString();
  m_profileMade   = o.value("profileMade").toBool();
  m_separatorName = o.value("separator").toString();
  m_autoOpen      = o.value("autoOpen").toBool(true);
  // a blanket "replace" from an older state file isn't taken: the folders are
  // asked about again
  m_approved = o.value("approvedReplacements").toObject();
}

void CollectionSession::saveState() const
{
  // nothing to keep before the collection was read and started
  if (!m_haveManifest || (!m_started && m_saved.isEmpty())) {
    return;
  }

  QJsonObject o;
  o.insert("slug", m_link.slug);
  o.insert("revision", m_revision);
  o.insert("game", m_domain);
  o.insert("name", displayName());
  o.insert("author", author());
  o.insert("archive", m_archivePath);
  o.insert("started", m_started);
  o.insert("finished", m_stage == Stage::Done);
  o.insert("newProfile", m_newProfile);
  o.insert("profile", m_profileName);
  o.insert("profileMade", m_profileMade);
  o.insert("separator", m_separatorName);
  o.insert("autoOpen", m_autoOpen);
  o.insert("approvedReplacements", m_approved);

  QJsonArray entries;
  for (const auto& e : m_entries) {
    QJsonObject j;
    j.insert("mod", e.mod);
    j.insert("fileId", m_manifest.mods[e.mod].fileId);
    j.insert("status", statusToString(e.status));
    j.insert("reason", e.reason);
    j.insert("archive", e.archive);
    j.insert("modName", e.modName);
    j.insert("selected", e.selected);
    j.insert("preset", e.presetResult);
    j.insert("kept", e.keptExisting);
    entries.append(j);
  }
  o.insert("entries", entries);

  const QString path = statePath(m_link.slug, m_revision);
  if (path.isEmpty()) {
    return;
  }
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
      log::warn("collection state couldn't be saved to {}", path);
    }
  }
}

// ---------------------------------------------------------------------------
// downloads

void CollectionSession::setStatus(int row, Status s, const QString& reason)
{
  Entry& e = m_entries[row];
  if (e.status == s && e.reason == reason) {
    return;
  }
  e.status = s;
  e.reason = reason;
  if (s != Status::Downloading) {
    e.progress   = -1;
    e.queuedByUs = false;
  }
  emit entryChanged(row);
}

void CollectionSession::scheduleScan()
{
  if (m_haveManifest && !m_entries.empty() && !m_scanTimer.isActive()) {
    m_scanTimer.start();
  }
}

void CollectionSession::scanDownloads()
{
  if (m_entries.empty()) {
    return;
  }

  // a box shown while queuing a download (Already queued, Wrong game) runs
  // the event loop and its timers: the scan in progress finishes first
  if (m_scanning) {
    scheduleScan();
    return;
  }
  m_scanning = true;
  const auto unlock = qScopeGuard([this] {
    m_scanning = false;
  });

  DownloadManager* dm = m_core.downloadManager();
  const int total     = dm->numTotalDownloads();
  bool arrived        = false;
  bool changedAny     = false;

  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    Entry& e        = m_entries[row];
    const auto& mod = modOf(e);
    if (mod.type != SourceType::Nexus || !e.selected ||
        (e.status != Status::Pending && e.status != Status::WaitingForClick &&
         e.status != Status::Downloading)) {
      continue;
    }

    int complete = -1, running = -1, failed = -1;
    for (int i = 0; i < total && complete < 0; ++i) {
      const ModRepositoryFileInfo* info = dm->getFileInfo(i);
      const bool byId = info != nullptr && mod.fileId > 0 && info->fileID == mod.fileId &&
                        (mod.modId <= 0 || info->modID == mod.modId);
      const bool byName = !byId && !e.fileName.isEmpty() && e.size > 0 &&
                          dm->getFileName(i).compare(e.fileName, Qt::CaseInsensitive) ==
                              0 &&
                          dm->getFileSize(i) == e.size;
      if (!byId && !byName) {
        continue;
      }
      const auto state = dm->getState(i);
      if (isDownloadComplete(state)) {
        complete = i;
      } else if (byId && isDownloadRunning(state)) {
        running = i;
      } else if (byId && state == DownloadManager::STATE_ERROR) {
        failed = i;
      }
    }

    bool pending = false;
    for (int i = 0; i < dm->numPendingDownloads() && !pending; ++i) {
      const auto p = dm->getPendingDownload(i);
      pending      = std::get<1>(p) == mod.modId && std::get<2>(p) == mod.fileId;
    }

    const Status before = e.status;
    if (complete >= 0) {
      e.archive = dm->getFilePath(complete);
      setStatus(row, Status::Downloaded);
    } else if (running >= 0 || pending) {
      const int progress = running >= 0 ? dm->getProgress(running).first : -1;
      const bool queued  = e.queuedByUs;
      setStatus(row, Status::Downloading);
      e.queuedByUs = queued;
      if (e.progress != progress) {
        e.progress = progress;
        emit entryChanged(row);
      }
    } else if (failed >= 0 && e.status == Status::Downloading) {
      setStatus(row, Status::Failed,
                tr("The download failed: %1").arg(dm->getLastError(failed)));
    } else if (e.status == Status::Downloading) {
      if (e.queuedByUs && e.queuedAt.isValid() && e.queuedAt.elapsed() < QueueTimeoutMs) {
        // queued, not in the list yet
      } else if (e.queuedByUs) {
        setStatus(row, Status::Failed, tr("The download didn't start."));
      } else {
        // removed from the downloads
        setStatus(row, Status::Pending);
      }
    }

    if (e.status != before) {
      changedAny = true;
      if (before == Status::WaitingForClick &&
          (e.status == Status::Downloading || e.status == Status::Downloaded)) {
        arrived = true;
      }
    }
  }

  if (changedAny) {
    saveState();
    emit changed();
  }

  if (m_stage != Stage::Running) {
    return;
  }

  // the next page opens once the last one's file came
  if (arrived && !isPremium() && m_autoOpen) {
    bool waiting = false;
    for (const auto& e : m_entries) {
      waiting = waiting || e.status == Status::WaitingForClick;
    }
    if (!waiting) {
      openNext();
    }
  }

  pumpDownloads();
  pumpInstall();
}

void CollectionSession::pumpDownloads()
{
  if (m_stage != Stage::Running || !isPremium() || m_pumping) {
    return;
  }
  m_pumping         = true;
  const auto unlock = qScopeGuard([this] {
    m_pumping = false;
  });

  int active = count(Status::Downloading);
  for (int row = 0; row < static_cast<int>(m_entries.size()) && active < PremiumParallel;
       ++row) {
    Entry& e        = m_entries[row];
    const auto& mod = modOf(e);
    if (!e.selected || e.byHand || mod.type != SourceType::Nexus ||
        e.status != Status::Pending || mod.modId <= 0 || mod.fileId <= 0) {
      continue;
    }
    // only links this instance takes: another game's file is installed by hand
    if (!gameMatches(domainOf(e))) {
      e.byHand = true;
      emit entryChanged(row);
      continue;
    }
    setStatus(row, Status::Downloading);
    e.queuedByUs = true;
    e.queuedAt.start();
    ++active;
    m_core.downloadManager()->addNXMDownload(QStringLiteral("nxm://%1/mods/%2/files/%3")
                                                 .arg(domainOf(e))
                                                 .arg(mod.modId)
                                                 .arg(mod.fileId),
                                             false);
  }
}

bool CollectionSession::openNext()
{
  for (int pass = 0; pass < 2; ++pass) {
    // a page not opened yet, then one opened before that has no file yet
    const Status wanted = pass == 0 ? Status::Pending : Status::WaitingForClick;
    for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
      const Entry& e = m_entries[row];
      if (e.selected && !e.byHand && modOf(e).type == SourceType::Nexus &&
          e.status == wanted) {
        openPage(row);
        return true;
      }
    }
  }
  return false;
}

void CollectionSession::openPage(int row)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  const Entry& e  = m_entries[row];
  const auto& mod = modOf(e);
  if (mod.type == SourceType::Nexus) {
    if (mod.modId <= 0 || mod.fileId <= 0) {
      return;
    }
    shell::Open(QUrl(nexusPage(e)));
    // a free account's click on the page brings the file; Premium queues it
    if (e.status == Status::Pending && !e.byHand && !isPremium()) {
      setStatus(row, Status::WaitingForClick);
      saveState();
      emit changed();
    }
  } else if (!mod.url.isEmpty()) {
    const QUrl url(mod.url);
    if (url.scheme() == "https" || url.scheme() == "http") {
      shell::Open(url);
    }
  }
}

void CollectionSession::chooseFile(int row, const QString& path)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size()) || path.isEmpty()) {
    return;
  }
  m_entries[row].archive = path;
  setStatus(row, Status::Downloaded);
  saveState();
  emit changed();
  scheduleScan();
}

void CollectionSession::markDone(int row)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  setStatus(row, Status::Installed, tr("Marked as done"));
  saveState();
  emit changed();
  scheduleScan();
}

void CollectionSession::retry(int row)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  Entry& e = m_entries[row];
  e.selected = true;
  setStatus(row, !e.archive.isEmpty() && QFile::exists(e.archive) ? Status::Downloaded
                                                                   : Status::Pending);
  if (m_stage == Stage::Done) {
    setStage(Stage::Paused);
  }
  saveState();
  emit changed();
  scheduleScan();
}

void CollectionSession::skip(int row)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  setStatus(row, Status::Skipped, tr("Skipped"));
  saveState();
  emit changed();
  scheduleScan();
}

// ---------------------------------------------------------------------------
// installing

void CollectionSession::pause()
{
  if (m_stage == Stage::Running) {
    setStage(Stage::Paused);
    saveState();
  }
}

bool CollectionSession::prepareProfile()
{
  const Profile* current = m_core.currentProfile();

  if (!m_newProfile) {
    if (!m_profileMade) {
      m_profileName = current != nullptr ? current->name() : QString();
      m_profileMade = true;
    }
  } else if (!m_profileMade) {
    QString name = m_profileName.trimmed();
    if (!fixDirectoryName(name) || name.isEmpty()) {
      QMessageBox::warning(qApp->activeWindow(), tr("Profile"),
                           tr("\"%1\" can't be used as a profile name.").arg(m_profileName));
      return false;
    }

    QDir profiles(m_core.settings().paths().profiles());
    if (profiles.exists(name)) {
      if (QMessageBox::question(qApp->activeWindow(), tr("Profile"),
                                tr("A profile named \"%1\" already exists. Install the "
                                   "collection into it?")
                                    .arg(name)) != QMessageBox::Yes) {
        return false;
      }
      // the folder's own name, as Windows ignores case
      for (const QString& folder : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (folder.compare(name, Qt::CaseInsensitive) == 0) {
          name = folder;
          break;
        }
      }
    } else {
      try {
        Profile profile(name, m_core.managedGame(), m_core.gameFeatures(), false);
        m_core.profileCreated(&profile);
      } catch (const std::exception& e) {
        QMessageBox::warning(qApp->activeWindow(), tr("Profile"),
                             tr("The profile couldn't be created: %1")
                                 .arg(QString::fromUtf8(e.what())));
        return false;
      }
    }
    m_profileName = name;
    m_profileMade = true;
    saveState();
  }

  current = m_core.currentProfile();
  if (current == nullptr ||
      current->name().compare(m_profileName, Qt::CaseInsensitive) != 0) {
    // a profile made for this collection is switched to; any other only when asked
    if (m_started && QMessageBox::question(
                         qApp->activeWindow(), tr("Profile"),
                         tr("This collection is being installed into the profile "
                            "\"%1\". Switch to it?")
                             .arg(m_profileName)) != QMessageBox::Yes) {
      return false;
    }
    if (!m_switchProfile || !m_switchProfile(m_profileName)) {
      QMessageBox::warning(qApp->activeWindow(), tr("Profile"),
                           tr("The profile \"%1\" couldn't be selected.")
                               .arg(m_profileName));
      return false;
    }
  }
  return true;
}

QString CollectionSession::folderFingerprint(const QString& modName) const
{
  // what tells this folder from another one put under the same name later:
  // when it was made, and what its meta.ini says it is
  const QDir mods(m_core.settings().paths().mods());
  const QFileInfo fi(mods.filePath(modName));
  if (!fi.isDir()) {
    return {};
  }
  QSettings meta(QDir(fi.absoluteFilePath()).filePath("meta.ini"), QSettings::IniFormat);
  return QStringLiteral("%1|%2|%3|%4")
      .arg(fi.birthTime().toMSecsSinceEpoch())
      .arg(meta.value("modid").toString())
      .arg(meta.value("version").toString())
      .arg(meta.value("installationFile").toString());
}

QStringList CollectionSession::approvedNow() const
{
  // only folders that are still the ones the user said yes to
  QStringList names;
  for (auto it = m_approved.begin(); it != m_approved.end(); ++it) {
    const QString now = folderFingerprint(it.key());
    if (!now.isEmpty() && now == it.value().toString()) {
      names.append(it.key());
    }
  }
  return names;
}

bool CollectionSession::askAboutExisting()
{
  const QDir mods(m_core.settings().paths().mods());
  std::vector<int> rows;
  QStringList names;
  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    const Entry& e = m_entries[row];
    if (!e.selected || isFinished(e) || !mods.exists(e.modName)) {
      continue;
    }
    // approved before, and still the same folder
    const QString key = e.modName.toLower();
    if (m_approved.contains(key) &&
        m_approved.value(key).toString() == folderFingerprint(e.modName)) {
      continue;
    }
    rows.push_back(row);
    names.append(e.modName);
  }
  if (rows.empty()) {
    return true;
  }

  FluentContentDialog dlg(
      qApp->activeWindow(), QStringLiteral("CollectionExistingDialog"),
      tr("Mods already installed"),
      tr("%n mod(s) of this collection are already installed under the same name. "
         "What should happen to them?",
         "", static_cast<int>(rows.size())));
  auto* list = new QListWidget(&dlg);
  list->addItems(names);
  list->setSelectionMode(QAbstractItemView::NoSelection);
  list->setMaximumHeight(180);
  dlg.body()->addWidget(list);
  auto* replace = dlg.addButton(tr("Replace them"), true);
  auto* keep    = dlg.addButton(tr("Keep the installed ones"));
  auto* cancel  = dlg.addButton(tr("Cancel"));
  QPushButton* clicked = nullptr;
  for (auto* b : {replace, keep, cancel}) {
    connect(b, &QPushButton::clicked, &dlg, [&dlg, &clicked, b] {
      clicked = b;
      dlg.accept();
    });
  }
  dlg.askFirst(cancel);
  dlg.exec();

  if (clicked == replace) {
    // these folders exactly, as they are now
    for (int row : rows) {
      const QString& name = m_entries[row].modName;
      m_approved.insert(name.toLower(), folderFingerprint(name));
    }
  } else if (clicked == keep) {
    for (int row : rows) {
      m_entries[row].keptExisting = true;
      setStatus(row, Status::Installed, tr("Kept the installed mod"));
      m_core.modList()->setActive(m_entries[row].modName, true);
    }
  } else {
    return false;
  }
  saveState();
  return true;
}

void CollectionSession::start()
{
  if (m_stage != Stage::Ready && m_stage != Stage::Paused && m_stage != Stage::Done) {
    return;
  }
  if (m_core.installationManager()->isRunning()) {
    QMessageBox::information(qApp->activeWindow(), tr("Collection"),
                             tr("Another installation is in progress. Try again when "
                                "it has finished."));
    return;
  }

  if (!prepareProfile() || !askAboutExisting()) {
    return;
  }

  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    if (!m_entries[row].selected && m_entries[row].status != Status::Installed) {
      setStatus(row, Status::Skipped, tr("Not selected"));
    }
  }

  // the separator the collection's mods go under
  QString separator = m_separatorName.trimmed();
  if (fixDirectoryName(separator) && !separator.isEmpty()) {
    const QString internal = separator + "_separator";
    if (m_core.modList()->getMod(internal) == nullptr) {
      GuessedValue<QString> name(internal, GUESS_USER);
      if (m_core.createMod(name) != nullptr) {
        m_core.refresh();
      }
    }
  }

  m_started = true;
  setStage(Stage::Running);
  saveState();

  scanDownloads();

  // a free account starts with the first page
  if (!isPremium() && m_autoOpen) {
    bool waiting = false;
    for (const auto& e : m_entries) {
      waiting = waiting || e.status == Status::WaitingForClick;
    }
    if (!waiting) {
      openNext();
    }
  }
}

void CollectionSession::pumpInstall()
{
  if (m_stage != Stage::Running || m_installing || m_finalizing) {
    return;
  }
  if (m_core.installationManager()->isRunning()) {
    return;  // the watchdog comes back
  }

  // the earliest phase with something left
  int phase = INT_MAX;
  for (const auto& e : m_entries) {
    if (!isFinished(e)) {
      phase = std::min(phase, modOf(e).phase);
    }
  }
  if (phase == INT_MAX) {
    finalize();
    return;
  }

  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    const Entry& e  = m_entries[row];
    const auto& mod = modOf(e);
    if (isFinished(e) || mod.phase != phase) {
      continue;
    }
    if (e.status == Status::Downloaded ||
        (mod.type == SourceType::Bundle && e.status == Status::Pending)) {
      m_installing = true;
      QPointer<CollectionSession> self(this);
      QTimer::singleShot(0, this, [self, row] {
        if (!self) {
          return;
        }
        // whatever happens in the install, the next one can start
        const auto done = qScopeGuard([self] {
          if (self) {
            self->m_installing = false;
            self->saveState();
            emit self->changed();
            self->scheduleScan();
          }
        });
        try {
          self->installEntry(row);
        } catch (const std::exception& e) {
          if (self) {
            self->setStatus(row, Status::Failed,
                            tr("Not installed: %1").arg(QString::fromUtf8(e.what())));
          }
        } catch (...) {
          if (self) {
            self->setStatus(row, Status::Failed,
                            tr("Not installed: the installation was cancelled or "
                               "failed."));
          }
        }
      });
      return;
    }
  }
  // waiting for downloads or for the user
}

bool CollectionSession::installEntry(int row)
{
  const Profile* current = m_core.currentProfile();
  if (current == nullptr ||
      current->name().compare(m_profileName, Qt::CaseInsensitive) != 0) {
    pause();
    QMessageBox::information(qApp->activeWindow(), tr("Collection"),
                             tr("Another profile was selected. The collection is paused; "
                                "press Continue to resume it in \"%1\".")
                                 .arg(m_profileName));
    return false;
  }

  Entry& e        = m_entries[row];
  const auto& mod = modOf(e);

  if (mod.type == SourceType::Bundle) {
    return installBundle(row);
  }

  if (!QFile::exists(e.archive)) {
    e.archive.clear();
    setStatus(row, Status::Failed, tr("The downloaded file is missing."));
    return false;
  }

  InstallationManager* im = m_core.installationManager();
  const QString choices   = mod.choicesJson();

  // the approved folders and the preset are for this install only, whatever
  // way it ends
  im->setApprovedReplacements(approvedNow());
  qApp->setProperty(PresetResultProperty, QVariant());
  qApp->setProperty(PresetProperty, choices.isEmpty() ? QVariant() : QVariant(choices));
  const auto reset = qScopeGuard([im] {
    im->setApprovedReplacements({});
    qApp->setProperty(PresetProperty, QVariant());
    qApp->setProperty(PresetResultProperty, QVariant());
  });

  log::debug("collection {}: installing '{}'", m_link.slug, e.modName);
  QPointer<CollectionSession> self(this);
  ModInfo::Ptr installed;
  QString error;
  try {
    installed = m_core.installArchive(e.archive, -1, false, nullptr, e.modName);
  } catch (const std::exception& ex) {
    error = QString::fromUtf8(ex.what());
  }
  const QString preset = qApp->property(PresetResultProperty).toString();

  if (!self) {
    return false;
  }

  if (!error.isEmpty()) {
    setStatus(row, Status::Failed, tr("Not installed: %1").arg(error));
    return false;
  }
  if (installed == nullptr) {
    setStatus(row, Status::Failed,
              tr("Not installed: the installation was cancelled or failed."));
    return false;
  }

  e.modName      = installed->name();
  e.presetResult = choices.isEmpty() ? QString() : (preset.isEmpty() ? "none" : preset);
  m_core.modList()->setActive(e.modName, true);
  setStatus(row, Status::Installed);
  return true;
}

namespace
{

// a path inside a bundle, relative to the mod's folder: no root, no drive, no
// "..", no empty parts, nothing Windows would read as something else
bool isSafeRelativePath(const QString& relative)
{
  if (relative.isEmpty()) {
    return false;
  }
  static const QString forbidden = QStringLiteral("<>:\"|?*");
  for (const QString& part : relative.split('/')) {
    if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') ||
        part.endsWith(' ')) {
      return false;
    }
    for (const QChar c : part) {
      if (c.unicode() < 32 || forbidden.contains(c)) {
        return false;
      }
    }
  }
  const std::filesystem::path p(relative.toStdWString());
  return !p.has_root_name() && !p.has_root_directory() && p.is_relative();
}

// whether target, once resolved, is inside base
bool staysInside(const std::filesystem::path& base, const std::filesystem::path& target)
{
  std::error_code ec;
  const auto b = std::filesystem::weakly_canonical(base, ec);
  if (ec) {
    return false;
  }
  const auto t = std::filesystem::weakly_canonical(target, ec);
  if (ec) {
    return false;
  }
  const auto rel = t.lexically_relative(b);
  return !rel.empty() && *rel.begin() != ".." && !rel.is_absolute();
}

}  // namespace

bool CollectionSession::installBundle(int row)
{
  Entry& e        = m_entries[row];
  const auto& mod = modOf(e);

  if (mod.fileExpression.isEmpty() || m_archivePath.isEmpty()) {
    setStatus(row, Status::Failed,
              tr("The mod's files are missing from the collection's archive."));
    return false;
  }

  // the files are in bundled/<fileExpression>/ in the collection's archive
  std::unique_ptr<Archive> archive = CreateArchive();
  if (!archive->isValid() || !archive->open(m_archivePath.toStdWString(), {})) {
    setStatus(row, Status::Failed, tr("The collection's archive couldn't be opened."));
    return false;
  }
  const auto closeArchive = qScopeGuard([&archive] {
    archive->close();
  });

  // every name is checked before anything is extracted: one that could land
  // outside the mod's folder refuses the whole mod
  const QString prefix = QStringLiteral("bundled/%1/").arg(mod.fileExpression);
  std::vector<std::pair<FileData*, QString>> files;
  for (FileData* f : archive->getFileList()) {
    QString path = QString::fromStdWString(f->getArchiveFilePath());
    path.replace('\\', '/');
    if (f->isDirectory() || !path.startsWith(prefix, Qt::CaseInsensitive)) {
      continue;
    }
    const QString relative = path.mid(prefix.size());
    if (!isSafeRelativePath(relative)) {
      log::warn("collection {}: unsafe path in bundle '{}': {}", m_link.slug,
                mod.fileExpression, path);
      setStatus(row, Status::Failed,
                tr("The mod's files in the collection's archive have unsafe names."));
      return false;
    }
    files.emplace_back(f, relative);
  }
  if (files.empty()) {
    setStatus(row, Status::Failed,
              tr("The mod's files are missing from the collection's archive."));
    return false;
  }

  // an installed mod set aside by Replace comes back if this fails
  GuessedValue<QString> name(e.modName, GUESS_USER);
  IModInterface* created = nullptr;
  {
    InstallationManager* im = m_core.installationManager();
    im->setApprovedReplacements(approvedNow());
    const auto reset = qScopeGuard([im] {
      im->setApprovedReplacements({});
    });
    created = m_core.createMod(name, false);
  }
  if (created == nullptr) {
    setStatus(row, Status::Failed, tr("Not installed: the mod couldn't be created."));
    return false;
  }
  const bool isNew = m_core.createdModIsNew();

  const QString target = QDir::toNativeSeparators(created->absolutePath());
  const std::filesystem::path base(target.toStdWString());
  bool ok = true;
  QString error;
  for (auto& [f, relative] : files) {
    const std::filesystem::path out = base / relative.toStdWString();
    if (!staysInside(base, out)) {
      ok    = false;
      error = tr("The mod's files in the collection's archive have unsafe names.");
      break;
    }
    f->addOutputFilePath(QDir::toNativeSeparators(relative).toStdWString());
  }

  if (ok) {
    try {
      ok = archive->extract(target.toStdWString(), {}, {},
                            [&error](const std::wstring& message) {
                              error = QString::fromStdWString(message);
                            });
    } catch (const std::exception& ex) {
      ok    = false;
      error = QString::fromUtf8(ex.what());
    }
  }

  // what was written stays in the folder
  if (ok) {
    for (const auto& [f, relative] : files) {
      for (const auto& written : f->getOutputFilePaths()) {
        if (!staysInside(base, base / written)) {
          ok    = false;
          error = tr("The mod's files in the collection's archive have unsafe names.");
        }
      }
    }
  }

  if (!ok) {
    // the old version back, or the new folder gone
    m_core.finishCreatedMod(false);
    if (isNew) {
      QDir(target).removeRecursively();
    }
    m_core.refresh();
    setStatus(row, Status::Failed,
              error.isEmpty() ? tr("The mod's files couldn't be extracted.")
                              : tr("The mod's files couldn't be extracted: %1").arg(error));
    return false;
  }

  m_core.finishCreatedMod(true);
  m_core.refresh();

  e.modName = static_cast<const QString&>(name);
  if (ModInfo::getIndex(e.modName) != UINT_MAX) {
    ModInfo::getByIndex(ModInfo::getIndex(e.modName))->setVersion(VersionInfo(mod.version));
  }
  m_core.modList()->setActive(e.modName, true);
  setStatus(row, Status::Installed);
  return true;
}

void CollectionSession::finalize()
{
  if (m_finalizing) {
    return;
  }
  m_finalizing = true;
  setLoading(tr("Ordering the mods…"));

  applyModOrder();
  m_core.refresh();

  QPointer<CollectionSession> self(this);
  m_core.onNextRefresh(
      [self] {
        if (!self) {
          return;
        }
        self->applyPlugins();
        self->m_finalizing = false;
        self->m_loadingText.clear();
        self->setStage(Stage::Done);
        self->saveState();
        emit self->finished();
      },
      OrganizerCore::RefreshCallbackGroup::EXTERNAL,
      OrganizerCore::RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

void CollectionSession::applyModOrder()
{
  std::vector<int> mods;
  std::map<int, int> rowOfMod;
  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    const Entry& e = m_entries[row];
    if (e.status == Status::Installed && !e.modName.isEmpty() &&
        ModInfo::getIndex(e.modName) != UINT_MAX) {
      mods.push_back(e.mod);
      rowOfMod[e.mod] = row;
    }
  }

  const SortResult sorted = sortModsByRules(m_manifest, mods);
  m_modCycles.clear();
  for (const auto& [a, b] : sorted.broken) {
    m_modCycles.append(QStringLiteral("%1 > %2").arg(m_entries[rowOfMod[a]].modName,
                                                     m_entries[rowOfMod[b]].modName));
  }

  ModList* list     = m_core.modList();
  QString separator = m_separatorName.trimmed();
  if (fixDirectoryName(separator) && !separator.isEmpty() &&
      list->getMod(separator + "_separator") != nullptr) {
    list->setPriority(separator + "_separator", INT_MAX);
  }

  QStringList names;
  for (int mod : sorted.order) {
    const QString& name = m_entries[rowOfMod[mod]].modName;
    list->setPriority(name, INT_MAX);
    names.append(name);
  }
  list->setActive(names, true);
}

void CollectionSession::applyPlugins()
{
  m_pluginCycles.clear();
  m_offerLoot = false;
  if (!isBethesda()) {
    return;
  }

  PluginList* plugins = m_core.pluginList();
  for (const auto& p : m_manifest.plugins) {
    if (plugins->state(p.name) != IPluginList::STATE_MISSING) {
      plugins->setState(p.name,
                        p.enabled ? IPluginList::STATE_ACTIVE : IPluginList::STATE_INACTIVE);
    }
  }

  if (!m_manifest.pluginRules.empty()) {
    QStringList all = plugins->pluginNames();
    std::stable_sort(all.begin(), all.end(), [plugins](const QString& a, const QString& b) {
      return plugins->priority(a) < plugins->priority(b);
    });
    QStringList masters, others;
    for (const QString& p : all) {
      (plugins->isMasterFlagged(p) ? masters : others).append(p);
    }
    QStringList order = sortPluginsByRules(masters, m_manifest.pluginRules, &m_pluginCycles);
    order += sortPluginsByRules(others, m_manifest.pluginRules, &m_pluginCycles);
    plugins->setLoadOrder(order);
    plugins->refreshLoadOrder();
  } else {
    m_offerLoot = true;
  }

  m_core.savePluginList();
}

QString CollectionSession::summary() const
{
  QStringList lines;
  lines.append(tr("Installed: %1", "summary count").arg(count(Status::Installed)));
  lines.append(tr("Skipped: %1", "summary count").arg(count(Status::Skipped)));
  lines.append(tr("Failed: %1", "summary count").arg(count(Status::Failed)));

  QStringList failed, byHand, patched;
  for (const auto& e : m_entries) {
    if (e.status == Status::Failed) {
      failed.append(QStringLiteral("%1: %2").arg(e.modName, e.reason));
    }
    if (e.status == Status::Installed && !modOf(e).choicesJson().isEmpty() &&
        e.presetResult != "applied" && !e.keptExisting) {
      byHand.append(e.modName);
    }
    if (e.status == Status::Installed && modOf(e).hasPatches) {
      patched.append(e.modName);
    }
  }

  auto section = [&lines](const QString& title, const QStringList& items) {
    if (!items.isEmpty()) {
      lines.append(QString());
      lines.append(title);
      for (const auto& i : items) {
        lines.append(QStringLiteral("  • ") + i);
      }
    }
  };
  section(tr("Not installed:"), failed);
  section(tr("Installer options chosen by hand:"), byHand);
  section(tr("Patches not applied (not supported yet):"), patched);
  section(tr("Mod order rules dropped to break a loop:"), m_modCycles);
  section(tr("Plugin order rules dropped to break a loop:"), m_pluginCycles);

  return lines.join('\n');
}
