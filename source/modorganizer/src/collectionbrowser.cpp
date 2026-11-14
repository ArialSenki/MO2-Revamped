#include "collectionbrowser.h"

#include "nexusinterface.h"

#include <utility.h>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{

constexpr int PageSize = 20;
constexpr int SlugRole = Qt::UserRole + 1;

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
  ColVersion,
  ColUpdated,
  ColCount
};

}  // namespace

CollectionBrowser::CollectionBrowser(const QString& gameDomain, const QString& gameName,
                                     std::function<void(const QString&)> open,
                                     QWidget* parent)
    : QDialog(parent), m_domain(gameDomain), m_open(std::move(open))
{
  setObjectName(QStringLiteral("CollectionBrowser"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Nexus collections for %1").arg(gameName));
  setWindowFlag(Qt::WindowMaximizeButtonHint);
  resize(1000, 640);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(8);

  auto* bar = new QHBoxLayout;
  m_search  = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Search collections"));
  m_search->setClearButtonEnabled(true);
  m_sort = new QComboBox(this);
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
  m_list->setColumnCount(ColCount);
  m_list->setHeaderLabels({tr("Name"), tr("Author"), tr("Mods"), tr("Size"),
                           tr("Game version"), tr("Updated")});
  m_list->setRootIsDecorated(false);
  m_list->setUniformRowHeights(true);
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->header()->setStretchLastSection(false);
  m_list->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  for (int c = ColAuthor; c < ColCount; ++c) {
    m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  layout->addWidget(m_list, 1);

  auto* footer = new QHBoxLayout;
  m_status     = new QLabel(this);
  m_status->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_more = new QPushButton(tr("Load more"), this);
  m_more->setAutoDefault(false);
  m_openButton = new QPushButton(tr("Open"), this);
  m_openButton->setProperty("fluentPrimary", true);
  m_openButton->setAutoDefault(false);
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
  connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
    m_openButton->setEnabled(m_list->currentItem() != nullptr);
  });
  connect(close, &QPushButton::clicked, this, &QDialog::close);

  auto& nexus = NexusInterface::instance();
  connect(&nexus, &NexusInterface::nxmApiV2Available, this, &CollectionBrowser::onResult);
  connect(&nexus, &NexusInterface::nxmRequestFailed, this, &CollectionBrowser::onFailed);

  m_openButton->setEnabled(false);
  search(false);
}

void CollectionBrowser::search(bool more)
{
  // one request at a time, and only when asked: light on the API
  if (m_request >= 0) {
    return;
  }
  if (!more) {
    m_list->clear();
    m_loaded = 0;
    m_total  = 0;
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
  for (const QVariant& v : data.value("nodes").toList()) {
    const QVariantMap c   = v.toMap();
    const QVariantMap rev = c.value("latestPublishedRevision").toMap();
    if (rev.isEmpty()) {
      continue;
    }
    auto* item = new QTreeWidgetItem(m_list);
    item->setText(ColName, c.value("name").toString());
    // plain text: a summary with markup would load its images
    const QString summary = c.value("summary").toString();
    item->setToolTip(ColName, summary.isEmpty()
                                  ? QString()
                                  : Qt::convertFromPlainText(summary, Qt::WhiteSpaceNormal));
    item->setData(ColName, SlugRole, c.value("slug").toString());
    item->setText(ColAuthor, c.value("user").toMap().value("name").toString());
    item->setText(ColMods, QString::number(rev.value("modCount").toInt()));
    item->setTextAlignment(ColMods, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColSize,
                  localizedByteSize(rev.value("totalSize").toString().toULongLong()));
    item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    QStringList versions;
    for (const QVariant& g : rev.value("gameVersions").toList()) {
      versions.append(g.toMap().value("reference").toString());
    }
    item->setText(ColVersion, versions.join(", "));
    const QDateTime updated =
        QDateTime::fromString(rev.value("updatedAt").toString(), Qt::ISODate);
    item->setText(ColUpdated, QLocale().toString(updated.toLocalTime().date(),
                                                 QLocale::ShortFormat));
    item->setToolTip(ColAuthor, tr("%1 endorsements, %2 downloads")
                                    .arg(c.value("endorsements").toInt())
                                    .arg(c.value("totalDownloads").toInt()));
  }
  m_loaded += PageSize;
  m_more->setEnabled(m_loaded < m_total);
  m_status->setText(m_total == 0 ? tr("No collections found.")
                                 : tr("%1 of %2 collections").arg(m_list->topLevelItemCount())
                                                             .arg(m_total));
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

void CollectionBrowser::openSelected()
{
  const auto* item = m_list->currentItem();
  if (item == nullptr || !m_open) {
    return;
  }
  m_open(QStringLiteral("https://www.nexusmods.com/games/%1/collections/%2")
             .arg(m_domain, item->data(ColName, SlugRole).toString()));
}
