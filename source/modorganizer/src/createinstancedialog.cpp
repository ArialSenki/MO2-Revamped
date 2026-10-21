#include "createinstancedialog.h"
#include "fluenticons.h"
#include "createinstancedialogpages.h"
#include "eldenringsavesettings.h"
#include "instancemanager.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include "createinstancedialogui.h"
#include "envshortcut.h"
#include "fluentmotion.h"
#include "gameiconutils.h"
#include <iplugingame.h>
#include <utility.h>

#include <QLayout>
#include <QBoxLayout>
#include <QComboBox>
#include <QGuiApplication>
#include <QProgressBar>
#include <QScreen>
#include <QVariantAnimation>
#include <QCheckBox>
#include <QPushButton>
#include <QShortcut>
#include <QTextEdit>
#include <QCommandLinkButton>
#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextOption>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <QWindow>


using namespace MOBase;

class Failed
{};

namespace {

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
  // one size on every step, about the original wizard's, so the window
  // doesn't jump between steps; a page taller than the card scrolls
  setFixedSize(804, 640);
  // the Elden Ring plugin leaves the size to the wizard
  setProperty("mo2RevampedFitsPages", true);
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
  m_ready = true;

  ui->pages->setCurrentIndex(0);
  ui->launch->setChecked(true);

  if (!InstanceManager::singleton().hasAnyInstances()) {
    // first run of MO, there are no instances yet, force launch
    ui->launch->setEnabled(false);
  }

  if (m_pages[0]->skip()) {
    next();
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

void CreateInstanceDialog::showEvent(QShowEvent* e)
{
  QDialog::showEvent(e);

  // the theme is applied on the first show, and with it the real sizes; the
  // window then sits over the one that opened it, on that one's screen
  if (!m_shownOnce) {
    m_shownOnce = true;
    QTimer::singleShot(0, this, [this] {
      if (const QWidget* owner = parentWidget() ? parentWidget()->window() : nullptr) {
        QRect frame = frameGeometry();
        frame.moveCenter(owner->frameGeometry().center());
        if (const QScreen* sc = QGuiApplication::screenAt(owner->frameGeometry().center())) {
          const QRect area = sc->availableGeometry();
          frame.moveLeft(qBound(area.left(), frame.left(), area.right() - frame.width()));
          frame.moveTop(qBound(area.top(), frame.top(), area.bottom() - frame.height()));
        }
        move(frame.topLeft());
      }
    });
  }
}

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

    // what the user wrote on the name page, and the shortcut; an isolated
    // copy keeps its own settings, so they don't apply to it
    if (ci.type != Isolated) {
      const Instance created(ci.dataPath, ci.type == Portable);
      GlobalSettings::setInstanceValue(QStringLiteral("InstanceGroups"),
                                       created.instanceIdentifier(),
                                       ui->instanceGroup->currentText());
      GlobalSettings::setInstanceValue(QStringLiteral("InstanceNotes"),
                                       created.instanceIdentifier(),
                                       ui->instanceNotes->text());
      if (ui->desktopShortcut->isChecked() &&
          !env::instanceShortcut(created).add(env::Shortcut::Desktop)) {
        logCreation(tr("The desktop shortcut could not be added."));
      }
    }

    // launch the new instance
    if (ui->launch->isChecked()) {
      if (ci.type == Isolated) {
        // The copy cannot start while this MO2 holds the single-instance
        // lock, so close this one and start the copy's own executable.
        const QString executable =
            QDir(ci.dataPath).filePath(QStringLiteral("ModOrganizer.exe"));
        SetRestartExecutable(executable);
        if (ExitModOrganizer(Exit::Restart)) {
          m_switching = true;
        } else {
          SetRestartExecutable(QString());
          logCreation(tr("Close Mod Organizer, then start the isolated copy: %1")
                          .arg(QDir::toNativeSeparators(executable)));
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
    ui->creationLog->parentWidget()->show();
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
  FluentMotion::pageIn(ui->pages->currentWidget(), 10);
}

void CreateInstanceDialog::selectPageOf(const cid::Page* page)
{
  for (std::size_t i = 0; i < m_pages.size(); ++i) {
    if (m_pages[i].get() == page) {
      selectPage(i);
      return;
    }
  }
}

void CreateInstanceDialog::updateNavigation()
{
  if (!m_ready) {
    // a page asked before all of them were created
    return;
  }

  const auto i    = ui->pages->currentIndex();
  const auto last = isOnLastPage();

  // the steps that aren't skipped, and which one this is
  int step  = 0;
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
  ui->progress->setRange(0, qMax(steps, 1));
  ui->progress->setValue(step);
  ui->progress->setVisible(!m_singlePage);
  if (!m_singlePage) {
    ui->title->setText(tr("Step %1 of %2").arg(step).arg(steps));
  }

  ui->next->setEnabled(canNext());
  ui->back->setEnabled(canBack());
  ui->back->setVisible(!m_singlePage);

  if (last) {
    ui->next->setText(m_singlePage ? tr("Finish") : tr("Create instance"));
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
