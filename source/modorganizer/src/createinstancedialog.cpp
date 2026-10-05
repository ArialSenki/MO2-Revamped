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
#include <QCommandLinkButton>
#include <QLabel>
#include <QSize>
#include <QSizePolicy>
#include <QWidget>

using namespace MOBase;

class Failed
{};

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
  setMinimumSize(760, 500);
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

  ui->title->setAlignment(Qt::AlignHCenter);
  ui->stepIndicator->setAlignment(Qt::AlignHCenter);

  // Give each page a roomy, centered content column. The shared width keeps
  // cards, save options, and path controls aligned from step to step.
  constexpr int pageContentWidth = 680;
  const QString choiceButtonStyle =
      QStringLiteral("QCommandLinkButton:checked { "
                     "background-color: palette(alternate-base); "
                     "color: palette(text); "
                     "border: 1px solid palette(mid); }");
  for (int i = 0; i < ui->pages->count(); ++i) {
    QWidget* page = ui->pages->widget(i);
    if (QLayout* pageLayout = page->layout()) {
      pageLayout->setContentsMargins(20, 18, 20, 18);
      pageLayout->setSpacing(16);
      for (int itemIndex = 0; itemIndex < pageLayout->count(); ++itemIndex) {
        if (QWidget* content = pageLayout->itemAt(itemIndex)->widget()) {
          content->setMaximumWidth(pageContentWidth);
          if (content->layout() != nullptr) {
            content->setMinimumWidth(pageContentWidth);
            content->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Preferred);
            content->layout()->setSpacing(
                qMax(content->layout()->spacing(), 12));
          }
          pageLayout->setAlignment(content, Qt::AlignHCenter);
        }
      }
    }

    for (QLabel* heading : page->findChildren<QLabel*>()) {
      if (heading->text().trimmed().startsWith(QStringLiteral("<h3"),
                                                Qt::CaseInsensitive)) {
        heading->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
      }
    }

    // Short instructions read better centered above their controls, while
    // explanatory copy inside panels remains left aligned for readability.
    for (const auto* objectName : {"label_15", "portableExistsLabel",
                                   "gameSelectionHint", "label_14",
                                   "label_18", "label_17"}) {
      if (QLabel* label = page->findChild<QLabel*>(QString::fromLatin1(objectName))) {
        label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
      }
    }

    for (QCommandLinkButton* button :
         page->findChildren<QCommandLinkButton*>()) {
      if (button->isCheckable()) {
        button->setStyleSheet(choiceButtonStyle);
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
  // Reserve the largest page size once so the dialog does not grow or shrink
  // as the user moves through the wizard.
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
    ui->pages->setMinimumSize(largestPageSize);
    ui->pages->setMaximumHeight(largestPageSize.height());
    ui->pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    adjustSize();
    // The dialog layout can still request a wider window when a page fills in
    // game-specific heading text. Lock the outer frame as well as the stack.
    setFixedSize(size());
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
    dirs.push_back(createDir(ci.dataPath));
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
      InstanceManager::singleton().setCurrentInstance(
          Instance(ci.dataPath, ci.type == Portable));

      if (mustRestart) {
        ExitModOrganizer(Exit::Restart);
        m_switching = true;
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
