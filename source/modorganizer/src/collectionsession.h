#ifndef COLLECTIONSESSION_H
#define COLLECTIONSESSION_H

// MO2 Revamped: installing a Nexus collection, from its link to the summary.
// Reads the collection from Nexus (GraphQL v2 through NexusInterface), fetches
// its archive, follows the downloads of its mods (Premium queues them, a free
// account clicks each one on the website), installs them by phase and orders
// them. Its progress is kept in <instance>/collections/<slug>-<rev>.json, so
// the user can close MO2 and continue later.

#include "collectionmanifest.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <boost/signals2/connection.hpp>
#include <functional>
#include <optional>
#include <map>
#include <vector>

class OrganizerCore;
class QNetworkReply;

class CollectionSession : public QObject
{
  Q_OBJECT

public:
  enum class Stage
  {
    Loading,  // asking Nexus, fetching and reading the archive
    Ready,    // shown, not started
    Running,  // downloading and installing
    Paused,   // started before, stopped
    Done,     // everything selected was handled
    Error     // can't go on (wrong game, not found...)
  };

  enum class Status
  {
    Pending,
    WaitingForClick,
    Downloading,
    Downloaded,
    Installed,
    Skipped,
    Failed
  };

  // where the collection goes
  enum class Target
  {
    NewInstance,  // an instance of its own; MO2 restarts in it
    NewProfile,   // a new profile in this instance
    CurrentProfile
  };

  struct Entry
  {
    int mod = -1;  // index in the manifest's mods
    Status status = Status::Pending;
    QString reason;     // why it failed, or a note
    QString archive;    // the downloaded file (or the one the user chose)
    QString modName;    // the mod in MO2
    int categoryId = 0;  // the Nexus mod's category
    QString category;
    bool selected = true;
    QString presetResult;  // applied, partial or none, for FOMOD choices
    bool keptExisting = false;

    // a Nexus file of another game than the instance's: never queued, the
    // user installs it by hand
    bool byHand = false;

    // from Nexus
    QString fileName;
    QString fileVersion;
    qint64 size     = 0;
    int progress    = -1;  // percent while downloading
    bool queuedByUs = false;
    QElapsedTimer queuedAt;
  };

  CollectionSession(OrganizerCore& core, const Collections::CollectionLink& link,
                    QObject* parent = nullptr);
  ~CollectionSession() override;

  // the main window switches profiles and runs LOOT
  void setSwitchProfile(std::function<bool(const QString&)> f);

  void load();

  const Collections::CollectionLink& link() const { return m_link; }
  int revision() const { return m_revision; }
  Stage stage() const { return m_stage; }
  QString loadingText() const { return m_loadingText; }
  QString errorText() const { return m_error; }
  const Collections::CollectionManifest& manifest() const { return m_manifest; }
  QString displayName() const;
  QString author() const;
  QString gameName() const;
  bool isPremium() const;
  bool isBethesda() const;

  const std::vector<Entry>& entries() const { return m_entries; }
  const Collections::CollectionMod& modOf(const Entry& e) const;

  // the selected mods, and what of them is still to download
  qint64 totalSize() const;
  qint64 remainingSize() const;
  int count(Status s) const;
  bool started() const { return m_started; }
  // where it installs can no longer change: started, or in the instance made for it
  bool placeFixed() const { return m_started || m_profileMade || m_placeFixed; }

  // options
  Target target() const { return m_target; }
  void setTarget(Target t);
  // whether this instance may make another one and switch to it
  bool canMakeInstance() const;
  QString instanceName() const { return m_instanceName; }
  void setInstanceName(const QString& name);
  // why the name can't be used, empty when it can
  QString instanceNameProblem(const QString& name) const;
  // the window was opened by the restart into a new instance: Start goes on
  // by itself once the collection is read
  void setAutoStart(bool b) { m_autoStart = b; }
  // a link from the browser for one of the files waited for: its download
  // gives no notice of its own
  bool claims(const QString& nxmUrl) const;

  // the last place chosen, for the next collection
  static Target lastTarget();

  // the note a new instance finds at its start, see startInNewInstance();
  // empty when there is none. It is removed as it is read
  static QString takePendingCollection();
  QString profileName() const { return m_profileName; }
  void setProfileName(const QString& name);
  QString separatorName() const { return m_separatorName; }
  void setSeparatorName(const QString& name);
  // Premium: everything downloads after Start (the default), or page by page
  // as a free account does
  // the mods go under a separator per category, when the order allows it
  bool categorySeparators() const { return m_categorySeparators; }
  void setCategorySeparators(bool b);
  static bool lastCategorySeparators();
  bool autoDownloadChosen() const { return m_autoDownload; }
  void setAutoDownload(bool b);
  // whether downloads are queued without clicks: chosen, and the account is
  // Premium or not known yet (the sign-in after a restart)
  bool downloadsAutomatically() const;
  static bool lastAutoDownload();
  bool autoOpenNext() const { return m_autoOpen; }
  void setAutoOpenNext(bool b);
  void setSelected(int row, bool selected);

  // Start and Continue
  void start();
  void pause();

  // free accounts: opens the next file's page on Nexus; false when none is left
  bool openNext();
  void openPage(int row);
  void chooseFile(int row, const QString& path);
  void markDone(int row);
  void retry(int row);
  void skip(int row);

  // after Done
  QString summary() const;
  bool shouldOfferLoot() const { return m_offerLoot; }

  // the archive couldn't be read: no installer choices, order or bundled mods
  bool isReduced() const { return m_reduced; }

  static QString statePath(const QString& slug, int revision);

signals:
  void changed();
  void entryChanged(int row);
  void finished();

private:
  OrganizerCore& m_core;
  Collections::CollectionLink m_link;
  int m_revision = -1;
  Stage m_stage  = Stage::Loading;
  QString m_loadingText;
  QString m_error;

  // from GraphQL
  QString m_name;
  QString m_author;
  QString m_domain;
  QString m_downloadLink;
  struct NexusFile
  {
    int modId = 0;
    QString name;
    QString modName;
    QString version;
    QString uri;
    qint64 size   = 0;
    bool optional = false;
    int categoryId = 0;
    QString category;
  };
  std::map<int, NexusFile> m_files;  // by file id

  QJsonObject m_saved;  // the state file, until the entries take it
  Collections::CollectionManifest m_manifest;
  bool m_haveManifest = false;
  QString m_archivePath;
  std::vector<Entry> m_entries;

  bool m_started       = false;
  Target m_target      = Target::NewInstance;
  QString m_instanceName;
  bool m_autoStart     = false;
  bool m_placeFixed    = false;
  bool m_autoDownload  = true;
  bool m_categorySeparators = true;
  // the category separators made for this collection: category to name
  QJsonObject m_categorySeparatorNames;
  bool m_notGrouped = false;  // the rules crossed categories
  QString m_profileName;
  QString m_separatorName;
  bool m_autoOpen      = true;
  // the installed folders the user said may be replaced: lowercase name to
  // what the folder was when they said so (see folderFingerprint())
  QJsonObject m_approved;
  bool m_profileMade   = false;
  bool m_installing    = false;
  bool m_scanning      = false;
  bool m_pumping       = false;
  bool m_refetched     = false;  // a damaged archive was fetched again
  bool m_reduced       = false;  // read from the Nexus list, without the archive
  bool m_finalizing    = false;
  bool m_offerLoot     = false;
  QStringList m_modCycles;
  QStringList m_pluginCycles;

  int m_metaRequest  = -1;
  int m_filesRequest = -1;
  int m_linkRequest  = -1;
  QPointer<QNetworkReply> m_archiveReply;
  QFile m_archiveFile;

  QTimer m_scanTimer;
  QTimer m_watchdog;
  std::function<bool(const QString&)> m_switchProfile;
  std::vector<boost::signals2::connection> m_connections;

  void setStage(Stage s);
  void fail(const QString& message);
  void setLoading(const QString& text);

  void requestMetadata();
  void requestFiles();
  void onApiResult(QVariant userData, QVariant result, int id);
  void onApiFailed(QString, int, int, QVariant, int id, int code, const QString& error);
  bool checkGame(const QString& domain);
  bool gameMatches(const QString& domain) const;
  QString folderFingerprint(const QString& modName) const;
  QStringList approvedNow() const;
  void findOrFetchArchive();
  void downloadArchive(const QString& url);
  void archiveDownloaded();
  void readManifest();
  void buildEntries();

  void loadState();
  void saveState() const;
  QJsonObject stateObject() const;

  void scheduleScan();
  void scanDownloads();
  void pumpDownloads();
  void pumpInstall();
  bool installEntry(int row);
  bool installBundle(int row);
  bool prepareProfile();
  void startInNewInstance();
  bool askAboutExisting();
  void finalize();
  void applyModOrder();
  // the MO2 category for an entry, 0 for none; made when it doesn't exist
  int categoryFor(const Entry& e) const;
  QString categoryName(const Entry& e) const;
  // a separator for a category, made the way MO2 makes them
  QString categorySeparator(const QString& category);
  void applyPlugins();

  void setStatus(int row, Status s, const QString& reason = {});
  QString uniqueModName(int row) const;
  QString nexusPage(const Entry& e) const;
  QString domainOf(const Entry& e) const;
  bool isFinished(const Entry& e) const;
};

#endif  // COLLECTIONSESSION_H
