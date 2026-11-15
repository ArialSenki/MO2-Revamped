#include "wabbajackdialog.h"

#include "downloadmanager.h"
#include "fluenticons.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "settings.h"

#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

using namespace MOBase;
using Wabbajack::DirectiveType;
using Wabbajack::Source;
using Wabbajack::TypeCount;
using Wabbajack::findGamePlugin;
using Wabbajack::gameInfo;
using Wabbajack::hashFile;
using Wabbajack::readModList;
// Archive and ModList are names MO2 has already
using WjArchive = Wabbajack::Archive;
using WjList    = Wabbajack::ModList;

namespace
{

constexpr char FolderSetting[] = "Wabbajack/downloadsFolder";
constexpr char FileSetting[]   = "Wabbajack/lastListFolder";

constexpr char16_t FolderGlyph = 0xE8B7;  // Folder
constexpr char16_t LinkGlyph   = 0xE8A7;  // OpenInNewWindow

enum Column
{
  ColName = 0,
  ColSource,
  ColSize,
  ColStatus,
  ColCount
};

constexpr int RowRole = Qt::UserRole + 1;

// the order the archives are listed in, and the groups of the summary
int sourceOrder(Source s)
{
  switch (s) {
  case Source::Nexus:
    return 0;
  case Source::WabbajackCdn:
    return 1;
  case Source::Http:
    return 2;
  case Source::GameFile:
    return 4;
  default:
    return 3;
  }
}

QString plainTip(const QString& text)
{
  return text.isEmpty() ? QString() : Qt::convertFromPlainText(text, Qt::WhiteSpaceNormal);
}

// a web address from the list, shown only when it is one
bool isWebLink(const QString& text)
{
  const QUrl url(text);
  const QString scheme = url.scheme().toLower();
  return url.isValid() && (scheme == u"http" || scheme == u"https") && !url.host().isEmpty();
}

}  // namespace

WabbajackDialog::WabbajackDialog(OrganizerCore& core, const QString& path, QWidget* anchor)
    : QDialog(nullptr, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                           Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint),
      m_core(core), m_path(path), m_anchor(anchor),
      m_cancel(std::make_shared<std::atomic_bool>(false)),
      m_readCancel(std::make_shared<std::atomic_bool>(false))
{
  setAttribute(Qt::WA_DeleteOnClose);
  setProperty("mo2rOwnWindow", true);
  setProperty("mo2rOwnPosition", true);
  setupUi();
  placeWindow();

  m_reader = new QFutureWatcher<std::optional<WjList>>(this);
  connect(m_reader, &QFutureWatcherBase::finished, this, &WabbajackDialog::onRead);
  const QString file = m_path;
  auto cancel        = m_readCancel;
  m_reader->setFuture(QtConcurrent::run([file, cancel]() -> std::optional<WjList> {
    // a failure is handed over in the list's description: the future carries
    // one value, and an exception would end MO2 when the result is taken
    auto failed = [](const QString& why) {
      WjList list;
      list.description    = why;
      list.directiveCount = -1;
      return std::optional<WjList>(std::move(list));
    };
    try {
      QString error;
      auto list = readModList(file, &error, false, cancel.get());
      if (!list) {
        return failed(error);
      }
      return list;
    } catch (const std::exception& e) {
      return failed(QString::fromLocal8Bit(e.what()));
    } catch (...) {
      return failed(QStringLiteral("unknown error"));
    }
  }));
}

WabbajackDialog::~WabbajackDialog()
{
  // the reading only holds its own copies: it is told to stop, not waited for
  m_readCancel->store(true);
  cancelHashes();
}

void WabbajackDialog::reject()
{
  close();
}

QString WabbajackDialog::askFile(QWidget* parent)
{
  QString start = GlobalSettings::value(FileSetting).toString();
  if (start.isEmpty() || !QDir(start).exists()) {
    start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  }
  const QString file = QFileDialog::getOpenFileName(
      parent, tr("Open a Wabbajack list"), start,
      tr("Wabbajack lists (*.wabbajack);;All files (*)"));
  if (!file.isEmpty()) {
    GlobalSettings::setValue(FileSetting, QFileInfo(file).absolutePath());
  }
  return file;
}

// ---------------------------------------------------------------------------
// the window

void WabbajackDialog::setupUi()
{
  setObjectName(QStringLiteral("WabbajackDialog"));
  setWindowTitle(tr("Wabbajack list"));

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(16, 16, 16, 12);
  outer->setSpacing(8);
  outer->setSizeConstraint(QLayout::SetNoConstraint);

  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("collectionScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("collectionContent"));
  content->setAutoFillBackground(false);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addWidget(createHeader());
  layout->addWidget(createNeeds());
  layout->addWidget(createArchives(), 1);
  layout->addStretch(0);
  m_archivesCard->setMinimumHeight(300);
  scroll->setWidget(content);
  outer->addWidget(scroll, 1);

  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_status->setWordWrap(true);
  m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  footer->addWidget(m_status, 1);

  m_install = new QPushButton(tr("Install"), this);
  m_install->setObjectName(QStringLiteral("wabbajackInstall"));
  m_install->setProperty("fluentPrimary", true);
  m_install->setAutoDefault(false);
  m_install->setMinimumWidth(102);
  m_install->setEnabled(false);
  m_install->setToolTip(tr("Installing Wabbajack lists comes in a coming version. For now "
                           "this window shows what a list needs."));
  m_close = new QPushButton(tr("Close"), this);
  m_close->setObjectName(QStringLiteral("wabbajackClose"));
  m_close->setAutoDefault(false);
  m_close->setMinimumWidth(102);
  footer->addWidget(m_install);
  footer->addWidget(m_close);
  outer->addLayout(footer);

  connect(m_close, &QPushButton::clicked, this, &QDialog::close);
}

QFrame* WabbajackDialog::createHeader()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("wabbajackHeaderCard"));
  card->setProperty("fluentCard", true);
  // as tall as its text: alone in the window (a list that can't be read) it
  // doesn't fill it
  card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  auto* layout = new QHBoxLayout(card);
  layout->setContentsMargins(18, 14, 18, 14);
  layout->setSpacing(14);

  m_image = new QLabel(card);
  m_image->setObjectName(QStringLiteral("wabbajackImage"));
  m_image->setFixedSize(256, 144);
  m_image->setAlignment(Qt::AlignCenter);
  m_image->hide();
  layout->addWidget(m_image, 0, Qt::AlignTop);

  auto* names = new QVBoxLayout;
  names->setSpacing(4);
  m_name = new QLabel(QFileInfo(m_path).completeBaseName(), card);
  m_name->setTextFormat(Qt::PlainText);
  m_name->setObjectName(QStringLiteral("instanceDetailName"));
  m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_caption->setWordWrap(true);
  m_caption->setTextFormat(Qt::PlainText);
  m_description = new QLabel(card);
  m_description->setWordWrap(true);
  m_description->setTextFormat(Qt::PlainText);
  m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_description->hide();

  // the list's links: buttons straight in the card's layout (a widget around
  // them is painted as a panel of its own)
  m_links = new QHBoxLayout;
  m_links->setContentsMargins(0, 4, 0, 0);
  m_links->setSpacing(8);
  m_links->addStretch(1);

  names->addWidget(m_name);
  names->addWidget(m_caption);
  names->addWidget(m_description);
  names->addLayout(m_links);
  names->addStretch(1);
  layout->addLayout(names, 1);

  m_loading = new QLabel(tr("Reading the list…"), card);
  m_loading->setObjectName(QStringLiteral("fluentStatCaption"));
  m_loading->setWordWrap(true);
  layout->addWidget(m_loading, 0, Qt::AlignTop | Qt::AlignRight);
  return card;
}

QFrame* WabbajackDialog::createNeeds()
{
  m_needsCard = new QFrame(this);
  m_needsCard->setObjectName(QStringLiteral("wabbajackNeedsCard"));
  m_needsCard->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(m_needsCard);
  layout->setContentsMargins(14, 10, 14, 10);
  layout->setSpacing(6);

  auto* title = new QLabel(tr("What it needs"), m_needsCard);
  title->setObjectName(QStringLiteral("fluentCardTitle"));
  layout->addWidget(title);

  m_needs = new QLabel(m_needsCard);
  m_needs->setWordWrap(true);
  m_needs->setTextFormat(Qt::RichText);
  m_needs->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_needs);

  m_notes = new QLabel(m_needsCard);
  m_notes->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_notes->setWordWrap(true);
  m_notes->setTextFormat(Qt::RichText);
  m_notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_notes);

  m_needsCard->hide();
  return m_needsCard;
}

QFrame* WabbajackDialog::createArchives()
{
  m_archivesCard = new QFrame(this);
  m_archivesCard->setObjectName(QStringLiteral("wabbajackArchivesCard"));
  m_archivesCard->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(m_archivesCard);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Archives"), m_archivesCard);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_count = new QLabel(m_archivesCard);
  m_count->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_count);
  layout->addLayout(title);

  // the downloads folder: where the archives are looked for, and later where
  // they are downloaded to; one folder can serve several lists
  auto* folderRow = new QHBoxLayout;
  folderRow->setContentsMargins(6, 0, 6, 2);
  folderRow->setSpacing(8);
  auto* folderLabel = new QLabel(tr("Downloads folder"), m_archivesCard);
  m_folder          = new QLineEdit(m_archivesCard);
  m_folder->setObjectName(QStringLiteral("wabbajackFolder"));
  m_folder->setToolTip(tr("Where the list's archives are looked for. A folder used by "
                          "Wabbajack or by another list can be used again: archives are "
                          "recognised by their hash."));
  QString folder = GlobalSettings::value(FolderSetting).toString();
  if (folder.isEmpty() && m_core.downloadManager() != nullptr) {
    folder = m_core.downloadManager()->getOutputDirectory();
  }
  m_folder->setText(QDir::toNativeSeparators(folder));
  m_chooseFolder =
      new QPushButton(FluentIcons::glyph(FolderGlyph), tr("Choose…"), m_archivesCard);
  m_chooseFolder->setAutoDefault(false);
  m_check = new QPushButton(tr("Check by hash"), m_archivesCard);
  m_check->setObjectName(QStringLiteral("wabbajackCheck"));
  m_check->setAutoDefault(false);
  m_check->setToolTip(tr("Reads the archives found in the folder and the game files the "
                         "list takes from the game, and compares their hashes with the "
                         "list's. Nothing is changed. Large lists take a while."));
  folderRow->addWidget(folderLabel);
  folderRow->addWidget(m_folder, 1);
  folderRow->addWidget(m_chooseFolder);
  folderRow->addWidget(m_check);
  layout->addLayout(folderRow);

  m_tree = new QTreeWidget(m_archivesCard);
  m_tree->setObjectName(QStringLiteral("wabbajackArchiveList"));
  m_tree->setColumnCount(ColCount);
  m_tree->setHeaderLabels({tr("Name"), tr("Source"), tr("Size"), tr("Status")});
  m_tree->setRootIsDecorated(false);
  m_tree->setUniformRowHeights(true);
  m_tree->setFrameShape(QFrame::NoFrame);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setAccessibleName(tr("Archives"));
  m_tree->setSortingEnabled(false);
  m_tree->header()->setStretchLastSection(true);
  m_tree->header()->setSectionResizeMode(ColName, QHeaderView::Interactive);
  m_tree->setColumnWidth(ColName, 420);
  m_tree->setColumnWidth(ColSource, 200);
  m_tree->setColumnWidth(ColSize, 90);
  layout->addWidget(m_tree, 1);

  connect(m_chooseFolder, &QPushButton::clicked, this, [this] {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Downloads folder for Wabbajack lists"), m_folder->text());
    if (!dir.isEmpty()) {
      m_folder->setText(QDir::toNativeSeparators(dir));
      GlobalSettings::setValue(FolderSetting, QDir::fromNativeSeparators(dir));
      scanFolder();
    }
  });
  connect(m_folder, &QLineEdit::editingFinished, this, [this] {
    GlobalSettings::setValue(FolderSetting, QDir::fromNativeSeparators(m_folder->text()));
    scanFolder();
  });
  connect(m_check, &QPushButton::clicked, this, [this] {
    if (m_hasher != nullptr && m_hasher->isRunning()) {
      cancelHashes();
    } else {
      checkHashes();
    }
  });
  // a link opens the archive's page, for the ones that have one
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* item, int) {
            if (!m_list) {
              return;
            }
            const int i = item->data(ColName, RowRole).toInt();
            const WjArchive& a = m_list->archives[std::size_t(i)];
            QString link;
            if (a.source == Source::Nexus && a.modId > 0) {
              const auto game = gameInfo(a.nexusGame);
              const QString domain =
                  game ? game->nexusDomain : a.nexusGame.toLower();
              link = QStringLiteral("https://www.nexusmods.com/%1/mods/%2?tab=files")
                         .arg(domain)
                         .arg(a.modId);
            } else if (a.source != Source::GoogleDrive && isWebLink(a.url)) {
              link = a.url;
            }
            if (!link.isEmpty()) {
              shell::Open(QUrl(link));
            }
          });

  m_archivesCard->hide();
  return m_archivesCard;
}

void WabbajackDialog::placeWindow()
{
  QWidget* anchor = m_anchor;
  QScreen* screen = anchor != nullptr ? anchor->screen() : QGuiApplication::primaryScreen();
  if (screen == nullptr) {
    return;
  }
  const QRect available = screen->availableGeometry();
  const QSize least     = QSize(900, 680).boundedTo(available.size());
  setMinimumSize(least);

  if (Settings::instance().geometry().restoreGeometry(this) &&
      QGuiApplication::screenAt(geometry().center()) == screen &&
      available.contains(geometry().center()) && width() >= least.width() &&
      height() >= least.height()) {
    resize(size().boundedTo(available.size()));
    return;
  }

  QSize wanted(available.width() * 65 / 100, available.height() * 85 / 100);
  wanted = wanted.expandedTo(least).boundedTo(available.size() - QSize(16, 40));
  resize(wanted);
  const bool overWindow = anchor != nullptr && anchor->isVisible() &&
                          !anchor->isMinimized() &&
                          available.contains(anchor->frameGeometry().center());
  const QPoint middle = overWindow ? anchor->frameGeometry().center() : available.center();
  QPoint pos(middle.x() - wanted.width() / 2, middle.y() - wanted.height() / 2);
  pos.setX(qBound(available.left(), pos.x(),
                  qMax(available.left(), available.right() - wanted.width() - 16)));
  pos.setY(qBound(available.top(), pos.y(),
                  qMax(available.top(), available.bottom() - wanted.height() - 40)));
  move(pos);
}

void WabbajackDialog::closeEvent(QCloseEvent* e)
{
  Settings::instance().geometry().saveGeometry(this);
  m_readCancel->store(true);
  cancelHashes();
  QDialog::closeEvent(e);
}

// ---------------------------------------------------------------------------
// the list

void WabbajackDialog::onRead()
{
  std::optional<WjList> result = m_reader->result();
  if (!result || result->directiveCount < 0) {
    m_readError = result ? result->description : QString();
    m_loading->clear();
    m_caption->setText(tr("This list can't be read: %1").arg(m_readError));
    m_status->setText(QDir::toNativeSeparators(m_path));
    log::warn("wabbajack: {} can't be read: {}", m_path, m_readError);
    return;
  }
  m_list = std::make_shared<WjList>(std::move(*result));
  m_states.assign(m_list->archives.size(), FileState::Unknown);
  showList();
  fillTree();
  scanFolder();
}

QString WabbajackDialog::gameDirectory() const
{
  if (!m_list) {
    return {};
  }
  IPluginGame* game = findGamePlugin(
      m_list->game, m_core.pluginContainer().plugins<IPluginGame>());
  if (game == nullptr || !game->isInstalled()) {
    return {};
  }
  return game->gameDirectory().absolutePath();
}

void WabbajackDialog::showList()
{
  const WjList& l = *m_list;
  const auto info  = gameInfo(l.game);
  const QString gameName = info ? info->mo2Name : l.game;

  setWindowTitle(tr("Wabbajack list: %1").arg(l.name.isEmpty() ? m_name->text() : l.name));
  if (!l.name.isEmpty()) {
    m_name->setText(l.name);
  }

  QStringList caption;
  if (!l.author.isEmpty()) {
    caption << tr("by %1").arg(l.author);
  }
  if (!l.version.isEmpty()) {
    caption << tr("version %1").arg(l.version);
  }
  caption << gameName;
  if (l.nsfw) {
    caption << tr("adult content (NSFW)");
  }
  if (!l.wabbajackVersion.isEmpty()) {
    caption << tr("made with Wabbajack %1").arg(l.wabbajackVersion);
  }
  m_caption->setText(caption.join(QStringLiteral(" · ")));

  // some authors type the line breaks as the characters \n
  QString description = l.description;
  description.replace(QStringLiteral("\\r\\n"), QStringLiteral("\n"))
      .replace(QStringLiteral("\\n"), QStringLiteral("\n"));
  if (!description.trimmed().isEmpty()) {
    m_description->setText(description.trimmed());
    m_description->show();
  }

  QPixmap image;
  if (!l.image.isEmpty() && image.loadFromData(l.image)) {
    const qreal dpr = devicePixelRatioF();
    QPixmap scaled  = image.scaled(QSize(256, 144) * dpr, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    m_image->setPixmap(scaled);
    m_image->show();
  }

  // the list's links: web addresses only
  auto addLink = [&](const QString& text, const QString& url) {
    if (!isWebLink(url)) {
      return;
    }
    auto* b = new QPushButton(FluentIcons::glyph(LinkGlyph), text, this);
    b->setAutoDefault(false);
    b->setToolTip(plainTip(url));
    connect(b, &QPushButton::clicked, this, [url] {
      shell::Open(QUrl(url));
    });
    m_links->insertWidget(m_links->count() - 1, b);
  };
  addLink(tr("Website"), l.website);
  if (l.readme != l.website) {
    addLink(tr("Readme"), l.readme);
  }
  addLink(tr("Community"), l.community);

  // what it needs
  int nexus = 0, cdn = 0, http = 0, game = 0, manual = 0, other = 0;
  qint64 nexusSize = 0, cdnSize = 0, httpSize = 0, gameSize = 0, manualSize = 0;
  QString gameVersion;
  for (const WjArchive& a : l.archives) {
    if (a.isModOrganizer) {
      continue;
    }
    switch (a.source) {
    case Source::Nexus:
      ++nexus;
      nexusSize += a.size;
      break;
    case Source::WabbajackCdn:
      ++cdn;
      cdnSize += a.size;
      break;
    case Source::Http:
      ++http;
      httpSize += a.size;
      break;
    case Source::GameFile:
      ++game;
      gameSize += a.size;
      if (gameVersion.isEmpty()) {
        gameVersion = a.gameVersion;
      }
      break;
    case Source::Unknown:
      ++other;
      break;
    default:
      ++manual;
      manualSize += a.size;
      break;
    }
  }

  QStringList lines;
  const qint64 download = [&] {
    qint64 total = 0;
    for (const WjArchive& a : l.archives) {
      if (a.source != Source::GameFile && !a.isModOrganizer) {
        total += a.size;
      }
    }
    return total;
  }();
  lines << tr("<b>%1</b> to download, about <b>%2</b> installed")
               .arg(localizedByteSize(download))
               .arg(localizedByteSize(l.installSize));
  // what: "%n files ... (%1)", %1 the size
  auto line = [&](int count, qint64 size, const QString& what) {
    if (count > 0) {
      lines << QStringLiteral("• %1").arg(what.arg(localizedByteSize(size)));
    }
  };
  line(nexus, nexusSize, tr("%n files from Nexus Mods (%1)", nullptr, nexus));
  line(cdn, cdnSize, tr("%n files from Wabbajack's servers (%1)", nullptr, cdn));
  line(http, httpSize, tr("%n direct downloads (%1)", nullptr, http));
  line(manual, manualSize, tr("%n files to download in the browser (%1)", nullptr, manual));
  line(game, gameSize, tr("%n files taken from the game's own folder (%1)", nullptr, game));
  if (other > 0) {
    lines << QStringLiteral("• %1").arg(tr("%n files from sources MO2 Revamped doesn't know",
                                           nullptr, other));
  }

  const TypeCount& copies  = l.byType[int(DirectiveType::FromArchive)];
  const TypeCount& patches = l.byType[int(DirectiveType::PatchedFromArchive)];
  const TypeCount& inlined = l.byType[int(DirectiveType::InlineFile)];
  const TypeCount& bsas    = l.byType[int(DirectiveType::CreateBSA)];
  const TypeCount& tex     = l.byType[int(DirectiveType::TransformedTexture)];
  const TypeCount& merged  = l.byType[int(DirectiveType::MergedPatch)];
  QStringList work;
  work << tr("%n files copied from archives", nullptr, copies.count);
  if (patches.count > 0) {
    work << tr("%n patched", nullptr, patches.count);
  }
  if (inlined.count > 0) {
    work << tr("%n carried by the list", nullptr, inlined.count);
  }
  if (bsas.count > 0) {
    work << tr("%n BSA archives built", nullptr, bsas.count);
  }
  if (tex.count > 0) {
    work << tr("%n textures resized", nullptr, tex.count);
  }
  if (merged.count > 0) {
    work << tr("%n zEdit merges", nullptr, merged.count);
  }
  lines << tr("To install: %1.").arg(work.join(QStringLiteral(", ")));

  if (!l.profiles.isEmpty()) {
    lines << tr("Profiles: %1").arg(l.profiles.join(QStringLiteral(", ")).toHtmlEscaped());
  }
  m_needs->setText(lines.join(QStringLiteral("<br>")));

  // notes: the game, what is left out, what isn't known
  QStringList notes;
  IPluginGame* plugin =
      findGamePlugin(l.game, m_core.pluginContainer().plugins<IPluginGame>());
  if (plugin == nullptr) {
    notes << tr("MO2 Revamped has no game plugin for %1.").arg(gameName.toHtmlEscaped());
  } else if (!plugin->isInstalled()) {
    notes << tr("%1 isn't installed on this PC, or MO2 Revamped didn't find it.")
                 .arg(gameName.toHtmlEscaped());
  } else {
    QString note = tr("%1 found in %2.")
                       .arg(gameName.toHtmlEscaped())
                       .arg(QDir::toNativeSeparators(gameDirectory()).toHtmlEscaped());
    const QString have = plugin->gameVersion();
    if (!gameVersion.isEmpty() && !have.isEmpty()) {
      note += QStringLiteral(" ") + tr("The list was made with version %1; you have %2.")
                                        .arg(gameVersion.toHtmlEscaped())
                                        .arg(have.toHtmlEscaped());
    }
    notes << note;
  }
  if (l.modOrganizerFiles.count > 0) {
    notes << tr("The list brings its own Mod Organizer (%n files): MO2 Revamped takes its "
                "place, so those files are not installed.",
                nullptr, l.modOrganizerFiles.count);
  }
  if (!l.extraPlugins.isEmpty()) {
    notes << tr("It adds Mod Organizer plugins: %1. They will be offered at the end, not "
                "installed by themselves.")
                 .arg(l.extraPlugins.join(QStringLiteral(", ")).toHtmlEscaped());
  }
  if (!l.unknownTypes.isEmpty()) {
    notes << tr("Parts MO2 Revamped doesn't know yet: %1.")
                 .arg(l.unknownTypes.join(QStringLiteral(", ")).toHtmlEscaped());
  }
  m_notes->setText(notes.join(QStringLiteral("<br>")));
  m_notes->setVisible(!notes.isEmpty());

  m_loading->setText(tr("%n archives", nullptr, int(l.archives.size())));
  m_needsCard->show();
  m_archivesCard->show();
}

QString WabbajackDialog::sourceText(const WjArchive& a) const
{
  if (a.isModOrganizer) {
    return tr("Mod Organizer (not used)");
  }
  switch (a.source) {
  case Source::Nexus:
    return tr("Nexus Mods");
  case Source::GameFile:
    return tr("Game folder");
  case Source::Http:
    return tr("Direct download");
  case Source::WabbajackCdn:
    return tr("Wabbajack servers");
  case Source::GoogleDrive:
    return tr("Google Drive");
  case Source::Mega:
    return tr("MEGA");
  case Source::MediaFire:
    return tr("MediaFire");
  case Source::ModDB:
    return tr("ModDB");
  case Source::LoversLab:
    return tr("Forum site");
  case Source::Manual:
    return tr("By hand");
  case Source::Bethesda:
    return tr("Bethesda.net");
  case Source::Unknown:
    break;
  }
  return a.sourceType.isEmpty() ? tr("Unknown") : a.sourceType;
}

QString WabbajackDialog::stateText(FileState s) const
{
  switch (s) {
  case FileState::Unknown:
    return {};
  case FileState::Missing:
    return tr("Not downloaded");
  case FileState::Present:
    return tr("In the folder");
  case FileState::Verified:
    return tr("In the folder, verified");
  case FileState::WrongHash:
    return tr("In the folder, but a different file");
  case FileState::GameFound:
    return tr("In the game, verified");
  case FileState::GameDiffers:
    return tr("In the game, but another version");
  case FileState::GameMissing:
    return tr("Not in the game folder");
  }
  return {};
}

void WabbajackDialog::fillTree()
{
  const WjList& l = *m_list;
  std::vector<int> order(l.archives.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = int(i);
  }
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    const WjArchive& x = l.archives[std::size_t(a)];
    const WjArchive& y = l.archives[std::size_t(b)];
    const int ox     = x.isModOrganizer ? 5 : sourceOrder(x.source);
    const int oy     = y.isModOrganizer ? 5 : sourceOrder(y.source);
    if (ox != oy) {
      return ox < oy;
    }
    return QString::compare(x.name, y.name, Qt::CaseInsensitive) < 0;
  });

  m_tree->clear();
  m_items.assign(l.archives.size(), nullptr);
  QList<QTreeWidgetItem*> items;
  items.reserve(int(order.size()));
  for (int i : order) {
    const WjArchive& a = l.archives[std::size_t(i)];
    auto* item         = new QTreeWidgetItem;
    m_items[std::size_t(i)] = item;
    item->setData(ColName, RowRole, i);
    item->setText(ColName, a.name);
    QString tip = a.name;
    if (!a.modName.isEmpty() && a.modName != a.name) {
      tip += QStringLiteral("\n") + a.modName;
      if (!a.version.isEmpty()) {
        tip += QStringLiteral(" ") + a.version;
      }
    }
    if (!a.author.isEmpty()) {
      tip += QStringLiteral("\n") + tr("by %1").arg(a.author);
    }
    if (a.source == Source::GameFile) {
      tip += QStringLiteral("\n") + QDir::toNativeSeparators(a.gameFile);
    }
    if (!a.prompt.isEmpty()) {
      tip += QStringLiteral("\n\n") + a.prompt;
    }
    item->setToolTip(ColName, plainTip(tip));
    item->setText(ColSource, sourceText(a));
    item->setText(ColSize, a.size > 0 ? localizedByteSize(a.size) : QString());
    item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    items << item;
  }
  m_tree->addTopLevelItems(items);
}

void WabbajackDialog::updateRow(int row)
{
  if (row < 0 || std::size_t(row) >= m_items.size() || m_items[std::size_t(row)] == nullptr) {
    return;
  }
  // Mod Organizer's own archive is never needed
  m_items[std::size_t(row)]->setText(ColStatus,
                                     m_list->archives[std::size_t(row)].isModOrganizer
                                         ? tr("Not needed")
                                         : stateText(m_states[std::size_t(row)]));
}

QString WabbajackDialog::gameFilePath(const QString& gameDir, const WjArchive& a) const
{
  const QString relative = QDir::fromNativeSeparators(a.gameFile);
  if (gameDir.isEmpty() || relative.isEmpty() || QDir::isAbsolutePath(relative) ||
      relative.contains(u':') || relative.split(u'/').contains(QStringLiteral(".."))) {
    return {};
  }
  return QDir(gameDir).filePath(relative);
}

void WabbajackDialog::updateCount()
{
  if (!m_list) {
    return;
  }
  int needed = 0, have = 0;
  for (std::size_t i = 0; i < m_list->archives.size(); ++i) {
    const WjArchive& a = m_list->archives[i];
    if (a.isModOrganizer) {
      continue;
    }
    ++needed;
    const FileState s = m_states[i];
    if (s == FileState::Present || s == FileState::Verified || s == FileState::GameFound) {
      ++have;
    }
  }
  m_count->setText(tr("%1 of %2 found").arg(have).arg(needed));
}

void WabbajackDialog::scanFolder()
{
  if (!m_list) {
    return;
  }
  // what was found by hash in another folder says nothing of this one
  const QString folder =
      QDir::cleanPath(QDir::fromNativeSeparators(m_folder->text().trimmed()));
  if (folder.compare(m_scannedFolder, Qt::CaseInsensitive) != 0) {
    m_scannedFolder = folder;
    for (std::size_t i = 0; i < m_states.size(); ++i) {
      if (m_list->archives[i].source != Source::GameFile) {
        m_states[i] = FileState::Unknown;
      }
    }
  }

  // the names in the folder, once
  QHash<QString, qint64> present;
  const QDir dir(QDir::fromNativeSeparators(m_folder->text().trimmed()));
  if (!m_folder->text().trimmed().isEmpty() && dir.exists()) {
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Hidden)) {
      present.insert(fi.fileName().toLower(), fi.size());
    }
  }
  const QString gameDir = gameDirectory();

  for (std::size_t i = 0; i < m_list->archives.size(); ++i) {
    const WjArchive& a = m_list->archives[i];
    FileState& s     = m_states[i];
    if (a.source == Source::GameFile) {
      if (s == FileState::GameFound || s == FileState::GameDiffers) {
        continue;  // known by hash already
      }
      const QString path = gameFilePath(gameDir, a);
      const QFileInfo fi(path);
      if (path.isEmpty() || !fi.isFile()) {
        s = FileState::GameMissing;
      } else if (a.size > 0 && fi.size() != a.size) {
        s = FileState::GameDiffers;
      } else {
        s = FileState::Unknown;
      }
    } else {
      const auto it = present.constFind(a.name.toLower());
      if (it == present.constEnd() || (a.size > 0 && it.value() != a.size)) {
        s = FileState::Missing;
      } else if (s != FileState::Verified && s != FileState::WrongHash) {
        s = FileState::Present;
      }
    }
  }
  for (int i = 0; i < int(m_states.size()); ++i) {
    updateRow(i);
  }
  updateCount();
}

void WabbajackDialog::checkHashes()
{
  if (!m_list || (m_hasher != nullptr && m_hasher->isRunning())) {
    return;
  }
  scanFolder();

  struct Job
  {
    int row;
    QString path;
    uint64_t expected;
    qint64 size;
    bool game;
  };
  std::vector<Job> jobs;
  const QDir dir(QDir::fromNativeSeparators(m_folder->text().trimmed()));
  const QString gameDir = gameDirectory();
  qint64 total          = 0;
  for (std::size_t i = 0; i < m_list->archives.size(); ++i) {
    const WjArchive& a = m_list->archives[i];
    if (a.isModOrganizer) {
      continue;
    }
    const FileState s = m_states[i];
    if (a.source == Source::GameFile) {
      const QString path = gameFilePath(gameDir, a);
      if (s == FileState::Unknown && !path.isEmpty()) {
        jobs.push_back({int(i), path, a.hash, a.size, true});
        total += a.size;
      }
    } else if (s == FileState::Present) {
      jobs.push_back({int(i), dir.filePath(a.name), a.hash, a.size, false});
      total += a.size;
    }
  }
  if (jobs.empty()) {
    m_status->setText(tr("Nothing to check: no archive of the list is in the folder."));
    return;
  }

  m_cancel->store(false);
  m_check->setText(tr("Stop"));
  // the folder stays as it is while its files are read
  m_folder->setEnabled(false);
  m_chooseFolder->setEnabled(false);
  m_status->setText(tr("Checking %n files…", nullptr, int(jobs.size())));

  auto cancel = m_cancel;
  QPointer<WabbajackDialog> self(this);
  auto* watcher = new QFutureWatcher<void>(this);
  m_hasher      = watcher;
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
    watcher->deleteLater();
    if (m_hasher != watcher) {
      return;  // an earlier check, stopped: a newer one runs
    }
    m_hasher = nullptr;
    m_check->setText(tr("Check by hash"));
    m_folder->setEnabled(true);
    m_chooseFolder->setEnabled(true);
    int wrong = 0, good = 0;
    for (FileState s : m_states) {
      if (s == FileState::WrongHash || s == FileState::GameDiffers) {
        ++wrong;
      } else if (s == FileState::Verified || s == FileState::GameFound) {
        ++good;
      }
    }
    if (m_cancel->load()) {
      m_status->setText(tr("Check stopped."));
    } else if (wrong > 0) {
      m_status->setText(tr("%n files verified.", nullptr, good) + QStringLiteral(" ") +
                        tr("%n don't match the list.", nullptr, wrong));
    } else {
      m_status->setText(tr("%n files verified.", nullptr, good));
    }
    updateCount();
  });

  // the window waits for this before it goes (cancelHashes()), so the raw
  // pointer stays valid for the whole run; the QPointer is read on the UI thread
  WabbajackDialog* dialog = this;
  watcher->setFuture(QtConcurrent::run([jobs = std::move(jobs), cancel, self, dialog, total] {
    qint64 done = 0;
    for (const Job& j : jobs) {
      if (cancel->load()) {
        return;
      }
      const qint64 before = done;
      std::optional<uint64_t> hash;
      try {
        hash = hashFile(j.path, cancel.get(), [&](qint64 n) {
          done = before + n;
        });
      } catch (...) {
        hash.reset();
      }
      done = before + j.size;
      if (cancel->load()) {
        return;
      }
      FileState state;
      if (!hash) {
        state = j.game ? FileState::GameMissing : FileState::Missing;
      } else if (*hash == j.expected) {
        state = j.game ? FileState::GameFound : FileState::Verified;
      } else {
        state = j.game ? FileState::GameDiffers : FileState::WrongHash;
      }
      const int percent = total > 0 ? int(done * 100 / total) : 100;
      QMetaObject::invokeMethod(
          dialog,
          [self, row = j.row, state, percent] {
            if (!self) {
              return;
            }
            self->m_states[std::size_t(row)] = state;
            self->updateRow(row);
            self->m_status->setText(tr("Checking… %1%").arg(percent));
          },
          Qt::QueuedConnection);
    }
  }));
}

void WabbajackDialog::cancelHashes()
{
  m_cancel->store(true);
  if (m_hasher != nullptr) {
    m_hasher->waitForFinished();
  }
}
