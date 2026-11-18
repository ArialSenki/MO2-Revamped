#include "collectionbrowser.h"

#include "fluenticons.h"
#include "nexusinterface.h"
#include "screenfitter.h"
#include "selfupdater.h"

#include <utility.h>

#include <QApplication>
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
#include <QNetworkReply>
#include <QPushButton>
#include <QScreen>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{

constexpr int PageSize  = 20;
constexpr int EntryRole = Qt::UserRole + 1;
// a picture of a card: anything larger is dropped
constexpr qint64 MaxImageBytes = 8 * 1024 * 1024;
const QSize ThumbSize(96, 54);

constexpr char16_t OpenGlyph = 0xE8A7;  // OpenInNewWindow
constexpr char16_t ListGlyph = 0xE8FD;  // BulletedList

const char* BrowseQuery = R"(query ($filter: CollectionsSearchFilter,
    $sort: [CollectionsSearchSort!], $offset: Int, $count: Int) {
  collectionsV2(filter: $filter, sort: $sort, offset: $offset, count: $count) {
    totalCount
    nodes {
      slug
      name
      summary
      endorsements
      totalDownloads
      adultContent
      category { name }
      tileImage { thumbnailUrl(size: small) }
      user { name }
      latestPublishedRevision {
        revisionNumber modCount totalSize updatedAt gameVersions { reference }
      }
    }
  }
})";

enum Column
{
  ColName = 0,
  ColAuthor,
  ColMods,
  ColSize,
  ColEndorsements,
  ColVersion,
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

}  // namespace CollectionImages

CollectionBrowser::CollectionBrowser(const QString& gameDomain, const QString& gameName,
                                     std::function<void(const QString&)> open,
                                     QWidget* parent)
    : QDialog(parent), m_domain(gameDomain), m_open(std::move(open))
{
  setObjectName(QStringLiteral("CollectionBrowser"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Nexus collections for %1").arg(gameName));
  setWindowFlag(Qt::WindowMaximizeButtonHint);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(8);

  auto* bar = new QHBoxLayout;
  m_search  = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Search collections"));
  m_search->setClearButtonEnabled(true);
  m_search->setAccessibleName(m_search->placeholderText());
  m_sort = new QComboBox(this);
  m_sort->setAccessibleName(tr("Sort"));
  m_sort->addItem(tr("Most endorsed"), QStringLiteral("endorsements"));
  m_sort->addItem(tr("Most downloaded"), QStringLiteral("downloads"));
  m_sort->addItem(tr("Recently updated"), QStringLiteral("updatedAt"));
  auto* go = new QPushButton(tr("Search"), this);
  go->setAutoDefault(false);
  bar->addWidget(m_search, 1);
  bar->addWidget(m_sort);
  bar->addWidget(go);
  layout->addLayout(bar);

  m_list = new QTreeWidget(this);
  m_list->setObjectName(QStringLiteral("collectionBrowserList"));
  m_list->setAccessibleName(tr("Collections"));
  m_list->setColumnCount(ColCount);
  m_list->setHeaderLabels({tr("Name"), tr("Author"), tr("Mods"), tr("Size"),
                           tr("Endorsements"), tr("Game version"), tr("Updated")});
  m_list->setRootIsDecorated(false);
  m_list->setUniformRowHeights(true);
  // a picture on each row, as the Wabbajack gallery
  m_list->setIconSize(ThumbSize);
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->header()->setStretchLastSection(false);
  m_list->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  for (int c = ColAuthor; c < ColCount; ++c) {
    m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  layout->addWidget(m_list, 1);

  // the chosen collection: what it is, and its page
  auto* details = new QHBoxLayout;
  details->setSpacing(12);
  m_details = new QLabel(this);
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
  m_status->setWordWrap(true);
  m_more = new QPushButton(tr("Load more"), this);
  m_more->setAutoDefault(false);
  m_openButton =
      new QPushButton(FluentIcons::glyph(ListGlyph), tr("Open the collection"), this);
  m_openButton->setProperty("fluentPrimary", true);
  m_openButton->setAutoDefault(false);
  m_openButton->setToolTip(tr("Shows the collection's mods and what it needs; nothing "
                              "downloads until you press Start there."));
  auto* close = new QPushButton(tr("Close"), this);
  close->setAutoDefault(false);
  footer->addWidget(m_status, 1);
  footer->addWidget(m_more);
  footer->addWidget(m_openButton);
  footer->addWidget(close);
  layout->addLayout(footer);

  connect(go, &QPushButton::clicked, this, [this] {
    search(false);
  });
  connect(m_search, &QLineEdit::returnPressed, this, [this] {
    search(false);
  });
  connect(m_sort, &QComboBox::activated, this, [this] {
    search(false);
  });
  connect(m_more, &QPushButton::clicked, this, [this] {
    search(true);
  });
  connect(m_openButton, &QPushButton::clicked, this, &CollectionBrowser::openSelected);
  connect(m_list, &QTreeWidget::itemActivated, this, &CollectionBrowser::openSelected);
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

  // its size by the screen it is on, as the Wabbajack gallery
  auto* fitter          = new ScreenFitter(this, QSize(820, 560));
  const QWidget* anchor = parentWidget();
  QScreen* screen = anchor != nullptr ? anchor->screen() : QGuiApplication::primaryScreen();
  if (screen != nullptr) {
    const bool overWindow = anchor != nullptr && anchor->isVisible() && !anchor->isMinimized();
    fitter->place(screen, overWindow ? anchor->frameGeometry().center()
                                     : screen->availableGeometry().center());
  }
  // the wheel over the sort box scrolls rather than re-sorting (see
  // MOApplication::dispatch)
  setProperty("fluentWheelScrollsPage", true);

  // MO2 Revamped: maximized unless the user left it otherwise, so the table
  // and the details fit; the size above is the one it restores to
  if (ScreenFitter::opensMaximized(QStringLiteral("collection_browser"))) {
    setWindowState(windowState() | Qt::WindowMaximized);
  }
  connect(this, &QDialog::finished, this, [this] {
    ScreenFitter::rememberMaximized(QStringLiteral("collection_browser"), this);
  });

  showDetails();
  search(false);
}

QString CollectionBrowser::pageOf(const Entry& e) const
{
  return QStringLiteral("https://www.nexusmods.com/games/%1/collections/%2")
      .arg(m_domain, e.slug);
}

void CollectionBrowser::search(bool more)
{
  // one request at a time, and only when asked: light on the API
  if (m_request >= 0) {
    return;
  }
  if (!more) {
    m_list->clear();
    m_entries.clear();
    m_loaded = 0;
    m_total  = 0;
    showDetails();
  }

  QJsonObject filter;
  filter.insert("gameDomain", QJsonArray{QJsonObject{{"value", m_domain}, {"op", "EQUALS"}}});
  const QString text = m_search->text().trimmed();
  if (!text.isEmpty()) {
    filter.insert("generalSearch",
                  QJsonArray{QJsonObject{{"value", text}, {"op", "WILDCARD"}}});
  }
  QJsonObject vars;
  vars.insert("filter", filter);
  vars.insert("sort", QJsonArray{QJsonObject{
                          {m_sort->currentData().toString(),
                           QJsonObject{{"direction", "DESC"}}}}});
  vars.insert("offset", m_loaded);
  vars.insert("count", PageSize);

  m_request = NexusInterface::instance().requestGraphQL(
      QString::fromUtf8(BrowseQuery), vars, QVariant(), QStringLiteral("collections"));
  if (m_request < 0) {
    m_status->setText(tr("Nexus can't be reached right now: the request limit was "
                         "reached. Try again later."));
    return;
  }
  m_status->setText(tr("Searching…"));
  m_more->setEnabled(false);
}

void CollectionBrowser::onResult(QVariant, QVariant result, int id)
{
  if (id < 0 || id != m_request) {
    return;
  }
  m_request = -1;

  const QVariantMap data =
      result.toMap().value("data").toMap().value("collectionsV2").toMap();
  m_total = data.value("totalCount").toInt();
  // the rows hold an index into m_entries: it only grows until a new search
  m_entries.reserve(m_entries.size() + PageSize);
  for (const QVariant& v : data.value("nodes").toList()) {
    const QVariantMap c   = v.toMap();
    const QVariantMap rev = c.value("latestPublishedRevision").toMap();
    if (rev.isEmpty()) {
      continue;
    }
    Entry e;
    e.slug         = c.value("slug").toString();
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
    for (const QVariant& g : rev.value("gameVersions").toList()) {
      e.gameVersions.append(g.toMap().value("reference").toString());
    }

    auto* item = new QTreeWidgetItem(m_list);
    item->setData(ColName, EntryRole, qulonglong(m_entries.size()));
    item->setText(ColName, e.name);
    // plain text: a summary with markup would load its images
    item->setToolTip(ColName, e.summary.isEmpty()
                                  ? QString()
                                  : Qt::convertFromPlainText(e.summary, Qt::WhiteSpaceNormal));
    item->setText(ColAuthor, e.author);
    item->setText(ColMods, QString::number(e.mods));
    item->setTextAlignment(ColMods, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColSize, localizedByteSize(e.size));
    item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColEndorsements, QLocale().toString(e.endorsements));
    item->setTextAlignment(ColEndorsements, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColVersion, e.gameVersions.join(", "));
    const QDateTime updated =
        QDateTime::fromString(rev.value("updatedAt").toString(), Qt::ISODate);
    item->setText(ColUpdated, QLocale().toString(updated.toLocalTime().date(),
                                                 QLocale::ShortFormat));
    const QString image = e.image;
    m_entries.push_back(std::move(e));
    if (const auto it = m_images.constFind(image); it != m_images.constEnd()) {
      item->setIcon(ColName, QIcon(it.value()));
    } else {
      // every row as tall as a picture from the start (the rows share one
      // height): no row grows when its picture comes
      item->setIcon(ColName, placeholder());
      loadImage(image);
    }
  }
  m_loaded += PageSize;
  m_more->setEnabled(m_loaded < m_total);
  m_status->setText(m_total == 0 ? tr("No collections found.")
                                 : tr("%1 of %2 collections").arg(m_list->topLevelItemCount())
                                                             .arg(m_total));
  if (m_list->currentItem() == nullptr && m_list->topLevelItemCount() > 0) {
    m_list->setCurrentItem(m_list->topLevelItem(0));
  }
  showDetails();
}

void CollectionBrowser::onFailed(QString, int, int, QVariant, int id, int,
                                 const QString& error)
{
  if (id < 0 || id != m_request) {
    return;
  }
  m_request = -1;
  m_more->setEnabled(m_loaded < m_total);
  m_status->setText(tr("Nexus didn't answer: %1").arg(error));
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
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap  = QPixmap::fromImage(
        image.scaled(ThumbSize * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pixmap.setDevicePixelRatio(dpr);
    m_images.insert(url, pixmap);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
      QTreeWidgetItem* it = m_list->topLevelItem(i);
      const auto index    = it->data(ColName, EntryRole).toULongLong();
      if (index < m_entries.size() && m_entries[index].image == url) {
        it->setIcon(ColName, QIcon(pixmap));
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
  const auto i = item->data(ColName, EntryRole).toULongLong();
  return i < m_entries.size() ? &m_entries[i] : nullptr;
}

void CollectionBrowser::showDetails()
{
  const Entry* e = selected();
  m_page->setVisible(e != nullptr);
  m_openButton->setEnabled(e != nullptr);
  if (e == nullptr) {
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
  m_details->setText(lines.join(u'\n'));
  m_page->setToolTip(pageOf(*e));
}

void CollectionBrowser::openSelected()
{
  const Entry* e = selected();
  if (e == nullptr || !m_open) {
    return;
  }
  m_open(pageOf(*e));
}
