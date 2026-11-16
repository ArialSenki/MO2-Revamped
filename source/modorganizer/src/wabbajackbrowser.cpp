#include "wabbajackbrowser.h"

#include "fluenticons.h"
#include "screenfitter.h"
#include "settings.h"
#include "wabbajackdownloads.h"
#include "wabbajacklist.h"

#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace MOBase;

namespace Wabbajack
{

namespace
{

// Wabbajack's own index of the repositories of lists, and its report of how
// each list fares (small images, failing archives)
constexpr char RepositoriesUrl[] =
    "https://raw.githubusercontent.com/wabbajack-tools/mod-lists/master/"
    "repositories.json";
constexpr char SummaryUrl[] =
    "https://raw.githubusercontent.com/wabbajack-tools/mod-lists/master/reports/"
    "modListSummary.json";
// read again after this long; Refresh reads it at once
constexpr qint64 CacheSeconds = 3600;

constexpr char UnofficialSetting[] = "Wabbajack/showUnofficialLists";
constexpr char NsfwSetting[]       = "Wabbajack/showNsfwLists";
constexpr char GameSetting[]       = "Wabbajack/browserGame";

constexpr int EntryRole = Qt::UserRole + 1;
constexpr int SortRole  = Qt::UserRole + 2;
constexpr int PartTries = 3;

enum Column
{
  ColName = 0,
  ColGame,
  ColAuthor,
  ColVersion,
  ColDownload,
  ColInstall,
  ColUpdated,
  ColCount
};

QString cacheFile()
{
  return QDir(GlobalSettings::directory()).filePath(QStringLiteral("wabbajack-gallery.json"));
}

// a list sorts by what a column means: sizes and dates as numbers
class Item : public QTreeWidgetItem
{
public:
  using QTreeWidgetItem::QTreeWidgetItem;
  bool operator<(const QTreeWidgetItem& other) const override
  {
    const int c        = treeWidget() != nullptr ? treeWidget()->sortColumn() : 0;
    const QVariant a   = data(c, SortRole);
    const QVariant b   = other.data(c, SortRole);
    if (a.isValid() && b.isValid()) {
      return a.toLongLong() < b.toLongLong();
    }
    return text(c).compare(other.text(c), Qt::CaseInsensitive) < 0;
  }
};

QNetworkRequest request(const QUrl& url)
{
  QNetworkRequest r(url);
  r.setHeader(QNetworkRequest::UserAgentHeader,
              QStringLiteral("MO2-Revamped/%1").arg(QApplication::applicationVersion()));
  r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  r.setRawHeader("Accept-Encoding", "identity");
  r.setTransferTimeout(60000);
  return r;
}

bool isWebAddress(const QString& text)
{
  const QUrl u(text);
  return u.isValid() && (u.scheme() == u"https" || u.scheme() == u"http");
}

// the list's file name from its address on Wabbajack's servers
// ("…/Tempus Maledictum.wabbajack_c52ec8de-…"), plain and safe
QString fileNameFor(const QString& download, const QString& title)
{
  QString name = QUrl(download).fileName();
  const int cut = name.indexOf(QStringLiteral(".wabbajack"), 0, Qt::CaseInsensitive);
  name          = cut > 0 ? name.left(cut) : title;
  QString clean;
  for (const QChar c : name) {
    clean += (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c)) ? u'_' : c;
  }
  clean = clean.trimmed();
  if (clean.isEmpty() || clean.startsWith(u'.')) {
    clean = QStringLiteral("list");
  }
  return clean + QStringLiteral(".wabbajack");
}

}  // namespace

struct ListBrowser::Download
{
  QString title;
  QString target;
  QUrl base;
  QFile file;
  std::vector<QJsonObject> parts;
  std::size_t index = 0;
  int tries         = 0;
  qint64 total      = 0;
  qint64 done       = 0;
  QPointer<QNetworkReply> reply;
  bool cancelled = false;
};

ListBrowser::ListBrowser(std::vector<IPluginGame*> games, const IPluginGame* managedGame,
                         const QString& downloadsFolder,
                         std::function<void(const QString&)> open, QWidget* parent)
    : QDialog(parent), m_games(std::move(games)), m_managed(managedGame),
      m_open(std::move(open))
{
  m_listsFolder = QDir(downloadsFolder).filePath(QStringLiteral("Wabbajack lists"));
  setupUi();
  // its size by the screen it is on, also when it is moved to another one
  auto* fitter            = new ScreenFitter(this, QSize(820, 560));
  const QWidget* anchor   = parentWidget();
  QScreen* screen = anchor != nullptr ? anchor->screen() : QGuiApplication::primaryScreen();
  if (screen != nullptr) {
    const bool overWindow = anchor != nullptr && anchor->isVisible() &&
                            !anchor->isMinimized();
    fitter->place(screen, overWindow ? anchor->frameGeometry().center()
                                     : screen->availableGeometry().center());
  }
  loadGallery(false);
}

ListBrowser::~ListBrowser()
{
  if (m_download && m_download->reply) {
    m_download->reply->abort();
  }
}

void ListBrowser::setupUi()
{
  setObjectName(QStringLiteral("WabbajackBrowser"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Wabbajack lists"));
  setWindowFlag(Qt::WindowMaximizeButtonHint);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(8);

  auto* bar = new QHBoxLayout;
  m_search  = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Search lists, authors and tags"));
  m_search->setClearButtonEnabled(true);
  m_search->setAccessibleName(m_search->placeholderText());
  m_game = new QComboBox(this);
  m_game->setAccessibleName(tr("Game"));
  m_game->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_game->setMinimumContentsLength(18);
  m_unofficial = new QCheckBox(tr("Unofficial lists"), this);
  m_unofficial->setToolTip(tr("Lists Wabbajack doesn't feature. Wabbajack hides them "
                              "unless asked, too."));
  m_unofficial->setChecked(GlobalSettings::value(UnofficialSetting, false).toBool());
  m_nsfw = new QCheckBox(tr("Adult lists"), this);
  m_nsfw->setChecked(GlobalSettings::value(NsfwSetting, false).toBool());
  bar->addWidget(m_search, 1);
  bar->addWidget(m_game);
  bar->addWidget(m_unofficial);
  bar->addWidget(m_nsfw);
  layout->addLayout(bar);

  m_list = new QTreeWidget(this);
  m_list->setObjectName(QStringLiteral("wabbajackBrowserList"));
  m_list->setAccessibleName(tr("Lists"));
  m_list->setColumnCount(ColCount);
  m_list->setHeaderLabels({tr("Name"), tr("Game"), tr("Author"), tr("Version"),
                           tr("Download"), tr("Installed"), tr("Updated")});
  m_list->setRootIsDecorated(false);
  m_list->setUniformRowHeights(true);
  m_list->setIconSize(QSize(96, 54));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setSortingEnabled(true);
  m_list->sortByColumn(ColName, Qt::AscendingOrder);
  m_list->header()->setStretchLastSection(false);
  m_list->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  for (int c = ColGame; c < ColCount; ++c) {
    m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  layout->addWidget(m_list, 1);

  // the selected list: what it is, its links
  auto* details = new QHBoxLayout;
  m_details     = new QLabel(this);
  m_details->setWordWrap(true);
  m_details->setTextFormat(Qt::PlainText);
  m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_details->setMinimumHeight(72);
  m_details->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  auto* links = new QVBoxLayout;
  auto link   = [this, links](const QString& text) {
    auto* b = new QPushButton(FluentIcons::glyph(0xE8A7), text, this);
    b->setAutoDefault(false);
    links->addWidget(b);
    return b;
  };
  m_readme  = link(tr("Readme"));
  m_website = link(tr("Website"));
  m_discord = link(tr("Community"));
  links->addStretch(1);
  details->addWidget(m_details, 1);
  details->addLayout(links);
  layout->addLayout(details);

  auto* footer = new QHBoxLayout;
  m_status     = new QLabel(this);
  m_status->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_status->setWordWrap(true);
  m_progress = new QProgressBar(this);
  m_progress->setMaximumWidth(220);
  m_progress->setTextVisible(false);
  m_progress->hide();
  m_refresh = new QPushButton(FluentIcons::glyph(0xE72C), tr("Refresh"), this);
  m_refresh->setAutoDefault(false);
  m_install = new QPushButton(FluentIcons::glyph(0xE896), tr("Open the list"), this);
  m_install->setProperty("fluentPrimary", true);
  m_install->setAutoDefault(false);
  m_install->setToolTip(tr("Downloads the list's file from Wabbajack's servers and opens "
                           "it: there its archives download and it installs."));
  m_close = new QPushButton(tr("Close"), this);
  m_close->setAutoDefault(false);
  footer->addWidget(m_status, 1);
  footer->addWidget(m_progress);
  footer->addWidget(m_refresh);
  footer->addWidget(m_install);
  footer->addWidget(m_close);
  layout->addLayout(footer);

  connect(m_search, &QLineEdit::textChanged, this, &ListBrowser::fillList);
  connect(m_game, &QComboBox::activated, this, [this] {
    GlobalSettings::setValue(GameSetting, m_game->currentData().toString());
    fillList();
  });
  connect(m_unofficial, &QCheckBox::toggled, this, [this](bool on) {
    GlobalSettings::setValue(UnofficialSetting, on);
    fillGames();
    fillList();
  });
  connect(m_nsfw, &QCheckBox::toggled, this, [this](bool on) {
    GlobalSettings::setValue(NsfwSetting, on);
    fillGames();
    fillList();
  });
  connect(m_list, &QTreeWidget::itemSelectionChanged, this, &ListBrowser::showDetails);
  connect(m_list, &QTreeWidget::itemActivated, this, &ListBrowser::install);
  connect(m_install, &QPushButton::clicked, this, &ListBrowser::install);
  connect(m_refresh, &QPushButton::clicked, this, [this] {
    loadGallery(true);
  });
  connect(m_close, &QPushButton::clicked, this, &QDialog::close);
  auto open = [this](QString Entry::*field) {
    if (const Entry* e = selected(); e != nullptr && isWebAddress(e->*field)) {
      QDesktopServices::openUrl(QUrl(e->*field));
    }
  };
  connect(m_readme, &QPushButton::clicked, this, [open] {
    open(&Entry::readme);
  });
  connect(m_website, &QPushButton::clicked, this, [open] {
    open(&Entry::website);
  });
  connect(m_discord, &QPushButton::clicked, this, [open] {
    open(&Entry::discord);
  });
  showDetails();
}

// ---- the gallery ----------------------------------------------------------

void ListBrowser::loadGallery(bool fresh)
{
  // the last answer, when it is recent: the gallery is about 150 files
  if (!fresh) {
    QFile f(cacheFile());
    if (f.open(QIODevice::ReadOnly)) {
      const QJsonObject cached = QJsonDocument::fromJson(f.readAll()).object();
      const qint64 age = QDateTime::currentSecsSinceEpoch() -
                         cached.value("time").toVariant().toLongLong();
      if (age >= 0 && age < CacheSeconds && !cached.value("lists").toArray().isEmpty()) {
        readGallery(cached.value("lists").toArray(),
                    QJsonDocument(cached.value("summary").toArray()).toJson());
        return;
      }
    }
  }

  const int generation = ++m_generation;
  m_collected          = QJsonArray();
  m_summary.clear();
  m_pending = 2;  // the index and the summary
  m_refresh->setEnabled(false);
  m_status->setText(tr("Reading Wabbajack's gallery…"));

  QNetworkReply* summary = m_network.get(request(QUrl(QString::fromLatin1(SummaryUrl))));
  connect(summary, &QNetworkReply::finished, this, [this, summary, generation] {
    summary->deleteLater();
    if (generation != m_generation) {
      return;
    }
    if (summary->error() == QNetworkReply::NoError) {
      m_summary = summary->readAll();
    }
    galleryDone(generation);
  });

  QNetworkReply* index = m_network.get(request(QUrl(QString::fromLatin1(RepositoriesUrl))));
  connect(index, &QNetworkReply::finished, this, [this, index, generation] {
    index->deleteLater();
    if (generation != m_generation) {
      return;
    }
    if (index->error() != QNetworkReply::NoError) {
      m_status->setText(tr("Wabbajack's gallery can't be read: %1")
                            .arg(networkErrorText(index)));
      galleryDone(generation);
      return;
    }
    const QJsonObject repositories = QJsonDocument::fromJson(index->readAll()).object();
    for (auto it = repositories.begin(); it != repositories.end(); ++it) {
      const QString name = it.key();
      const QUrl url(it.value().toString());
      if (!url.isValid() || url.scheme() != u"https") {
        continue;
      }
      ++m_pending;
      QNetworkReply* repo = m_network.get(request(url));
      connect(repo, &QNetworkReply::finished, this, [this, repo, name, generation] {
        repo->deleteLater();
        if (generation != m_generation) {
          return;
        }
        if (repo->error() == QNetworkReply::NoError) {
          for (const QJsonValue& v : QJsonDocument::fromJson(repo->readAll()).array()) {
            QJsonObject o = v.toObject();
            o.insert(QStringLiteral("repositoryName"), name);
            m_collected.append(o);
          }
        } else {
          log::debug("wabbajack gallery: repository {} not read: {}", name,
                     repo->errorString());
        }
        galleryDone(generation);
      });
    }
    galleryDone(generation);
  });
}

void ListBrowser::galleryDone(int generation)
{
  if (generation != m_generation || --m_pending > 0) {
    return;
  }
  m_refresh->setEnabled(true);
  if (m_collected.isEmpty()) {
    if (m_entries.empty()) {
      m_status->setText(tr("Wabbajack's gallery can't be read right now. Try Refresh "
                           "in a while."));
    }
    return;
  }
  // kept for the next hour
  QJsonObject cached;
  cached.insert("time", QDateTime::currentSecsSinceEpoch());
  cached.insert("lists", m_collected);
  cached.insert("summary", QJsonDocument::fromJson(m_summary).array());
  QSaveFile f(cacheFile());
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(cached).toJson(QJsonDocument::Compact));
    f.commit();
  }
  readGallery(m_collected, m_summary);
}

void ListBrowser::readGallery(const QJsonArray& lists, const QByteArray& summary)
{
  // the report: by "repository/machine name"
  QHash<QString, QJsonObject> reports;
  for (const QJsonValue& v : QJsonDocument::fromJson(summary).array()) {
    const QJsonObject o = v.toObject();
    reports.insert(o.value("machineURL").toString().toLower(), o);
  }

  m_entries.clear();
  for (const QJsonValue& v : lists) {
    const QJsonObject o     = v.toObject();
    const QJsonObject links = o.value("links").toObject();
    const QJsonObject meta  = o.value("download_metadata").toObject();
    Entry e;
    e.title       = o.value("title").toString().trimmed();
    e.description = o.value("description").toString().trimmed();
    e.author      = o.value("author").toString().trimmed();
    e.game        = o.value("game").toString().trimmed().toLower();
    e.version     = o.value("version").toString().trimmed();
    e.repository  = o.value("repositoryName").toString();
    e.machine     = links.value("machineURL").toString();
    e.image       = links.value("image").toString();
    e.readme      = links.value("readme").toString();
    e.website     = links.value("websiteURL").toString();
    e.discord     = links.value("discordURL").toString();
    e.download    = links.value("download").toString();
    e.hash        = meta.value("Hash").toString();
    e.size        = meta.value("Size").toVariant().toLongLong();
    e.downloadSize = meta.value("SizeOfArchives").toVariant().toLongLong();
    e.installSize  = meta.value("SizeOfInstalledFiles").toVariant().toLongLong();
    e.archives     = meta.value("NumberOfArchives").toInt();
    e.nsfw         = o.value("nsfw").toBool();
    e.official     = o.value("official").toBool();
    e.utility      = o.value("utility_list").toBool();
    e.down         = o.value("force_down").toBool();
    e.updated = QDateTime::fromString(o.value("dateUpdated").toString(), Qt::ISODate);
    for (const QJsonValue& t : o.value("tags").toArray()) {
      e.tags.append(t.toString());
    }
    const QJsonObject report =
        reports.value((e.repository + u'/' + e.machine).toLower());
    e.smallImage = report.value("small_image").toString();
    e.failing    = report.value("has_failures").toBool() ||
                report.value("modlist_missing").toBool();
    if (e.title.isEmpty() || e.utility) {
      continue;
    }
    m_entries.push_back(std::move(e));
  }
  fillGames();
  fillList();
}

void ListBrowser::fillGames()
{
  // the games of the lists shown, the one MO2 manages first chosen
  QString current = m_game->currentData().toString();
  if (m_game->count() == 0) {
    current = GlobalSettings::value(GameSetting).toString();
    if (current.isEmpty() && m_managed != nullptr) {
      for (const Entry& e : m_entries) {
        if (findGamePlugin(e.game, m_games) == m_managed) {
          current = e.game;
          break;
        }
      }
    }
  }
  QMap<QString, int> counts;
  for (const Entry& e : m_entries) {
    if ((e.official || m_unofficial->isChecked()) && (!e.nsfw || m_nsfw->isChecked())) {
      ++counts[e.game];
    }
  }
  std::vector<std::pair<QString, QString>> games;  // shown name, key
  for (auto it = counts.begin(); it != counts.end(); ++it) {
    const auto info  = gameInfo(it.key());
    const QString nm = info ? info->mo2Name : it.key();
    games.emplace_back(QStringLiteral("%1 (%2)").arg(nm).arg(it.value()), it.key());
  }
  std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
    return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
  });
  m_game->blockSignals(true);
  m_game->clear();
  m_game->addItem(tr("All games"), QString());
  for (const auto& [shown, key] : games) {
    m_game->addItem(shown, key);
  }
  const int at = m_game->findData(current);
  m_game->setCurrentIndex(at >= 0 ? at : 0);
  m_game->blockSignals(false);
}

void ListBrowser::fillList()
{
  const QString game = m_game->currentData().toString();
  const QString text = m_search->text().trimmed();
  m_list->setSortingEnabled(false);
  m_list->clear();
  int shown = 0, hidden = 0;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    const Entry& e = m_entries[i];
    if (!game.isEmpty() && e.game != game) {
      continue;
    }
    if ((!e.official && !m_unofficial->isChecked()) || (e.nsfw && !m_nsfw->isChecked())) {
      ++hidden;
      continue;
    }
    if (!text.isEmpty() && !e.title.contains(text, Qt::CaseInsensitive) &&
        !e.author.contains(text, Qt::CaseInsensitive) &&
        !e.description.contains(text, Qt::CaseInsensitive) &&
        !e.tags.join(u' ').contains(text, Qt::CaseInsensitive)) {
      continue;
    }
    auto* item = new Item(m_list);
    item->setData(ColName, EntryRole, qulonglong(i));
    item->setText(ColName, e.title);
    const auto info = gameInfo(e.game);
    item->setText(ColGame, info ? info->mo2Name : e.game);
    item->setText(ColAuthor, e.author);
    item->setText(ColVersion, e.version);
    item->setText(ColDownload, e.downloadSize > 0 ? localizedByteSize(e.downloadSize) : QString());
    item->setData(ColDownload, SortRole, e.downloadSize);
    item->setTextAlignment(ColDownload, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColInstall, e.installSize > 0 ? localizedByteSize(e.installSize) : QString());
    item->setData(ColInstall, SortRole, e.installSize);
    item->setTextAlignment(ColInstall, Qt::AlignRight | Qt::AlignVCenter);
    if (e.updated.isValid() && e.updated.date().year() > 1970) {
      item->setText(ColUpdated,
                    QLocale().toString(e.updated.toLocalTime().date(), QLocale::ShortFormat));
      item->setData(ColUpdated, SortRole, e.updated.toSecsSinceEpoch());
    } else {
      item->setData(ColUpdated, SortRole, qint64(0));
    }
    if (e.down) {
      item->setText(ColVersion, tr("Down for maintenance"));
    }
    if (const QString img = !e.smallImage.isEmpty() ? e.smallImage : e.image;
        isWebAddress(img)) {
      loadImage(item, img);
    }
    ++shown;
  }
  m_list->setSortingEnabled(true);
  if (m_list->topLevelItemCount() > 0 && m_list->currentItem() == nullptr) {
    m_list->setCurrentItem(m_list->topLevelItem(0));
  }
  if (!m_entries.empty()) {
    m_status->setText(hidden > 0 && !m_unofficial->isChecked()
                          ? tr("%n lists. Tick \"Unofficial lists\" to see the others too.",
                               nullptr, shown)
                          : tr("%n lists.", nullptr, shown));
  }
  showDetails();
}

void ListBrowser::loadImage(QTreeWidgetItem* item, const QString& url)
{
  const auto cached = m_images.constFind(url);
  if (cached != m_images.constEnd()) {
    item->setIcon(ColName, QIcon(cached.value()));
    return;
  }
  if (m_imagesAsked.contains(url)) {
    return;
  }
  m_imagesAsked.insert(url);
  QNetworkReply* reply = m_network.get(request(QUrl(url)));
  connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
    reply->deleteLater();
    QImage image;
    // a picture of a few hundred KB at most: a gallery card
    if (reply->error() != QNetworkReply::NoError ||
        reply->bytesAvailable() > 8 * 1024 * 1024 || !image.loadFromData(reply->readAll())) {
      return;
    }
    const QPixmap pixmap = QPixmap::fromImage(
        image.scaled(QSize(96, 54), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_images.insert(url, pixmap);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
      QTreeWidgetItem* it = m_list->topLevelItem(i);
      const Entry& e = m_entries[it->data(ColName, EntryRole).toULongLong()];
      if ((!e.smallImage.isEmpty() ? e.smallImage : e.image) == url) {
        it->setIcon(ColName, QIcon(pixmap));
      }
    }
  });
}

const ListBrowser::Entry* ListBrowser::selected() const
{
  const QTreeWidgetItem* item = m_list->currentItem();
  if (item == nullptr) {
    return nullptr;
  }
  const auto i = item->data(ColName, EntryRole).toULongLong();
  return i < m_entries.size() ? &m_entries[i] : nullptr;
}

void ListBrowser::showDetails()
{
  const Entry* e = selected();
  m_readme->setVisible(e != nullptr && isWebAddress(e->readme));
  m_website->setVisible(e != nullptr && isWebAddress(e->website));
  m_discord->setVisible(e != nullptr && isWebAddress(e->discord));
  if (e == nullptr) {
    m_details->setText(m_entries.empty() ? QString() : tr("Choose a list."));
    updateButtons();
    return;
  }
  QStringList lines;
  QStringList about{e->title};
  if (!e->author.isEmpty()) {
    about << tr("by %1").arg(e->author);
  }
  if (!e->version.isEmpty()) {
    about << tr("version %1").arg(e->version);
  }
  lines << about.join(QStringLiteral(" · "));
  if (!e->description.isEmpty()) {
    lines << e->description;
  }
  QStringList facts;
  if (e->archives > 0) {
    facts << tr("%n archives", nullptr, e->archives);
  }
  if (e->downloadSize > 0) {
    facts << tr("%1 to download").arg(localizedByteSize(e->downloadSize));
  }
  if (e->installSize > 0) {
    facts << tr("%1 installed").arg(localizedByteSize(e->installSize));
  }
  if (!e->tags.isEmpty()) {
    facts << e->tags.join(QStringLiteral(", "));
  }
  if (!facts.isEmpty()) {
    lines << facts.join(QStringLiteral(" · "));
  }
  if (e->down) {
    lines << tr("Its author took it down for maintenance: it can't be installed now.");
  } else if (e->failing) {
    lines << tr("Wabbajack's checks find archives of it that don't download any more; it "
                "may not install whole.");
  }
  if (findGamePlugin(e->game, m_games) == nullptr) {
    lines << tr("MO2 Revamped has no game plugin for its game.");
  }
  m_details->setText(lines.join(u'\n'));
  updateButtons();
}

void ListBrowser::updateButtons()
{
  const Entry* e      = selected();
  const bool fetching = m_download != nullptr;
  m_install->setEnabled(!fetching && e != nullptr && !e->down &&
                        isWabbajackCdnHost(cdnUrl(e->download)));
  m_install->setText(fetching ? tr("Downloading the list…") : tr("Open the list"));
  m_refresh->setEnabled(!fetching && m_pending <= 0);
  m_close->setText(fetching ? tr("Stop") : tr("Close"));
}

// ---- the list's file --------------------------------------------------------

void ListBrowser::install()
{
  const Entry* e = selected();
  if (e == nullptr || m_download != nullptr || e->down) {
    return;
  }
  const QUrl base = cdnUrl(e->download);
  if (!isWabbajackCdnHost(base)) {
    m_status->setText(tr("This list's file isn't on Wabbajack's servers: open its .wabbajack "
                         "with Tools > Wabbajack lists > Open a Wabbajack list."));
    return;
  }
  if (!QDir().mkpath(m_listsFolder)) {
    m_status->setText(tr("%1 can't be made.").arg(QDir::toNativeSeparators(m_listsFolder)));
    return;
  }
  const QString target = QDir(m_listsFolder).filePath(fileNameFor(e->download, e->title));

  // already here, the same file: no download
  const QFileInfo existing(target);
  bool ok             = false;
  const uint64_t want = hashFromBase64(e->hash, &ok);
  if (existing.isFile() && ok && e->size > 0 && existing.size() == e->size) {
    m_status->setText(tr("Checking the list's file that is already here…"));
    auto* watcher = new QFutureWatcher<std::optional<uint64_t>>(this);
    const Entry copy = *e;
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, copy, target, want] {
      watcher->deleteLater();
      const auto hash = watcher->result();
      if (hash && *hash == want) {
        m_status->setText(tr("Opening %1…").arg(copy.title));
        if (m_open) {
          m_open(target);
        }
        close();
        return;
      }
      startDownload(copy, target);
    });
    watcher->setFuture(QtConcurrent::run([target]() -> std::optional<uint64_t> {
      const LowPriority low(LowPriority::Mode::Background);
      try {
        return hashFile(target);
      } catch (...) {
        return std::nullopt;
      }
    }));
    return;
  }
  startDownload(*e, target);
}

void ListBrowser::startDownload(const Entry& e, const QString& target)
{
  m_download         = std::make_unique<Download>();
  m_download->title  = e.title;
  m_download->target = target;
  m_download->base   = cdnUrl(e.download);
  m_progress->setRange(0, 0);
  m_progress->show();
  m_status->setText(tr("Downloading %1's file…").arg(e.title));
  updateButtons();

  QUrl definition = m_download->base;
  definition.setPath(definition.path() + QStringLiteral("/definition.json.gz"));
  QNetworkReply* reply = m_network.get(request(definition));
  m_download->reply    = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    if (!m_download || m_download->reply != reply || m_download->cancelled) {
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      finishDownload(false, networkErrorText(reply));
      return;
    }
    const auto json = gunzip(reply->readAll());
    const QJsonObject d =
        json ? QJsonDocument::fromJson(*json).object() : QJsonObject();
    const qint64 size = d.value("Size").toVariant().toLongLong();
    for (const QJsonValue& p : d.value("Parts").toArray()) {
      m_download->parts.push_back(p.toObject());
    }
    std::sort(m_download->parts.begin(), m_download->parts.end(),
              [](const QJsonObject& a, const QJsonObject& b) {
                return a.value("Index").toInt() < b.value("Index").toInt();
              });
    // the parts cover the file, each where it goes
    qint64 covered = 0;
    for (const QJsonObject& p : m_download->parts) {
      const qint64 offset = p.value("Offset").toVariant().toLongLong();
      const qint64 length = p.value("Size").toVariant().toLongLong();
      if (offset != covered || length <= 0 || length > 64 * 1024 * 1024) {
        covered = -1;
        break;
      }
      covered += length;
    }
    if (size <= 0 || covered != size) {
      finishDownload(false, tr("Wabbajack's server describes the file in a way MO2 "
                               "Revamped doesn't understand."));
      return;
    }
    m_download->total = size;
    m_download->file.setFileName(m_download->target + QStringLiteral(".mo2r-part"));
    if (!m_download->file.open(QIODevice::WriteOnly) || !m_download->file.resize(size)) {
      finishDownload(false, tr("%1 can't be written.")
                                .arg(QDir::toNativeSeparators(m_download->file.fileName())));
      return;
    }
    m_progress->setRange(0, 1000);
    nextPart();
  });
}

void ListBrowser::nextPart()
{
  Download& d = *m_download;
  if (d.index >= d.parts.size()) {
    d.file.close();
    QFile::remove(d.target);
    if (!QFile::rename(d.file.fileName(), d.target)) {
      finishDownload(false, tr("%1 can't be written.").arg(QDir::toNativeSeparators(d.target)));
      return;
    }
    finishDownload(true, {});
    return;
  }
  const QJsonObject part = d.parts[d.index];
  QUrl url               = d.base;
  url.setPath(url.path() + QStringLiteral("/parts/%1").arg(part.value("Index").toInt()));
  QNetworkReply* reply = m_network.get(request(url));
  d.reply              = reply;
  connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
    if (m_download && m_download->total > 0) {
      m_progress->setValue(int((m_download->done + received) * 1000 / m_download->total));
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, part] {
    reply->deleteLater();
    if (!m_download || m_download->reply != reply || m_download->cancelled) {
      return;
    }
    Download& d          = *m_download;
    const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                                     : QByteArray();
    bool ok             = false;
    const uint64_t hash = hashFromBase64(part.value("Hash").toString(), &ok);
    const qint64 size   = part.value("Size").toVariant().toLongLong();
    if (data.size() != size || !ok ||
        XxHash64::of(data.constData(), std::size_t(data.size())) != hash) {
      if (++d.tries > PartTries) {
        finishDownload(false, reply->error() != QNetworkReply::NoError
                                  ? networkErrorText(reply)
                                  : tr("A part of the list's file kept arriving damaged."));
        return;
      }
      QTimer::singleShot(3000 * d.tries, this, [this] {
        if (m_download && !m_download->cancelled) {
          nextPart();
        }
      });
      return;
    }
    if (!d.file.seek(part.value("Offset").toVariant().toLongLong()) ||
        d.file.write(data) != data.size()) {
      finishDownload(false, tr("The disk is full, or %1 can't be written.")
                                .arg(QDir::toNativeSeparators(d.file.fileName())));
      return;
    }
    d.done += size;
    d.tries = 0;
    ++d.index;
    m_progress->setValue(int(d.done * 1000 / qMax<qint64>(1, d.total)));
    nextPart();
  });
}

void ListBrowser::finishDownload(bool ok, const QString& error)
{
  std::unique_ptr<Download> d = std::move(m_download);
  m_progress->hide();
  if (d && d->file.isOpen()) {
    d->file.close();
  }
  if (!ok) {
    if (d) {
      QFile::remove(d->target + QStringLiteral(".mo2r-part"));
    }
    if (!error.isEmpty()) {
      log::warn("wabbajack gallery: the list's file failed: {}", error);
      m_status->setText(tr("The list's file couldn't be downloaded: %1").arg(error));
    }
    updateButtons();
    return;
  }
  log::info("wabbajack gallery: {} downloaded to {}", d->title, d->target);
  if (m_open) {
    m_open(d->target);
  }
  close();
}

void ListBrowser::closeEvent(QCloseEvent* e)
{
  // the first close stops a download; the window stays
  if (m_download) {
    m_download->cancelled = true;
    if (m_download->reply) {
      m_download->reply->abort();
    }
    finishDownload(false, {});
    m_status->setText(tr("Stopped."));
    e->ignore();
    return;
  }
  QDialog::closeEvent(e);
}

}  // namespace Wabbajack
