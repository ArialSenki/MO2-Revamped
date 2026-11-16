#include "wabbajackdialog.h"

#include "screenfitter.h"

#include "createinstancedialog.h"
#include "downloadmanager.h"
#include "instancemanager.h"
#include "shared/util.h"
#include "fluenticons.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "settings.h"

#include <filesystemutilities.h>
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <Windows.h>
#include <QtConcurrent/QtConcurrentRun>

using namespace MOBase;
using Wabbajack::DirectiveType;
using Wabbajack::Source;
using Wabbajack::TypeCount;
using Wabbajack::findGamePlugin;
using Wabbajack::gameInfo;
using Wabbajack::DownloadQueue;
using Wabbajack::isListFile;
using Wabbajack::installStateFolder;
using Wabbajack::readModList;
// Archive and ModList are names MO2 has already
using WjArchive = Wabbajack::Archive;
using WjList    = Wabbajack::ModList;

namespace
{

constexpr char FolderSetting[] = "Wabbajack/downloadsFolder";
constexpr char FileSetting[]   = "Wabbajack/lastListFolder";
constexpr char ResumeSetting[] = "Wabbajack/resumeList";
constexpr char WatchSetting[]  = "Wabbajack/watchFolder";
constexpr char16_t NextGlyph   = 0xE8A7;  // OpenInNewWindow
constexpr char16_t DownloadGlyph = 0xE896;  // Download

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
  // (the queue, a child, stops its downloads and its hashing as it goes)
  m_readCancel->store(true);
  // MO2 ending another way than through its question (Windows shutting
  // down): the install stops, not left writing while MO2 goes
  if (isInstalling()) {
    m_installCancel->store(true);
    m_installer->waitForFinished();
  }
  setQuiet(false);
}

void WabbajackDialog::setQuiet(bool quiet)
{
  // MO2's own notices (a failed Nexus request, say), each with the system's
  // sound, stay in the log while a list downloads or installs: the window
  // shows how it goes, and the user may be playing meanwhile
  qApp->setProperty("mo2r.wabbajackQuiet", quiet ? QVariant(true) : QVariant());
}

void WabbajackDialog::onRunFinished()
{
  // once, at the end of the downloads: a flashing taskbar button when the
  // window is behind, and one sound
  if (!isActiveWindow()) {
    QApplication::alert(this);
  }
  QApplication::beep();
}

bool WabbajackDialog::isRunning() const
{
  return isInstalling() ||
         (m_queue != nullptr && (m_queue->isRunning() || m_queue->isChecking()));
}

bool WabbajackDialog::isInstalling() const
{
  return m_installer != nullptr && m_installer->isRunning();
}

void WabbajackDialog::stopInstallAndWait()
{
  if (!isInstalling()) {
    return;
  }
  m_installCancel->store(true);
  QApplication::setOverrideCursor(Qt::WaitCursor);
  m_installer->waitForFinished();
  QApplication::restoreOverrideCursor();
}

bool WabbajackDialog::claimNxm(const QString& url)
{
  if (m_queue == nullptr || !m_queue->claimNxm(url)) {
    return false;
  }
  // the next page right away, as the collections do: one click per file
  QTimer::singleShot(1500, this, [this] {
    if (m_queue != nullptr && m_queue->isRunning()) {
      m_queue->openNext();
    }
  });
  return true;
}

// the list to open again is the instance's own: one left running in an instance
// of another game doesn't open in this one. Kept in the instance's ini
QString WabbajackDialog::resumeList()
{
  // older builds kept it for every instance at once
  if (GlobalSettings::value(ResumeSetting).isValid()) {
    GlobalSettings::remove(ResumeSetting);
  }
  const QSettings ini(Settings::instance().filename(), QSettings::IniFormat);
  const QString path = ini.value(QStringLiteral("Settings/wabbajack_resume_list")).toString();
  return isListFile(path) ? path : QString();
}

void WabbajackDialog::rememberResume(bool running)
{
  QSettings ini(m_core.settings().filename(), QSettings::IniFormat);
  const QString key = QStringLiteral("Settings/wabbajack_resume_list");
  if (running) {
    ini.setValue(key, m_path);
  } else if (ini.value(key).toString() == m_path) {
    ini.remove(key);
  }
}

void WabbajackDialog::reject()
{
  // Escape closes the window, but never stops downloads by accident: those
  // stop with Pause or the close button
  if (!isRunning()) {
    close();
  }
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

  m_retry = new QPushButton(tr("Retry failed"), this);
  m_retry->setObjectName(QStringLiteral("wabbajackRetry"));
  m_retry->setAutoDefault(false);
  m_retry->setToolTip(tr("Tries every file that failed again."));
  m_retry->hide();
  m_next = new QPushButton(FluentIcons::glyph(NextGlyph), tr("Open next on Nexus"), this);
  m_next->setObjectName(QStringLiteral("wabbajackNext"));
  m_next->setAutoDefault(false);
  m_next->setToolTip(tr("Opens the next file's page on Nexus. Press \"Slow download\" "
                        "there and the file comes to MO2 Revamped; the next page opens "
                        "by itself."));
  m_next->hide();
  m_download = new QPushButton(FluentIcons::glyph(DownloadGlyph), tr("Download"), this);
  m_download->setObjectName(QStringLiteral("wabbajackDownload"));
  m_download->setProperty("fluentPrimary", true);
  m_download->setAutoDefault(false);
  m_download->setMinimumWidth(110);
  m_download->setEnabled(false);
  m_download->setToolTip(tr("Downloads the archives from Nexus Mods, Wabbajack's servers "
                            "and direct links into the downloads folder, checking each "
                            "one's hash. Without Nexus Premium, each Nexus file is one "
                            "click on its page."));

  m_install = new QPushButton(tr("Install"), this);
  m_install->setObjectName(QStringLiteral("wabbajackInstall"));
  m_install->setProperty("fluentPrimary", true);
  m_install->setAutoDefault(false);
  m_install->setMinimumWidth(102);
  m_install->setEnabled(false);
  connect(m_install, &QPushButton::clicked, this, [this] {
    if (isInstalling()) {
      m_installCancel->store(true);
      m_status->setText(tr("Stopping the install…"));
      m_install->setEnabled(false);
    } else {
      startInstall();
    }
  });
  m_close = new QPushButton(tr("Close"), this);
  m_close->setObjectName(QStringLiteral("wabbajackClose"));
  m_close->setAutoDefault(false);
  m_close->setMinimumWidth(102);
  footer->addWidget(m_retry);
  footer->addWidget(m_next);
  footer->addWidget(m_download);
  footer->addWidget(m_install);
  footer->addWidget(m_close);
  outer->addLayout(footer);

  connect(m_close, &QPushButton::clicked, this, &QDialog::close);
  connect(m_download, &QPushButton::clicked, this, [this] {
    if (m_queue == nullptr) {
      return;
    }
    if (m_queue->isRunning()) {
      m_queue->pause();
    } else {
      m_queue->start();
      if (m_queue->isRunning() && !m_queue->downloadsNexusByThemselves()) {
        m_queue->openNext();
      }
    }
    updateState();
  });
  connect(m_next, &QPushButton::clicked, this, [this] {
    if (m_queue != nullptr && !m_queue->openNext()) {
      m_status->setText(tr("No Nexus file waits for a click."));
    }
  });
  connect(m_retry, &QPushButton::clicked, this, [this] {
    if (m_queue != nullptr) {
      m_queue->retryFailed();
    }
  });
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

  m_space = new QLabel(m_needsCard);
  m_space->setWordWrap(true);
  m_space->setTextFormat(Qt::PlainText);
  layout->addWidget(m_space);

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

  // the files only a browser gets: how they come in
  m_byHand = new QLabel(m_archivesCard);
  m_byHand->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_byHand->setWordWrap(true);
  m_byHand->setContentsMargins(6, 0, 6, 2);
  m_byHand->hide();
  layout->addWidget(m_byHand);

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
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
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
      emit m_folder->editingFinished();
    }
  });
  connect(m_folder, &QLineEdit::editingFinished, this, [this] {
    if (m_queue == nullptr) {
      return;
    }
    const QString folder = QDir::fromNativeSeparators(m_folder->text().trimmed());
    if (m_queue->setFolder(folder)) {
      GlobalSettings::setValue(FolderSetting, folder);
    } else {
      m_folder->setText(QDir::toNativeSeparators(m_queue->folder()));
    }
    updateState();
  });
  connect(m_check, &QPushButton::clicked, this, [this] {
    if (m_queue == nullptr) {
      return;
    }
    if (m_queue->isChecking()) {
      m_queue->stopCheck();
    } else {
      m_queue->checkHashes();
    }
    updateState();
  });
  // a double click opens the archive's page, for the ones that have one
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* item, int) {
            if (m_queue != nullptr) {
              m_queue->openPage(item->data(ColName, RowRole).toInt());
            }
          });
  connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (item == nullptr || m_queue == nullptr) {
      return;
    }
    const int row = item->data(ColName, RowRole).toInt();
    const auto& a = m_list->archives[std::size_t(row)];
    if (a.source == Source::GameFile || a.isModOrganizer) {
      return;
    }
    QMenu menu(this);
    menu.addAction(tr("Open its page"), this, [this, row] {
      m_queue->openPage(row);
    });
    QAction* choose = menu.addAction(tr("Choose the downloaded file…"), this, [this, row] {
      const auto& a      = m_list->archives[std::size_t(row)];
      const QString file = QFileDialog::getOpenFileName(
          this, tr("The downloaded file of %1").arg(a.name),
          m_queue->watchFolder().isEmpty() ? m_queue->folder() : m_queue->watchFolder());
      if (!file.isEmpty()) {
        m_status->setText(tr("Checking %1…").arg(QFileInfo(file).fileName()));
        m_queue->adoptFile(row, file);
      }
    });
    choose->setEnabled(m_queue->canAdopt(row) && !m_queue->isRunning() &&
                       !m_queue->isChecking() && !isInstalling());
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
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
  const QSize least(900, 680);
  // its size by the screen it is on, also when it is moved to another one
  auto* fitter = new ScreenFitter(this, least);
  setMinimumSize(least.boundedTo(available.size()));

  if (Settings::instance().geometry().restoreGeometry(this) &&
      QGuiApplication::screenAt(geometry().center()) == screen &&
      available.contains(geometry().center()) &&
      width() >= least.boundedTo(available.size()).width() &&
      height() >= least.boundedTo(available.size()).height()) {
    // where the user left it, the user's size. The one older versions saved
    // (most of a 2K screen) comes down to this screen's once
    const char* shrunk = "Wabbajack/windowSizeChecked";
    if (!GlobalSettings::value(QString::fromLatin1(shrunk), false).toBool()) {
      resize(size().boundedTo(ScreenFitter::wantedSize(available, least)));
      GlobalSettings::setValue(QString::fromLatin1(shrunk), true);
    } else {
      resize(size().boundedTo(available.size() - QSize(16, 40)));
    }
    fitter->setUserSized(true);
    return;
  }

  const bool overWindow = anchor != nullptr && anchor->isVisible() &&
                          !anchor->isMinimized() &&
                          available.contains(anchor->frameGeometry().center());
  fitter->place(screen, overWindow ? anchor->frameGeometry().center() : available.center());
}

void WabbajackDialog::closeEvent(QCloseEvent* e)
{
  Settings::instance().geometry().saveGeometry(this);
  m_readCancel->store(true);
  if (isInstalling()) {
    // the install stops (inside the archive it is in, too); the window closes
    // once it has, without holding the rest of MO2 meanwhile
    m_installCancel->store(true);
    m_closeAfterInstall = true;
    m_status->setText(tr("Stopping the install…"));
    m_install->setEnabled(false);
    e->ignore();
    return;
  }
  // nothing is lost: the downloads go on from where they were, and MO2 opens
  // this list again at its next start
  const bool wasRunning = m_queue != nullptr && m_queue->isRunning();
  if (m_queue != nullptr) {
    m_queue->pause();
  }
  rememberResume(wasRunning);
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
  showList();

  m_queue = new DownloadQueue(m_list, gameDirectory(), this);
  connect(m_queue, &DownloadQueue::entryChanged, this, [this](int i) {
    updateRow(i);
  });
  // the rows and the buttons follow at most four times a second: a list of
  // thousands of archives changes often while it downloads
  auto* refresh = new QTimer(this);
  refresh->setSingleShot(true);
  refresh->setInterval(250);
  connect(refresh, &QTimer::timeout, this, [this] {
    if (m_queue == nullptr) {
      return;
    }
    for (int i = 0; i < m_queue->count(); ++i) {
      updateRow(i);
    }
    updateState();
  });
  connect(m_queue, &DownloadQueue::changed, this, [refresh] {
    if (!refresh->isActive()) {
      refresh->start();
    }
  });
  connect(m_queue, &DownloadQueue::message, m_status, &QLabel::setText);
  connect(m_queue, &DownloadQueue::runFinished, this, &WabbajackDialog::onRunFinished);

  fillTree();
  m_queue->setFolder(QDir::fromNativeSeparators(m_folder->text().trimmed()));
  // the browser's downloads: where the files only a browser gets arrive
  // (a browser saving elsewhere: the "Wabbajack/watchFolder" setting)
  m_queue->setWatchFolder(
      GlobalSettings::value(WatchSetting,
                            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
          .toString());
  m_download->setEnabled(true);
  updateState();
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
  if (m_queue == nullptr || row < 0 || std::size_t(row) >= m_items.size() ||
      m_items[std::size_t(row)] == nullptr) {
    return;
  }
  QTreeWidgetItem* item  = m_items[std::size_t(row)];
  const auto& e          = m_queue->entry(row);
  item->setText(ColStatus, m_queue->statusText(row));
  item->setToolTip(ColStatus, plainTip(e.reason));
}

void WabbajackDialog::updateState()
{
  if (m_queue == nullptr) {
    return;
  }
  const int total = m_queue->downloadable();
  const int done  = m_queue->done();
  QString count   = tr("%1 of %2 downloaded").arg(done).arg(total);
  const qint64 left = m_queue->remaining();
  if (left > 0) {
    count += QStringLiteral(" · ") + tr("%1 left").arg(localizedByteSize(left));
  }
  m_count->setText(count);

  const qint64 free = m_queue->freeSpace();
  if (free >= 0) {
    QString space = tr("Free space in the downloads folder's drive: %1.")
                        .arg(localizedByteSize(free));
    if (left > free) {
      space += QStringLiteral(" ") + tr("Not enough for the %1 still to download.")
                                         .arg(localizedByteSize(left));
    }
    m_space->setText(space);
    m_space->show();
  } else {
    m_space->hide();
  }

  const int byHand = m_queue->byHandCount();
  if (byHand > 0) {
    m_byHand->setText(
        m_queue->isWatching()
            ? tr("%n files have to be downloaded in the browser: double-click one to open its "
                 "page. MO2 Revamped watches %1 and takes them by their hash as they arrive; "
                 "a file saved elsewhere goes in with a right click.",
                 nullptr, byHand)
                  .arg(QDir::toNativeSeparators(m_queue->watchFolder()))
            : tr("%n files have to be downloaded in the browser: double-click one to open its "
                 "page, then choose the downloaded file with a right click.",
                 nullptr, byHand));
    m_byHand->show();
  } else {
    m_byHand->hide();
  }

  const bool running  = m_queue->isRunning();
  const bool checking = m_queue->isChecking();
  m_download->setText(running ? tr("Pause") : tr("Download"));
  m_download->setEnabled(!checking || running);
  m_check->setText(checking ? tr("Stop") : tr("Check by hash"));
  m_check->setEnabled(!running);
  m_folder->setEnabled(!running && !checking);
  m_chooseFolder->setEnabled(!running && !checking);
  m_next->setVisible(running && !m_queue->downloadsNexusByThemselves() &&
                     m_queue->countOf(DownloadQueue::Status::NeedsClick) > 0);
  m_retry->setVisible(!running && m_queue->countOf(DownloadQueue::Status::Failed) > 0);

  const bool installing = isInstalling();
  setQuiet(isRunning());
  const QString problem = installProblem();
  m_install->setText(installing ? tr("Stop installing") : tr("Install"));
  m_install->setEnabled(installing ? !m_installCancel->load()
                                   : problem.isEmpty() && !running && !checking);
  m_install->setToolTip(installing ? QString()
                        : problem.isEmpty()
                            ? tr("Installs the list into a new instance of its own.")
                            : problem);
  if (installing) {
    m_download->setEnabled(false);
    m_check->setEnabled(false);
    m_folder->setEnabled(false);
    m_chooseFolder->setEnabled(false);
    m_retry->setVisible(false);
  }
}

// ---------------------------------------------------------------------------
// installing

QString WabbajackDialog::installProblem() const
{
  if (!m_list || m_queue == nullptr) {
    return tr("The list isn't read yet.");
  }
  if (findGamePlugin(m_list->game, m_core.pluginContainer().plugins<IPluginGame>()) ==
      nullptr) {
    return tr("MO2 Revamped has no game plugin for this list's game.");
  }
  if (gameDirectory().isEmpty()) {
    return tr("The list's game isn't installed, or MO2 Revamped didn't find it.");
  }
  int missing = 0;
  for (std::size_t i = 0; i < m_list->archives.size(); ++i) {
    const auto& a = m_list->archives[i];
    if (a.isModOrganizer || a.uses == 0) {
      continue;
    }
    const auto s = m_queue->entry(int(i)).status;
    const bool here = s == DownloadQueue::Status::Verified ||
                      s == DownloadQueue::Status::GameFound ||
                      (a.source == Source::GameFile && s == DownloadQueue::Status::Unknown);
    if (!here) {
      ++missing;
    }
  }
  if (missing > 0) {
    return tr("%n archives the list uses aren't downloaded and verified yet.", nullptr,
              missing);
  }
  return {};
}

void WabbajackDialog::startInstall()
{
  if (!installProblem().isEmpty() || isRunning()) {
    return;
  }

  // the instance's name and the folder it goes into
  QDialog ask(this);
  ask.setWindowTitle(tr("Install the list"));
  auto* layout = new QVBoxLayout(&ask);
  auto* intro  = new QLabel(
      tr("The list is installed into a new instance of its own, as Wabbajack does. Its "
         "folder must be empty or new; the downloads stay where they are."),
      &ask);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto* form = new QFormLayout;
  QString defaultName = m_list->name.trimmed();
  if (!fixDirectoryName(defaultName)) {
    defaultName = QStringLiteral("Wabbajack list");
  }
  auto* name = new QLineEdit(defaultName, &ask);
  form->addRow(tr("Instance name"), name);
  auto* folderRow = new QHBoxLayout;
  const QString downloads = m_queue->folder();
  QDir parent(downloads);
  parent.cdUp();
  auto* folder = new QLineEdit(QDir::toNativeSeparators(parent.filePath(defaultName)), &ask);
  folder->setAccessibleName(tr("Install folder"));
  // the folder follows the name until it is chosen or typed by hand
  auto folderTouched = std::make_shared<bool>(false);
  connect(folder, &QLineEdit::textEdited, &ask, [folderTouched] {
    *folderTouched = true;
  });
  connect(name, &QLineEdit::textChanged, &ask, [=](const QString& text) {
    QString fixed = text.trimmed();
    if (!*folderTouched && !fixed.isEmpty() && fixDirectoryName(fixed)) {
      folder->setText(QDir::toNativeSeparators(parent.filePath(fixed)));
    }
  });
  auto* choose = new QPushButton(FluentIcons::glyph(FolderGlyph), tr("Choose…"), &ask);
  choose->setAutoDefault(false);
  folderRow->addWidget(folder, 1);
  folderRow->addWidget(choose);
  form->addRow(tr("Install folder"), folderRow);
  layout->addLayout(form);
  auto* note = new QLabel(&ask);
  note->setWordWrap(true);
  note->setObjectName(QStringLiteral("instanceDetailCaption"));
  layout->addWidget(note);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Install"));
  layout->addWidget(buttons);
  ask.resize(640, ask.sizeHint().height());

  connect(choose, &QPushButton::clicked, &ask, [&] {
    const QString dir = QFileDialog::getExistingDirectory(&ask, tr("Install folder"),
                                                          folder->text());
    if (!dir.isEmpty()) {
      *folderTouched = true;
      folder->setText(QDir::toNativeSeparators(dir));
    }
  });

  // why the name or the folder can't be used, empty when they can
  auto problemOf = [&]() -> QString {
    const QString n = name->text().trimmed();
    QString fixed   = n;
    if (n.isEmpty()) {
      return tr("Enter a name for the instance.");
    }
    if (!fixDirectoryName(fixed) || fixed != n) {
      return tr("Windows doesn't allow this name for a folder.");
    }
    const QString f = QDir::cleanPath(QDir::fromNativeSeparators(folder->text().trimmed()));
    const auto& instances = InstanceManager::singleton();
    // the instance of an install that didn't finish (some files failed) is
    // the one installing again goes on with
    if (!sameInstall(instances.instancePath(n), f)) {
      if (QFileInfo::exists(instances.instancePath(n))) {
        return tr("An instance with this name already exists.");
      }
      for (const QString& p : instances.globalInstancePaths()) {
        if (QFileInfo(p).fileName().compare(n, Qt::CaseInsensitive) == 0) {
          return tr("An instance with this name already exists.");
        }
      }
    }

    if (f.isEmpty() || !QDir::isAbsolutePath(f)) {
      return tr("Choose the folder to install into.");
    }
    if (QDir(f).isRoot()) {
      return tr("A drive's root can't be the install folder.");
    }
    auto inside = [&](const QString& outer) {
      const QString o = QDir::cleanPath(outer);
      return !o.isEmpty() && (f.compare(o, Qt::CaseInsensitive) == 0 ||
                              f.startsWith(o + u'/', Qt::CaseInsensitive) ||
                              o.startsWith(f + u'/', Qt::CaseInsensitive));
    };
    if (inside(gameDirectory())) {
      return tr("The install folder can't be the game's folder, inside it or around it.");
    }
    if (inside(downloads)) {
      return tr("The install folder can't be the downloads folder, inside it or around it.");
    }
    const QDir dir(f);
    if (dir.exists()) {
      // empty, or an install of this list stopped before
      const QStringList entries =
          dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
      if (!entries.isEmpty() &&
          (!QFileInfo::exists(installStateFolder(f)) || Wabbajack::isFinishedInstall(f))) {
        return tr("The install folder isn't empty.");
      }
    }
    // the nearest folder that exists, for its drive's free space
    QString existing = f;
    while (!QFileInfo(existing).isDir() && QFileInfo(existing).absolutePath() != existing) {
      existing = QFileInfo(existing).absolutePath();
    }
    const QStorageInfo storage(existing);
    if (storage.isValid() && storage.bytesAvailable() < m_list->installSize) {
      return tr("Not enough free space there: the list takes about %1 and the drive has %2.")
          .arg(localizedByteSize(m_list->installSize))
          .arg(localizedByteSize(storage.bytesAvailable()));
    }
    return {};
  };
  auto refresh = [&] {
    const QString p = problemOf();
    note->setText(p.isEmpty() ? tr("About %1 will be written there.")
                                    .arg(localizedByteSize(m_list->installSize))
                              : p);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(p.isEmpty());
  };
  connect(name, &QLineEdit::textChanged, &ask, refresh);
  connect(folder, &QLineEdit::textChanged, &ask, refresh);
  connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
  refresh();
  if (ask.exec() != QDialog::Accepted || !problemOf().isEmpty()) {
    return;
  }

  m_instanceName  = name->text().trimmed();
  m_installFolder = QDir::cleanPath(QDir::fromNativeSeparators(folder->text().trimmed()));
  if (!QDir().mkpath(m_installFolder)) {
    QMessageBox::warning(this, tr("Install the list"),
                         tr("The install folder can't be made: %1")
                             .arg(QDir::toNativeSeparators(m_installFolder)));
    return;
  }

  Wabbajack::InstallPlan plan;
  plan.listPath        = m_path;
  plan.installFolder   = m_installFolder;
  plan.downloadsFolder = downloads;
  plan.gameFolder      = gameDirectory();
  // the screen the game will show on, in pixels, as Wabbajack writes it
  // (the display's mode: Qt's size is rounded to device-independent pixels)
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0) {
    plan.screenWidth  = int(mode.dmPelsWidth);
    plan.screenHeight = int(mode.dmPelsHeight);
  } else if (const QScreen* screen = QGuiApplication::primaryScreen()) {
    const QSize size  = screen->size() * screen->devicePixelRatio();
    plan.screenWidth  = size.width();
    plan.screenHeight = size.height();
  }
  for (std::size_t i = 0; i < m_list->archives.size(); ++i) {
    const QString p = m_queue->pathOf(int(i));
    if (!p.isEmpty()) {
      plan.archives.insert(m_list->archives[i].hash, p);
    }
  }

  log::info("wabbajack: installing '{}' into {}", m_list->name, m_installFolder);
  m_installCancel = std::make_shared<std::atomic_bool>(false);
  m_installer     = new QFutureWatcher<Wabbajack::InstallResult>(this);
  connect(m_installer, &QFutureWatcherBase::finished, this, &WabbajackDialog::onInstalled);
  auto cancel             = m_installCancel;
  QPointer<QLabel> status = m_status;
  m_installer->setFuture(QtConcurrent::run([plan, cancel, status] {
    const Wabbajack::LowPriority low(Wabbajack::LowPriority::Mode::BelowNormal);
    return Wabbajack::install(plan, *cancel, [status, cancel](const QString& text, int permille) {
      QMetaObject::invokeMethod(
          status.data(),
          [status, cancel, text, permille] {
            if (status && !cancel->load()) {
              status->setText(WabbajackDialog::tr("Installing… %1% · %2")
                                  .arg(permille / 10)
                                  .arg(text));
            }
          },
          Qt::QueuedConnection);
    });
  }));
  updateState();
}

namespace
{

// "C:/a/b" is in "c:\a": the same folder or one under it
bool isIn(const QString& path, const QString& folder)
{
  if (path.isEmpty() || folder.isEmpty()) {
    return false;
  }
  const QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
  const QString f = QDir::cleanPath(QDir::fromNativeSeparators(folder));
  return p.compare(f, Qt::CaseInsensitive) == 0 ||
         p.startsWith(f + u'/', Qt::CaseInsensitive);
}

// the list's programs (its ModOrganizer.ini, its paths already this install's)
// that the new instance takes:
// - Explorer++ isn't installed with the list (it comes with the list's own
//   MO2): the list's "Explore Virtual Folder" goes, so MO2 Revamped adds its
//   own with the instance's data folder; another program of the list's
//   Explorer++ gets MO2 Revamped's;
// - a program that isn't a file (a folder, nothing) goes;
// - one missing outside the install and the game's folder was on the author's
//   computer (C:/Modding/xEdit/xEdit.exe) and goes; one missing in the game's
//   folder stays, for the files the list's guide has you copy there
std::vector<QVariantMap> keptPrograms(const std::vector<QVariantMap>& programs,
                                      const QString& installFolder,
                                      const QString& gameFolder, QStringList* notes)
{
  const QString listExplorer = QDir(installFolder).filePath("explorer++");
  const QString ownExplorer =
      QDir(QCoreApplication::applicationDirPath()).filePath("explorer++");
  const bool haveExplorer = QFileInfo(QDir(ownExplorer).filePath("Explorer++.exe")).isFile();

  std::vector<QVariantMap> kept;
  QStringList elsewhere;
  for (QVariantMap program : programs) {
    const QString title = program.value("title").toString();
    QString binary      = program.value("binary").toString();

    if (isIn(binary, listExplorer)) {
      if (title.compare(u"Explore Virtual Folder", Qt::CaseInsensitive) == 0 ||
          !haveExplorer) {
        log::info("wabbajack: program '{}' left out, {}", title,
                  haveExplorer ? "MO2 Revamped adds its own"
                               : "Explorer++ isn't in MO2 Revamped's folder");
        continue;
      }
      const QString rest =
          QDir(listExplorer).relativeFilePath(QDir::fromNativeSeparators(binary));
      binary = QDir(ownExplorer).filePath(rest);
      program.insert("binary", binary);
      if (isIn(program.value("workingDirectory").toString(), listExplorer)) {
        program.insert("workingDirectory", QDir::toNativeSeparators(ownExplorer));
      }
    }

    const QFileInfo file(binary);
    if (binary.trimmed().isEmpty() || title.trimmed().isEmpty() || file.isDir() ||
        (!file.exists() && !isIn(binary, installFolder) && !isIn(binary, gameFolder))) {
      log::info("wabbajack: program '{}' left out, '{}' isn't a file here", title,
                binary);
      if (!title.trimmed().isEmpty() && !file.isDir() && !binary.trimmed().isEmpty()) {
        elsewhere.append(title);
      }
      continue;
    }
    kept.push_back(std::move(program));
  }

  if (!elsewhere.isEmpty()) {
    notes->append(QCoreApplication::translate(
                      "WabbajackDialog",
                      "Programs that were on the list author's computer, left out: %1. "
                      "Add your own copy in Edit programs if you need it.")
                      .arg(elsewhere.join(QStringLiteral(", "))));
  }
  return kept;
}

}  // namespace

bool WabbajackDialog::sameInstall(const QString& instanceFolder, const QString& installFolder)
{
  const QString ini = QDir(instanceFolder).filePath("ModOrganizer.ini");
  if (installFolder.isEmpty() || !QFileInfo(ini).isFile() ||
      Wabbajack::isFinishedInstall(installFolder) ||
      !QFileInfo::exists(installStateFolder(installFolder))) {
    return false;
  }
  const QSettings s(ini, QSettings::IniFormat);
  const QString base = QDir::cleanPath(
      QDir::fromNativeSeparators(s.value("Settings/base_directory").toString()));
  return !base.isEmpty() && base.compare(QDir::cleanPath(installFolder), Qt::CaseInsensitive) == 0;
}

void WabbajackDialog::importExecutables(QSettings& from, QSettings& to,
                                        const IPluginGame* game, QStringList* notes) const
{
  // the list's programs, as they work on this PC: the magic paths are already
  // this PC's. keptPrograms() leaves out what can't run here (folders, the
  // list's own Explorer++, programs on the author's machine); here a working
  // folder on the author's drive is dropped, and "<game>\Data" follows the game
  // plugin's data folder where that Data folder isn't a real one
  const QString gameDir = QDir::cleanPath(gameDirectory());

  QString dataHere;
  if (game != nullptr && !gameDir.isEmpty()) {
    const QString pluginGame = QDir::cleanPath(game->gameDirectory().absolutePath());
    const QString pluginData = QDir::cleanPath(game->dataDirectory().absolutePath());
    if (!pluginGame.isEmpty() && pluginData.startsWith(pluginGame, Qt::CaseInsensitive)) {
      dataHere = QDir::cleanPath(gameDir + pluginData.mid(pluginGame.size()));
    }
  }
  const QString dataThere = QDir::cleanPath(gameDir + QStringLiteral("/Data"));
  // only when that Data folder isn't a real one here: Baldur's Gate 3 has a
  // real <game>\Data (Gustav.pak, the mods' loose files), so a program's
  // "...\Data\Gustav.pak" stays as it is
  const bool remap = !dataHere.isEmpty() &&
                     dataHere.compare(dataThere, Qt::CaseInsensitive) != 0 &&
                     !QFileInfo(dataThere).isDir();
  auto remapData = [&](QString text) {
    if (!remap) {
      return text;
    }
    // the folder itself, not a longer name that starts like it ("Data2")
    for (const QString& form : {QDir::toNativeSeparators(dataThere), dataThere}) {
      const QRegularExpression path(
          QRegularExpression::escape(form) + QStringLiteral(R"((?=$|[\\/"\s]))"),
          QRegularExpression::CaseInsensitiveOption);
      text.replace(path, form.contains(u'\\') ? QDir::toNativeSeparators(dataHere) : dataHere);
    }
    return text;
  };

  const int count = from.beginReadArray("customExecutables");
  std::vector<QVariantMap> programs;
  for (int i = 0; i < count; ++i) {
    from.setArrayIndex(i);
    QVariantMap program;
    for (const QString& key : from.childKeys()) {
      program.insert(key, from.value(key));
    }
    programs.push_back(std::move(program));
  }
  from.endArray();
  if (count <= 0) {
    return;
  }
  QString gameFolder = to.value("gamePath").toString();
  if (gameFolder.isEmpty()) {
    gameFolder = gameDirectory();
  }
  std::vector<QVariantMap> kept = keptPrograms(programs, m_installFolder, gameFolder, notes);
  for (QVariantMap& p : kept) {
    const QString work = p.value("workingDirectory").toString();
    if (!work.isEmpty() && !QFileInfo(remapData(work)).isDir() &&
        !isIn(work, m_installFolder) && !isIn(work, gameFolder)) {
      // the author's own folder ("D:\SteamLibrary\..."): MO2 starts it in the
      // program's folder
      log::info("wabbajack: '{}' starts in its own folder, not the author's {}",
                p.value("title").toString(), work);
      p.insert("workingDirectory", QString());
    } else if (!work.isEmpty()) {
      p.insert("workingDirectory", remapData(work));
    }
    p.insert("arguments", remapData(p.value("arguments").toString()));
  }
  to.remove("customExecutables");
  to.beginWriteArray("customExecutables", int(kept.size()));
  for (int i = 0; i < int(kept.size()); ++i) {
    to.setArrayIndex(i);
    for (auto it = kept[i].cbegin(); it != kept[i].cend(); ++it) {
      to.setValue(it.key(), it.value());
    }
  }
  to.endArray();
}

bool WabbajackDialog::makeInstance(const Wabbajack::InstallResult& result, QString* dataPath,
                                   QStringList* notes)
{
  IPluginGame* game =
      findGamePlugin(m_list->game, m_core.pluginContainer().plugins<IPluginGame>());
  if (game == nullptr) {
    return false;
  }
  const Settings& settings = m_core.settings();
  CreateInstanceDialog::ProfileSettings profiles;
  profiles.localInis           = settings.profileLocalInis();
  profiles.localSaves          = settings.profileLocalSaves();
  profiles.archiveInvalidation = settings.profileArchiveInvalidation();
  profiles.eldenRingSaveMode   = settings.eldenRingDefaultSaveMode();

  // the game's edition (Steam, GOG...), so the new instance doesn't ask for it:
  // this instance's when it is the same game, else from where the game is;
  // when neither says, MO2 asks at the instance's first start as always
  QString edition;
  const QStringList variants = game->gameVariants();
  if (variants.size() > 1) {
    const IPluginGame* managed = m_core.managedGame();
    if (managed != nullptr && managed->gameName() == game->gameName()) {
      edition = settings.game().edition().value_or(QString());
    }
    if (edition.isEmpty()) {
      const QString dir = gameDirectory().toLower();
      auto pick         = [&](const QString& word) {
        for (const QString& v : variants) {
          if (v.contains(word, Qt::CaseInsensitive)) {
            return v;
          }
        }
        return QString();
      };
      // an edition the folder names ("Fallout 3 goty" is Game Of The Year):
      // by its name or the initials of its words, when only one matches
      const QString folder = QFileInfo(dir).fileName();
      QStringList named;
      for (const QString& v : variants) {
        QString initials;
        for (const QString& w : v.split(u' ', Qt::SkipEmptyParts)) {
          initials += w.front();
        }
        const auto word = [&](const QString& text) {
          return !text.isEmpty() &&
                 QRegularExpression(QStringLiteral("(^|[^a-z0-9])%1($|[^a-z0-9])")
                                        .arg(QRegularExpression::escape(text.toLower())))
                     .match(folder)
                     .hasMatch();
        };
        if (word(v) || (initials.size() >= 3 && word(initials))) {
          named.append(v);
        }
      }
      if (named.size() == 1) {
        edition = named.front();
      } else if (dir.contains(u"/steamapps/")) {
        edition = pick(QStringLiteral("Steam"));
      } else if (dir.contains(u"gog")) {
        edition = pick(QStringLiteral("GOG"));
      } else if (dir.contains(u"epic")) {
        edition = pick(QStringLiteral("Epic"));
      }
    }
  }

  auto ci = CreateInstanceDialog::globalCreationInfo(
      m_instanceName, game, QDir::toNativeSeparators(gameDirectory()), edition, profiles);
  // the install folder is the instance's base: its mods, profiles and overwrite
  ci.paths.base      = QDir::toNativeSeparators(m_installFolder);
  ci.paths.downloads = QDir::toNativeSeparators(m_queue->folder());

  QStringList lines;
  if (sameInstall(QFileInfo(ci.iniPath).absolutePath(), m_installFolder)) {
    // installed again into the folder of an install that didn't finish: its
    // instance stays, with what the list's ini says written again below
    log::info("wabbajack: instance '{}' kept for {}", m_instanceName, m_installFolder);
  } else if (!CreateInstanceDialog::createInstance(ci, [&lines](QString line) {
               lines.append(line);
             })) {
    QMessageBox::warning(this, tr("New instance"),
                         tr("The instance couldn't be created.") + "\n\n" + lines.join('\n'));
    return false;
  }
  *dataPath = ci.dataPath;

  // what the list's own ModOrganizer.ini says that the instance can use: the
  // game's folder (its Stock Game), the profile it starts with, its programs
  if (!result.modOrganizerIni.isEmpty()) {
    const QString copy = QDir(installStateFolder(m_installFolder)).filePath("ModOrganizer.ini");
    QSaveFile f(copy);
    if (f.open(QIODevice::WriteOnly)) {
      f.write(result.modOrganizerIni);
      f.commit();
    }
    QSettings from(copy, QSettings::IniFormat);
    QSettings to(ci.iniPath, QSettings::IniFormat);

    // QSettings reads the [General] section as its top-level keys
    // the list's own copy of the game (its Stock Game), when it is one: the
    // game plugin recognises it. A folder the guide has you fill by hand
    // stays out until then, or the instance couldn't find its game
    const QString gamePath = from.value("gamePath").toString();
    if (!gamePath.isEmpty() && QFileInfo(gamePath).isDir() &&
        QDir::cleanPath(QDir::fromNativeSeparators(gamePath))
            .startsWith(m_installFolder + u'/', Qt::CaseInsensitive)) {
      if (game->looksValid(QDir(gamePath))) {
        to.setValue("gamePath", from.value("gamePath"));
      } else {
        notes->append(
            tr("The list runs the game from its own copy, %1, but the game isn't there "
               "yet: the instance uses your game folder. Once you have copied the game there "
               "as the list's guide says, set gamePath in the instance's ModOrganizer.ini "
               "to that folder.")
                .arg(QDir::toNativeSeparators(gamePath)));
        log::warn("wabbajack: {} isn't the game yet; the instance uses {}", gamePath,
                  gameDirectory());
      }
    }
    const QString profile = from.value("selected_profile").toString();
    if (!profile.isEmpty() && result.profiles.contains(profile, Qt::CaseInsensitive)) {
      to.setValue("selected_profile", from.value("selected_profile"));
    }
    importExecutables(from, to, game, notes);
    to.sync();
  }

  // the mod list as the author sees it: notes ("Optional") and categories
  {
    QSettings to(ci.iniPath, QSettings::IniFormat);
    to.setValue("Settings/mo2r_list_columns", true);
    // a profile of the list to start with when its ini names none MO2 has
    // here ("Default" isn't one of the list's)
    const QString selected = to.value("selected_profile").toString();
    if (!result.profiles.isEmpty() &&
        !result.profiles.contains(selected, Qt::CaseInsensitive)) {
      to.setValue("selected_profile", result.profiles.front().toUtf8());
    }
    to.sync();
  }
  // the list's own categories ("6," is its "User Interface"): MO2 reads them
  // from the instance's folder, not from the install's
  const QString categories = QDir(m_installFolder).filePath("categories.dat");
  if (QFileInfo(categories).isFile() && !ci.dataPath.isEmpty()) {
    const QString target = QDir(ci.dataPath).filePath("categories.dat");
    QFile::remove(target);
    if (!QFile::copy(categories, target)) {
      log::warn("wabbajack: the list's categories couldn't be copied to {}", target);
    }
    // without a Nexus category map MO2 adds its default categories over the
    // list's (its 7 became "Creatures, Mounts, & Vehicles"): an empty map,
    // or the list's own
    const QString map       = QDir(m_installFolder).filePath("nexuscatmap.dat");
    const QString mapTarget = QDir(ci.dataPath).filePath("nexuscatmap.dat");
    QFile::remove(mapTarget);
    if (QFileInfo(map).isFile()) {
      QFile::copy(map, mapTarget);
    } else {
      QFile empty(mapTarget);
      if (!empty.open(QIODevice::WriteOnly)) {
        log::warn("wabbajack: {} couldn't be written", mapTarget);
      }
    }
  }
  log::info("wabbajack: instance '{}' made for {}", m_instanceName, m_installFolder);
  return true;
}

void WabbajackDialog::onInstalled()
{
  const Wabbajack::InstallResult r = m_installer->result();
  m_installer->deleteLater();
  m_installer = nullptr;
  updateState();

  QStringList lines;
  lines << tr("%n files installed and verified.", nullptr, r.written);
  if (r.modOrganizer > 0) {
    lines << tr("%n files of the list's own Mod Organizer left out.", nullptr, r.modOrganizer);
  }
  if (!r.unsupported.isEmpty()) {
    lines << tr("Not done yet by MO2 Revamped: %1.").arg(r.unsupported.join(", "));
  }
  if (!r.plugins.isEmpty()) {
    lines << tr("MO2 plugins the list adds, set aside in %1: %2.")
                 .arg(QDir::toNativeSeparators(
                     QDir(installStateFolder(m_installFolder)).filePath("plugins")))
                 .arg(r.plugins.join(", "));
  }
  lines << r.notes;
  if (r.failed > 0) {
    lines << tr("%n files failed:", nullptr, r.failed);
    lines << r.failures.mid(0, 30);
  }

  // the summary, kept with the install
  {
    QSaveFile f(QDir(installStateFolder(m_installFolder)).filePath("summary.txt"));
    if (f.open(QIODevice::WriteOnly)) {
      QStringList all = lines;
      all << QString() << r.failures;
      f.write(all.join("\r\n").toUtf8());
      f.commit();
    }
  }

  if (m_closeAfterInstall) {
    close();
    return;
  }
  if (r.cancelled) {
    m_status->setText(
        tr("Install stopped. Installing again into the same folder starts it over."));
    return;
  }
  if (!r.ok) {
    m_status->setText(tr("The install failed: %1").arg(r.error));
    if (!isActiveWindow()) {
      QApplication::alert(this);
    }
    QMessageBox::warning(this, tr("Install the list"),
                         tr("The install failed: %1").arg(r.error));
    return;
  }

  QString dataPath;
  QStringList instanceNotes;
  if (!makeInstance(r, &dataPath, &instanceNotes)) {
    m_status->setText(tr("Installed, but the instance couldn't be made."));
    return;
  }
  if (!instanceNotes.isEmpty()) {
    // before the failures, as in the summary written above
    const int at = r.failed > 0 ? int(lines.size()) - int(r.failures.mid(0, 30).size()) - 1
                                : int(lines.size());
    for (int i = 0; i < instanceNotes.size(); ++i) {
      lines.insert(at + i, instanceNotes[i]);
    }
    QSaveFile f(QDir(installStateFolder(m_installFolder)).filePath("summary.txt"));
    if (f.open(QIODevice::WriteOnly)) {
      QStringList all = lines;
      all << QString() << r.failures;
      f.write(all.join("\r\n").toUtf8());
      f.commit();
    }
  }
  m_status->setText(tr("Installed into %1.").arg(QDir::toNativeSeparators(m_installFolder)));

  // the box brings its own sound; behind other windows the taskbar button
  // flashes too
  if (!isActiveWindow()) {
    QApplication::alert(this);
  }
  QMessageBox box(r.failed > 0 ? QMessageBox::Warning : QMessageBox::Information,
                  tr("Install the list"),
                  tr("\"%1\" is installed in the instance \"%2\".")
                      .arg(m_list->name, m_instanceName),
                  QMessageBox::NoButton, this);
  box.setInformativeText(lines.join("\n"));
  QPushButton* restart = box.addButton(tr("Restart in the new instance"), QMessageBox::AcceptRole);
  box.addButton(tr("Later"), QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() != restart) {
    return;
  }
  if (ExitModOrganizer(Exit::Restart)) {
    // only once closing is certain, as the instance manager does
    InstanceManager::singleton().setCurrentInstance(Instance(dataPath, false));
  }
}
