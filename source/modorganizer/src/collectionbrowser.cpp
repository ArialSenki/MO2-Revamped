#include "collectionbrowser.h"

#include "fluenticons.h"
#include "nexusinterface.h"
#include "screenfitter.h"
#include "selfupdater.h"
#include "settings.h"

#include <log.h>
#include <utility.h>

#include <QApplication>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

using namespace MOBase;

namespace
{

constexpr int PageSize  = 20;
// "All games": each game's first ones, in its section
constexpr int SectionSize = 6;
// games in one request: Nexus refuses a query above a complexity of 10000,
// about 300 for each game's six collections
constexpr int GamesPerRequest = 15;
constexpr int EntryRole   = Qt::UserRole + 1;
// a section's header or its "show all" row: the game it opens
constexpr int GameRole = Qt::UserRole + 2;
constexpr char AllGamesSetting[] = "Collections/browserAllGames";
// a picture of a card: anything larger is dropped
constexpr qint64 MaxImageBytes = 8 * 1024 * 1024;
const QSize ThumbSize(96, 54);

constexpr char16_t OpenGlyph = 0xE8A7;  // OpenInNewWindow
constexpr char16_t ListGlyph = 0xE8FD;  // BulletedList

// what each row reads of a collection
const char* NodeFields = R"(
      slug
      name
      summary
      endorsements
      totalDownloads
      adultContent
      game { domainName name }
      category { name }
      tileImage { thumbnailUrl(size: small) }
      user { name }
      latestPublishedRevision {
        revisionNumber modCount totalSize updatedAt gameVersions { reference }
      })";

QString browseQuery()
{
  return QStringLiteral(R"(query ($filter: CollectionsSearchFilter,
    $sort: [CollectionsSearchSort!], $offset: Int, $count: Int) {
  collectionsV2(filter: $filter, sort: $sort, offset: $offset, count: $count) {
    totalCount
    nodes {%1
    }
  }
})")
      .arg(QString::fromLatin1(NodeFields));
}

// games first to first + count - 1 in one request: an alias for each (g<n>)
// with its own filter ($f<n>)
QString overviewQuery(int first, int count)
{
  QStringList params{QStringLiteral("$sort: [CollectionsSearchSort!]"),
                     QStringLiteral("$count: Int")};
  QStringList parts;
  for (int i = first; i < first + count; ++i) {
    params << QStringLiteral("$f%1: CollectionsSearchFilter").arg(i);
    parts << QStringLiteral("  g%1: collectionsV2(filter: $f%1, sort: $sort, count: $count) "
                            "{ totalCount nodes {%2 } }")
                 .arg(i)
                 .arg(QString::fromLatin1(NodeFields));
  }
  return QStringLiteral("query (%1) {\n%2\n}")
      .arg(params.join(QStringLiteral(", ")), parts.join(u'\n'));
}

enum Column
{
  ColName = 0,
  ColGame,
  ColAuthor,
  ColRevision,
  ColMods,
  ColSize,
  ColEndorsements,
  ColUpdated,
  ColCount
};

}  // namespace

namespace CollectionImages
{

void fetch(QNetworkAccessManager& network, const QString& url, QObject* context,
           std::function<void(const QImage&)> done)
{
  const QUrl u(url);
  const QString host = u.host().toLower();
  if (!u.isValid() || u.scheme() != u"https" ||
      !(host == u"nexusmods.com" || host.endsWith(QStringLiteral(".nexusmods.com")))) {
    done({});
    return;
  }
  QNetworkRequest r(u);
  r.setHeader(QNetworkRequest::UserAgentHeader,
              QStringLiteral("MO2-Revamped/%1").arg(SelfUpdater::revampedVersionString()));
  r.setTransferTimeout(30000);
  QNetworkReply* reply = network.get(r);
  // stopped as soon as it is too large, not read whole first
  QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                   [reply](qint64 received, qint64) {
                     if (received > MaxImageBytes) {
                       reply->abort();
                     }
                   });
  QObject::connect(reply, &QNetworkReply::finished, context,
                   [reply, done = std::move(done)] {
                     reply->deleteLater();
                     QImage image;
                     if (reply->error() != QNetworkReply::NoError ||
                         reply->bytesAvailable() > MaxImageBytes ||
                         !image.loadFromData(reply->readAll())) {
                       done({});
                       return;
                     }
                     done(image);
                   });
}

QPixmap thumbnail(const QImage& image, QSize size, qreal dpr)
{
  const QSize box     = size * dpr;
  const QImage scaled = image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap pixmap(box);
  pixmap.fill(Qt::transparent);
  {
    QPainter painter(&pixmap);
    painter.drawImage((box.width() - scaled.width()) / 2,
                      (box.height() - scaled.height()) / 2, scaled);
  }
  pixmap.setDevicePixelRatio(dpr);
  return pixmap;
}

}  // namespace CollectionImages


CollectionBrowser::CollectionBrowser(std::vector<Game> games,
                                     const QStringList& instanceDomains,
                                     std::function<void(const QString&)> open,
                                     QWidget* parent)
    : QDialog(parent), m_games(std::move(games)), m_instanceDomains(instanceDomains),
      m_open(std::move(open))
{
  setObjectName(QStringLiteral("CollectionBrowser"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Nexus collections"));
  setWindowFlag(Qt::WindowMaximizeButtonHint);
  std::sort(m_games.begin(), m_games.end(), [](const Game& a, const Game& b) {
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });

  // as the Wabbajack gallery: the same bar, table, details and footer
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(8);

  auto* bar = new QHBoxLayout;
  m_search  = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Search collections"));
  m_search->setClearButtonEnabled(true);
  m_search->setAccessibleName(m_search->placeholderText());
  m_game = new QComboBox(this);
  m_game->setAccessibleName(tr("Game"));
  // as wide as a usual game's name: a long one is cut in the box, not in the list
  m_game->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_game->setMinimumContentsLength(26);
  m_sort = new QComboBox(this);
  m_sort->setAccessibleName(tr("Sort"));
  m_sort->addItem(tr("Most endorsed"), QStringLiteral("endorsements"));
  m_sort->addItem(tr("Most downloaded"), QStringLiteral("downloads"));
  m_sort->addItem(tr("Recently updated"), QStringLiteral("updatedAt"));
  // MO2 Revamped: hidden unless asked for, as on Nexus' own site; the API
  // doesn't know the account's adult content preference
  m_adult = new QCheckBox(tr("Adult content"), this);
  m_adult->setToolTip(tr("Also lists collections Nexus marks as adult (NSFW)."));
  bar->addWidget(m_search, 1);
  bar->addWidget(m_game);
  bar->addWidget(m_sort);
  bar->addWidget(m_adult);
  layout->addLayout(bar);

  m_list = new QTreeWidget(this);
  m_list->setObjectName(QStringLiteral("collectionBrowserList"));
  m_list->setAccessibleName(tr("Collections"));
  m_list->setColumnCount(ColCount);
  m_list->setHeaderLabels({tr("Name"), tr("Game"), tr("Author"), tr("Revision"), tr("Mods"),
                           tr("Size"), tr("Endorsements"), tr("Updated")});
  m_list->setRootIsDecorated(false);
  m_list->setItemsExpandable(false);
  m_list->setExpandsOnDoubleClick(false);
  // a picture on each row, as the Wabbajack gallery
  m_list->setIconSize(ThumbSize);
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->header()->setStretchLastSection(false);
  m_list->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  for (int c = ColAuthor; c < ColCount; ++c) {
    m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  // a game's long name ("The Elder Scrolls V: Skyrim Special Edition") would
  // take the names' room: cut, whole in its tooltip, and the user can widen it
  m_list->header()->setSectionResizeMode(ColGame, QHeaderView::Interactive);
  m_list->header()->resizeSection(ColGame, 190);
  layout->addWidget(m_list, 1);

  // the chosen collection: what it is, and its page
  auto* details = new QHBoxLayout;
  m_details     = new QLabel(this);
  m_details->setObjectName(QStringLiteral("collectionBrowserDetails"));
  m_details->setWordWrap(true);
  m_details->setTextFormat(Qt::PlainText);
  m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_details->setMinimumHeight(72);
  m_details->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  auto* links = new QVBoxLayout;
  m_page = new QPushButton(FluentIcons::glyph(OpenGlyph), tr("Collection page"), this);
  m_page->setAutoDefault(false);
  m_page->setToolTip(tr("Opens the collection's page on Nexus Mods in the browser."));
  links->addWidget(m_page);
  links->addStretch(1);
  details->addWidget(m_details, 1);
  details->addLayout(links);
  layout->addLayout(details);

  auto* footer = new QHBoxLayout;
  m_status     = new QLabel(this);
  m_status->setObjectName(QStringLiteral("instanceDetailCaption"));
  // plain: it shows Nexus' error text
  m_status->setTextFormat(Qt::PlainText);
  m_status->setWordWrap(true);
  auto* refreshButton = new QPushButton(FluentIcons::glyph(0xE72C), tr("Refresh"), this);
  refreshButton->setAutoDefault(false);
  m_more = new QPushButton(tr("Load more"), this);
  m_more->setAutoDefault(false);
  m_openButton =
      new QPushButton(FluentIcons::glyph(ListGlyph), tr("Open the collection"), this);
  m_openButton->setProperty("fluentPrimary", true);
  m_openButton->setAutoDefault(false);
  auto* close = new QPushButton(tr("Close"), this);
  close->setAutoDefault(false);
  footer->addWidget(m_status, 1);
  footer->addWidget(refreshButton);
  footer->addWidget(m_more);
  footer->addWidget(m_openButton);
  footer->addWidget(close);
  layout->addLayout(footer);

  // the search runs on Nexus: once the typing stops, not on every key
  m_typing = new QTimer(this);
  m_typing->setSingleShot(true);
  m_typing->setInterval(500);
  connect(m_typing, &QTimer::timeout, this, &CollectionBrowser::refresh);
  connect(m_search, &QLineEdit::textEdited, this, [this] {
    m_typing->start();
  });
  connect(m_search, &QLineEdit::returnPressed, this, &CollectionBrowser::refresh);
  // the clear button doesn't send textEdited
  connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (text.isEmpty()) {
      m_typing->start();
    }
  });
  // any change, by mouse, keys or UI Automation (fillGames() keeps it quiet)
  connect(m_game, &QComboBox::currentIndexChanged, this, [this] {
    GlobalSettings::setValue(AllGamesSetting, allGames());
    search(false);
  });
  connect(m_sort, &QComboBox::activated, this, [this] {
    // the order changes no count: one game only asks for its own page
    if (allGames()) {
      refresh();
    } else {
      search(false);
    }
  });
  connect(m_adult, &QCheckBox::toggled, this, &CollectionBrowser::refresh);
  connect(refreshButton, &QPushButton::clicked, this, &CollectionBrowser::refresh);
  connect(m_more, &QPushButton::clicked, this, [this] {
    search(true);
  });
  connect(m_openButton, &QPushButton::clicked, this, &CollectionBrowser::openSelected);
  connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
    // a section's header or its "show all" row: that game's collections
    if (const QString domain = item->data(ColName, GameRole).toString(); !domain.isEmpty()) {
      chooseGame(domain);
      return;
    }
    openSelected();
  });
  connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
    if (const QString domain = item->data(ColName, GameRole).toString(); !domain.isEmpty()) {
      chooseGame(domain);
    }
  });
  connect(m_list, &QTreeWidget::itemSelectionChanged, this, &CollectionBrowser::showDetails);
  connect(m_page, &QPushButton::clicked, this, [this] {
    if (const Entry* e = selected()) {
      shell::Open(QUrl(pageOf(*e)));
    }
  });
  connect(close, &QPushButton::clicked, this, &QDialog::close);

  auto& nexus = NexusInterface::instance();
  connect(&nexus, &NexusInterface::nxmApiV2Available, this, &CollectionBrowser::onResult);
  connect(&nexus, &NexusInterface::nxmRequestFailed, this, &CollectionBrowser::onFailed);

  // its size by the screen it is on, as the Wabbajack gallery; as the user
  // left it last time
  auto* fitter          = new ScreenFitter(this, QSize(820, 560));
  const QWidget* anchor = parentWidget();
  QScreen* screen = anchor != nullptr ? anchor->screen() : QGuiApplication::primaryScreen();
  if (screen != nullptr) {
    const bool overWindow = anchor != nullptr && anchor->isVisible() && !anchor->isMinimized();
    fitter->restore(QStringLiteral("collection_browser"), screen,
                    overWindow ? anchor->frameGeometry().center()
                               : screen->availableGeometry().center());
  }
  // the wheel over the sort box scrolls rather than re-sorting (see
  // MOApplication::dispatch)
  setProperty("fluentWheelScrollsPage", true);

  // this instance's game first, as the Wabbajack gallery; "All games" when
  // the user left it there, or when the instance's game isn't one of them
  fillGames();
  if (!GlobalSettings::value(AllGamesSetting, false).toBool() &&
      !m_instanceDomains.isEmpty()) {
    const int at = m_game->findData(m_instanceDomains.first());
    if (at >= 0) {
      const QSignalBlocker quiet(m_game);
      m_game->setCurrentIndex(at);
    }
  }

  // the requests wait for the Nexus sign-in; without one they wait for good
  QPointer<CollectionBrowser> self(this);
  QTimer::singleShot(15000, this, [self] {
    if (self && (!self->m_overview.isEmpty() || self->m_request >= 0) &&
        !NexusInterface::instance().getAPIUserAccount().isValid()) {
      self->m_status->setText(tr("Waiting for the Nexus sign-in. If Mod Organizer isn't "
                                 "signed in, sign in under Settings > Nexus."));
    }
  });

  showDetails();
  refresh();
}

bool CollectionBrowser::allGames() const
{
  return m_game->currentData().toString().isEmpty();
}

QString CollectionBrowser::gameName(const QString& domain) const
{
  for (const Game& g : m_games) {
    if (g.domain.compare(domain, Qt::CaseInsensitive) == 0) {
      return g.name;
    }
  }
  return domain;
}

bool CollectionBrowser::takes(const QString& domain) const
{
  return m_instanceDomains.contains(domain, Qt::CaseInsensitive);
}

QString CollectionBrowser::pageOf(const Entry& e) const
{
  return QStringLiteral("https://www.nexusmods.com/games/%1/collections/%2")
      .arg(e.domain, e.slug);
}

QJsonObject CollectionBrowser::filterFor(const QString& domain) const
{
  QJsonObject filter;
  filter.insert("gameDomain", QJsonArray{QJsonObject{{"value", domain}, {"op", "EQUALS"}}});
  const QString text = m_search->text().trimmed();
  if (!text.isEmpty()) {
    filter.insert("generalSearch",
                  QJsonArray{QJsonObject{{"value", text}, {"op", "WILDCARD"}}});
  }
  if (!m_adult->isChecked()) {
    filter.insert("adultContent",
                  QJsonArray{QJsonObject{{"value", false}, {"op", "EQUALS"}}});
  }
  return filter;
}

void CollectionBrowser::refresh()
{
  m_typing->stop();
  if (!allGames()) {
    search(false);
  } else {
    // "All games" is read from the overview itself
    m_request = -1;
    m_more->hide();
    m_list->clear();
    m_entries.clear();
    showDetails();
  }
  // the games' counts follow the search, as the Wabbajack gallery's do
  requestOverview();
}

void CollectionBrowser::requestOverview()
{
  if (m_games.empty()) {
    m_status->setText(tr("None of the games MO2 Revamped has a plugin for is on Nexus."));
    return;
  }
  // a newer one replaces it: the older answers are dropped when they come
  m_overview.clear();
  m_overviewData.clear();
  m_overviewError.clear();
  const int games = int(m_games.size());
  for (int first = 0; first < games; first += GamesPerRequest) {
    const int count = qMin(GamesPerRequest, games - first);
    QJsonObject vars;
    vars.insert("sort", QJsonArray{QJsonObject{
                            {m_sort->currentData().toString(),
                             QJsonObject{{"direction", "DESC"}}}}});
    vars.insert("count", SectionSize);
    for (int i = first; i < first + count; ++i) {
      vars.insert(QStringLiteral("f%1").arg(i), filterFor(m_games[std::size_t(i)].domain));
    }
    const int id = NexusInterface::instance().requestGraphQL(
        overviewQuery(first, count), vars, QVariant(), QStringLiteral("collections"));
    if (id < 0) {
      m_overview.clear();
      m_status->setText(tr("Nexus can't be reached right now: the request limit was "
                           "reached. Try again later."));
      return;
    }
    m_overview.insert(id);
  }
  if (allGames()) {
    m_status->setText(tr("Searching…"));
  }
}

void CollectionBrowser::search(bool more)
{
  if (allGames()) {
    if (!more) {
      refresh();
    }
    return;
  }
  // a new search replaces the one under way; more waits for it
  if (more && m_request >= 0) {
    return;
  }
  if (!more) {
    m_list->clear();
    m_entries.clear();
    m_loaded = 0;
    m_total  = 0;
    showDetails();
  }

  QJsonObject vars;
  vars.insert("filter", filterFor(m_game->currentData().toString()));
  vars.insert("sort", QJsonArray{QJsonObject{
                          {m_sort->currentData().toString(),
                           QJsonObject{{"direction", "DESC"}}}}});
  vars.insert("offset", m_loaded);
  vars.insert("count", PageSize);

  m_request = NexusInterface::instance().requestGraphQL(
      browseQuery(), vars, QVariant(), QStringLiteral("collections"));
  if (m_request < 0) {
    m_status->setText(tr("Nexus can't be reached right now: the request limit was "
                         "reached. Try again later."));
    return;
  }
  m_status->setText(tr("Searching…"));
  m_more->show();
  m_more->setEnabled(false);
}

void CollectionBrowser::onResult(QVariant, QVariant result, int id)
{
  if (id < 0) {
    return;
  }
  const QVariantMap data = result.toMap().value("data").toMap();
  // an answer with errors may still carry the games that worked
  QString error;
  if (const QVariantList errors = result.toMap().value("errors").toList(); !errors.isEmpty()) {
    error = errors.first().toMap().value("message").toString();
    log::warn("collections gallery: Nexus answered with {} error(s), the first: {}",
              errors.size(), error);
  }
  if (m_overview.remove(id)) {
    // a game Nexus failed on comes as null: left out, not counted as none
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
      if (it.value().isValid() && !it.value().isNull()) {
        m_overviewData.insert(it.key(), it.value());
      }
    }
    if (!error.isEmpty()) {
      m_overviewError = error;
    }
    if (m_overview.isEmpty()) {
      readOverview(m_overviewData);
    }
  } else if (id == m_request) {
    m_request = -1;
    if (!data.contains("collectionsV2") || data.value("collectionsV2").isNull()) {
      m_more->setEnabled(m_loaded < m_total);
      m_status->setText(tr("Nexus didn't answer: %1")
                            .arg(error.isEmpty() ? tr("invalid response") : error));
      return;
    }
    readPage(data.value("collectionsV2").toMap());
  }
}

void CollectionBrowser::onFailed(QString, int, int, QVariant, int id, int,
                                 const QString& error)
{
  if (id < 0) {
    return;
  }
  if (m_overview.remove(id)) {
    // the games of the other parts are still shown
    m_overviewError = error;
    if (m_overview.isEmpty()) {
      readOverview(m_overviewData);
    }
    return;
  }
  if (id != m_request) {
    return;
  }
  m_request = -1;
  m_more->setEnabled(m_loaded < m_total);
  m_status->setText(tr("Nexus didn't answer: %1").arg(error));
}

CollectionBrowser::Entry CollectionBrowser::readEntry(const QVariantMap& c) const
{
  const QVariantMap rev  = c.value("latestPublishedRevision").toMap();
  const QVariantMap game = c.value("game").toMap();
  Entry e;
  e.slug   = c.value("slug").toString();
  e.domain = game.value("domainName").toString();
  e.game   = gameName(e.domain);
  if (e.game == e.domain) {
    e.game = game.value("name").toString();
  }
  e.name         = c.value("name").toString().trimmed();
  e.author       = c.value("user").toMap().value("name").toString();
  e.summary      = c.value("summary").toString().trimmed();
  e.category     = c.value("category").toMap().value("name").toString();
  e.image        = c.value("tileImage").toMap().value("thumbnailUrl").toString();
  e.revision     = rev.value("revisionNumber").toInt();
  e.mods         = rev.value("modCount").toInt();
  e.size         = rev.value("totalSize").toString().toLongLong();
  e.endorsements = c.value("endorsements").toInt();
  e.downloads    = c.value("totalDownloads").toInt();
  e.adult        = c.value("adultContent").toBool();
  e.updated      = QDateTime::fromString(rev.value("updatedAt").toString(), Qt::ISODate);
  for (const QVariant& g : rev.value("gameVersions").toList()) {
    e.gameVersions.append(g.toMap().value("reference").toString());
  }
  return e;
}

QTreeWidgetItem* CollectionBrowser::addRow(QTreeWidgetItem* section, Entry e)
{
  auto* item =
      section != nullptr ? new QTreeWidgetItem(section) : new QTreeWidgetItem(m_list);
  item->setData(ColName, EntryRole, qulonglong(m_entries.size()));
  item->setText(ColName, e.name);
  // plain text: a summary with markup would load its images
  item->setToolTip(ColName, e.summary.isEmpty()
                                ? QString()
                                : Qt::convertFromPlainText(e.summary, Qt::WhiteSpaceNormal));
  item->setText(ColGame, e.game);
  item->setToolTip(ColGame, Qt::convertFromPlainText(e.game, Qt::WhiteSpaceNormal));
  item->setText(ColAuthor, e.author);
  item->setText(ColRevision, e.revision > 0 ? QString::number(e.revision) : QString());
  item->setTextAlignment(ColRevision, Qt::AlignRight | Qt::AlignVCenter);
  item->setText(ColMods, QString::number(e.mods));
  item->setTextAlignment(ColMods, Qt::AlignRight | Qt::AlignVCenter);
  item->setText(ColSize, e.size > 0 ? localizedByteSize(e.size) : QString());
  item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
  item->setText(ColEndorsements, QLocale().toString(e.endorsements));
  item->setTextAlignment(ColEndorsements, Qt::AlignRight | Qt::AlignVCenter);
  if (e.updated.isValid()) {
    item->setText(ColUpdated,
                  QLocale().toString(e.updated.toLocalTime().date(), QLocale::ShortFormat));
  }
  const QString image = e.image;
  m_entries.push_back(std::move(e));
  if (const auto it = m_images.constFind(image); it != m_images.constEnd()) {
    item->setIcon(ColName, QIcon(it.value()));
  } else {
    // every row as tall as a picture from the start: no row grows when its
    // picture comes
    item->setIcon(ColName, placeholder());
    loadImage(image);
  }
  return item;
}

void CollectionBrowser::readOverview(const QVariantMap& data)
{
  struct Section
  {
    std::size_t game;
    int total;
    QVariantList nodes;
  };
  std::vector<Section> sections;

  // not one game came back (`{"data": null}`, or a proxy's page that isn't
  // JSON, without "errors"): Nexus' failure, not "no collections"; the games'
  // counts from before stay
  bool anyGame = false;
  for (std::size_t i = 0; i < m_games.size() && !anyGame; ++i) {
    const QString key = QStringLiteral("g%1").arg(i);
    anyGame = data.contains(key) && !data.value(key).isNull();
  }
  if (!anyGame && !m_games.empty()) {
    const QString error =
        m_overviewError.isEmpty() ? tr("invalid response") : m_overviewError;
    log::warn("collections gallery: no game in Nexus' answer ({})", error);
    // (only the counts were asked for while one game is shown: its page stays)
    if (!allGames()) {
      return;
    }
    m_more->hide();
    m_status->setText(tr("Nexus didn't answer: %1").arg(error));
    showDetails();
    return;
  }

  m_counts.clear();
  int all = 0;
  for (std::size_t i = 0; i < m_games.size(); ++i) {
    const QString key = QStringLiteral("g%1").arg(i);
    // a game of a part that failed: no count, rather than 0
    if (!data.contains(key) || data.value(key).isNull()) {
      continue;
    }
    const QVariantMap g = data.value(key).toMap();
    const int total     = g.value("totalCount").toInt();
    m_counts.insert(m_games[i].domain, total);
    all += total;
    if (total > 0) {
      sections.push_back({i, total, g.value("nodes").toList()});
    }
  }
  fillGames();
  if (!allGames()) {
    return;
  }

  m_list->clear();
  m_entries.clear();
  m_entries.reserve(m_games.size() * SectionSize);
  // the instance's own game first, then those it takes links for, then the
  // rest, each by name (m_games is sorted)
  const auto rank = [this](const Section& s) {
    const QString& domain = m_games[s.game].domain;
    if (!m_instanceDomains.isEmpty() &&
        domain.compare(m_instanceDomains.first(), Qt::CaseInsensitive) == 0) {
      return 0;
    }
    return takes(domain) ? 1 : 2;
  };
  std::stable_sort(sections.begin(), sections.end(),
                   [&rank](const Section& a, const Section& b) {
                     return rank(a) < rank(b);
                   });
  QFont bold = m_list->font();
  bold.setBold(true);
  if (bold.pointSizeF() > 0) {
    bold.setPointSizeF(bold.pointSizeF() * 1.1);
  }
  for (const Section& s : sections) {
    const Game& game = m_games[s.game];
    auto* header     = new QTreeWidgetItem(m_list);
    header->setText(ColName, tr("%1 · %2 collections")
                                 .arg(game.name, QLocale().toString(s.total)));
    header->setFont(ColName, bold);
    header->setData(ColName, GameRole, game.domain);
    header->setToolTip(ColName, tr("Shows all of %1's collections.").arg(game.name));
    header->setFlags(Qt::ItemIsEnabled);
    header->setFirstColumnSpanned(true);
    for (const QVariant& v : s.nodes) {
      Entry e = readEntry(v.toMap());
      if (e.slug.isEmpty() || e.revision <= 0) {
        continue;
      }
      if (e.domain.isEmpty()) {
        e.domain = game.domain;
        e.game   = game.name;
      }
      addRow(header, std::move(e));
    }
    if (s.total > header->childCount()) {
      auto* rest = new QTreeWidgetItem(header);
      rest->setText(ColName, tr("Show all %1 of %2's collections")
                                 .arg(QLocale().toString(s.total), game.name));
      rest->setData(ColName, GameRole, game.domain);
      rest->setFlags(Qt::ItemIsEnabled);
      rest->setFirstColumnSpanned(true);
      rest->setForeground(ColName, palette().brush(QPalette::Link));
    }
    header->setExpanded(true);
  }
  m_more->hide();
  // nothing came at all: Nexus' failure, not an empty search
  if (m_overviewData.isEmpty() && !m_overviewError.isEmpty()) {
    m_status->setText(tr("Nexus didn't answer: %1").arg(m_overviewError));
    showDetails();
    return;
  }
  m_status->setText(all == 0 ? tr("No collections found.")
                             : tr("%1 collections in %2 games. Choose a game to see all "
                                  "of its own.")
                                   .arg(QLocale().toString(all))
                                   .arg(sections.size()));
  if (!m_overviewError.isEmpty()) {
    m_status->setText(m_status->text() + u' ' +
                      tr("Some games couldn't be read: %1").arg(m_overviewError));
  }
  for (QTreeWidgetItemIterator it(m_list); *it != nullptr; ++it) {
    if ((*it)->data(ColName, EntryRole).isValid()) {
      m_list->setCurrentItem(*it);
      break;
    }
  }
  showDetails();
}

void CollectionBrowser::readPage(const QVariantMap& data)
{
  m_total = data.value("totalCount").toInt();
  // the rows hold an index into m_entries: it only grows until a new search
  m_entries.reserve(m_entries.size() + PageSize);
  const QString domain = m_game->currentData().toString();
  for (const QVariant& v : data.value("nodes").toList()) {
    Entry e = readEntry(v.toMap());
    if (e.slug.isEmpty() || e.revision <= 0) {
      continue;
    }
    if (e.domain.isEmpty()) {
      e.domain = domain;
      e.game   = gameName(domain);
    }
    addRow(nullptr, std::move(e));
  }
  m_loaded += PageSize;
  m_more->show();
  m_more->setEnabled(m_loaded < m_total);
  m_status->setText(m_total == 0 ? tr("No collections found.")
                                 : tr("%1 of %2 collections")
                                       .arg(m_list->topLevelItemCount())
                                       .arg(m_total));
  if (m_list->currentItem() == nullptr && m_list->topLevelItemCount() > 0) {
    m_list->setCurrentItem(m_list->topLevelItem(0));
  }
  showDetails();
}

void CollectionBrowser::fillGames()
{
  // its list open: rebuilt at the next answer, not under the user's cursor
  if (m_game->view() != nullptr && m_game->view()->isVisible()) {
    return;
  }
  // the games with collections (with the search's filters), with how many,
  // as the Wabbajack gallery's; the instance's game always
  const QString current =
      m_game->count() > 0 ? m_game->currentData().toString() : QString();
  int all = 0;
  for (auto it = m_counts.cbegin(); it != m_counts.cend(); ++it) {
    all += it.value();
  }
  m_game->blockSignals(true);
  m_game->clear();
  m_game->addItem(m_counts.isEmpty() ? tr("All games")
                                     : tr("All games (%1)").arg(QLocale().toString(all)),
                  QString());
  for (const Game& g : m_games) {
    const auto count = m_counts.constFind(g.domain);
    if (count == m_counts.constEnd()) {
      m_game->addItem(g.name, g.domain);
    } else if (count.value() > 0 || takes(g.domain) || g.domain == current) {
      m_game->addItem(
          QStringLiteral("%1 (%2)").arg(g.name, QLocale().toString(count.value())), g.domain);
    }
  }
  const int at = m_game->findData(current);
  m_game->setCurrentIndex(at >= 0 ? at : 0);
  m_game->blockSignals(false);
}

void CollectionBrowser::chooseGame(const QString& domain)
{
  const int at = m_game->findData(domain);
  if (at < 0 || at == m_game->currentIndex()) {
    return;
  }
  // the box's handler searches
  m_game->setCurrentIndex(at);
}

QIcon CollectionBrowser::placeholder()
{
  if (m_placeholder.isNull()) {
    QPixmap empty(ThumbSize);
    empty.fill(Qt::transparent);
    m_placeholder = QIcon(empty);
  }
  return m_placeholder;
}

void CollectionBrowser::loadImage(const QString& url)
{
  if (url.isEmpty() || m_imagesAsked.contains(url)) {
    return;
  }
  m_imagesAsked.insert(url);
  CollectionImages::fetch(m_network, url, this, [this, url](const QImage& image) {
    if (image.isNull()) {
      return;
    }
    const QPixmap pixmap = CollectionImages::thumbnail(image, ThumbSize, devicePixelRatioF());
    m_images.insert(url, pixmap);
    // the rows in the sections too
    for (QTreeWidgetItemIterator it(m_list); *it != nullptr; ++it) {
      const QVariant index = (*it)->data(ColName, EntryRole);
      if (index.isValid() && index.toULongLong() < m_entries.size() &&
          m_entries[index.toULongLong()].image == url) {
        (*it)->setIcon(ColName, QIcon(pixmap));
      }
    }
  });
}

const CollectionBrowser::Entry* CollectionBrowser::selected() const
{
  const QTreeWidgetItem* item = m_list->currentItem();
  if (item == nullptr) {
    return nullptr;
  }
  const QVariant i = item->data(ColName, EntryRole);
  return i.isValid() && i.toULongLong() < m_entries.size() ? &m_entries[i.toULongLong()]
                                                            : nullptr;
}

void CollectionBrowser::showDetails()
{
  const Entry* e = selected();
  m_page->setVisible(e != nullptr);
  if (e == nullptr) {
    m_openButton->setEnabled(false);
    m_details->setText(m_entries.empty() ? QString() : tr("Choose a collection."));
    return;
  }
  QStringList lines;
  QStringList about{e->name};
  if (!e->author.isEmpty()) {
    about << tr("by %1").arg(e->author);
  }
  if (e->revision > 0) {
    about << tr("revision %1").arg(e->revision);
  }
  if (!e->game.isEmpty()) {
    about << e->game;
  }
  lines << about.join(QStringLiteral(" · "));
  if (!e->summary.isEmpty()) {
    lines << e->summary;
  }
  QStringList facts;
  facts << tr("%n mod(s)", nullptr, e->mods);
  if (e->size > 0) {
    facts << tr("%1 to download").arg(localizedByteSize(e->size));
  }
  facts << tr("%n endorsement(s)", nullptr, e->endorsements);
  facts << tr("%n download(s)", nullptr, e->downloads);
  if (!e->category.isEmpty()) {
    facts << e->category;
  }
  if (!e->gameVersions.isEmpty()) {
    facts << tr("game version %1").arg(e->gameVersions.join(QStringLiteral(", ")));
  }
  lines << facts.join(QStringLiteral(" · "));
  if (e->adult) {
    lines << tr("Adult content (NSFW).");
  }
  // a collection installs into an instance of its own game
  const bool here = takes(e->domain);
  if (!here) {
    lines << tr("It is for %1: open it from an instance for %1 to install it. Its page "
                "opens from here.")
                 .arg(e->game);
  }
  m_openButton->setEnabled(here);
  m_openButton->setToolTip(here ? tr("Shows the collection's mods and what it needs; "
                                     "nothing downloads until you press Start there.")
                                : tr("This instance manages another game."));
  m_details->setText(lines.join(u'\n'));
  m_page->setToolTip(Qt::convertFromPlainText(pageOf(*e), Qt::WhiteSpaceNormal));
}

void CollectionBrowser::openSelected()
{
  const Entry* e = selected();
  if (e == nullptr || !m_open || !takes(e->domain)) {
    return;
  }
  m_open(pageOf(*e));
}
