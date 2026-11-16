#include "collectionsession.h"

#include "createinstancedialog.h"
#include "instancemanager.h"
#include "shared/appconfig.h"
#include "shared/util.h"

#include "downloadmanager.h"
#include "fluentdialogs.h"
#include "installationmanager.h"
#include "messagedialog.h"
#include "modinfo.h"
#include "categories.h"
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
#include <QCryptographicHash>
#include <QEventLoop>
#include <QThread>
#include <QDirIterator>
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

// a collection's archive holds collection.json and its bundled mods
constexpr qint64 MaxArchiveSize = qint64(2) << 30;

const char* TargetSetting       = "Collections/installTarget";
const char* AutoDownloadSetting = "Collections/autoDownload";
const char* CategorySeparatorsSetting = "Collections/categorySeparators";

QString targetToString(CollectionSession::Target t)
{
  switch (t) {
  case CollectionSession::Target::NewInstance:
    return QStringLiteral("instance");
  case CollectionSession::Target::NewProfile:
    return QStringLiteral("profile");
  case CollectionSession::Target::CurrentProfile:
    break;
  }
  return QStringLiteral("current");
}

std::optional<CollectionSession::Target> targetFromString(const QString& s)
{
  if (s == "instance") {
    return CollectionSession::Target::NewInstance;
  } else if (s == "profile") {
    return CollectionSession::Target::NewProfile;
  } else if (s == "current") {
    return CollectionSession::Target::CurrentProfile;
  }
  return {};
}

QString pendingPath()
{
  return QDir(Settings::instance().paths().base()).filePath("collections/pending.json");
}

const char* QuietProperty = "mo2r.collectionQuiet";

constexpr int MaxRetries = 3;

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
    collection {
      name user { name } game { domainName }
      latestPublishedRevision { revisionNumber }
    }
    modFiles {
      optional
      fileId
      file { modId name version sizeInBytes uri mod { name modCategory { categoryId name } } }
    }
  }
})";

}  // namespace

CollectionSession::CollectionSession(OrganizerCore& core, const CollectionLink& link,
                                     QObject* parent)
    : QObject(parent), m_core(core), m_link(link), m_revision(link.revision)
{
  m_target       = lastTarget();
  m_autoDownload = lastAutoDownload();
  m_categorySeparators = lastCategorySeparators();
  if (m_target == Target::NewInstance && !canMakeInstance()) {
    m_target = Target::NewProfile;
  }

  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(3000);
  connect(&m_saveTimer, &QTimer::timeout, this, [this] {
    flushState();
  });

  m_scanTimer.setSingleShot(true);
  m_scanTimer.setInterval(250);
  connect(&m_scanTimer, &QTimer::timeout, this, &CollectionSession::scanDownloads);

  m_watchdog.setInterval(5000);
  connect(&m_watchdog, &QTimer::timeout, this, &CollectionSession::scheduleScan);

  auto& nexus = NexusInterface::instance();
  connect(&nexus, &NexusInterface::nxmApiV2Available, this,
          &CollectionSession::onApiResult);
  // once the account is known: page by page needs its first page opened
  connect(nexus.getAccessManager(), &NXMAccessManager::credentialsReceived, this,
          [this](const APIUserAccount&) {
            QTimer::singleShot(0, this, [this] {
              emit changed();
              if (m_stage != Stage::Running || downloadsAutomatically() || !m_autoOpen) {
                return;
              }
              for (const auto& e : m_entries) {
                if (e.status == Status::WaitingForClick) {
                  return;
                }
              }
              openNext();
            });
          });
  connect(&nexus, &NexusInterface::nxmRequestFailed, this,
          &CollectionSession::onApiFailed);

  DownloadManager* dm = m_core.downloadManager();
  connect(dm, &DownloadManager::update, this, &CollectionSession::onDownloadUpdate);
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
  if (m_stage == Stage::Running) {
    qApp->setProperty(QuietProperty, QVariant());
  }
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
  flushState();
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

void CollectionSession::setTarget(Target t)
{
  if (placeFixed() || (t == Target::NewInstance && !canMakeInstance())) {
    return;
  }
  m_target = t;
  GlobalSettings::setValue(TargetSetting, targetToString(t));
  saveState();
}

CollectionSession::Target CollectionSession::lastTarget()
{
  return targetFromString(GlobalSettings::value(TargetSetting).toString())
      .value_or(Target::NewInstance);
}

void CollectionSession::setAutoDownload(bool b)
{
  m_autoDownload = b;
  GlobalSettings::setValue(AutoDownloadSetting, b);
  saveState();
  emit changed();
  scheduleScan();
}

void CollectionSession::setCategorySeparators(bool b)
{
  m_categorySeparators = b;
  GlobalSettings::setValue(CategorySeparatorsSetting, b);
  saveState();
}

bool CollectionSession::lastCategorySeparators()
{
  return GlobalSettings::value(CategorySeparatorsSetting, true).toBool();
}

bool CollectionSession::lastAutoDownload()
{
  return GlobalSettings::value(AutoDownloadSetting, true).toBool();
}

bool CollectionSession::downloadsAutomatically() const
{
  if (!m_autoDownload) {
    return false;
  }
  const auto account = NexusInterface::instance().getAPIUserAccount();
  return !account.isValid() || account.type() == APIUserAccountTypes::Premium;
}

bool CollectionSession::canMakeInstance() const
{
  return InstanceManager::singleton().allowedToChangeInstance();
}

void CollectionSession::setInstanceName(const QString& name)
{
  m_instanceName = name;
}

QString CollectionSession::instanceNameProblem(const QString& name) const
{
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return tr("Enter a name for the instance.");
  }
  QString fixed = trimmed;
  if (!fixDirectoryName(fixed) || fixed != trimmed) {
    return tr("Windows doesn't allow this name for a folder.");
  }

  // a folder with that name, in any case
  const auto& instances = InstanceManager::singleton();
  if (QFileInfo::exists(instances.instancePath(trimmed))) {
    return tr("An instance with this name already exists.");
  }
  for (const QString& path : instances.globalInstancePaths()) {
    if (QFileInfo(path).fileName().compare(trimmed, Qt::CaseInsensitive) == 0) {
      return tr("An instance with this name already exists.");
    }
  }
  return {};
}

bool CollectionSession::claims(const QString& nxmUrl) const
{
  static const QRegularExpression exp(
      QStringLiteral("^nxm://[^/]+/mods/(\\d+)/files/(\\d+)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto m = exp.match(nxmUrl.trimmed());
  if (!m.hasMatch() || m_entries.empty() ||
      (m_stage != Stage::Running && m_stage != Stage::Paused)) {
    return false;
  }
  const int modId  = m.captured(1).toInt();
  const int fileId = m.captured(2).toInt();
  for (const auto& e : m_entries) {
    const auto& mod = modOf(e);
    if (e.selected && !e.byHand && mod.type == SourceType::Nexus &&
        mod.modId == modId && mod.fileId == fileId &&
        (e.status == Status::Pending || e.status == Status::WaitingForClick ||
         e.status == Status::Downloading)) {
      return true;
    }
  }
  return false;
}

QString CollectionSession::takePendingCollection()
{
  QFile f(pendingPath());
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QString link =
      QJsonDocument::fromJson(f.readAll()).object().value("link").toString();
  f.close();
  // once: a collection that fails to open doesn't come back at every start
  if (!QFile::remove(f.fileName())) {
    log::warn("the pending collection note couldn't be removed: {}", f.fileName());
  }
  return isCollectionNxmLink(link) ? link : QString();
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
  const bool wasRunning = m_stage == Stage::Running;
  m_stage = s;
  // the per-file notices (and their sound) stay quiet while it runs: the
  // window shows the progress
  qApp->setProperty(QuietProperty, s == Stage::Running ? QVariant(true) : QVariant());
  // once it stops, one notice says how many were held back (unrelated
  // downloads and Nexus errors among them), so none goes unnoticed
  if (wasRunning && s != Stage::Running) {
    const int hidden = qApp->property("mo2r.collectionQuietHidden").toInt();
    qApp->setProperty("mo2r.collectionQuietHidden", QVariant());
    if (hidden > 0) {
      MessageDialog::showMessage(
          QObject::tr("%1 notices weren't shown while the collection installed. "
                      "They're in MO2's log.")
              .arg(hidden),
          nullptr);
    }
  }
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
    m_latestRevision =
        c.value("latestPublishedRevision").toMap().value("revisionNumber").toInt();

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
      const QVariantMap category =
          file.value("mod").toMap().value("modCategory").toMap();
      f.categoryId = category.value("categoryId").toInt();
      f.category   = category.value("name").toString();
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
    if (!m_archiveReply) {
      return;
    }
    const QByteArray data = m_archiveReply->readAll();
    if (m_archiveFile.write(data) != data.size()) {
      m_archiveProblem = tr("%1 couldn't be written.")
                             .arg(QDir::toNativeSeparators(m_archivePath));
      m_archiveReply->abort();
    } else if (m_archiveFile.size() > MaxArchiveSize) {
      m_archiveProblem = tr("The collection's archive is too large.");
      m_archiveReply->abort();
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

  const QByteArray rest = reply->readAll();
  if (m_archiveProblem.isEmpty() && m_archiveFile.write(rest) != rest.size()) {
    m_archiveProblem =
        tr("%1 couldn't be written.").arg(QDir::toNativeSeparators(m_archivePath));
  }
  const qint64 written = m_archiveFile.size();
  m_archiveFile.close();

  // a full disk, a cut connection: never kept as if whole
  const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
  if (m_archiveProblem.isEmpty() && reply->error() == QNetworkReply::NoError &&
      length.isValid() && length.toLongLong() != written) {
    m_archiveProblem = tr("The collection's download was incomplete.");
  }
  if (!m_archiveProblem.isEmpty()) {
    m_archiveFile.remove();
    const QString problem = m_archiveProblem;
    m_archiveProblem.clear();
    fail(problem);
    return;
  }

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

QString CollectionSession::uniqueModName(int row, const QSet<QString>& taken) const
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

  // the names of the rows before this one, without case
  QString name = base;
  for (int n = 2; taken.contains(name.toCaseFolded()); ++n) {
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
        e.categoryId = it->second.categoryId;
        e.category   = it->second.category;
      }
    }
    m_entries.push_back(e);
  }

  // what was done before
  const QJsonArray saved = m_saved.value("entries").toArray();
  QHash<int, int> rowOfMod;
  rowOfMod.reserve(static_cast<qsizetype>(m_entries.size()));
  for (int row = static_cast<int>(m_entries.size()) - 1; row >= 0; --row) {
    rowOfMod.insert(m_entries[row].mod, row);  // the first row of a mod wins
  }
  for (const auto& v : saved) {
    const QJsonObject o = v.toObject();
    const int mod       = o.value("mod").toInt(-1);
    const auto found    = rowOfMod.constFind(mod);
    if (found != rowOfMod.constEnd()) {
      Entry& e = m_entries[found.value()];
      if (m_manifest.mods[mod].fileId != o.value("fileId").toInt()) {
        continue;
      }
      e.status       = statusFromString(o.value("status").toString());
      e.reason       = o.value("reason").toString();
      e.archive      = o.value("archive").toString();
      e.modName      = o.value("modName").toString();
      e.selected     = o.value("selected").toBool(true) || !m_manifest.mods[mod].optional;
      e.presetResult = o.value("preset").toString();
      e.keptExisting = o.value("kept").toBool();
      e.fingerprint  = o.value("fingerprint").toString();
      for (const auto& n : o.value("notes").toArray()) {
        e.notes.append(n.toString());
      }
      if (e.status == Status::Downloaded && !QFile::exists(e.archive)) {
        e.status = Status::Pending;
        e.archive.clear();
      }
    }
  }

  m_rowByFileId.clear();
  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    const int fileId = m_manifest.mods[m_entries[row].mod].fileId;
    if (fileId > 0 && !m_rowByFileId.contains(fileId)) {
      m_rowByFileId.insert(fileId, row);
    }
  }

  QSet<QString> taken;
  taken.reserve(static_cast<qsizetype>(m_entries.size()));
  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    if (m_entries[row].modName.isEmpty()) {
      m_entries[row].modName = uniqueModName(row, taken);
    }
    taken.insert(m_entries[row].modName.toCaseFolded());
  }

  if (m_profileName.isEmpty()) {
    m_profileName = displayName();
    fixDirectoryName(m_profileName);
  }
  if (m_instanceName.isEmpty()) {
    m_instanceName = displayName();
    fixDirectoryName(m_instanceName);
  }
  if (m_separatorName.isEmpty()) {
    m_separatorName = displayName();
    fixDirectoryName(m_separatorName);
  }

  m_loadingText.clear();
  // a new revision of a collection installed before: what changed
  if (m_saved.isEmpty() && m_updateFrom > 0) {
    prepareUpdate();
  }

  setStage(m_saved.value("finished").toBool() ? Stage::Done
           : m_started                        ? Stage::Paused
                                              : Stage::Ready);
  scanDownloads();

  // the state file read is no longer needed, only that there was one
  if (!m_saved.isEmpty()) {
    m_saved = QJsonObject{{QStringLiteral("loaded"), true}};
  }

  // opened by the restart into the instance made for it
  if (m_autoStart) {
    m_autoStart = false;
    if (m_stage == Stage::Ready || m_stage == Stage::Paused) {
      QTimer::singleShot(0, this, &CollectionSession::start);
    }
  }
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
  // older state files had only "new profile" or not
  m_target = targetFromString(o.value("target").toString())
                 .value_or(o.value("newProfile").toBool(true) ? Target::NewProfile
                                                              : Target::CurrentProfile);
  if (m_target == Target::NewInstance && !canMakeInstance()) {
    m_target = Target::NewProfile;
  }
  m_instanceName  = o.value("instance").toString();
  m_profileName   = o.value("profile").toString();
  m_profileMade   = o.value("profileMade").toBool();
  m_separatorName = o.value("separator").toString();
  m_autoOpen      = o.value("autoOpen").toBool(true);
  m_autoDownload  = o.value("autoDownload").toBool(m_autoDownload);
  m_placeFixed    = o.value("placeFixed").toBool();
  m_profileCreatedHere = o.value("profileCreatedHere").toBool();
  m_toRemove.clear();
  for (const auto& v : o.value("toRemove").toArray()) {
    m_toRemove.append(v.toString());
  }
  m_categorySeparators =
      o.value("categorySeparators").toBool(m_categorySeparators);
  m_categorySeparatorNames = o.value("categorySeparatorNames").toObject();
  // a blanket "replace" from an older state file isn't taken: the folders are
  // asked about again
  m_approved = o.value("approvedReplacements").toObject();
}

void CollectionSession::saveState() const
{
  // written soon, once for many changes; at once on pause, at the end and
  // when the window closes (flushState())
  m_stateDirty = true;
  if (!m_saveTimer.isActive()) {
    m_saveTimer.start();
  }
}

void CollectionSession::flushState() const
{
  m_saveTimer.stop();
  if (!m_stateDirty) {
    return;
  }
  m_stateDirty = false;
  writeState();
}

void CollectionSession::writeState() const
{
  // nothing to keep before the collection was read and started
  if (!m_haveManifest || (!m_started && m_saved.isEmpty())) {
    return;
  }

  const QString path = statePath(m_link.slug, m_revision);
  if (path.isEmpty()) {
    return;
  }
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(stateObject()).toJson(QJsonDocument::Compact));
    if (!f.commit()) {
      log::warn("collection state couldn't be saved to {}", path);
    }
  }
}

QJsonObject CollectionSession::stateObject() const
{
  QJsonObject o;
  o.insert("slug", m_link.slug);
  o.insert("revision", m_revision);
  o.insert("game", m_domain);
  o.insert("name", displayName());
  o.insert("author", author());
  o.insert("archive", m_archivePath);
  o.insert("started", m_started);
  o.insert("finished", m_stage == Stage::Done);
  o.insert("target", targetToString(m_target));
  o.insert("instance", m_instanceName);
  o.insert("profile", m_profileName);
  o.insert("profileMade", m_profileMade);
  o.insert("separator", m_separatorName);
  o.insert("autoOpen", m_autoOpen);
  o.insert("autoDownload", m_autoDownload);
  o.insert("placeFixed", m_placeFixed);
  o.insert("profileCreatedHere", m_profileCreatedHere);
  o.insert("toRemove", QJsonArray::fromStringList(m_toRemove));
  o.insert("categorySeparators", m_categorySeparators);
  o.insert("categorySeparatorNames", m_categorySeparatorNames);
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
    j.insert("fingerprint", e.fingerprint);
    j.insert("modId", m_manifest.mods[e.mod].modId);
    j.insert("notes", QJsonArray::fromStringList(e.notes));
    entries.append(j);
  }
  o.insert("entries", entries);
  return o;
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

void CollectionSession::onDownloadUpdate(int index)
{
  // a running download's progress: only its row; anything else is a scan
  DownloadManager* dm = m_core.downloadManager();
  if (index < 0 || index >= dm->numTotalDownloads()) {
    scheduleScan();
    return;
  }
  const auto state = dm->getState(index);
  const ModRepositoryFileInfo* info = dm->getFileInfo(index);
  if (info == nullptr || (state != DownloadManager::STATE_STARTED &&
                          state != DownloadManager::STATE_DOWNLOADING)) {
    scheduleScan();
    return;
  }
  const auto row = m_rowByFileId.constFind(info->fileID);
  if (row == m_rowByFileId.constEnd()) {
    return;  // not one of the collection's
  }
  Entry& e = m_entries[row.value()];
  if (e.status != Status::Downloading || modOf(e).modId != info->modID) {
    scheduleScan();
    return;
  }
  const int progress = dm->getProgress(index).first;
  if (e.progress != progress) {
    e.progress = progress;
    emit entryChanged(row.value());
  }
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

  // the downloads by file id and by name and size, and the links being
  // fetched: once per scan, so a scan is linear in files and downloads
  QHash<int, std::vector<int>> byFileId;
  QHash<QString, std::vector<int>> byNameSize;
  byFileId.reserve(total);
  for (int i = 0; i < total; ++i) {
    if (const ModRepositoryFileInfo* info = dm->getFileInfo(i)) {
      byFileId[info->fileID].push_back(i);
    }
    byNameSize[QStringLiteral("%1|%2")
                   .arg(dm->getFileName(i).toCaseFolded())
                   .arg(dm->getFileSize(i))]
        .push_back(i);
  }
  QSet<quint64> pendingFiles;
  for (int i = 0; i < dm->numPendingDownloads(); ++i) {
    const auto p = dm->getPendingDownload(i);
    pendingFiles.insert((quint64(quint32(std::get<1>(p))) << 32) | quint32(std::get<2>(p)));
  }

  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    Entry& e        = m_entries[row];
    const auto& mod = modOf(e);
    if (mod.type != SourceType::Nexus || !e.selected ||
        (e.status != Status::Pending && e.status != Status::WaitingForClick &&
         e.status != Status::Downloading)) {
      continue;
    }

    int complete = -1, running = -1, stalled = -1;
    // the candidates, in the downloads' order as a full pass would see them
    std::vector<int> candidates;
    if (mod.fileId > 0) {
      const auto it = byFileId.constFind(mod.fileId);
      if (it != byFileId.constEnd()) {
        candidates = it.value();
      }
    }
    if (!e.fileName.isEmpty() && e.size > 0) {
      const auto it = byNameSize.constFind(
          QStringLiteral("%1|%2").arg(e.fileName.toCaseFolded()).arg(e.size));
      if (it != byNameSize.constEnd()) {
        candidates.insert(candidates.end(), it.value().begin(), it.value().end());
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()),
                         candidates.end());
      }
    }
    for (int i : candidates) {
      if (complete >= 0) {
        break;
      }
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
      } else if (byId && (state == DownloadManager::STATE_ERROR ||
                          (state == DownloadManager::STATE_PAUSED &&
                           !dm->getLastError(i).isEmpty()))) {
        // stopped by a failure (the download manager pauses one that is out
        // of its own retries), not by the user
        stalled = i;
      } else if (byId && isDownloadRunning(state)) {
        running = i;
      }
    }

    const bool pending = pendingFiles.contains(
        (quint64(quint32(mod.modId)) << 32) | quint32(mod.fileId));

    const Status before = e.status;
    if (stalled < 0) {
      e.errorSeen.invalidate();
    }
    if (complete >= 0) {
      e.archive = dm->getFilePath(complete);
      setStatus(row, Status::Downloaded);
    } else if (running >= 0 || pending) {
      e.awaitingClick = false;
      e.retryAt.invalidate();
      const int progress = running >= 0 ? dm->getProgress(running).first : -1;
      const bool queued  = e.queuedByUs;
      setStatus(row, Status::Downloading);
      e.queuedByUs = queued;
      if (e.progress != progress) {
        e.progress = progress;
        emit entryChanged(row);
      }
    } else if (stalled >= 0 && (e.status == Status::Downloading ||
                                e.status == Status::WaitingForClick)) {
      handleStalled(row, stalled);
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

  // the 5 s check runs while files download, wait for a click or wait to be
  // installed; download signals start a scan otherwise
  bool watching = false;
  for (const auto& e : m_entries) {
    if (e.status == Status::Downloading || e.status == Status::WaitingForClick ||
        e.status == Status::Downloaded) {
      watching = true;
      break;
    }
  }
  if (watching && !m_watchdog.isActive()) {
    m_watchdog.start();
  } else if (!watching && m_watchdog.isActive()) {
    m_watchdog.stop();
  }

  // the next page opens once the last one's file came
  if (arrived && !downloadsAutomatically() && m_autoOpen) {
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
  // only a Premium account queues: one not known yet waits for the sign-in
  if (m_stage != Stage::Running || !isPremium() || !m_autoDownload || m_pumping) {
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

void CollectionSession::handleStalled(int row, int download)
{
  // nothing is retried while the collection is paused or finishing
  if (m_stage != Stage::Running) {
    return;
  }
  Entry& e            = m_entries[row];
  DownloadManager* dm = m_core.downloadManager();
  const QString why   = dm->getLastError(download);

  // a free account's file waits for the user's new click: the stopped
  // download stays as it is until that click resumes it
  if (e.awaitingClick) {
    return;
  }

  // a download in the error state is still in the download manager's own
  // retries (2, 10 and 30 seconds): they come first
  if (dm->getState(download) == DownloadManager::STATE_ERROR) {
    if (!e.errorSeen.isValid()) {
      e.errorSeen.start();
    }
    if (e.errorSeen.elapsed() < 50000) {
      return;
    }
  }

  // one retry at a time per file; a retry just sent gets a moment to fetch
  // its new link before the stop counts again
  if (e.retryPending || (e.retryAt.isValid() && e.retryAt.elapsed() < 15000)) {
    return;
  }
  if (e.retries >= MaxRetries) {
    setStatus(row, Status::Failed,
              why.isEmpty() ? tr("The download failed after %1 tries.").arg(MaxRetries)
                            : tr("The download failed after %1 tries: %2")
                                  .arg(MaxRetries)
                                  .arg(why));
    return;
  }

  // the stop counts here, once per stop, and the retry follows after 5, 20
  // and 60 seconds
  static const int delays[MaxRetries] = {5, 20, 60};
  const int delay = delays[e.retries];
  ++e.retries;
  e.retryPending = true;
  setStatus(row, Status::Downloading,
            tr("The download stopped; trying again (%1 of %2).")
                .arg(e.retries)
                .arg(MaxRetries));
  QPointer<CollectionSession> self(this);
  const int fileId = modOf(e).fileId;
  QTimer::singleShot(delay * 1000, this, [self, row, fileId] {
    if (self) {
      self->retryDownload(row, fileId);
    }
  });
}

int CollectionSession::stalledDownload(const Entry& e) const
{
  // the file's download, when it is stopped by a failure and nothing is
  // already fetching a link for it
  DownloadManager* dm = m_core.downloadManager();
  const auto& mod     = modOf(e);
  for (int i = 0; i < dm->numPendingDownloads(); ++i) {
    const auto p = dm->getPendingDownload(i);
    if (std::get<1>(p) == mod.modId && std::get<2>(p) == mod.fileId) {
      return -1;
    }
  }
  for (int i = 0; i < dm->numTotalDownloads(); ++i) {
    const ModRepositoryFileInfo* info = dm->getFileInfo(i);
    if (info == nullptr || info->fileID != mod.fileId ||
        (mod.modId > 0 && info->modID != mod.modId)) {
      continue;
    }
    const auto state = dm->getState(i);
    if (state == DownloadManager::STATE_ERROR ||
        (state == DownloadManager::STATE_PAUSED && !dm->getLastError(i).isEmpty())) {
      return i;
    }
  }
  return -1;
}

void CollectionSession::retryDownload(int row, int fileId)
{
  if (row < 0 || row >= static_cast<int>(m_entries.size())) {
    return;
  }
  Entry& e        = m_entries[row];
  const auto& mod = modOf(e);
  if (mod.fileId != fileId) {
    return;
  }
  e.retryPending = false;
  if (m_stage != Stage::Running) {
    // paused meanwhile: the stop is looked at again on Continue
    --e.retries;
    return;
  }
  if (e.status != Status::Downloading && e.status != Status::WaitingForClick) {
    return;  // it came, or the user did something else meanwhile
  }
  // only a download still stopped: one that resumed or is being fetched
  // would get a box about it already running
  if (stalledDownload(e) < 0) {
    return;
  }
  e.retryAt.start();
  e.errorSeen.invalidate();

  if (isPremium() && m_autoDownload && !e.byHand && gameMatches(domainOf(e))) {
    // a fresh link from Nexus; the download manager resumes the stopped file
    // with it (one request for the file, within the API limits)
    log::debug("collection {}: retrying the download of file {}", m_link.slug, fileId);
    m_core.downloadManager()->addNXMDownload(QStringLiteral("nxm://%1/mods/%2/files/%3")
                                                 .arg(domainOf(e))
                                                 .arg(mod.modId)
                                                 .arg(mod.fileId),
                                             false);
  } else {
    // a free account's link comes only from a click on the page
    e.awaitingClick = true;
    setStatus(row, Status::WaitingForClick,
              tr("Click the file on Nexus again: its download stopped."));
    openPage(row);
  }
  saveState();
  emit changed();
}

void CollectionSession::retryFailed()
{
  int count = 0;
  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    Entry& e = m_entries[row];
    if (e.status == Status::Failed && e.selected) {
      e.retries = 0;
      e.retryAt.invalidate();
      e.errorSeen.invalidate();
      retry(row);
      ++count;
    }
  }
  if (count > 0 && m_stage != Stage::Running) {
    start();
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
    if (e.status == Status::Pending && !e.byHand && !downloadsAutomatically()) {
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
  e.selected      = true;
  e.retries       = 0;
  e.retryPending  = false;
  e.awaitingClick = false;
  e.retryAt.invalidate();
  e.errorSeen.invalidate();
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
    flushState();
  }
}

bool CollectionSession::prepareProfile()
{
  const Profile* current = m_core.currentProfile();

  if (m_target == Target::CurrentProfile) {
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
        m_profileCreatedHere = true;
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

void CollectionSession::startInNewInstance()
{
  const QString problem = instanceNameProblem(m_instanceName);
  if (!problem.isEmpty()) {
    QMessageBox::warning(qApp->activeWindow(), tr("New instance"), problem);
    return;
  }
  const QString name = m_instanceName.trimmed();

  // the same game, folder and edition as this instance
  const IPluginGame* managed = m_core.managedGame();
  IPluginGame* game = managed != nullptr ? m_core.getGame(managed->gameShortName()) : nullptr;
  if (game == nullptr) {
    QMessageBox::warning(qApp->activeWindow(), tr("New instance"),
                         tr("The instance couldn't be created: no game plugin."));
    return;
  }
  const Settings& settings = m_core.settings();
  CreateInstanceDialog::ProfileSettings profiles;
  profiles.localInis           = settings.profileLocalInis();
  profiles.localSaves          = settings.profileLocalSaves();
  profiles.archiveInvalidation = settings.profileArchiveInvalidation();
  profiles.eldenRingSaveMode   = settings.eldenRingDefaultSaveMode();

  const auto ci = CreateInstanceDialog::globalCreationInfo(
      name, game, QDir::toNativeSeparators(game->gameDirectory().absolutePath()),
      settings.game().edition().value_or(QString()), profiles);

  QStringList lines;
  if (!CreateInstanceDialog::createInstance(ci, [&lines](QString line) {
        lines.append(line);
      })) {
    QMessageBox::warning(qApp->activeWindow(), tr("New instance"),
                         tr("The instance couldn't be created.") + "\n\n" +
                             lines.join('\n'));
    return;
  }
  log::info("collection {}: instance '{}' created", m_link.slug, name);

  // the collection's state in the new instance, as chosen here (its mods into
  // the instance's own profile), and the note that opens it at its start
  QJsonObject state = stateObject();
  state.insert("started", false);
  state.insert("finished", false);
  state.insert("target", targetToString(Target::CurrentProfile));
  state.insert("profile", QString());
  state.insert("profileMade", false);
  // the place is the new instance's own profile
  state.insert("placeFixed", true);
  // its Default profile is made by the new instance, for the collection
  state.insert("profileCreatedHere", true);
  state.insert("instance", QString());
  state.insert("approvedReplacements", QJsonObject());
  state.insert("archive", QString());
  QJsonArray entries;
  for (const auto& v : state.value("entries").toArray()) {
    QJsonObject e = v.toObject();
    // its downloads folder is its own: everything starts again from Nexus
    const QString status = e.value("status").toString();
    e.insert("status", status == "skipped" ? status : QStringLiteral("pending"));
    e.insert("archive", QString());
    e.insert("reason", status == "skipped" ? e.value("reason") : QJsonValue(QString()));
    e.insert("kept", false);
    e.insert("preset", QString());
    entries.append(e);
  }
  state.insert("entries", entries);

  // the collection's archive, so it isn't fetched again there (small; the
  // mods' files are downloaded in the new instance)
  if (!m_archivePath.isEmpty() && QFile::exists(m_archivePath)) {
    const QDir archives(QDir(ci.dataPath).filePath(
        QString::fromStdWString(AppConfig::downloadPath()) + "/collections"));
    archives.mkpath(".");
    if (!QFile::copy(m_archivePath,
                     archives.filePath(QFileInfo(m_archivePath).fileName()))) {
      log::warn("collection {}: the archive couldn't be copied to the new instance",
                m_link.slug);
    }
  }

  const QDir target(QDir(ci.dataPath).filePath("collections"));
  target.mkpath(".");
  const QString link = QStringLiteral("nxm://%1/collections/%2/revisions/%3")
                           .arg(m_domain, m_link.slug)
                           .arg(m_revision);
  QJsonObject pending;
  pending.insert("link", link);
  QSaveFile stateFile(target.filePath(QStringLiteral("%1-%2.json")
                                          .arg(m_link.slug)
                                          .arg(m_revision)));
  QSaveFile pendingFile(target.filePath("pending.json"));
  bool written = stateFile.open(QIODevice::WriteOnly) &&
                 stateFile.write(QJsonDocument(state).toJson()) > 0 &&
                 stateFile.commit();
  written = written && pendingFile.open(QIODevice::WriteOnly) &&
            pendingFile.write(QJsonDocument(pending).toJson()) > 0 &&
            pendingFile.commit();
  if (!written) {
    log::warn("collection {}: the note for the new instance couldn't be written",
              m_link.slug);
  }

  // switched to as the instance manager does: Mod Organizer restarts in it
  const QString text =
      written ? tr("The instance \"%1\" was created. Mod Organizer now restarts in it, "
                   "and the collection goes on there.")
                    .arg(name)
              : tr("The instance \"%1\" was created. Mod Organizer now restarts in it; "
                   "open the collection there again.")
                    .arg(name);
  if (QMessageBox::information(qApp->activeWindow(), tr("New instance"), text,
                               QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) {
    QMessageBox::information(qApp->activeWindow(), tr("New instance"),
                             tr("The collection goes on when you open the instance "
                                "\"%1\" from Manage instances.")
                                 .arg(name));
    return;
  }
  if (ExitModOrganizer(Exit::Restart)) {
    // only once closing is certain, as the instance manager does
    InstanceManager::singleton().setCurrentInstance(Instance(ci.dataPath, false));
  } else {
    QMessageBox::information(qApp->activeWindow(), tr("New instance"),
                             tr("Mod Organizer can't close right now. The collection "
                                "goes on when you open the instance \"%1\".")
                                 .arg(name));
  }
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

  // an instance of its own: made here, the collection goes on there
  if (m_target == Target::NewInstance && !m_profileMade) {
    startInNewInstance();
    return;
  }

  if (!prepareProfile() || !askAboutExisting()) {
    return;
  }

  // an update: the earlier revision's mods that aren't in this one
  if (!m_toRemove.isEmpty() && !removeDropped()) {
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
        if (Profile* profile = m_core.currentProfile()) {
          profile->writeModlistNow();
        }
      }
    }
  }

  m_started = true;
  setStage(Stage::Running);
  saveState();

  scanDownloads();

  // page by page starts with the first page; automatic downloads start in the
  // scan above, or with the next scan once the sign-in is known
  if (!downloadsAutomatically() && m_autoOpen) {
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

  // a mod for the game's folder goes in as Vortex lays it out; when its
  // archive doesn't show where the game's folder is, the usual installers
  e.rootLayout = false;
  if (!rootLayoutData(mod).isEmpty()) {
    bool handled = false;
    const bool ok = installRootMod(row, &handled);
    if (handled) {
      return ok;
    }
  }

  InstallationManager* im = m_core.installationManager();
  const QString choices   = mod.choicesJson();

  // the approved folders and the preset are for this install only, whatever
  // way it ends
  im->setApprovedReplacements(approvedNow());
  qApp->setProperty(PresetResultProperty, QVariant());
  qApp->setProperty(PresetProperty, choices.isEmpty() ? QVariant() : QVariant(choices));
  // installers that only ask for the mod's name take the collection's
  qApp->setProperty("mo2r.collectionInstall", true);
  const auto reset = qScopeGuard([im] {
    im->setApprovedReplacements({});
    qApp->setProperty("mo2r.collectionInstall", QVariant());
    qApp->setProperty(PresetProperty, QVariant());
    qApp->setProperty(PresetResultProperty, QVariant());
  });

  log::debug("collection {}: installing '{}'", m_link.slug, e.modName);
  QPointer<CollectionSession> self(this);
  ModInfo::Ptr installed;
  QString error;
  QSet<QString> existingBefore;
  QStringList approvedBefore;
  try {
    // which folders were there (read once, then kept as installs add to it),
    // and which the user said may be replaced
    if (!m_modFoldersRead) {
      for (const QString& folder : QDir(m_core.settings().paths().mods())
                                       .entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        m_modFolders.insert(folder.toLower());
      }
      m_modFoldersRead = true;
    }
    existingBefore = m_modFolders;
    approvedBefore = approvedNow();
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
  // into a folder that was there: only one the user said may be replaced gets
  // the collection's patches and file list (a Merge keeps the user's files)
  const QString folder = e.modName.toLower();
  if (existingBefore.contains(folder) &&
      !approvedBefore.contains(folder, Qt::CaseInsensitive)) {
    if (!modOf(e).patches.empty() || !modOf(e).hashes.empty()) {
      e.notes = {tr("Patches and the exact file list weren't applied: the mod went into "
                    "an existing mod's folder.")};
    }
  } else {
    postInstall(row);
  }
  m_modFolders.insert(folder);
  e.fingerprint = folderFingerprint(e.modName);
  setStatus(row, Status::Installed);
  return true;
}

namespace
{

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

QString CollectionSession::rootLayoutData(const CollectionMod& mod) const
{
  // a mod with installer choices is laid out by its FOMOD, as Vortex does
  if (!isGameRootType(mod.deployType) || !mod.choicesJson().isEmpty()) {
    return {};
  }
  const IPluginGame* game = m_core.managedGame();
  if (game == nullptr) {
    return {};
  }
  // a data folder right in the game's (Data, Data Files): Root/ is then the
  // game's folder, and the data folder's files are the mod's own
  const QString data   = QDir::cleanPath(game->dataDirectory().absolutePath());
  const QString parent = QFileInfo(data).absolutePath();
  if (parent.compare(QDir::cleanPath(game->gameDirectory().absolutePath()),
                     Qt::CaseInsensitive) != 0) {
    return {};
  }
  return QFileInfo(data).fileName();
}

bool CollectionSession::installRootMod(int row, bool* handled)
{
  *handled        = false;
  Entry& e        = m_entries[row];
  const auto& mod = modOf(e);
  const QString dataName = rootLayoutData(mod);

  std::unique_ptr<Archive> archive = CreateArchive();
  if (!archive->isValid() || !archive->open(e.archive.toStdWString(), {})) {
    // the usual installers say why
    return false;
  }
  const auto closeArchive = qScopeGuard([&archive] {
    archive->close();
  });

  QStringList paths;
  std::vector<std::pair<FileData*, QString>> all;
  for (FileData* f : archive->getFileList()) {
    if (f->isDirectory()) {
      continue;
    }
    QString path = QString::fromStdWString(f->getArchiveFilePath());
    path.replace('\\', '/');
    paths.append(path);
    all.emplace_back(f, path);
  }
  const QString base = gameRootBase(paths, mod.deployType, mod.hashes, dataName);
  if (base.isNull()) {
    log::debug("collection {}: '{}' is a {} mod, but its archive doesn't show the "
               "game's folder; the installers take it",
               m_link.slug, e.modName, mod.deployType);
    return false;
  }

  // the files under that folder, each to where MO2 has it; outside it,
  // nothing (Vortex leaves those out too)
  std::vector<std::pair<FileData*, QString>> files;
  for (auto& [f, path] : all) {
    if (!path.startsWith(base, Qt::CaseInsensitive)) {
      continue;
    }
    const QString target = rootLayoutPath(path.mid(base.size()), dataName);
    if (!isSafeRelativePath(target)) {
      log::warn("collection {}: unsafe path in '{}': {}", m_link.slug, e.modName, path);
      *handled = true;
      setStatus(row, Status::Failed, tr("The mod's archive has unsafe file names."));
      return false;
    }
    files.emplace_back(f, target);
  }
  if (files.empty()) {
    return false;
  }
  *handled = true;

  // an installed mod set aside by Replace comes back if this fails; the
  // folders the user said may be replaced, before this one changes
  const QStringList approvedBefore = approvedNow();
  GuessedValue<QString> name(e.modName, GUESS_USER);
  IModInterface* created = nullptr;
  {
    InstallationManager* im = m_core.installationManager();
    im->setApprovedReplacements(approvedBefore);
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
  const QString folder = static_cast<const QString&>(name);
  const bool replaced  = approvedBefore.contains(folder, Qt::CaseInsensitive);

  const QString target = QDir::toNativeSeparators(created->absolutePath());
  const std::filesystem::path out(target.toStdWString());
  bool ok = true;
  QString error;
  for (auto& [f, relative] : files) {
    if (!staysInside(out, out / relative.toStdWString())) {
      ok    = false;
      error = tr("The mod's archive has unsafe file names.");
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
        if (!staysInside(out, out / written)) {
          ok    = false;
          error = tr("The mod's archive has unsafe file names.");
        }
      }
    }
  }
  if (!ok) {
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

  e.modName = folder;
  if (ModInfo::getIndex(e.modName) != UINT_MAX) {
    ModInfo::getByIndex(ModInfo::getIndex(e.modName))->setVersion(VersionInfo(mod.version));
  }
  m_core.modList()->setActive(e.modName, true);
  e.rootLayout = true;
  log::info("collection {}: '{}' installed for the game's folder ({} files, from '{}')",
            m_link.slug, e.modName, files.size(), base);
  // as for any mod: only a new folder, or one the user said may be replaced,
  // gets the collection's patches and file list
  if (isNew || replaced) {
    postInstall(row);
  } else if (!mod.patches.empty() || !mod.hashes.empty()) {
    e.notes = {tr("Patches and the exact file list weren't applied: the mod went into "
                  "an existing mod's folder.")};
  }
  m_modFolders.insert(e.modName.toLower());
  e.fingerprint = folderFingerprint(e.modName);
  setStatus(row, Status::Installed);
  return true;
}

// ---------------------------------------------------------------------------
// phase 2: patches, exact file lists, file overrides, INI tweaks, updates

bool CollectionSession::extractArchiveFiles(
    const std::function<bool(const QString& path)>& wanted, const QString& dir,
    std::vector<std::pair<QString, QString>>* found) const
{
  // path in the archive (forward slashes) to the extracted file
  if (m_archivePath.isEmpty()) {
    return false;
  }
  std::unique_ptr<Archive> archive = CreateArchive();
  if (!archive->isValid() || !archive->open(m_archivePath.toStdWString(), {})) {
    return false;
  }
  int n = 0;
  for (FileData* f : archive->getFileList()) {
    QString path = QString::fromStdWString(f->getArchiveFilePath());
    path.replace('\\', '/');
    if (f->isDirectory() || !wanted(path) || !isSafeRelativePath(path)) {
      continue;
    }
    // flat in the folder: numbered, so no path from the archive is used
    const QString name = QStringLiteral("%1.bin").arg(n++);
    f->addOutputFilePath(name.toStdWString());
    found->emplace_back(path, QDir(dir).filePath(name));
  }
  bool ok = true;
  if (n > 0) {
    ok = archive->extract(QDir::toNativeSeparators(dir).toStdWString(), {}, {}, {});
  }
  archive->close();
  return ok;
}

void CollectionSession::postInstall(int row)
{
  Entry& e        = m_entries[row];
  const auto& mod = modOf(e);
  const unsigned int index = ModInfo::getIndex(e.modName);
  if (index == UINT_MAX || (mod.patches.empty() && mod.hashes.empty())) {
    return;
  }
  e.notes.clear();
  const QDir dir(ModInfo::getByIndex(index)->absolutePath());
  // a mod installed for the game's folder: the collection's paths are the
  // game folder's, the mod's are MO2's (Root/...)
  const QString rootData = e.rootLayout ? rootLayoutData(mod) : QString();
  const auto place = [&rootData](const QString& path) {
    return rootData.isEmpty() ? path : rootLayoutPath(path, rootData);
  };
  std::vector<FileHash> listed = mod.hashes;
  for (FileHash& h : listed) {
    h.path = place(h.path);
  }

  // binary patches, from patches/<mod>/<file>.diff in the collection's archive
  if (!mod.patches.empty()) {
    QTemporaryDir temp;
    std::vector<std::pair<QString, QString>> diffs;
    if (temp.isValid()) {
      extractArchiveFiles(
          [](const QString& path) {
            return path.startsWith(QStringLiteral("patches/"), Qt::CaseInsensitive) &&
                   path.endsWith(QStringLiteral(".diff"), Qt::CaseInsensitive);
          },
          temp.path(), &diffs);
    }

    int applied = 0;
    for (const auto& [file, crcText] : mod.patches) {
      const QString relative = place(QString(file).replace('\\', '/'));
      if (!isSafeRelativePath(relative)) {
        e.notes.append(tr("Patch not applied to %1: unsafe file name.").arg(file));
        continue;
      }
      // as Vortex: a patch needs the CRC of the file it was made for
      const auto expected = parseCrc(crcText);
      if (!expected) {
        e.notes.append(tr("Patch not applied to %1: the collection gives no valid "
                          "checksum for it.")
                           .arg(file));
        continue;
      }
      // and comes from this mod's own folder of the archive only
      // (named by the path in the collection, the game folder's for a Root/ mod)
      const QString suffix = "/" + normalizedPath(file) + ".diff";
      QString diffFile;
      for (const auto& [path, extracted] : diffs) {
        const QString n = normalizedPath(path);
        if (!n.endsWith(suffix)) {
          continue;
        }
        const QString folder = n.mid(8, n.size() - 8 - suffix.size());
        if (folder == normalizedPath(mod.name) || folder == normalizedPath(e.modName)) {
          diffFile = extracted;
          break;
        }
      }
      if (diffFile.isEmpty()) {
        e.notes.append(
            tr("Patch not applied to %1: it is missing from the collection's archive.")
                .arg(file));
        continue;
      }

      QFile original(dir.filePath(relative));
      QFile diff(diffFile);
      if (!original.open(QIODevice::ReadOnly) || !diff.open(QIODevice::ReadOnly)) {
        e.notes.append(tr("Patch not applied to %1: the file is missing.").arg(file));
        continue;
      }
      // read whole: not for a file past the patcher's limit
      if (original.size() > MaxPatchedSize || !patchSizeAllowed(diff.size(), original.size())) {
        e.notes.append(tr("Patch not applied to %1: the patch or the file is too large.")
                           .arg(file));
        continue;
      }
      const QByteArray before = original.readAll();
      original.close();
      if (*expected != crc32(before)) {
        e.notes.append(tr("Patch not applied to %1: the file isn't the one the patch "
                          "was made for.")
                           .arg(file));
        continue;
      }
      QByteArray after;
      QString why;
      if (!bspatch(before, diff.readAll(), after, &why)) {
        e.notes.append(tr("Patch not applied to %1: %2").arg(file, why));
        continue;
      }
      // replaced at once, or the original stays
      QSaveFile out(dir.filePath(relative));
      if (!out.open(QIODevice::WriteOnly) || out.write(after) != after.size() ||
          !out.commit()) {
        e.notes.append(tr("Patch not applied to %1: the file couldn't be written.")
                           .arg(file));
        continue;
      }
      ++applied;
    }
    if (applied > 0) {
      e.notes.append(tr("%n file(s) patched.", "", applied));
    }
  }

  // a replicate install: only the files the collection lists
  if (!listed.empty()) {
    QStringList present;
    QDirIterator it(dir.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString relative = dir.relativeFilePath(it.next());
      if (relative.compare(QStringLiteral("meta.ini"), Qt::CaseInsensitive) != 0 &&
          !relative.endsWith(QStringLiteral(".mohidden"), Qt::CaseInsensitive)) {
        present.append(relative);
      }
    }
    // the MD5s the list asks for, on a worker thread: big files take long, and
    // the window keeps painting meanwhile
    QHash<QString, QString> presentByKey;
    for (const QString& p : present) {
      presentByKey.insert(normalizedPath(p), p);
    }
    QStringList toHash;
    for (const auto& h : listed) {
      const auto it = presentByKey.constFind(normalizedPath(h.path));
      if (!h.md5.isEmpty() && it != presentByKey.constEnd()) {
        toHash.append(it.value());
      }
    }
    toHash.removeDuplicates();
    auto sums = std::make_shared<QHash<QString, QString>>();
    if (!toHash.isEmpty()) {
      const QString root = dir.path();
      QEventLoop wait;
      std::unique_ptr<QThread> worker(QThread::create([root, toHash, sums] {
        const QDir d(root);
        for (const QString& relative : toHash) {
          QFile f(d.filePath(relative));
          QString sum;
          if (f.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Md5);
            hash.addData(&f);
            sum = QString::fromLatin1(hash.result().toHex());
          }
          sums->insert(relative, sum);
        }
      }));
      connect(worker.get(), &QThread::finished, &wait, &QEventLoop::quit);
      worker->start();
      wait.exec(QEventLoop::ExcludeUserInputEvents);
      worker->wait();
    }
    const auto md5Of = [&sums](const QString& relative) {
      return sums->value(relative);
    };
    const auto result = compareFileList(listed, present, md5Of);

    if (result.missing.size() * 2 > static_cast<qsizetype>(listed.size())) {
      // the mod's files aren't laid out as the collection's list says: nothing
      // is hidden on a guess
      e.notes.append(tr("The collection's exact file list wasn't applied: most of its "
                        "files aren't where it expects them."));
    } else {
      int hidden = 0;
      for (const QString& extra : result.extra) {
        const QString path = dir.filePath(extra);
        if (!QFile::exists(path + ".mohidden") && QFile::rename(path, path + ".mohidden")) {
          ++hidden;
        }
      }
      if (hidden > 0) {
        e.notes.append(
            tr("%n file(s) not in the collection's list were hidden.", "", hidden));
      }
      if (!result.missing.isEmpty()) {
        e.notes.append(tr("Files the collection lists that are missing: %1")
                           .arg(result.missing.mid(0, 10).join(", ")));
      }
    }
    if (!result.mismatched.isEmpty()) {
      e.notes.append(tr("Files that differ from the collection's: %1")
                         .arg(result.mismatched.mid(0, 10).join(", ")));
    }
  }
}

void CollectionSession::applyFileOverrides()
{
  m_overrideNotes.clear();
  ModList* list = m_core.modList();

  // only the mods this collection installed: the user's own are left alone
  std::vector<int> ours;
  for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
    const Entry& e = m_entries[row];
    if (e.status == Status::Installed && !e.keptExisting && e.reason.isEmpty() &&
        ModInfo::getIndex(e.modName) != UINT_MAX) {
      ours.push_back(row);
    }
  }

  // their priorities and folders, once
  std::vector<std::pair<int, QDir>> placeOf(m_entries.size());
  for (int row : ours) {
    const Entry& e = m_entries[row];
    placeOf[row]   = {list->priority(e.modName),
                      QDir(ModInfo::getByIndex(ModInfo::getIndex(e.modName))->absolutePath())};
  }

  for (int winner : ours) {
    const Entry& w = m_entries[winner];
    if (modOf(w).fileOverrides.isEmpty()) {
      continue;
    }
    const QDir& winnerDir    = placeOf[winner].second;
    const int winnerPriority = placeOf[winner].first;
    int hidden               = 0;
    // a mod installed for the game's folder: its paths are the game folder's.
    // In MO2 a data file is at the top of any mod and the rest in Root/, so
    // the winner's path is the same file in every other mod
    const QString rootData = w.rootLayout ? rootLayoutData(modOf(w)) : QString();
    for (const QString& file : modOf(w).fileOverrides) {
      const QString collectionPath = QString(file).replace('\\', '/');
      const QString relative =
          rootData.isEmpty() ? collectionPath : rootLayoutPath(collectionPath, rootData);
      if (!isSafeRelativePath(relative) || !QFile::exists(winnerDir.filePath(relative))) {
        continue;
      }
      // the copies that would win over it by the order
      for (int other : ours) {
        const Entry& o = m_entries[other];
        Q_UNUSED(o);
        if (other == winner || placeOf[other].first <= winnerPriority) {
          continue;
        }
        const QString path = placeOf[other].second.filePath(relative);
        if (QFile::exists(path) && !QFile::exists(path + ".mohidden") &&
            QFile::rename(path, path + ".mohidden")) {
          ++hidden;
        }
      }
    }
    if (hidden > 0) {
      m_overrideNotes.append(tr("%1: %n file(s) in mods after it hidden, so its own "
                                "win.",
                                "", hidden)
                                 .arg(w.modName));
    }
  }
}

void CollectionSession::applyIniTweaks()
{
  m_iniNotes.clear();
  Profile* profile = m_core.currentProfile();
  if (profile == nullptr) {
    return;
  }

  // "INI Tweaks/<name> [<target ini>].ini" in the collection's archive
  QTemporaryDir temp;
  std::vector<std::pair<QString, QString>> tweaks;
  if (!temp.isValid() ||
      !extractArchiveFiles(
          [](const QString& path) {
            return path.startsWith(QStringLiteral("ini tweaks/"), Qt::CaseInsensitive) &&
                   !iniTweakTarget(QFileInfo(path).fileName()).isEmpty();
          },
          temp.path(), &tweaks) ||
      tweaks.empty()) {
    return;
  }

  // a profile made for the collection keeps its own INI files; one of the
  // user's own isn't changed that way without asking
  if (!profile->localSettingsEnabled()) {
    if (m_profileCreatedHere ||
        QMessageBox::question(qApp->activeWindow(), tr("INI tweaks"),
                              tr("The collection changes game settings. Give the profile "
                                 "\"%1\" its own copy of the game's INI files for "
                                 "them?")
                                  .arg(profile->name())) == QMessageBox::Yes) {
      profile->enableLocalSettings(true);
    }
  }
  const IPluginGame* game = m_core.managedGame();
  const QStringList gameInis = game != nullptr ? game->iniFiles() : QStringList();
  const QString profileDir = QDir::cleanPath(profile->absolutePath());

  bool asked      = false;
  bool documentsOk = false;
  for (const auto& [path, extracted] : tweaks) {
    const QString name   = QFileInfo(path).fileName();
    const QString target = iniTweakTarget(name);
    if (iniTweakOptional(name)) {
      m_iniNotes.append(tr("Optional INI tweak not applied: %1").arg(name));
      continue;
    }
    // only the game's own settings files, as Vortex
    if (!gameInis.contains(target, Qt::CaseInsensitive)) {
      m_iniNotes.append(
          tr("INI tweak not applied, %1 isn't one of the game's INI files: %2")
              .arg(target, name));
      continue;
    }
    const QString file = QDir::cleanPath(profile->absoluteIniFilePath(target));
    if (!file.startsWith(profileDir + "/", Qt::CaseInsensitive)) {
      // the game's own INI files in Documents: only when the user says so
      if (!asked) {
        asked       = true;
        documentsOk = QMessageBox::question(
                          qApp->activeWindow(), tr("INI tweaks"),
                          tr("The collection changes settings in the game's own INI "
                             "files in Documents: %1. Change them?")
                              .arg(gameInis.join(", "))) == QMessageBox::Yes;
      }
      if (!documentsOk) {
        m_iniNotes.append(tr("INI tweak not applied: %1").arg(name));
        continue;
      }
    }

    QFile in(extracted);
    QFile current(file);
    if (!in.open(QIODevice::ReadOnly)) {
      continue;
    }
    QString text;
    IniEncoding encoding;
    if (current.open(QIODevice::ReadOnly)) {
      text = decodeIni(current.readAll(), &encoding);
      current.close();
    }
    const QString merged = mergeIni(text, decodeIni(in.readAll(), nullptr));
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    const QByteArray bytes = encodeIni(merged, encoding);
    if (out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() &&
        out.commit()) {
      m_iniNotes.append(tr("INI tweak applied to %1: %2").arg(target, name));
    } else {
      m_iniNotes.append(tr("INI tweak not applied: %1").arg(name));
    }
  }
}

void CollectionSession::setUpdateFrom(int revision)
{
  m_updateFrom = revision;
}

QString CollectionSession::revisionLink(int revision) const
{
  return QStringLiteral("nxm://%1/collections/%2/revisions/%3")
      .arg(m_domain, m_link.slug)
      .arg(revision);
}

void CollectionSession::prepareUpdate()
{
  QFile f(statePath(m_link.slug, m_updateFrom));
  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject old = QJsonDocument::fromJson(f.readAll()).object();
  if (old.isEmpty()) {
    return;
  }

  // the place and the choices made for the earlier revision
  m_target = targetFromString(old.value("target").toString()).value_or(Target::NewProfile);
  m_profileName   = old.value("profile").toString();
  m_profileMade   = old.value("profileMade").toBool() || old.value("started").toBool();
  m_placeFixed    = true;
  m_separatorName = old.value("separator").toString(m_separatorName);
  m_autoOpen      = old.value("autoOpen").toBool(m_autoOpen);
  m_autoDownload  = old.value("autoDownload").toBool(m_autoDownload);
  m_categorySeparators     = old.value("categorySeparators").toBool(m_categorySeparators);
  m_categorySeparatorNames = old.value("categorySeparatorNames").toObject();
  m_approved               = old.value("approvedReplacements").toObject();

  // what each earlier file was, and what became of it
  struct Old
  {
    QString status, modName, reason, fingerprint;
    bool kept = false;
  };
  std::vector<RevisionFile> before;
  std::vector<Old> olds;
  for (const auto& v : old.value("entries").toArray()) {
    const QJsonObject o = v.toObject();
    before.push_back({o.value("modId").toInt(), o.value("fileId").toInt()});
    olds.push_back({o.value("status").toString(), o.value("modName").toString(),
                    o.value("reason").toString(), o.value("fingerprint").toString(),
                    o.value("kept").toBool()});
  }
  std::vector<RevisionFile> after;
  for (const auto& e : m_entries) {
    after.push_back({modOf(e).modId, modOf(e).fileId});
  }
  const RevisionDiff diff = diffRevisions(before, after);
  const auto installedByUs = [](const Old& o) {
    return o.status == "installed" && !o.kept && o.reason.isEmpty();
  };

  for (const auto& [b, a] : diff.same) {
    Entry& e = m_entries[a];
    if (olds[b].status == "installed") {
      e.status       = Status::Installed;
      e.modName      = olds[b].modName;
      e.keptExisting = olds[b].kept;
      e.reason       = olds[b].reason;
    } else if (olds[b].status == "skipped") {
      e.status = Status::Skipped;
      e.reason = olds[b].reason;
    }
  }
  for (const auto& [b, a] : diff.changed) {
    // the new file goes into the same mod: replaced without asking, as updated
    if (installedByUs(olds[b]) && !olds[b].modName.isEmpty()) {
      m_entries[a].modName = olds[b].modName;
      m_approved.insert(olds[b].modName.toLower(), folderFingerprint(olds[b].modName));
    }
  }
  m_toRemove.clear();
  m_keptNotes.clear();
  for (int b : diff.removed) {
    if (!installedByUs(olds[b]) || olds[b].modName.isEmpty()) {
      continue;
    }
    // still the folder the collection installed: else it is the user's now
    if (!olds[b].fingerprint.isEmpty() &&
        olds[b].fingerprint == folderFingerprint(olds[b].modName)) {
      m_toRemove.append(olds[b].modName);
    } else if (ModInfo::getIndex(olds[b].modName) != UINT_MAX) {
      m_keptNotes.append(olds[b].modName);
    }
  }

  m_updateNote = tr("Update from revision %1: %2 unchanged, %3 updated, %4 new, %5 "
                    "no longer in it.")
                     .arg(m_updateFrom)
                     .arg(diff.same.size())
                     .arg(diff.changed.size())
                     .arg(diff.added.size())
                     .arg(diff.removed.size());
  m_saved = old;
  m_saved.insert("finished", false);
  log::info("collection {}: {}", m_link.slug, m_updateNote);
}

bool CollectionSession::removeDropped()
{
  QStringList present;
  for (const QString& name : m_toRemove) {
    if (ModInfo::getIndex(name) != UINT_MAX) {
      present.append(name);
    }
  }
  m_toRemove.clear();
  if (present.isEmpty()) {
    return true;
  }

  // the mods folder is the instance's: other profiles may use them
  QStringList elsewhere;
  const QDir profiles(m_core.settings().paths().profiles());
  const Profile* current = m_core.currentProfile();
  for (const QString& p : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (current != nullptr && p.compare(current->name(), Qt::CaseInsensitive) == 0) {
      continue;
    }
    QFile list(profiles.filePath(p + "/modlist.txt"));
    if (!list.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QStringList lines = QString::fromUtf8(list.readAll()).split('\n');
    for (const QString& name : present) {
      if (lines.contains("+" + name, Qt::CaseInsensitive) ||
          lines.contains("+" + name + "\r", Qt::CaseInsensitive)) {
        elsewhere.append(p);
        break;
      }
    }
  }
  QString text =
      tr("%n mod(s) are no longer in this revision. Move them to the Recycle Bin?", "",
         static_cast<int>(present.size())) +
      "\n\n" + present.join('\n');
  if (!elsewhere.isEmpty()) {
    text += "\n\n" + tr("Other profiles use some of them: %1").arg(elsewhere.join(", "));
  }
  const auto answer = QMessageBox::question(
      qApp->activeWindow(), tr("Update the collection"), text,
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
  if (answer == QMessageBox::Cancel) {
    m_toRemove = present;
    return false;
  }
  if (answer == QMessageBox::Yes) {
    // the Recycle Bin, never deleted for good
    m_core.modList()->removeModsForce(present);
    m_removedNotes = present;
    m_core.refresh();
  } else {
    m_keptNotes += present;
  }
  saveState();
  return true;
}

void CollectionSession::finalizeFailed(const QString& why)
{
  log::error("collection {}: ordering failed: {}", m_link.slug, why);
  m_finalizing  = false;
  m_loadingText = tr("Ordering the mods failed: %1").arg(why);
  setStage(Stage::Paused);
  saveState();
}

void CollectionSession::finalize()
{
  if (m_finalizing) {
    return;
  }
  m_finalizing = true;
  setLoading(tr("Ordering the mods…"));

  // the mods that share files, for the grouping: their folders are read on a
  // worker thread, and the ordering goes on when it is done
  if (m_categorySeparators && !m_conflictsReady) {
    std::vector<std::pair<int, QString>> folders;
    for (const auto& e : m_entries) {
      if (e.status != Status::Installed || e.modName.isEmpty()) {
        continue;
      }
      const unsigned int index = ModInfo::getIndex(e.modName);
      if (index != UINT_MAX) {
        folders.emplace_back(e.mod, ModInfo::getByIndex(index)->absolutePath());
      }
    }
    auto pairs = std::make_shared<std::vector<std::pair<int, int>>>();
    QThread* worker = QThread::create([folders, pairs] {
      *pairs = sharedFilePairs(folders);
    });
    QPointer<CollectionSession> self(this);
    connect(worker, &QThread::finished, this, [self, worker, pairs] {
      worker->deleteLater();
      if (!self) {
        return;
      }
      self->m_conflictPairs  = std::move(*pairs);
      self->m_conflictsReady = true;
      self->m_finalizing     = false;
      self->finalize();
    });
    worker->start();
    return;
  }
  m_conflictsReady = false;

  try {
    applyModOrder();
    applyFileOverrides();
    m_core.refresh();
  } catch (const std::exception& ex) {
    m_conflictPairs.clear();
    finalizeFailed(QString::fromUtf8(ex.what()));
    return;
  }
  m_conflictPairs.clear();
  m_conflictPairs.shrink_to_fit();

  QPointer<CollectionSession> self(this);
  m_core.onNextRefresh(
      [self] {
        if (!self) {
          return;
        }
        try {
          self->applyPlugins();
          self->applyIniTweaks();
        } catch (const std::exception& ex) {
          self->finalizeFailed(QString::fromUtf8(ex.what()));
          return;
        }
        // files put back meanwhile (Retry failed): the collection goes on
        for (const auto& e : self->m_entries) {
          if (!self->isFinished(e)) {
            self->m_finalizing = false;
            self->m_loadingText.clear();
            self->saveState();
            emit self->changed();
            self->scheduleScan();
            return;
          }
        }
        self->m_finalizing = false;
        self->m_loadingText.clear();
        self->setStage(Stage::Done);
        self->saveState();
        self->flushState();
        // the file lists and patches of installed mods are no longer needed
        for (const auto& e : self->m_entries) {
          if (e.status == Status::Installed) {
            auto& mod = self->m_manifest.mods[e.mod];
            std::vector<FileHash>().swap(mod.hashes);
            std::vector<std::pair<QString, QString>>().swap(mod.patches);
          }
        }
        // once, at the end
        QApplication::beep();
        emit self->finished();
      },
      OrganizerCore::RefreshCallbackGroup::EXTERNAL,
      OrganizerCore::RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

std::vector<std::pair<int, int>>
CollectionSession::sharedFilePairs(const std::vector<std::pair<int, QString>>& folders)
{
  // one index over every file: its path (without case) by a 64-bit hash, to
  // the first mod that has it; the paths two or more mods have keep all of
  // them. No set of strings per mod
  QHash<quint64, int> first;
  QHash<quint64, std::vector<int>> shared;
  for (const auto& [mod, path] : folders) {
    const QDir dir(path);
    QDirIterator walk(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    QSet<quint64> own;  // a mod's own repeats count once
    while (walk.hasNext()) {
      const QString relative = dir.relativeFilePath(walk.next()).toLower();
      if (relative == QLatin1String("meta.ini") ||
          relative.endsWith(QLatin1String(".mohidden"))) {
        continue;
      }
      const quint64 key = static_cast<quint64>(qHash(relative, 0x9E3779B97F4A7C15ull));
      if (own.contains(key)) {
        continue;
      }
      own.insert(key);
      const auto it = first.constFind(key);
      if (it == first.constEnd()) {
        first.insert(key, mod);
      } else {
        auto& owners = shared[key];
        if (owners.empty()) {
          owners.push_back(it.value());
        }
        owners.push_back(mod);
      }
    }
  }

  QSet<quint64> seen;
  std::vector<std::pair<int, int>> pairs;
  for (auto it = shared.cbegin(); it != shared.cend(); ++it) {
    const auto& owners = it.value();
    for (size_t a = 0; a < owners.size(); ++a) {
      for (size_t b = a + 1; b < owners.size(); ++b) {
        const int x = std::min(owners[a], owners[b]);
        const int y = std::max(owners[a], owners[b]);
        if (x == y) {
          continue;
        }
        const quint64 key = (quint64(quint32(x)) << 32) | quint32(y);
        if (!seen.contains(key)) {
          seen.insert(key);
          pairs.emplace_back(x, y);
        }
      }
    }
  }
  return pairs;
}

QString CollectionSession::categoryName(const Entry& e) const
{
  // the curator's category, else the Nexus mod's
  const QString curator = modOf(e).category;
  return !curator.isEmpty() ? curator : e.category.trimmed();
}

int CollectionSession::categoryFor(const Entry& e) const
{
  auto& factory      = CategoryFactory::instance();
  const QString name = categoryName(e);

  // the Nexus category, through the mapping MO2 keeps (categories.dat); only
  // when it is the category the collection names
  const bool nexusSame =
      e.categoryId > 0 &&
      (modOf(e).category.isEmpty() ||
       modOf(e).category.compare(e.category, Qt::CaseInsensitive) == 0);
  if (nexusSame) {
    const unsigned int index = factory.resolveNexusID(e.categoryId);
    if (index > 0 && index < factory.numCategories()) {
      return factory.getCategoryID(index);
    }
  }
  if (name.isEmpty()) {
    return 0;
  }

  // one of the user's categories with that name
  for (unsigned int i = 1; i < factory.numCategories(); ++i) {
    if (factory.getCategoryName(i).compare(name, Qt::CaseInsensitive) == 0) {
      return factory.getCategoryID(i);
    }
  }

  // a new one, tied to its Nexus category when there is one
  std::vector<CategoryFactory::NexusCategory> nexus;
  if (nexusSame) {
    nexus.emplace_back(e.category, e.categoryId);
  }
  const int id = factory.addCategory(name, nexus, 0);
  if (id > 0 && !m_createdCategories.contains(name)) {
    m_createdCategories.append(name);
  }
  return id > 0 ? id : 0;
}

QString CollectionSession::categorySeparator(const QString& category)
{
  ModList* list = m_core.modList();

  // the one made for this collection before
  const QString known = m_categorySeparatorNames.value(category.toLower()).toString();
  if (!known.isEmpty() && list->getMod(known + "_separator") != nullptr) {
    return known;
  }

  // its name, or with the collection's in front when the user has one already
  QString name = category;
  fixDirectoryName(name);
  if (name.isEmpty() || list->getMod(name + "_separator") != nullptr ||
      list->getMod(name) != nullptr) {
    QString prefixed = QStringLiteral("%1 - %2").arg(m_separatorName.trimmed(), category);
    fixDirectoryName(prefixed);
    name = prefixed;
    for (int n = 2; list->getMod(name + "_separator") != nullptr; ++n) {
      name = QStringLiteral("%1 (%2)").arg(prefixed).arg(n);
    }
  }

  GuessedValue<QString> internal(name + "_separator", GUESS_USER);
  if (m_core.createMod(internal) == nullptr) {
    return {};
  }
  m_categorySeparatorNames.insert(category.toLower(), name);
  return name;
}

std::vector<QStringList> CollectionSession::otherProfileModLists() const
{
  // every other profile's modlist.txt, names only, highest priority first
  std::vector<QStringList> result;
  const Profile* current = m_core.currentProfile();
  const QDir profiles(m_core.settings().paths().profiles());
  for (const QString& p : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (current != nullptr && p.compare(current->name(), Qt::CaseInsensitive) == 0) {
      continue;
    }
    QFile file(profiles.filePath(p + "/modlist.txt"));
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    QStringList lines;
    for (QString line : QString::fromUtf8(file.readAll()).split('\n')) {
      line = line.trimmed();
      if (!line.isEmpty() && !line.startsWith('#')) {
        lines.append(line.mid(1));
      }
    }
    result.push_back(std::move(lines));
  }
  return result;
}

bool CollectionSession::separatorEmptyEverywhere(
    const QString& internalName, const std::vector<QStringList>& otherProfiles) const
{
  // in this profile: no mod between it and the next separator by priority
  const Profile* current = m_core.currentProfile();
  const unsigned int self = ModInfo::getIndex(internalName);
  if (current == nullptr || self == UINT_MAX) {
    return false;
  }
  const int from = current->getModPriority(self);
  int until      = INT_MAX;
  std::vector<int> modPriorities;
  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    ModInfo::Ptr info = ModInfo::getByIndex(i);
    if (i == self || info->hasAutomaticPriority()) {
      continue;
    }
    const int p = current->getModPriority(i);
    if (info->isSeparator()) {
      if (p > from) {
        until = std::min(until, p);
      }
    } else {
      modPriorities.push_back(p);
    }
  }
  for (int p : modPriorities) {
    if (p > from && p < until) {
      return false;
    }
  }

  // in every other profile's modlist.txt (highest priority first): no mod
  // line between it and the separator above it
  for (const QStringList& lines : otherProfiles) {
    int at = -1;
    for (int k = 0; k < lines.size(); ++k) {
      if (lines[k].compare(internalName, Qt::CaseInsensitive) == 0) {
        at = k;
        break;
      }
    }
    if (at < 0) {
      continue;
    }
    for (int k = at - 1; k >= 0; --k) {
      if (lines[k].endsWith(QLatin1String("_separator"), Qt::CaseInsensitive)) {
        break;
      }
      return false;  // a mod under it in that profile
    }
  }
  return true;
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

  // each mod installed here gets its category; the user's own mods (kept as
  // they were, or marked as done) keep theirs. Not part of the order
  std::map<int, QString> groupOf;
  for (int mod : sorted.order) {
    const Entry& e   = m_entries[rowOfMod[mod]];
    const int index  = static_cast<int>(ModInfo::getIndex(e.modName));
    ModInfo::Ptr info = ModInfo::getByIndex(index);
    int category      = 0;
    if (!e.keptExisting && e.reason.isEmpty() && info != nullptr) {
      category = categoryFor(e);
      if (category > 0) {
        info->setCategory(category, true);
        info->setPrimaryCategory(category);
        info->saveMeta();
      }
    } else if (info != nullptr) {
      category = info->primaryCategory();
    }
    const QString name =
        category > 0 ? CategoryFactory::instance().getCategoryNameByID(category) : QString();
    groupOf[mod] = name;
  }

  // grouped under a separator per category. Only the rules between mods that
  // share a file count: the others change nothing the game sees
  m_overflowCount = 0;
  GroupResult grouped;
  grouped.order = sorted.order;
  if (m_categorySeparators) {
    // inside a category the collection's own order: phases, then mods[]
    const auto position = [&rowOfMod](int mod) {
      return rowOfMod[mod];
    };
    grouped = groupByCategory(
        sorted,
        [&groupOf](int mod) {
          return groupOf[mod];
        },
        {}, position, &m_conflictPairs);
    // one section only: its separator would say nothing more
    const size_t sectionCount = grouped.groups.size() + (grouped.overflow.empty() ? 0 : 1);
    if (sectionCount < 2) {
      grouped.grouped = false;
      grouped.order   = sorted.order;
    }
  }

  // the separators first, then one refresh, so their indexes exist
  std::vector<std::pair<QString, std::vector<int>>> sections;
  QSet<QString> usedSeparators;
  if (grouped.grouped) {
    for (const auto& [category, members] : grouped.groups) {
      const QString title = category.isEmpty() ? tr("Other") : category;
      sections.push_back({categorySeparator(title), members});
    }
    if (!grouped.overflow.empty()) {
      sections.push_back({categorySeparator(tr("Patches and overrides")), grouped.overflow});
      m_overflowCount = static_cast<int>(grouped.overflow.size());
    }
    for (const auto& section : sections) {
      usedSeparators.insert(section.first.toLower());
    }
    m_core.refresh();
  } else {
    sections.push_back({QString(), grouped.order});
  }

  // category separators this collection made before that this order no
  // longer uses (an earlier run grouped otherwise): removed only when no mod
  // sits under them in any profile, as the mods folder is shared
  if (m_categorySeparators && !usedSeparators.isEmpty()) {
    QStringList unused;
    std::vector<QStringList> otherLists;
    bool otherListsRead = false;
    for (auto it = m_categorySeparatorNames.begin(); it != m_categorySeparatorNames.end();) {
      const QString name = it.value().toString();
      if (usedSeparators.contains(name.toLower())) {
        ++it;
        continue;
      }
      if (ModInfo::getIndex(name + "_separator") == UINT_MAX) {
        it = m_categorySeparatorNames.erase(it);  // gone already
        continue;
      }
      if (otherLists.empty() && !otherListsRead) {
        otherLists     = otherProfileModLists();
        otherListsRead = true;
      }
      if (separatorEmptyEverywhere(name + "_separator", otherLists)) {
        unused.append(name + "_separator");
        it = m_categorySeparatorNames.erase(it);
      } else {
        ++it;
      }
    }
    if (!unused.isEmpty()) {
      m_core.modList()->removeModsForce(unused);
      m_core.refresh();
    }
  }

  // the final order, each moved to the end in turn: the collection's
  // separator, then each section's separator and mods
  ModList* list     = m_core.modList();
  QString separator = m_separatorName.trimmed();
  std::vector<unsigned int> moving;
  auto add = [&moving](const QString& name) {
    const unsigned int index = ModInfo::getIndex(name);
    if (index != UINT_MAX) {
      moving.push_back(index);
    }
  };
  if (fixDirectoryName(separator) && !separator.isEmpty() &&
      list->getMod(separator + "_separator") != nullptr) {
    add(separator + "_separator");
  }

  QStringList names;
  for (const auto& [section, members] : sections) {
    if (!section.isEmpty() && list->getMod(section + "_separator") != nullptr) {
      add(section + "_separator");
    }
    for (int mod : members) {
      const QString& name = m_entries[rowOfMod[mod]].modName;
      add(name);
      names.append(name);
    }
  }
  if (Profile* profile = m_core.currentProfile(); profile != nullptr && !moving.empty()) {
    // one change for the view, not one per mod
    emit list->layoutAboutToBeChanged();
    profile->moveModsToEnd(moving);
    emit list->layoutChanged();
  }
  list->setActive(names, true);
  // in the profile's modlist.txt now, not only when the list is next written
  if (Profile* profile = m_core.currentProfile()) {
    profile->writeModlistNow();
  }

  // the user's own mods in the collection's order: said in the summary
  m_movedOwn.clear();
  for (int mod : sorted.order) {
    const Entry& e = m_entries[rowOfMod[mod]];
    if (e.keptExisting || !e.reason.isEmpty()) {
      m_movedOwn.append(e.modName);
    }
  }
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

  if (!m_manifest.pluginRules.empty() && !m_manifest.excludePluginRules) {
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
    for (const QString& note : e.notes) {
      patched.append(QStringLiteral("%1: %2").arg(e.modName, note));
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
  if (!m_updateNote.isEmpty()) {
    lines.append(QString());
    lines.append(m_updateNote);
  }
  section(tr("Patches and exact file lists:"), patched);
  section(tr("Files that win over the other mods' copies:"), m_overrideNotes);
  section(tr("Categories added to your category list:"), m_createdCategories);
  section(tr("Your own mods, moved and enabled in the collection's order:"), m_movedOwn);
  section(tr("INI tweaks:"), m_iniNotes);
  section(tr("Moved to the Recycle Bin, no longer in the collection:"), m_removedNotes);
  section(tr("Still installed, no longer in the collection:"), m_keptNotes);
  section(tr("Mod order rules dropped to break a loop:"), m_modCycles);
  if (m_overflowCount > 0) {
    lines.append(QString());
    lines.append(tr("%n mod(s) whose order rules cross categories went under \"%1\", "
                    "at the end.",
                    "", m_overflowCount)
                     .arg(tr("Patches and overrides")));
  }
  section(tr("Plugin order rules dropped to break a loop:"), m_pluginCycles);

  return lines.join('\n');
}
