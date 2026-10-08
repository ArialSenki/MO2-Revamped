#include "createinstancedialog.h"
#include "createinstancedialogpages.h"
#include "eldenringsavesettings.h"
#include "instancemanager.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include "ui_createinstancedialog.h"
#include <iplugingame.h>
#include <utility.h>

#include <QLayout>
#include <QBoxLayout>
#include <QCommandLinkButton>
#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QSet>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTextOption>
#include <QUrl>
#include <QWidget>

using namespace MOBase;

class Failed
{};

namespace {

void compactVerticalSpacers(QLayout* layout)
{
  if (layout == nullptr) {
    return;
  }

  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem* item = layout->itemAt(i);
    if (QSpacerItem* spacer = item->spacerItem()) {
      if (spacer->expandingDirections().testFlag(Qt::Vertical)) {
        const QSize hint = spacer->sizeHint();
        spacer->changeSize(hint.width(), qMin(hint.height(), 10),
                           QSizePolicy::Minimum, QSizePolicy::Fixed);
      }
      continue;
    }

    if (item->layout() != nullptr) {
      compactVerticalSpacers(item->layout());
    } else if (QWidget* child = item->widget();
               child != nullptr && child->layout() != nullptr) {
      compactVerticalSpacers(child->layout());
    }
  }

  layout->invalidate();
}

bool copyIsolatedApplication(const QString& sourcePath,
                             const QString& destinationPath,
                             const std::function<void(QString)>& log)
{
  const QSet<QString> excludedDirectories = {
      QStringLiteral(".git"),           QStringLiteral("__pycache__"),
      QStringLiteral("crashdumps"),     QStringLiteral("downloads"),
      QStringLiteral("globalinstances"), QStringLiteral("isolatedinstances"),
      QStringLiteral("logs"),           QStringLiteral("mods"),
      QStringLiteral("overwrite"),      QStringLiteral("profiles"),
      QStringLiteral("portableinstances"), QStringLiteral("webcache")};
  const QSet<QString> excludedFiles = {
      QStringLiteral("modorganizer.ini"), QStringLiteral("portable.txt"),
      QStringLiteral("eldenring-only.portable"), QStringLiteral("unins000.exe"),
      QStringLiteral("unins000.dat")};

  std::function<bool(const QDir&, const QDir&, bool)> copyDirectory;
  copyDirectory = [&](const QDir& source, const QDir& destination,
                      bool topLevel) {
    const auto entries = source.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& entry : entries) {
      if (entry.isSymLink()) {
        continue;
      }

      const QString key = entry.fileName().toCaseFolded();
      if (topLevel && entry.isDir() && excludedDirectories.contains(key)) {
        continue;
      }
      if (entry.isFile() && excludedFiles.contains(key)) {
        continue;
      }

      const QString destinationEntry = destination.filePath(entry.fileName());
      if (entry.isDir()) {
        if (!destination.mkpath(entry.fileName()) ||
            !copyDirectory(QDir(entry.absoluteFilePath()),
                           QDir(destinationEntry), false)) {
          return false;
        }
      } else if (!entry.isFile() ||
                 !QFile::copy(entry.absoluteFilePath(), destinationEntry)) {
        log(QObject::tr("Could not copy %1 to the isolated MO2 folder.")
                .arg(entry.absoluteFilePath()));
        return false;
      }
    }

    return true;
  };

  return copyDirectory(QDir(sourcePath), QDir(destinationPath), true);
}

bool createMarkerFile(const QString& directory, const QString& name)
{
  QFile marker(QDir(directory).filePath(name));
  return marker.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

}  // namespace

// create() will create all the directories in `target`; if any path component
// fails to create, it will throw Failed
//
// unless commit() is called, all the created directories will be deleted in
// the destructor
//
class DirectoryCreator
{
public:
  DirectoryCreator(const DirectoryCreator&)            = delete;
  DirectoryCreator& operator=(const DirectoryCreator&) = delete;

  static std::unique_ptr<DirectoryCreator> create(const QDir& target,
                                                  std::function<void(QString)> log)
  {
    return std::unique_ptr<DirectoryCreator>(new DirectoryCreator(target, log));
  }

  ~DirectoryCreator() { rollback(); }

  void commit() { m_created.clear(); }

  void rollback() noexcept
  {
    try {
      // delete each directory starting from the end
      for (auto itor = m_created.rbegin(); itor != m_created.rend(); ++itor) {
        const auto r = shell::DeleteDirectoryRecursive(*itor);
        if (!r) {
          m_logger(r.toString());
        }
      }

      m_created.clear();
    } catch (...) {
      // eat it
    }
  }

private:
  std::function<void(QString)> m_logger;

  DirectoryCreator(const QDir& target, std::function<void(QString)> log) : m_logger(log)
  {
    try {
      // split on separators
      const QString s      = QDir::toNativeSeparators(target.absolutePath());
      const QStringList cs = s.split("\\");

      if (cs.empty()) {
        return;
      }

      // root directory
      QDir d(cs[0]);

      // for each directory after the root
      for (int i = 1; i < cs.size(); ++i) {
        d = d.filePath(cs[i]);

        if (!d.exists()) {
          m_logger(QObject::tr("Creating %1").arg(d.path()));
          const auto r = shell::CreateDirectories(d);

          if (!r) {
            m_logger(r.toString());
            throw Failed();
          }

          m_created.push_back(d);
        }
      }
    } catch (...) {
      rollback();
      throw;
    }
  }

private:
  std::vector<QDir> m_created;
};

CreateInstanceDialog::CreateInstanceDialog(const PluginContainer& pc, Settings* s,
                                           QWidget* parent)
    : QDialog(parent), ui(new Ui::CreateInstanceDialog), m_pc(pc), m_settings(s),
      m_switching(false), m_singlePage(false)
{
  using namespace cid;

  ui->setupUi(this);
  setMinimumSize(760, 480);
  resize(820, 520);
  m_originalNext = ui->next->text();

  m_pages.push_back(std::make_unique<IntroPage>(*this));
  m_pages.push_back(std::make_unique<TypePage>(*this));
  m_pages.push_back(std::make_unique<GamePage>(*this));
  m_pages.push_back(std::make_unique<VariantsPage>(*this));
  m_pages.push_back(std::make_unique<NamePage>(*this));
  m_pages.push_back(std::make_unique<ProfilePage>(*this));
  m_pages.push_back(std::make_unique<PathsPage>(*this));
  m_pages.push_back(std::make_unique<NexusPage>(*this));
  m_pages.push_back(std::make_unique<ConfirmationPage>(*this));

  ui->verticalLayout_15->setContentsMargins(12, 10, 12, 10);
  ui->verticalLayout_15->setSpacing(8);
  ui->horizontalLayout_15->setSpacing(12);
  ui->horizontalLayout->setContentsMargins(12, 8, 12, 8);
  ui->horizontalLayout->setSpacing(8);
  ui->title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  ui->stepIndicator->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  ui->stepProgress->setTextVisible(false);
  ui->stepProgress->setRange(0, 9);
  ui->stepProgress->setValue(1);
  ui->stepProgress->setFixedHeight(5);
  ui->back->setMinimumHeight(34);
  ui->next->setMinimumHeight(34);
  ui->cancel->setMinimumHeight(34);
  ui->verticalLayout_23->setContentsMargins(12, 12, 12, 12);
  ui->verticalLayout_23->setSpacing(10);
  ui->horizontalLayout_5->setContentsMargins(10, 6, 10, 6);
  ui->horizontalLayout_5->setSpacing(8);
  ui->review->setMinimumHeight(280);
  ui->review->setMaximumHeight(320);
  ui->review->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  ui->review->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  ui->creationLog->setMinimumHeight(72);
  ui->creationLog->setMaximumHeight(128);
  ui->creationLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  ui->creationLog->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  ui->creationLog->hide();
  ui->launch->setMinimumHeight(34);
  ui->launch->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  if (QBoxLayout* nameCardLayout =
          qobject_cast<QBoxLayout*>(ui->widget_9->layout())) {
    nameCardLayout->setSpacing(8);
    if (ui->verticalSpacer_2 != nullptr) {
      nameCardLayout->removeItem(ui->verticalSpacer_2);
      delete ui->verticalSpacer_2;
      ui->verticalSpacer_2 = nullptr;
    }
  }

  // Keep each step on the same responsive content grid. The page can grow
  // with the window, which gives game paths and advanced directory fields
  // enough room without centering controls in a narrow fixed-width column.
  for (int i = 0; i < ui->pages->count(); ++i) {
    QWidget* page = ui->pages->widget(i);
    if (QLayout* pageLayout = page->layout()) {
      pageLayout->setContentsMargins(20, 16, 20, 16);
      pageLayout->setSpacing(14);
      compactVerticalSpacers(pageLayout);
      const bool compactContentPage = page == ui->page_6 || page == ui->page_8;
      const auto hasVisibleChoice = [page](const char* objectName) {
        const auto* choice = page->findChild<QCommandLinkButton*>(
            QString::fromLatin1(objectName));
        return choice != nullptr && !choice->isHidden();
      };
      const bool fillTypeChoices =
          page == ui->page_2 && hasVisibleChoice("createGlobal") &&
          hasVisibleChoice("createPortable") &&
          hasVisibleChoice("createIsolated");
      bool pageHasScrollableContent = false;
      for (int itemIndex = 0; itemIndex < pageLayout->count(); ++itemIndex) {
        if (QWidget* content = pageLayout->itemAt(itemIndex)->widget()) {
          content->setMaximumWidth(QWIDGETSIZE_MAX);
          content->setMinimumWidth(0);
          const bool hasScrollArea =
              content->findChild<QAbstractScrollArea*>() != nullptr ||
              content->findChild<QStackedWidget*>() != nullptr;
          const bool hasScrollableContent = hasScrollArea && !compactContentPage;
          const bool fillsTypeCard =
              fillTypeChoices && content->objectName() == QStringLiteral("widget_3");
          const bool expandsContent = hasScrollableContent || fillsTypeCard;
          pageHasScrollableContent |= hasScrollableContent;
          content->setSizePolicy(
              QSizePolicy::Expanding,
              expandsContent ? QSizePolicy::Expanding : QSizePolicy::Maximum);
          if (expandsContent) {
            if (QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(pageLayout)) {
              boxLayout->setStretch(itemIndex, 1);
            }
          }
          if (content->layout() != nullptr) {
            compactVerticalSpacers(content->layout());
            content->layout()->setSpacing(
                qMax(content->layout()->spacing(), 12));
          }
          // Keep scrollable panels stretched with the current page. AlignTop
          // overrides an expanding size policy and leaves the game list
          // compressed at the top of the wizard.
          if (!expandsContent) {
            pageLayout->setAlignment(content, Qt::AlignTop);
          }
        }
      }
      if (fillTypeChoices) {
        // This panel contains only the three storage choices. Remove the old
        // helper and trailing spacer, and undo the generic page spacing so
        // the last card meets the panel's bottom edge.
        ui->portableExistsLabel->hide();
        if (QBoxLayout* panelLayout =
                qobject_cast<QBoxLayout*>(ui->widget_3->layout())) {
          panelLayout->removeWidget(ui->portableExistsLabel);
          if (ui->verticalSpacer != nullptr) {
            panelLayout->removeItem(ui->verticalSpacer);
            delete ui->verticalSpacer;
            ui->verticalSpacer = nullptr;
          }
          panelLayout->setContentsMargins(0, 0, 0, 0);
          panelLayout->setSpacing(0);
          panelLayout->invalidate();
        }

        QWidget* choices = page->findChild<QWidget*>(QStringLiteral("widget_15"));
        if (choices != nullptr) {
          choices->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
          if (QBoxLayout* optionsLayout =
                  qobject_cast<QBoxLayout*>(ui->widget_3->layout())) {
            const int choicesIndex = optionsLayout->indexOf(choices);
            if (choicesIndex >= 0) {
              optionsLayout->setStretch(choicesIndex, 1);
            }
          }
          if (QBoxLayout* choicesLayout =
                  qobject_cast<QBoxLayout*>(choices->layout())) {
            choicesLayout->setContentsMargins(0, 0, 0, 0);
            for (const auto* objectName : {"createGlobal", "createPortable",
                                           "createIsolated"}) {
              if (QCommandLinkButton* choice = page->findChild<QCommandLinkButton*>(
                      QString::fromLatin1(objectName))) {
                choice->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);
                const int choiceIndex = choicesLayout->indexOf(choice);
                if (choiceIndex >= 0) {
                  choicesLayout->setStretch(choiceIndex, 1);
                }
              }
            }
          }
        }
      }
      if (!pageHasScrollableContent && !fillTypeChoices) {
        if (QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(pageLayout)) {
          boxLayout->addStretch(1);
        }
      }
    }

    for (QLabel* heading : page->findChildren<QLabel*>()) {
      if (heading->text().trimmed().startsWith(QStringLiteral("<h3"),
                                                Qt::CaseInsensitive)) {
        heading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      }
    }

    for (const auto* objectName : {"label_15", "portableExistsLabel",
                                   "gameSelectionHint", "label_14",
                                   "label_18", "label_17"}) {
      if (QLabel* label = page->findChild<QLabel*>(QString::fromLatin1(objectName))) {
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      }
    }
  }

  ui->pages->setCurrentIndex(0);
  ui->launch->setChecked(true);

  if (!InstanceManager::singleton().hasAnyInstances()) {
    // first run of MO, there are no instances yet, force launch
    ui->launch->setEnabled(false);
  }

  if (m_pages[0]->skip()) {
    next();
  }

  // Pages can reveal game-specific controls after they are constructed.
  // Keep enough width for the widest page. Short steps retain the compact
  // dialog height; scrollable pages can use the available vertical space.
  QSize largestPageSize;
  for (int i = 0; i < ui->pages->count(); ++i) {
    QWidget* page = ui->pages->widget(i);
    page->ensurePolished();
    if (QLayout* pageLayout = page->layout()) {
      pageLayout->activate();
    }
    largestPageSize = largestPageSize.expandedTo(page->minimumSizeHint())
                          .expandedTo(page->sizeHint());
  }
  if (largestPageSize.isValid()) {
    ui->pages->setMinimumWidth(largestPageSize.width());
    ui->pages->setMinimumHeight(0);
    ui->pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  ui->next->setFocus();

  updateNavigation();

  addShortcutAction(QKeySequence::Find, Actions::Find);

  addShortcut(Qt::ALT + Qt::Key_Left, [&] {
    back();
  });
  addShortcut(Qt::ALT + Qt::Key_Right, [&] {
    next(false);
  });
  addShortcut(Qt::CTRL + Qt::Key_Return, [&] {
    next();
  });

  connect(ui->next, &QPushButton::clicked, [&] {
    next();
  });
  connect(ui->back, &QPushButton::clicked, [&] {
    back();
  });
  connect(ui->cancel, &QPushButton::clicked, [&] {
    reject();
  });
}

CreateInstanceDialog::~CreateInstanceDialog() = default;

Ui::CreateInstanceDialog* CreateInstanceDialog::getUI()
{
  return ui.get();
}

const PluginContainer& CreateInstanceDialog::pluginContainer()
{
  return m_pc;
}

Settings* CreateInstanceDialog::settings()
{
  return m_settings;
}

bool CreateInstanceDialog::isOnLastPage() const
{
  for (int i = ui->pages->currentIndex() + 1; i < ui->pages->count(); ++i) {
    if (!m_pages[i]->skip()) {
      return false;
    }
  }

  return true;
}

void CreateInstanceDialog::next(bool allowFinish)
{
  if (!canNext()) {
    return;
  }

  const auto i    = ui->pages->currentIndex();
  const auto last = isOnLastPage();

  if (last) {
    if (allowFinish) {
      if (m_singlePage) {
        // just close the dialog
        accept();
      } else {
        finish();
      }
    }
  } else {
    changePage(+1);
  }
}

void CreateInstanceDialog::back()
{
  if (!canBack()) {
    return;
  }

  changePage(-1);
}

void CreateInstanceDialog::addShortcut(QKeySequence seq, std::function<void()> f)
{
  auto* sc = new QShortcut(seq, this);

  sc->setAutoRepeat(false);
  sc->setContext(Qt::WidgetWithChildrenShortcut);

  QObject::connect(sc, &QShortcut::activated, f);
}

void CreateInstanceDialog::addShortcutAction(QKeySequence seq, Actions a)
{
  addShortcut(seq, [this, a] {
    doAction(a);
  });
}

void CreateInstanceDialog::doAction(Actions a)
{
  std::size_t i = static_cast<std::size_t>(ui->pages->currentIndex());

  if (i >= m_pages.size()) {
    return;
  }

  m_pages[i]->action(a);
}

void CreateInstanceDialog::setSinglePageImpl(const QString& instanceName)
{
  m_singlePage = true;

  if (m_pages[ui->pages->currentIndex()]->skip()) {
    next();
  }

  // don't show the "create a new instance" title for single pages, this is
  // when the instance already exists but some info is missing
  ui->title->setText(tr("Setting up instance %1").arg(instanceName));
  setWindowTitle(tr("Setting up an instance %1").arg(instanceName));
}

void CreateInstanceDialog::changePage(int d)
{
  std::size_t i = static_cast<std::size_t>(ui->pages->currentIndex());

  // goes back or forwards until an unskippable page is reached, or the
  // first/last page

  if (d > 0) {
    // forwards
    for (;;) {
      ++i;

      if (i >= m_pages.size()) {
        break;
      }

      if (!m_pages[i]->skip()) {
        break;
      }
    }
  } else {
    // backwards
    for (;;) {
      if (i == 0) {
        break;
      }

      --i;

      if (!m_pages[i]->skip()) {
        break;
      }
    }
  }

  if (i < m_pages.size()) {
    selectPage(i);
  }
}

void CreateInstanceDialog::finish()
{
  ui->creationLog->clear();
  logCreation(tr("Creating instance..."));

  const auto& m = InstanceManager::singleton();
  const auto ci = creationInfo();

  auto logger = [&](QString s) {
    logCreation(s);
  };

  auto createDir = [&](QString path) {
    return DirectoryCreator::create(path, logger);
  };

  // don't restart if this is the first instance, it'll be selected and opened
  const bool mustRestart = InstanceManager::singleton().hasAnyInstances();

  try {
    std::vector<std::unique_ptr<DirectoryCreator>> dirs;

    // creating all these directories; if any of them fail, this throws and
    // any newly created directory will be deleted in DirectoryCreator's dtor
    if (ci.type == Isolated && QFileInfo::exists(ci.dataPath)) {
      logCreation(tr("The isolated MO2 folder already exists: %1")
                      .arg(ci.dataPath));
      throw Failed();
    }

    dirs.push_back(createDir(ci.dataPath));
    if (ci.type == Isolated) {
      logCreation(tr("Copying MO2 application files into %1...").arg(ci.dataPath));
      if (!copyIsolatedApplication(QCoreApplication::applicationDirPath(),
                                   ci.dataPath, logger)) {
        logCreation(tr("The isolated copy could not be completed."));
        throw Failed();
      }
      if (!createMarkerFile(ci.dataPath, QStringLiteral("portable.txt")) ||
          !createMarkerFile(ci.dataPath,
                            QStringLiteral("eldenring-only.portable"))) {
        logCreation(tr("Could not initialize the isolated MO2 copy."));
        throw Failed();
      }
      logCreation(tr("MO2 application files copied."));
    }
    dirs.push_back(createDir(ci.paths.base));
    dirs.push_back(createDir(PathSettings::resolve(ci.paths.downloads, ci.paths.base)));
    dirs.push_back(createDir(PathSettings::resolve(ci.paths.mods, ci.paths.base)));
    dirs.push_back(createDir(PathSettings::resolve(ci.paths.profiles, ci.paths.base)));
    dirs.push_back(createDir(PathSettings::resolve(ci.paths.overwrite, ci.paths.base)));

    // creating ini
    Settings s(ci.iniPath);
    s.game().setName(ci.game->gameName());
    s.game().setDirectory(ci.gameLocation);

    // Instance settings are independent, but a newly created instance should
    // start with the active MO2 appearance. Use the fork's light theme when
    // there is no loaded instance to inherit from yet.
    const auto* activeSettings = Settings::maybeInstance();
    const auto activeStyle = activeSettings
                                 ? activeSettings->interface().styleName()
                                 : std::optional<QString>{};
    s.interface().setStyleName(
        activeStyle.value_or(Settings::defaultInterfaceStyleName()));

    if (!ci.gameVariant.isEmpty()) {
      s.game().setEdition(ci.gameVariant);
    }

    if (ci.paths.base != ci.dataPath) {
      s.paths().setBase(ci.paths.base);
    }

    if (ci.paths.downloads != cid::makeDefaultPath(AppConfig::downloadPath())) {
      s.paths().setDownloads(ci.paths.downloads);
    }

    if (ci.paths.mods != cid::makeDefaultPath(AppConfig::modsPath())) {
      s.paths().setMods(ci.paths.mods);
    }

    if (ci.paths.profiles != cid::makeDefaultPath(AppConfig::profilesPath())) {
      s.paths().setProfiles(ci.paths.profiles);
    }

    if (ci.paths.overwrite != cid::makeDefaultPath(AppConfig::overwritePath())) {
      s.paths().setOverwrite(ci.paths.overwrite);
    }

    s.setProfileLocalInis(ci.profileSettings.localInis);
    s.setProfileLocalSaves(ci.profileSettings.localSaves);
    s.setProfileArchiveInvalidation(ci.profileSettings.archiveInvalidation);

    if (EldenRingSaveSettings::supports(ci.game)) {
      s.setEldenRingDefaultSaveMode(ci.profileSettings.eldenRingSaveMode);
    }

    logCreation(tr("Writing %1...").arg(ci.iniPath));

    // writing ini
    const auto r = s.sync();

    if (r != QSettings::NoError) {
      switch (r) {
      case QSettings::AccessError:
        logCreation(formatSystemMessage(ERROR_ACCESS_DENIED));
        break;

      case QSettings::FormatError:
        logCreation(tr("Format error."));
        break;

      default:
        logCreation(tr("Error %1.").arg(static_cast<int>(r)));
        break;
      }

      throw Failed();
    }

    // committing all the directories so they don't get deleted
    for (auto& d : dirs) {
      d->commit();
    }

    logCreation(tr("Done."));

    // launch the new instance
    if (ui->launch->isChecked()) {
      if (ci.type == Isolated) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(ci.dataPath))) {
          logCreation(tr("Could not open the isolated copy folder: %1")
                          .arg(ci.dataPath));
        }
      } else {
        InstanceManager::singleton().setCurrentInstance(
            Instance(ci.dataPath, ci.type == Portable));

        if (mustRestart) {
          ExitModOrganizer(Exit::Restart);
          m_switching = true;
        }
      }
    }

    // close the dialog
    accept();
  } catch (Failed&) {
    // if Failed was thrown, all the directories have been deleted
  }
}

void CreateInstanceDialog::logCreation(const QString& s)
{
  if (ui->creationLog->isHidden()) {
    ui->creationLog->show();
    ui->widget_19->updateGeometry();
  }
  ui->creationLog->insertPlainText(s + "\n");
}

void CreateInstanceDialog::logCreation(const std::wstring& s)
{
  logCreation(QString::fromStdWString(s));
}

void CreateInstanceDialog::selectPage(std::size_t i)
{
  if (i >= m_pages.size()) {
    return;
  }

  ui->pages->setCurrentIndex(static_cast<int>(i));
  m_pages[i]->activated();

  updateNavigation();
}

void CreateInstanceDialog::updateNavigation()
{
  const auto i    = ui->pages->currentIndex();
  const auto last = isOnLastPage();

  int step = 0;
  int steps = 0;
  for (std::size_t page = 0; page < m_pages.size(); ++page) {
    if (m_pages[page]->skip()) {
      continue;
    }
    ++steps;
    if (static_cast<int>(page) <= i) {
      ++step;
    }
  }
  ui->stepIndicator->setText(tr("Step %1 of %2").arg(step).arg(steps));
  ui->stepProgress->setRange(0, qMax(steps, 1));
  ui->stepProgress->setValue(step);

  ui->next->setEnabled(canNext());
  ui->back->setEnabled(canBack());

  if (last) {
    ui->next->setText(tr("Finish"));
  } else {
    ui->next->setText(m_originalNext);
  }
}

bool CreateInstanceDialog::canNext() const
{
  const auto i = ui->pages->currentIndex();
  return m_pages[i]->skip() || m_pages[i]->ready();
}

bool CreateInstanceDialog::canBack() const
{
  auto i = ui->pages->currentIndex();

  for (;;) {
    if (i == 0) {
      break;
    }

    --i;

    if (!m_pages[i]->skip()) {
      return true;
    }
  }

  return false;
}

bool CreateInstanceDialog::switching() const
{
  return m_switching;
}

CreateInstanceDialog::CreationInfo CreateInstanceDialog::rawCreationInfo() const
{
  const auto iniFilename = QString::fromStdWString(AppConfig::iniFileName());

  CreationInfo ci;

  ci.type            = getSelected(&cid::Page::selectedInstanceType);
  ci.game            = getSelected(&cid::Page::selectedGame);
  ci.gameLocation    = getSelected(&cid::Page::selectedGameLocation);
  ci.gameVariant     = getSelected(&cid::Page::selectedGameVariant, ci.game);
  ci.instanceName    = getSelected(&cid::Page::selectedInstanceName);
  ci.profileSettings = getSelected(&cid::Page::profileSettings);
  ci.paths           = getSelected(&cid::Page::selectedPaths);

  if (ci.type == Portable) {
    const auto& instances = InstanceManager::singleton();
    ci.dataPath = ci.instanceName.isEmpty()
                      ? instances.portablePath()
                      : instances.portableInstancePath(ci.instanceName);
  } else if (ci.type == Isolated) {
    ci.dataPath =
        InstanceManager::singleton().isolatedInstancePath(ci.instanceName);
  } else {
    ci.dataPath = InstanceManager::singleton().instancePath(ci.instanceName);
  }

  ci.dataPath = QDir::toNativeSeparators(ci.dataPath);
  ci.iniPath  = ci.dataPath + "/" + iniFilename;

  return ci;
}

CreateInstanceDialog::CreationInfo CreateInstanceDialog::creationInfo() const
{
  auto fixVarDir = [](QString& path, const std::wstring& defaultDir) {
    // if the path is empty, it wasn't filled by the user, probably because
    // the "Advanced" checkbox wasn't checked, so use the base dir variable
    // with the default dir

    if (path.isEmpty()) {
      path = cid::makeDefaultPath(defaultDir);
    } else if (!path.contains(PathSettings::BaseDirVariable)) {
      path = QDir(path).absolutePath();
    }

    path = QDir::toNativeSeparators(path);
  };

  auto fixDirPath = [](QString& path) {
    path = QDir::toNativeSeparators(QDir(path).absolutePath());
  };

  auto fixFilePath = [](QString& path) {
    path = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
  };

  auto ci = rawCreationInfo();

  fixDirPath(ci.paths.base);
  fixFilePath(ci.paths.ini);

  fixVarDir(ci.paths.downloads, AppConfig::downloadPath());
  fixVarDir(ci.paths.mods, AppConfig::modsPath());
  fixVarDir(ci.paths.profiles, AppConfig::profilesPath());
  fixVarDir(ci.paths.overwrite, AppConfig::overwritePath());

  return ci;
}
