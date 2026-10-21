#include "createinstancedialogpages.h"
#include "eldenringsavesettings.h"
#include "filesystemutilities.h"
#include "gameiconutils.h"
#include "instancemanager.h"
#include "plugincontainer.h"
#include "settings.h"
#include "settingsdialognexus.h"
#include "shared/appconfig.h"
#include "createinstancedialogui.h"
#include "fluenticons.h"
#include "fluentswitch.h"
#include <iplugingame.h>
#include <report.h>
#include <utility.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QLayout>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSet>
#include <QStorageInfo>
#include <QSize>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace cid
{

using namespace MOBase;
using MOBase::TaskDialog;

namespace
{

// Segoe Fluent Icons code points for the choices
constexpr char16_t HomeGlyph    = 0xE80F;
constexpr char16_t DriveGlyph   = 0xE88E;  // USB, a folder that travels
constexpr char16_t LockGlyph    = 0xE72E;
constexpr char16_t GameGlyph    = 0xE7FC;
constexpr char16_t EditionGlyph = 0xE8F1;  // Library

QIcon choiceIcon(char16_t glyph)
{
  return FluentIcons::glyph(glyph, FluentIcons::Tone::Accent);
}

QString decodeSteamPath(QString path)
{
  path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
  path.replace(QStringLiteral("\\\""), QStringLiteral("\""));
  return QDir::fromNativeSeparators(path);
}

QString normalizedInstallPath(const QString& path)
{
  return QDir::cleanPath(
             QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()))
      .toCaseFolded();
}

// whether a path is under one of the given environment variables' folders
bool underAnyOf(const QString& path, std::initializer_list<const char*> variables)
{
  const QString target = QDir::cleanPath(QDir::fromNativeSeparators(path)) + '/';
  for (const char* variable : variables) {
    const QString root = qEnvironmentVariable(variable);
    if (!root.isEmpty() &&
        target.startsWith(QDir::cleanPath(QDir::fromNativeSeparators(root)) + '/',
                          Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

// Program Files: Windows blocks writes there for programs that aren't
// elevated, which breaks mods and tools that write next to the game
bool inProtectedFolder(const QString& path)
{
  return underAnyOf(path, {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"});
}

// a folder OneDrive syncs, which can lock or revert files while a game runs
bool inOneDrive(const QString& path)
{
  return underAnyOf(path, {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"});
}

// launchers that live in a game's folder and that game plugins don't add to
// the executables themselves; the first path found is used
struct KnownTool
{
  const char* title;
  const char* paths[4];
};

const KnownTool KnownTools[] = {
    {"Seamless Co-op", {"Game/ersc_launcher.exe", "ersc_launcher.exe"}},
    {"ModEngine 2",
     {"modengine2_launcher.exe", "Game/modengine2_launcher.exe",
      "ModEngine2/modengine2_launcher.exe", "Game/ModEngine2/modengine2_launcher.exe"}},
};

// an instance that exists already, as the wizard shows it
struct ExistingInstance
{
  QString name;
  QString id;
  QString game;
  QString downloads;
  bool portable = false;
};

// every global and portable instance, with the game and downloads folder its
// INI names
std::vector<ExistingInstance> existingInstances()
{
  std::vector<ExistingInstance> list;
  const auto& manager = InstanceManager::singleton();
  auto add            = [&](const QString& path, bool portable) {
    Settings settings(QDir(path).filePath(QString::fromStdWString(AppConfig::iniFileName())));
    ExistingInstance i;
    const Instance instance(path, portable);
    i.name      = instance.displayName();
    i.id        = instance.instanceIdentifier();
    i.game      = settings.game().name().value_or(QString());
    i.downloads = QDir::toNativeSeparators(settings.paths().downloads());
    i.portable  = portable;
    list.push_back(std::move(i));
  };
  for (auto&& d : manager.globalInstancePaths()) {
    add(d, false);
  }
  for (auto&& d : manager.portableInstancePaths()) {
    add(d, true);
  }
  return list;
}

QString instanceRootForType(CreateInstanceDialog::Types type,
                            const InstanceManager& manager)
{
  switch (type) {
  case CreateInstanceDialog::Portable:
    return manager.portableInstancesRootPath();
  case CreateInstanceDialog::Isolated:
    return manager.isolatedInstancesRootPath();
  case CreateInstanceDialog::Global:
  case CreateInstanceDialog::NoType:
  default:
    return manager.globalInstancesRootPath();
  }
}

QStringList steamLibraries(const QString& steamRoot)
{
  QStringList libraries;
  QSet<QString> seen;
  auto addLibrary = [&](const QString& path) {
    if (path.trimmed().isEmpty()) {
      return;
    }

    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString key = normalized.toCaseFolded();
    if (!seen.contains(key) && QDir(normalized).exists()) {
      seen.insert(key);
      libraries.push_back(normalized);
    }
  };

  addLibrary(steamRoot);

  QFile file(QDir(steamRoot).filePath(QStringLiteral("steamapps/libraryfolders.vdf")));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return libraries;
  }

  const QString contents = QString::fromUtf8(file.readAll());
  const QRegularExpression pathEntry(
      QStringLiteral(R"rx("path"\s*"((?:\\.|[^"\\])*)")rx"));
  auto pathMatches = pathEntry.globalMatch(contents);
  while (pathMatches.hasNext()) {
    addLibrary(decodeSteamPath(pathMatches.next().captured(1)));
  }

  // Steam's older libraryfolders.vdf format stores paths under numeric keys.
  const QRegularExpression legacyPath(
      QStringLiteral(R"rx(^\s*"\d+"\s*"((?:\\.|[^"\\])*)")rx"),
      QRegularExpression::MultilineOption);
  auto legacyMatches = legacyPath.globalMatch(contents);
  while (legacyMatches.hasNext()) {
    addLibrary(decodeSteamPath(legacyMatches.next().captured(1)));
  }

  return libraries;
}

std::vector<QPair<QString, QString>> findEldenRingInstallations(
    MOBase::IPluginGame* game)
{
  std::vector<QPair<QString, QString>> installations;
  if (!game ||
      (game->gameName().compare(QStringLiteral("ELDEN RING"),
                                Qt::CaseInsensitive) != 0 &&
       game->steamAPPId() != QStringLiteral("1245620"))) {
    return installations;
  }

  auto addInstallation = [&](const QString& path, const QString& source) {
    if (path.isEmpty()) {
      return;
    }

    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!QDir(normalized).exists() || !game->looksValid(normalized)) {
      return;
    }

    const QString key = normalizedInstallPath(normalized);
    auto found = std::find_if(
        installations.begin(), installations.end(),
        [&key](const QPair<QString, QString>& installation) {
          return normalizedInstallPath(installation.first) == key;
        });
    if (found == installations.end()) {
      installations.emplace_back(normalized, source);
    } else if (source.startsWith(QStringLiteral("Steam"), Qt::CaseInsensitive)) {
      found->second = source;
    }
  };

  if (game->isInstalled()) {
    addInstallation(game->gameDirectory().absolutePath(),
                    QObject::tr("MO2 detected"));
  }

  QSettings steamRegistry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                          QSettings::NativeFormat);
  QString steamRoot = steamRegistry.value(QStringLiteral("SteamPath")).toString();
  if (steamRoot.isEmpty()) {
    const QString steamExe =
        steamRegistry.value(QStringLiteral("SteamExe")).toString();
    if (!steamExe.isEmpty()) {
      steamRoot = QFileInfo(steamExe).absolutePath();
    }
  }

  const QString appId = game->steamAPPId().isEmpty()
                            ? QStringLiteral("1245620")
                            : game->steamAPPId();
  const QStringList names = {game->gameName(), game->displayGameName(),
                             QStringLiteral("ELDEN RING"),
                             QStringLiteral("Elden Ring")};

  const QStringList libraries =
      steamRoot.isEmpty() ? QStringList() : steamLibraries(steamRoot);
  for (const QString& library : libraries) {
    const QString manifestPath = QDir(library).filePath(
        QStringLiteral("steamapps/appmanifest_%1.acf").arg(appId));
    QString installDirectory;
    QFile manifest(manifestPath);
    if (manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
      const QString contents = QString::fromUtf8(manifest.readAll());
      const QRegularExpression installDirEntry(
          QStringLiteral(R"rx("installdir"\s*"((?:\\.|[^"\\])*)")rx"));
      const auto match = installDirEntry.match(contents);
      if (match.hasMatch()) {
        installDirectory = decodeSteamPath(match.captured(1));
      }
    }

    if (!installDirectory.isEmpty()) {
      addInstallation(QDir(library).filePath(
                          QStringLiteral("steamapps/common/%1")
                              .arg(installDirectory)),
                      QObject::tr("Steam library"));
    }

    for (const QString& name : names) {
      addInstallation(QDir(library).filePath(
                          QStringLiteral("steamapps/common/%1").arg(name)),
                      QObject::tr("Steam library"));
    }
  }

  // Check common non-Steam locations without recursively scanning full drives.
  // Arbitrary folders remain available through the existing Browse action.
  const QStringList relativeRoots = {
      QString(), QStringLiteral("Games"), QStringLiteral("Juegos"),
      QStringLiteral("Modding"),
      QStringLiteral("Program Files/Steam/steamapps/common"),
      QStringLiteral("Program Files (x86)/Steam/steamapps/common"),
      QStringLiteral("SteamLibrary/steamapps/common"),
      QStringLiteral("Steam/steamapps/common")};
  for (const QFileInfo& drive : QDir::drives()) {
    const QString root = drive.absoluteFilePath();
    for (const QString& relativeRoot : relativeRoots) {
      const QString base = relativeRoot.isEmpty()
                               ? root
                               : QDir(root).filePath(relativeRoot);
      for (const QString& name : names) {
        addInstallation(QDir(base).filePath(name),
                        QObject::tr("Other installation"));
      }
    }
  }

  return installations;
}

void fitStackedWidgetToCurrentPage(QStackedWidget* stacked)
{
  // only the page shown counts for the stack's size
  if (stacked == nullptr) {
    return;
  }
  for (int i = 0; i < stacked->count(); ++i) {
    QWidget* page      = stacked->widget(i);
    const bool current = page == stacked->currentWidget();
    const auto policy  = current ? QSizePolicy::Preferred : QSizePolicy::Ignored;
    page->setSizePolicy(policy, policy);
  }
  stacked->setMinimumHeight(0);
  stacked->setMaximumHeight(QWIDGETSIZE_MAX);
  stacked->updateGeometry();
}

}  // namespace

// returns %base_dir%/dir
//
QString makeDefaultPath(const std::wstring& dir)
{
  return QDir::toNativeSeparators(
      PathSettings::makeDefaultPath(QString::fromStdWString(dir)));
}

QString toLocalizedString(CreateInstanceDialog::Types t)
{
  switch (t) {
  case CreateInstanceDialog::Global:
    return QObject::tr("Global");

  case CreateInstanceDialog::Portable:
    return QObject::tr("Portable");

  case CreateInstanceDialog::Isolated:
    return QObject::tr("Isolated");

  default:
    return QObject::tr("Instance type: %1").arg(QObject::tr("?"));
  }
}

PlaceholderLabel::PlaceholderLabel(QLabel* label)
    : m_label(label), m_original(label->text())
{}

void PlaceholderLabel::setText(const QString& arg)
{
  if (m_original.contains("%1")) {
    m_label->setText(m_original.arg(arg));
  }
}

void PlaceholderLabel::setVisible(bool b)
{
  m_label->setVisible(b);

  // warnings sit in a padded box under the rows; it shows only while one of
  // them does, so it leaves no gap otherwise
  QWidget* box = m_label->parentWidget();
  if (box != nullptr && box->objectName() == QLatin1String("wizardPadded")) {
    bool any = false;
    for (QLabel* l : box->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
      any |= !l->isHidden();
    }
    box->setVisible(any);
  }
}

Page::Page(CreateInstanceDialog& dlg)
    : ui(dlg.getUI()), m_dlg(dlg), m_pc(dlg.pluginContainer()), m_skip(false),
      m_firstActivation(true)
{}

bool Page::ready() const
{
  return true;
}

bool Page::skip() const
{
  // setSkip() overrides this if it's true
  return m_skip || doSkip();
}

bool Page::doSkip() const
{
  return false;
}

void Page::doActivated(bool)
{
  // no-op
}

void Page::activated()
{
  doActivated(m_firstActivation);
  m_firstActivation = false;
}

void Page::setSkip(bool b)
{
  m_skip = b;
}

void Page::updateNavigation()
{
  m_dlg.updateNavigation();
}

void Page::next()
{
  m_dlg.next();
}

bool Page::action(CreateInstanceDialog::Actions a)
{
  // no-op
  return false;
}

CreateInstanceDialog::Types Page::selectedInstanceType() const
{
  // no-op
  return CreateInstanceDialog::NoType;
}

IPluginGame* Page::selectedGame() const
{
  // no-op
  return nullptr;
}

QString Page::selectedGameLocation() const
{
  // no-op
  return {};
}

QString Page::selectedGameVariant(MOBase::IPluginGame*) const
{
  // no-op
  return {};
}

QString Page::selectedInstanceName() const
{
  // no-op
  return {};
}

CreateInstanceDialog::Paths Page::selectedPaths() const
{
  // no-op
  return {};
}

CreateInstanceDialog::ProfileSettings Page::profileSettings() const
{
  // no-op
  return {};
}

IntroPage::IntroPage(CreateInstanceDialog& dlg)
    : Page(dlg), m_skip(GlobalSettings::hideCreateInstanceIntro())
{
  QObject::connect(ui->hideIntro, &QCheckBox::toggled, [&] {
    GlobalSettings::setHideCreateInstanceIntro(ui->hideIntro->isChecked());
  });

  // how many there are, by storage, then by game, most first
  const auto instances = existingInstances();
  int portable         = 0;
  std::map<QString, int> perGame;
  for (const auto& i : instances) {
    portable += i.portable ? 1 : 0;
    if (!i.game.isEmpty()) {
      ++perGame[i.game];
    }
  }
  if (instances.empty()) {
    ui->introInstances->setText(QObject::tr("None yet: this will be the first one."));
    return;
  }
  std::vector<std::pair<QString, int>> games(perGame.begin(), perGame.end());
  std::stable_sort(games.begin(), games.end(), [](auto&& a, auto&& b) {
    return a.second > b.second;
  });
  QStringList names;
  for (const auto& [game, count] : games) {
    names.push_back(QStringLiteral("%1 (%2)").arg(game).arg(count));
  }
  const int global = static_cast<int>(instances.size()) - portable;
  ui->introInstances->setText(
      QObject::tr("%1 global, %2 portable").arg(global).arg(portable) +
      QStringLiteral(" \u00B7 ") + names.join(QStringLiteral(", ")));
}

bool IntroPage::doSkip() const
{
  return m_skip;
}

TypePage::TypePage(CreateInstanceDialog& dlg)
    : Page(dlg), m_type(CreateInstanceDialog::NoType)
{
  const bool isolatedEldenRing =
      InstanceManager::singleton().isEldenRingOnlyPortableMode();

  ui->portableExistsLabel->setVisible(false);
  ui->createGlobal->setIcon(choiceIcon(HomeGlyph));
  ui->createPortable->setIcon(choiceIcon(DriveGlyph));
  ui->createIsolated->setIcon(choiceIcon(LockGlyph));

  // each kind with the folder it goes to and the instances already there
  {
    const auto& manager  = InstanceManager::singleton();
    const auto instances = existingInstances();
    const auto portable =
        std::count_if(instances.begin(), instances.end(), [](auto&& i) {
          return i.portable;
        });
    auto where = [](const QString& path, qsizetype count) {
      const QString n = count == 1 ? QObject::tr("1 instance")
                                   : QObject::tr("%1 instances").arg(count);
      return QDir::toNativeSeparators(path) + QStringLiteral(" \u00B7 ") + n;
    };
    ui->createGlobal->setDescription(
        ui->createGlobal->description() + QStringLiteral("\n") +
        where(manager.globalInstancesRootPath(),
              static_cast<qsizetype>(instances.size()) - portable));
    ui->createPortable->setDescription(ui->createPortable->description() +
                                       QStringLiteral("\n") +
                                       where(manager.portableInstancesRootPath(), portable));
    ui->createIsolated->setDescription(
        ui->createIsolated->description() + QStringLiteral("\n") +
        QDir::toNativeSeparators(manager.isolatedInstancesRootPath()));
  }

  QObject::connect(ui->createGlobal, &QAbstractButton::clicked, [&] {
    global();
  });

  QObject::connect(ui->createPortable, &QAbstractButton::clicked, [&] {
    portable();
  });

  QObject::connect(ui->createIsolated, &QAbstractButton::clicked, [&] {
    isolated();
  });

  if (isolatedEldenRing) {
    ui->createGlobal->setVisible(false);
    ui->createIsolated->setVisible(false);
    ui->typeComparison->setVisible(false);
    ui->portableExistsLabel->setVisible(false);
    ui->createPortable->setText(QCoreApplication::translate(
        "cid::TypePage", "Use this isolated portable setup"));
    ui->createPortable->setDescription(
        QCoreApplication::translate(
            "cid::TypePage",
            "Stores this instance with the MO2 installation. Other games and "
            "global instances are not used."));
    ui->createPortable->setChecked(true);
    m_type = CreateInstanceDialog::Portable;
  }
}

bool TypePage::ready() const
{
  return (m_type != CreateInstanceDialog::NoType);
}

CreateInstanceDialog::Types TypePage::selectedInstanceType() const
{
  return m_type;
}

void TypePage::global()
{
  m_type = CreateInstanceDialog::Global;

  ui->createGlobal->setChecked(true);
  ui->createPortable->setChecked(false);
  ui->createIsolated->setChecked(false);

  next();
}

void TypePage::portable()
{
  m_type = CreateInstanceDialog::Portable;

  ui->createGlobal->setChecked(false);
  ui->createPortable->setChecked(true);
  ui->createIsolated->setChecked(false);

  next();
}

void TypePage::isolated()
{
  m_type = CreateInstanceDialog::Isolated;

  ui->createGlobal->setChecked(false);
  ui->createPortable->setChecked(false);
  ui->createIsolated->setChecked(true);

  next();
}

GamePage::Game::Game(IPluginGame* g) : game(g), installed(g->isInstalled())
{
  if (installed) {
    dir = game->gameDirectory().path();
  }

  for (const auto& installation : findEldenRingInstallations(game)) {
    installations.push_back({installation.first, installation.second});
  }
  if (!installations.empty()) {
    installed = true;
    dir = installations.front().path;
  }
}

GamePage::GamePage(CreateInstanceDialog& dlg) : Page(dlg), m_selection(nullptr)
{}

void GamePage::doActivated(bool firstTime)
{
  if (firstTime) {
    auto* browseButton =
        m_dlg.findChild<QPushButton*>(QStringLiteral("browseGameFolderButton"));
    auto* gamesFilter =
        m_dlg.findChild<QLineEdit*>(QStringLiteral("gamesFilter"));
    auto* showAllGames =
        m_dlg.findChild<QCheckBox*>(QStringLiteral("showAllGames"));
    auto* games = m_dlg.findChild<QWidget*>(QStringLiteral("games"));
    auto* selectionHint =
        m_dlg.findChild<QLabel*>(QStringLiteral("gameSelectionHint"));

    if (browseButton != ui->browseGameFolderButton ||
        gamesFilter != ui->gamesFilter || showAllGames != ui->showAllGames ||
        games != ui->games || selectionHint != ui->gameSelectionHint) {
      log::warn("Create Instance wizard: refreshed game-page widget references from the dialog tree");
    }

    ui->browseGameFolderButton = browseButton;
    ui->gamesFilter = gamesFilter;
    ui->showAllGames = showAllGames;
    ui->games = games;
    ui->gameSelectionHint = selectionHint;

    // A new wizard always starts with detected installations only. Keep the
    // filter state in GamePage so list rebuilding never depends on a stale UI
    // reference or an implicit Designer default.
    m_showAllGames = false;
    if (ui->showAllGames != nullptr) {
      ui->showAllGames->setChecked(false);
    }

    if (ui->browseGameFolderButton != nullptr) {
      ui->browseGameFolderButton->setAutoDefault(false);
      QObject::connect(ui->browseGameFolderButton, &QPushButton::clicked,
                       &m_dlg, [this] { selectCustom(); });
    } else {
      log::error("Create Instance wizard: game-folder browse button is missing");
    }

    if (ui->gamesFilter != nullptr) {
      ui->gamesFilter->setToolTip(
          QObject::tr("Filter supported games by name."));
      QObject::connect(ui->gamesFilter, &QLineEdit::textChanged, &m_dlg,
                       [this] { fillList(); });
    } else {
      log::error("Create Instance wizard: game filter is missing");
    }

    if (ui->showAllGames != nullptr) {
      ui->showAllGames->setTristate(false);
      QObject::connect(ui->showAllGames, &QCheckBox::toggled, &m_dlg,
                       [this](bool showAll) {
                         m_showAllGames = showAll;
                         if (!showAll && m_selection != nullptr &&
                             !m_selection->installed && !m_isolatedModeActive) {
                           m_selection = nullptr;
                         }
                         fillList();
                         selectButton(m_selection);
                         updateNavigation();
                       });
    } else {
      log::error("Create Instance wizard: show-all-games option is missing");
    }
  }

  if (ui->games == nullptr || ui->games->layout() == nullptr) {
    log::error("Create Instance wizard: game list container or layout is missing");
    return;
  }

  const bool isolatedCopy =
      m_dlg.rawCreationInfo().type == CreateInstanceDialog::Isolated;
  auto* previousPlugin = m_selection ? m_selection->game : nullptr;
  const QString previousPath = m_selection ? m_selection->dir : QString{};

  // Keep the user's Show all choice independent from the instance type. The
  // isolated mode filters the game list to Elden Ring and includes that card
  // even when no installation has been detected.
  m_isolatedModeActive = isolatedCopy;

  if (ui->showAllGames != nullptr) {
    ui->showAllGames->setVisible(!isolatedCopy);
  }
  if (ui->gamesFilter != nullptr) {
    ui->gamesFilter->setVisible(!isolatedCopy);
  }
  if (ui->gameSelectionHint != nullptr) {
    ui->gameSelectionHint->setText(
        isolatedCopy
            ? QObject::tr("Isolated MO2 copies currently support Elden Ring. Choose its installed folder to continue.")
            : QObject::tr("Choose an installed game, or browse to its folder. The selected game and path are shown on each card."));
  }

  // instances that already manage each game, shown on its row
  m_instanceCounts.clear();
  {
    const auto& manager = InstanceManager::singleton();
    std::vector<QString> paths;
    for (auto&& d : manager.globalInstancePaths()) {
      paths.push_back(d);
    }
    for (auto&& d : manager.portableInstancePaths()) {
      paths.push_back(d);
    }
    for (const auto& path : paths) {
      Settings settings(QDir(path).filePath(QString::fromStdWString(AppConfig::iniFileName())));
      if (const auto name = settings.game().name(); name && !name->isEmpty()) {
        ++m_instanceCounts[name->toCaseFolded()];
      }
    }
  }

  clearButtons();
  m_selection = nullptr;
  createGames();
  if (previousPlugin != nullptr) {
    if (Game* previous = findGame(previousPlugin)) {
      previous->dir = previousPath;
      previous->installed = !previousPath.isEmpty();
      m_selection = previous;
    }
  }
  fillList();
  if (m_selection != nullptr) {
    selectButton(m_selection);
  }
}

bool GamePage::ready() const
{
  return (m_selection != nullptr);
}

bool GamePage::action(CreateInstanceDialog::Actions a)
{
  using Actions = CreateInstanceDialog::Actions;

  if (a == Actions::Find) {
    if (ui->gamesFilter != nullptr) {
      ui->gamesFilter->setFocus();
    }
    return true;
  }

  return false;
}

IPluginGame* GamePage::selectedGame() const
{
  if (!m_selection) {
    return nullptr;
  }

  return m_selection->game;
}

QString GamePage::selectedGameLocation() const
{
  if (!m_selection) {
    return {};
  }

  return QDir::toNativeSeparators(m_selection->dir);
}

void GamePage::select(IPluginGame* game, const QString& dir)
{
  Game* checked = findGame(game);

  if (checked) {
    if (!dir.isEmpty()) {
      if (detectMicrosoftStore(dir) && !confirmMicrosoftStore(dir, checked->game)) {
        checked = nullptr;
      } else {
        checked->dir       = dir;
        checked->installed = true;
      }
    } else if (!checked->installations.empty()) {
      if (checked->installations.size() > 1) {
        QStringList choices;
        for (const auto& installation : checked->installations) {
          choices.push_back(QStringLiteral("%1 — %2")
                                .arg(installation.source, installation.path));
        }

        bool accepted = false;
        const QString choice = QInputDialog::getItem(
            &m_dlg, QObject::tr("Choose game installation"),
            QObject::tr("Several Elden Ring installations were found. Choose the one this instance should use:"),
            choices, 0, false, &accepted);
        const int selected = choices.indexOf(choice);
        if (!accepted || selected < 0) {
          checked = nullptr;
        } else {
          const QString selectedPath = checked->installations.at(selected).path;
          if (detectMicrosoftStore(selectedPath) &&
              !confirmMicrosoftStore(selectedPath, checked->game)) {
            checked = nullptr;
          } else {
            checked->dir = selectedPath;
          }
        }
      } else {
        checked->dir = checked->installations.front().path;
      }

      if (checked) {
        checked->installed = true;
      }
    } else if (!checked->installed || checked->dir.isEmpty() ||
               !checked->game->looksValid(checked->dir)) {
      // The plugin has no valid automatic path, so let the user browse.
      const auto path = QFileDialog::getExistingDirectory(
          &m_dlg,
          QObject::tr("Find game installation for %1").arg(game->displayGameName()));

      if (path.isEmpty()) {
        checked = nullptr;
      } else if (detectMicrosoftStore(path) && !confirmMicrosoftStore(path, game)) {
        checked = nullptr;
      } else {
        // Check whether a plugin supports the chosen directory.
        checked = checkInstallation(path, checked);
        if (checked) {
          checked->dir       = path;
          checked->installed = true;
        }
      }
    } else if (detectMicrosoftStore(checked->dir) &&
               !confirmMicrosoftStore(checked->dir, checked->game)) {
      checked = nullptr;
    }
  }

  // select this plugin, if any
  m_selection = checked;

  // update the button associated with it in case the paths have changed
  updateButton(checked);

  // toggle it on
  selectButton(checked);

  updateNavigation();

  if (checked) {
    // automatically move to the next page when a game is selected
    next();
  }
}

void GamePage::selectCustom()
{
  const auto path =
      QFileDialog::getExistingDirectory(&m_dlg, QObject::tr("Find game installation"));

  if (path.isEmpty()) {
    // reselect the previous button
    selectButton(m_selection);
    return;
  }

  // Microsoft store games are not supported
  if (detectMicrosoftStore(path) && !confirmMicrosoftStore(path, nullptr)) {
    // reselect the previous button
    selectButton(m_selection);
    return;
  }

  // try to find a plugin that likes this directory
  for (auto& g : m_games) {
    if (g->game->looksValid(path)) {
      // found one
      g->dir       = path;
      g->installed = true;

      // select it
      select(g->game, path);

      // update the button because the path has changed
      updateButton(g.get());

      return;
    }
  }

  // warning to the user
  warnUnrecognized(path);

  // reselect the previous button
  selectButton(m_selection);
}

void GamePage::warnUnrecognized(const QString& path)
{
  // put the list of supported games in the details textbox
  QString supportedGames;
  for (auto* game : sortedGamePlugins()) {
    supportedGames += game->displayGameName() + "\n";
  }

  QMessageBox dlg(&m_dlg);

  dlg.setWindowTitle(QObject::tr("Unrecognized game"));
  dlg.setText(
      QObject::tr("The folder %1 does not seem to contain a game Mod Organizer can "
                  "manage.")
          .arg(path));
  dlg.setInformativeText(QObject::tr("See details for the list of supported games."));
  dlg.setDetailedText(supportedGames);
  dlg.setIcon(QMessageBox::Warning);
  dlg.setStandardButtons(QMessageBox::Ok);

  dlg.exec();
}

std::vector<IPluginGame*> GamePage::sortedGamePlugins() const
{
  std::vector<IPluginGame*> v;

  // GamePage builds its initial list before it has been appended to the
  // dialog's page collection. Use the mode cached by doActivated() instead
  // of asking the dialog to gather a partial CreationInfo here.
  // all game plugins
  for (auto* game : m_pc.plugins<IPluginGame>()) {
    if (m_isolatedModeActive &&
        game->gameShortName().compare(QStringLiteral("eldenring"),
                                      Qt::CaseInsensitive) != 0) {
      continue;
    }

    if (game->displayGameName().trimmed().isEmpty() &&
        game->gameName().trimmed().isEmpty() &&
        game->gameShortName().trimmed().isEmpty()) {
      log::warn("Create Instance wizard: skipped a game plugin with no display name");
      continue;
    }
    v.push_back(game);
  }

  // natsort
  std::sort(v.begin(), v.end(), [](auto* a, auto* b) {
    return (naturalCompare(a->displayGameName(), b->displayGameName()) < 0);
  });

  return v;
}

void GamePage::createGames()
{
  m_games.clear();

  for (auto* game : sortedGamePlugins()) {
    m_games.push_back(std::make_unique<Game>(game));
  }
}

GamePage::Game* GamePage::findGame(IPluginGame* game)
{
  for (auto& g : m_games) {
    if (g->game == game) {
      return g.get();
    }
  }

  return nullptr;
}

void GamePage::createGameButton(Game* g)
{
  g->button = new QCommandLinkButton;
  g->button->setObjectName(QStringLiteral("gameChoice"));
  g->button->setCheckable(true);
  g->button->setAutoDefault(false);
  g->button->setIconSize(QSize(28, 28));
  g->button->setCursor(Qt::PointingHandCursor);

  updateButton(g);

  QObject::connect(g->button, &QAbstractButton::clicked, [g, this] {
    select(g->game);
  });
}

void GamePage::addButton(QAbstractButton* b)
{
  if (ui->games == nullptr || ui->games->layout() == nullptr) {
    return;
  }

  auto* ly = static_cast<QVBoxLayout*>(ui->games->layout());

  // insert before the stretch
  ly->insertWidget(ly->count() - 1, b);
}

void GamePage::updateButton(Game* g)
{
  if (!g || !g->button) {
    return;
  }

  QString displayName = g->game->displayGameName().trimmed();
  if (displayName.isEmpty()) {
    displayName = g->game->gameName().trimmed();
  }
  if (displayName.isEmpty()) {
    displayName = g->game->gameShortName().trimmed();
  }
  g->button->setText(displayName.replace("&", "&&"));
  QIcon gameIcon = g->game->gameIcon();
  if (gameIcon.isNull()) {
    gameIcon = choiceIcon(GameGlyph);
  }
  gameIcon = MOBase::makeRunStyleGameIcon(gameIcon, g->button,
                                          g->button->iconSize());
  g->button->setIcon(gameIcon);

  QStringList description;
  if (g->installed) {
    for (const auto& installation : g->installations) {
      if (normalizedInstallPath(installation.path) == normalizedInstallPath(g->dir) &&
          !installation.source.isEmpty()) {
        description << installation.source;
        break;
      }
    }
    description << QDir::toNativeSeparators(g->dir);
    if (inProtectedFolder(g->dir)) {
      description << QObject::tr("In Program Files");
    }
  } else {
    description << QObject::tr("Not installed");
  }

  // so a second instance of the same game is a choice, not an accident
  const auto count = m_instanceCounts.find(g->game->gameName().toCaseFolded());
  if (count != m_instanceCounts.end() && count->second > 0) {
    description << (count->second == 1
                        ? QObject::tr("1 instance already")
                        : QObject::tr("%1 instances already").arg(count->second));
  }
  g->button->setDescription(description.join(QStringLiteral(" \u00B7 ")));
}

void GamePage::selectButton(Game* g)
{
  // go through each game, set the button that is for game `g` as active;
  // some button might not exist, which happens when selecting a custom
  // folder for a game that was considered uninstalled

  for (const auto& gg : m_games) {
    if (!g) {
      // nothing should be selected
      if (gg->button) {
        gg->button->setChecked(false);
      }

      continue;
    }

    if (gg->game == g->game) {
      // this is the button that should be selected

      if (!gg->button) {
        // this happens when the button wasn't visible because the game
        // was not installed; create it and show it
        // and it has a button, just check it
        createGameButton(gg.get());
        addButton(gg->button);
      }

      gg->button->setChecked(true);
    } else {
      // this is not the button you're looking for
      if (gg->button) {
        gg->button->setChecked(false);
      }
    }
  }
}

void GamePage::clearButtons()
{
  if (ui->games == nullptr || ui->games->layout() == nullptr) {
    return;
  }

  auto* ly = static_cast<QVBoxLayout*>(ui->games->layout());

  ui->games->setUpdatesEnabled(false);

  // Remove each layout item and explicitly delete its widget. Game buttons
  // have an object name, so clearing only unnamed direct children can leave
  // stale cards behind after the visible list is rebuilt.
  while (auto* child = ly->takeAt(0)) {
    if (auto* widget = child->widget()) {
      delete widget;
    }
    delete child;
  }

  // add a stretch, buttons will be added before
  ly->addStretch();

  ui->games->setUpdatesEnabled(true);

  for (auto& g : m_games) {
    // all buttons have been deleted
    g->button = nullptr;
  }
}

void GamePage::fillList()
{
  // GamePage is populated from doActivated(), after the complete dialog has
  // been constructed. Avoid reading the filter widget during page creation.
  if (ui->games == nullptr || ui->games->layout() == nullptr) {
    log::error("Create Instance wizard: skipped game list population because its container is missing");
    return;
  }

  // Read the visible control each time the list is rebuilt. This keeps the
  // rendered list synchronized with the checkbox after keyboard, mouse, and
  // programmatic state changes, instead of relying only on a cached flag.
  m_showAllGames = ui->showAllGames != nullptr &&
                   ui->showAllGames->isChecked();
  const bool showAll = !m_isolatedModeActive && m_showAllGames;
  const QString query = m_isolatedModeActive
                            ? QString{}
                            : (ui->gamesFilter != nullptr
                                   ? ui->gamesFilter->text().trimmed()
                                   : QString{});

  clearButtons();

  for (auto& g : m_games) {
    if (!showAll && !m_isolatedModeActive && !g->installed) {
      // not installed
      continue;
    }

    if (!query.isEmpty() &&
        !g->game->gameName().contains(query, Qt::CaseInsensitive) &&
        !g->game->displayGameName().contains(query, Qt::CaseInsensitive)) {
      // filtered out
      continue;
    }

    createGameButton(g.get());
    addButton(g->button);
  }

  updateProtectedWarning();
}

void GamePage::updateProtectedWarning()
{
  const bool show = m_selection != nullptr && m_selection->installed &&
                    inProtectedFolder(m_selection->dir);
  if (show) {
    QString name = m_selection->game->displayGameName().trimmed();
    if (name.isEmpty()) {
      name = m_selection->game->gameName();
    }
    ui->gameProtectedWarning->setText(
        QCoreApplication::translate(
            "CreateInstanceDialog",
            "%1 is in Program Files. Windows protects that folder, and some mods and "
            "tools fail when they write to it. Moving the game to another library avoids "
            "it (in Steam: Properties, Installed Files, Move install folder).")
            .arg(name));
  }
  ui->gameProtectedWarning->setVisible(show);
  ui->gameProtectedWarning->parentWidget()->setVisible(show);
}

GamePage::Game* GamePage::checkInstallation(const QString& path, Game* g)
{
  if (g->game->looksValid(path)) {
    // okay
    return g;
  }

  if (detectMicrosoftStore(path) && confirmMicrosoftStore(path, g->game)) {
    // okay
    return g;
  }

  // the selected game can't use that folder, find another one
  IPluginGame* otherGame = nullptr;

  for (auto* gg : m_pc.plugins<IPluginGame>()) {
    if (m_isolatedModeActive &&
        gg->gameShortName().compare(QStringLiteral("eldenring"),
                                    Qt::CaseInsensitive) != 0) {
      continue;
    }
    if (gg->looksValid(path)) {
      otherGame = gg;
      break;
    }
  }

  if (otherGame == g->game) {
    // shouldn't happen, but okay
    return g;
  }

  if (otherGame) {
    // an alternative was found, ask the user about it
    auto* confirmedGame = confirmOtherGame(path, g->game, otherGame);

    if (!confirmedGame) {
      // cancelled
      return nullptr;
    }

    // make it look like the user clicked that button instead
    g = findGame(confirmedGame);
    if (!g) {
      return nullptr;
    }
  } else {
    // nothing can manage this, but the user can override
    if (!confirmUnknown(path, g->game)) {
      // cancelled
      return nullptr;
    }
  }

  // remember this path
  g->dir       = path;
  g->installed = true;

  updateButton(g);

  return g;
}

bool GamePage::detectMicrosoftStore(const QString& path)
{
  return path.contains("/ModifiableWindowsApps/") || path.contains("/WindowsApps/");
}

bool GamePage::confirmMicrosoftStore(const QString& path, IPluginGame* game)
{
  const auto r =
      TaskDialog(&m_dlg)
          .title(QObject::tr("Microsoft Store game"))
          .main(QObject::tr("Microsoft Store game"))
          .content(
              QObject::tr(
                  "The folder %1 seems to be a Microsoft Store game install.  Games"
                  " installed through the Microsoft Store are not supported by Mod "
                  "Organizer"
                  " and will not work properly.")
                  .arg(path))
          .button(
              {game ? QObject::tr("Use this folder for %1").arg(game->displayGameName())
                    : QObject::tr("Use this folder"),
               QObject::tr("I know what I'm doing"), QMessageBox::Ignore})
          .button({QObject::tr("Cancel"), QMessageBox::Cancel})
          .exec();

  return (r == QMessageBox::Ignore);
}

bool GamePage::confirmUnknown(const QString& path, IPluginGame* game)
{
  const auto r =
      TaskDialog(&m_dlg)
          .title(QObject::tr("Unrecognized game"))
          .main(QObject::tr("Unrecognized game"))
          .content(
              QObject::tr("The folder %1 does not seem to contain an installation for "
                          "<span style=\"white-space: nowrap; font-weight: "
                          "bold;\">%2</span> or "
                          "for any other game Mod Organizer can manage.")
                  .arg(path)
                  .arg(game->displayGameName()))
          .button({QObject::tr("Use this folder for %1").arg(game->displayGameName()),
                   QObject::tr("I know what I'm doing"), QMessageBox::Ignore})
          .button({QObject::tr("Cancel"), QMessageBox::Cancel})
          .exec();

  return (r == QMessageBox::Ignore);
}

IPluginGame* GamePage::confirmOtherGame(const QString& path, IPluginGame* selectedGame,
                                        IPluginGame* guessedGame)
{
  const auto r =
      TaskDialog(&m_dlg)
          .title(QObject::tr("Incorrect game"))
          .main(QObject::tr("Incorrect game"))
          .content(
              QObject::tr(
                  "The folder %1 seems to contain an installation for "
                  "<span style=\"white-space: nowrap; font-weight: bold;\">%2</span>, "
                  "not "
                  "<span style=\"white-space: nowrap; font-weight: bold;\">%3</span>.")
                  .arg(path)
                  .arg(guessedGame->displayGameName())
                  .arg(selectedGame->displayGameName()))
          .button({QObject::tr("Manage %1 instead").arg(guessedGame->displayGameName()),
                   QMessageBox::Ok})
          .button({QObject::tr("Use this folder for %1")
                       .arg(selectedGame->displayGameName()),
                   QObject::tr("I know what I'm doing"), QMessageBox::Ignore})
          .button({QObject::tr("Cancel"), QMessageBox::Cancel})
          .exec();

  switch (r) {
  case QMessageBox::Ok:
    return guessedGame;

  case QMessageBox::Ignore:
    return selectedGame;

  case QMessageBox::Cancel:
  default:
    return nullptr;
  }
}

VariantsPage::VariantsPage(CreateInstanceDialog& dlg)
    : Page(dlg), m_previousGame(nullptr)
{}

bool VariantsPage::ready() const
{
  // note that this isn't called when doSkip() is true, which happens when
  // the game has no variants

  return !m_selection.isEmpty();
}

bool VariantsPage::doSkip() const
{
  auto* g = m_dlg.rawCreationInfo().game;
  if (!g) {
    // shouldn't happen
    return true;
  }

  return (g->gameVariants().size() < 2);
}

void VariantsPage::doActivated(bool)
{
  auto* g = m_dlg.rawCreationInfo().game;

  if (m_previousGame != g) {
    // recreate the list, the game has changed
    m_previousGame = g;
    m_selection    = "";
    fillList();
  }
}

void VariantsPage::select(const QString& variant)
{
  m_selection = variant;

  // find the button, set it checked
  for (auto* b : m_buttons) {
    if (b->text() == variant) {
      b->setChecked(true);
    } else {
      b->setChecked(false);
    }
  }

  updateNavigation();

  if (!m_selection.isEmpty()) {
    // automatically move to the next page when a variant is selected
    next();
  }
}

QString VariantsPage::selectedGameVariant(MOBase::IPluginGame* game) const
{
  if (!game) {
    return {};
  }

  if (game->gameVariants().size() < 2) {
    return {};
  } else {
    return m_selection;
  }
}

void VariantsPage::fillList()
{
  ui->editions->clear();
  m_buttons.clear();

  auto* g = m_dlg.rawCreationInfo().game;
  if (!g) {
    // shouldn't happen
    return;
  }

  // for each variant, create a checkable button and add it
  for (auto& v : g->gameVariants()) {
    auto* b = new QCommandLinkButton(v);
    b->setCheckable(true);
    b->setAutoDefault(false);
    b->setObjectName(QStringLiteral("editionChoice"));
    b->setProperty("wizardChoice", true);
    b->setIcon(choiceIcon(EditionGlyph));
    b->setIconSize(QSize(24, 24));
    b->setCursor(Qt::PointingHandCursor);

    QObject::connect(b, &QAbstractButton::clicked, [v, this] {
      select(v);
    });

    ui->editions->addButton(b, QDialogButtonBox::AcceptRole);
    m_buttons.push_back(b);
  }

}

NamePage::NamePage(CreateInstanceDialog& dlg)
    : Page(dlg), m_modified(false), m_okay(false),
      m_exists(ui->instanceNameExists), m_invalid(ui->instanceNameInvalid)
{
  // Dynamic names and folder paths can exceed the wizard's fixed width.
  // Wrap validation text so it stays inside the warning card.
  ui->instanceNameExists->setWordWrap(true);
  ui->instanceNameInvalid->setWordWrap(true);

  QObject::connect(ui->instanceName, &QLineEdit::textEdited, [&] {
    onChanged();
  });

  QObject::connect(ui->instanceName, &QLineEdit::returnPressed, [&] {
    next();
  });

  QStringList groups;
  const auto& manager = InstanceManager::singleton();
  auto addGroupOf     = [&](const QString& path, bool portable) {
    const QString group = GlobalSettings::instanceValue(
        QStringLiteral("InstanceGroups"), Instance(path, portable).instanceIdentifier());
    if (!group.isEmpty() && !groups.contains(group, Qt::CaseInsensitive)) {
      groups.push_back(group);
    }
  };
  for (auto&& d : manager.globalInstancePaths()) {
    addGroupOf(d, false);
  }
  for (auto&& d : manager.portableInstancePaths()) {
    addGroupOf(d, true);
  }
  groups.sort(Qt::CaseInsensitive);
  ui->instanceGroup->addItems(groups);
  ui->instanceGroup->setCurrentIndex(-1);
  ui->instanceGroup->lineEdit()->setPlaceholderText(QObject::tr("No group"));
}

bool NamePage::ready() const
{
  // checked when textboxes change or when the page is activated
  return m_okay;
}

void NamePage::doActivated(bool)
{
  auto* g = m_dlg.rawCreationInfo().game;
  if (!g) {
    // shouldn't happen, next should be disabled
    return;
  }

  const auto type = m_dlg.rawCreationInfo().type;
  ui->instanceGroup->parentWidget()->setVisible(type != CreateInstanceDialog::Isolated);
  ui->instanceNotes->parentWidget()->setVisible(type != CreateInstanceDialog::Isolated);
  ui->desktopShortcut->parentWidget()->setVisible(type != CreateInstanceDialog::Isolated);

  // the instances that manage this game already, with their group, so the
  // new one gets a name of its own
  {
    QStringList siblings;
    for (const auto& i : existingInstances()) {
      if (i.game.compare(g->gameName(), Qt::CaseInsensitive) != 0) {
        continue;
      }
      QString text = i.name;
      const QString group =
          GlobalSettings::instanceValue(QStringLiteral("InstanceGroups"), i.id);
      if (!group.isEmpty()) {
        text += QStringLiteral(" (") + group + QStringLiteral(")");
      }
      siblings.push_back(text);
    }
    ui->instanceSiblings->setText(siblings.join(QStringLiteral(" \u00B7 ")));
    ui->instanceSiblings->parentWidget()->setVisible(
        !siblings.isEmpty() && type != CreateInstanceDialog::Isolated);
  }
  // The heading and live folder preview already explain the name's purpose.
  // Keep the extra paragraph hidden so this step stays focused and compact.
  ui->instanceNameContext->setVisible(false);
  if (type == CreateInstanceDialog::Isolated) {
    ui->instanceNameLabel->setText(plainHeading(
        QCoreApplication::translate("cid::NamePage",
                                    "<h3>Name your isolated MO2 copy for %1.</h3>")
            .arg(g->gameName())));
  } else {
    const QString typeName =
        type == CreateInstanceDialog::Portable
            ? QCoreApplication::translate("cid::NamePage", "portable")
            : QCoreApplication::translate("cid::NamePage", "global");
    ui->instanceNameLabel->setText(plainHeading(
        QCoreApplication::translate("cid::NamePage",
                                    "<h3>Name this %1 instance for %2.</h3>")
            .arg(typeName, g->gameName())));
  }

  const auto& manager = InstanceManager::singleton();
  if (manager.isEldenRingOnlyPortableMode()) {
    ui->instanceNameLabel->setText(plainHeading(
        QCoreApplication::translate(
            "cid::NamePage",
            "<h3>This setup will create one isolated portable instance for %1.</h3>")
            .arg(g->gameName())));
    ui->instanceName->setVisible(false);
    ui->instanceNameContext->setVisible(false);
    m_exists.setVisible(false);
    m_invalid.setVisible(false);
    ui->instanceNamePath->setText(
        QCoreApplication::translate("cid::NamePage", "Storage folder: %1")
            .arg(QDir::toNativeSeparators(manager.portablePath())));
    m_okay = true;
    updateNavigation();
    return;
  }

  const QString parentDirectory = instanceRootForType(type, manager);

  // generate a name if the user hasn't changed the text in case the game
  // changed, or if it's empty
  if (!m_modified || ui->instanceName->text().isEmpty()) {
    const auto n = manager.makeUniqueName(g->gameName(), parentDirectory);
    ui->instanceName->setText(n);
    m_modified = false;
  }

  updatePathHint();
  verify();
  fillPrograms();
}

void NamePage::fillPrograms()
{
  const auto ci = m_dlg.rawCreationInfo();
  auto* layout  = ui->detectedPrograms->layout();

  // the switches keep their state while the same launcher is offered again
  QSet<QString> off;
  for (auto* box : ui->detectedPrograms->findChildren<QCheckBox*>()) {
    if (!box->isChecked()) {
      off.insert(box->property("toolBinary").toString());
    }
  }
  while (auto* item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  auto addRow = [&](const QString& title, const QString& description) {
    auto* row = new QFrame(ui->detectedPrograms);
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(20, 10, 16, 10);
    rowLayout->setSpacing(16);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
    auto* descriptionLabel = new QLabel(description, row);
    descriptionLabel->setObjectName(QStringLiteral("fluentSettingDescription"));
    descriptionLabel->setWordWrap(true);
    text->addWidget(titleLabel);
    text->addWidget(descriptionLabel);
    rowLayout->addLayout(text, 1);
    layout->addWidget(row);
    return rowLayout;
  };

  bool found = false;
  if (ci.type != CreateInstanceDialog::Isolated && !ci.gameLocation.isEmpty()) {
    const QDir dir(ci.gameLocation);
    for (const auto& tool : KnownTools) {
      for (const char* path : tool.paths) {
        if (path == nullptr) {
          break;
        }
        const QString binary = dir.filePath(QString::fromLatin1(path));
        if (!QFileInfo(binary).isFile()) {
          continue;
        }
        if (!found) {
          // the section's title and what the switches do
          QStringList always;
          if (ci.game != nullptr) {
            for (const auto& info : ci.game->executables()) {
              if (info.isValid()) {
                always.push_back(info.title());
              }
            }
          }
          addRow(QObject::tr("Programs found"),
                 always.isEmpty()
                     ? QObject::tr("The ones turned on are added to Executables.")
                     : QObject::tr("The ones turned on are added to Executables, after %1.")
                           .arg(always.join(QStringLiteral(", "))));
          found = true;
        }
        auto* rowLayout = addRow(QString::fromLatin1(tool.title),
                                 QDir::toNativeSeparators(QString::fromLatin1(path)));
        auto* box = new QCheckBox(rowLayout->parentWidget());
        box->setProperty("fluentSwitch", true);
        box->setProperty("toolBinary", QDir::toNativeSeparators(binary));
        box->setProperty("toolTitle", QString::fromLatin1(tool.title));
        box->setAccessibleName(QObject::tr("Add %1").arg(QString::fromLatin1(tool.title)));
        box->setCursor(Qt::PointingHandCursor);
        box->setChecked(!off.contains(QDir::toNativeSeparators(binary)));
        if (FluentIcons::active()) {
          FluentSwitch::animate(box);
        }
        rowLayout->addWidget(box, 0, Qt::AlignVCenter);
        break;
      }
    }
  }
  ui->detectedPrograms->setVisible(found);
}

QString NamePage::selectedInstanceName() const
{
  if (!m_okay) {
    return {};
  }

  if (InstanceManager::singleton().isEldenRingOnlyPortableMode()) {
    return {};
  }

  const auto text = ui->instanceName->text().trimmed();
  return MOBase::sanitizeFileName(text);
}

void NamePage::onChanged()
{
  m_modified = true;
  updatePathHint();
  verify();
}

void NamePage::verify()
{
  const auto& manager = InstanceManager::singleton();
  const auto root =
      instanceRootForType(m_dlg.rawCreationInfo().type, manager);
  m_okay          = checkName(root, ui->instanceName->text());
  updateNavigation();
}

void NamePage::updatePathHint()
{
  const auto& manager = InstanceManager::singleton();
  const auto type = m_dlg.rawCreationInfo().type;
  const QString root = instanceRootForType(type, manager);
  const QString name = MOBase::sanitizeFileName(ui->instanceName->text().trimmed());
  const QString folder = name.isEmpty()
                             ? QDir(root).filePath(QCoreApplication::translate(
                                   "cid::NamePage", "<instance name>"))
                             : QDir(root).filePath(name);
  ui->instanceNamePath->setText(
      QCoreApplication::translate("cid::NamePage", "Storage folder: %1")
          .arg(QDir::toNativeSeparators(folder)));
}

bool NamePage::checkName(QString parentDir, QString name)
{
  bool exists  = false;
  bool invalid = false;
  bool empty   = false;

  name = name.trimmed();

  if (name.isEmpty()) {
    empty = true;
  } else {
    if (MOBase::validFileName(name)) {
      exists = QDir(parentDir).exists(name);
    } else {
      invalid = true;
    }
  }

  bool okay = false;

  if (exists) {
    m_exists.setVisible(true);
    m_exists.setText(QDir(parentDir).filePath(name));
    m_invalid.setVisible(false);
  } else if (invalid) {
    m_exists.setVisible(false);
    m_invalid.setVisible(true);
    m_invalid.setText(name);
  } else {
    okay = !empty;
    m_exists.setVisible(false);
    m_invalid.setVisible(false);
  }

  return okay;
}

ProfilePage::ProfilePage(CreateInstanceDialog& dlg)
    : Page(dlg), m_eldenRingSaveGroup(new QGroupBox(ui->widget_25)),
      m_eldenRingSaveScope(new QLabel(m_eldenRingSaveGroup)),
      m_eldenRingSaveMode(new QComboBox(m_eldenRingSaveGroup)),
      m_eldenRingSaveDescription(new QLabel(m_eldenRingSaveGroup)),
      m_defaultHeading(ui->profileSettingsLabel->text())
{
  m_eldenRingSaveGroup->setObjectName(QStringLiteral("eldenRingSaveIsolationGroup"));
  m_eldenRingSaveGroup->setTitle(QString());
  m_eldenRingSaveGroup->setVisible(false);

  auto* groupLayout = new QVBoxLayout(m_eldenRingSaveGroup);
  groupLayout->setContentsMargins(0, 0, 0, 0);
  groupLayout->setSpacing(6);

  m_eldenRingSaveScope->setText(QCoreApplication::translate(
      "cid::ProfilePage",
      "This choice applies only to Elden Ring profiles in the instance being created."));
  m_eldenRingSaveScope->setWordWrap(true);
  m_eldenRingSaveScope->setStyleSheet(
      QStringLiteral("QLabel { background-color: transparent; border: none; padding: 0; }"));
  groupLayout->addWidget(m_eldenRingSaveScope);

  m_eldenRingSaveMode->addItem(
      EldenRingSaveSettings::modeName(EldenRingSaveSettings::ProfileIsolated),
      QString::fromLatin1(EldenRingSaveSettings::ProfileIsolated));
  m_eldenRingSaveMode->setObjectName(QStringLiteral("eldenRingSaveMode"));
  m_eldenRingSaveMode->addItem(
      EldenRingSaveSettings::modeName(EldenRingSaveSettings::InstanceShared),
      QString::fromLatin1(EldenRingSaveSettings::InstanceShared));
  m_eldenRingSaveMode->addItem(
      EldenRingSaveSettings::modeName(EldenRingSaveSettings::GlobalShared),
      QString::fromLatin1(EldenRingSaveSettings::GlobalShared));
  m_eldenRingSaveMode->setMinimumContentsLength(36);
  m_eldenRingSaveMode->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_eldenRingSaveMode->setMaxVisibleItems(8);
  groupLayout->addWidget(m_eldenRingSaveMode);

  m_eldenRingSaveDescription->setWordWrap(true);
  // Keep room for translated descriptions without reserving a tall block on
  // the save setup page.
  m_eldenRingSaveDescription->setMinimumHeight(
      m_eldenRingSaveDescription->fontMetrics().lineSpacing() * 3);
  groupLayout->addWidget(m_eldenRingSaveDescription);

  if (auto* pageLayout = qobject_cast<QVBoxLayout*>(ui->widget_25->layout())) {
    pageLayout->insertWidget(0, m_eldenRingSaveGroup);
  }

  QObject::connect(m_eldenRingSaveMode,
                   qOverload<int>(&QComboBox::currentIndexChanged), [this] {
                     const QString mode = m_eldenRingSaveMode->currentData().toString();
                     m_eldenRingSaveDescription->setText(
                         EldenRingSaveSettings::modeDescription(mode));
                   });
  m_eldenRingSaveDescription->setText(EldenRingSaveSettings::modeDescription(
      m_eldenRingSaveMode->currentData().toString()));

  // the warning's link turns on both switches that keep the files away
  QObject::connect(ui->oneDriveWarning, &QLabel::linkActivated, &m_dlg, [this] {
    ui->profileInisCheckbox->setChecked(true);
    ui->profileSavesCheckbox->setChecked(true);
  });

  // The plugin replaces this native fallback when it successfully injects
  // its richer save controls. If the plugin is unavailable (including during
  // first-time instance creation), ProfilePage::doActivated shows this group.
  m_eldenRingSaveGroup->hide();

}

bool ProfilePage::ready() const
{
  return true;
}

CreateInstanceDialog::ProfileSettings ProfilePage::profileSettings() const
{
  CreateInstanceDialog::ProfileSettings profileSettings;

  auto* gamePage = m_dlg.getPage<GamePage>();
  if (gamePage != nullptr &&
      EldenRingSaveSettings::supports(gamePage->selectedGame())) {
    profileSettings.eldenRingSaveMode =
        m_eldenRingSaveMode->currentData().toString();
    profileSettings.localInis = false;
    profileSettings.localSaves =
        profileSettings.eldenRingSaveMode !=
        QLatin1String(EldenRingSaveSettings::GlobalShared);
    profileSettings.archiveInvalidation = false;
    return profileSettings;
  }

  profileSettings.localInis           = ui->profileInisCheckbox->isChecked();
  profileSettings.localSaves          = ui->profileSavesCheckbox->isChecked();
  profileSettings.archiveInvalidation = ui->archiveInvalidationCheckbox->isChecked();
  return profileSettings;
}

void ProfilePage::doActivated(bool firstTime)
{
  auto* gamePage = m_dlg.getPage<GamePage>();
  const bool isEldenRing =
      gamePage != nullptr && EldenRingSaveSettings::supports(gamePage->selectedGame());

  // the plugin's save card (or the native one) sits inside the rows' padding;
  // the switch rows run edge to edge
  ui->widget_25->layout()->setContentsMargins(isEldenRing ? 20 : 0, isEldenRing ? 4 : 0,
                                              isEldenRing ? 20 : 0, isEldenRing ? 12 : 0);

  const bool pluginOwnsSaveControls =
      m_eldenRingSaveGroup
          ->property("_eldenRingSaveUiPluginControlsActive")
          .toBool();
  m_eldenRingSaveGroup->setVisible(isEldenRing && !pluginOwnsSaveControls);
  ui->profileInisCheckbox->setVisible(!isEldenRing);
  ui->profileSavesCheckbox->setVisible(!isEldenRing);
  ui->archiveInvalidationCheckbox->setVisible(!isEldenRing);
  ui->profileInisDescription->setVisible(!isEldenRing);
  ui->profileSavesDescription->setVisible(!isEldenRing);
  ui->archiveInvalidationDescription->setVisible(!isEldenRing);
  ui->gameInisPath->parentWidget()->setVisible(!isEldenRing);
  ui->gameSavesPath->parentWidget()->setVisible(!isEldenRing);
  if (!isEldenRing && gamePage != nullptr && gamePage->selectedGame() != nullptr) {
    const auto* game = gamePage->selectedGame();
    const QString documents =
        QDir::toNativeSeparators(game->documentsDirectory().absolutePath());
    const QStringList inis = game->iniFiles();
    ui->gameInisPath->setText(
        inis.isEmpty() ? documents
                       : documents + QStringLiteral(" \u00B7 ") +
                             inis.join(QStringLiteral(", ")));
    ui->gameSavesPath->setText(
        QDir::toNativeSeparators(game->savesDirectory().absolutePath()));
  }
  const bool oneDrive =
      !isEldenRing && gamePage != nullptr && gamePage->selectedGame() != nullptr &&
      (inOneDrive(gamePage->selectedGame()->documentsDirectory().absolutePath()) ||
       inOneDrive(gamePage->selectedGame()->savesDirectory().absolutePath()));
  ui->oneDriveWarning->setVisible(oneDrive);
  ui->oneDriveWarning->parentWidget()->setVisible(oneDrive);
  ui->profileSettingsLabel->setText(
      isEldenRing
          ? plainHeading(QCoreApplication::translate(
                "cid::ProfilePage", "<h3>Choose how Elden Ring saves are handled.</h3>"))
          : m_defaultHeading);

  if (isEldenRing && firstTime) {
    m_eldenRingSaveMode->setCurrentIndex(0);
  }

  if (isEldenRing) {
    m_eldenRingSaveDescription->setText(EldenRingSaveSettings::modeDescription(
        m_eldenRingSaveMode->currentData().toString()));
  }
}

PathsPage::PathsPage(CreateInstanceDialog& dlg)
    : Page(dlg), m_lastType(CreateInstanceDialog::NoType), m_label(ui->pathsLabel),
      m_simpleExists(ui->locationExists), m_simpleInvalid(ui->locationInvalid),
      m_advancedExists(ui->advancedDirExists),
      m_advancedInvalid(ui->advancedDirInvalid), m_okay(false)
{
  // These labels display user-selected paths and must remain readable at the
  // wizard's fixed width, including when the path is long.
  ui->locationExists->setWordWrap(true);
  ui->locationInvalid->setWordWrap(true);
  ui->advancedDirExists->setWordWrap(true);
  ui->advancedDirInvalid->setWordWrap(true);

  auto setEdit = [&](QLineEdit* e) {
    QObject::connect(e, &QLineEdit::textEdited, [&] {
      onChanged();
    });
    QObject::connect(e, &QLineEdit::returnPressed, [&] {
      next();
    });
  };

  auto setBrowse = [&](QAbstractButton* b, QLineEdit* e) {
    QObject::connect(b, &QAbstractButton::clicked, [this, e] {
      browse(e);
    });
  };

  setEdit(ui->location);
  setEdit(ui->base);
  setEdit(ui->downloads);
  setEdit(ui->mods);
  setEdit(ui->profiles);
  setEdit(ui->overwrite);

  setBrowse(ui->browseLocation, ui->location);
  setBrowse(ui->browseBase, ui->base);
  setBrowse(ui->browseDownloads, ui->downloads);
  setBrowse(ui->browseMods, ui->mods);
  setBrowse(ui->browseProfiles, ui->profiles);
  setBrowse(ui->browseOverwrite, ui->overwrite);

  QObject::connect(ui->advancedPathOptions, &QCheckBox::clicked, [&] {
    onAdvanced();
  });

  QObject::connect(ui->sharedDownloads, qOverload<int>(&QComboBox::currentIndexChanged),
                   [&] {
                     onChanged();
                   });

  ui->pathPages->setCurrentIndex(0);
}

bool PathsPage::ready() const
{
  // set when the page is activated, textboxes are changed or the advanced
  // checkbox is toggled
  return m_okay;
}

void PathsPage::doActivated(bool firstTime)
{
  const auto name = m_dlg.rawCreationInfo().instanceName;
  const auto type = m_dlg.rawCreationInfo().type;

  // if the instance name or type have changed, all the paths must be
  // regenerated
  const bool changed = (m_lastInstanceName != name) || (m_lastType != type);

  // this instance's own downloads, or those of an instance of the same game;
  // the choice stays while it's still offered
  {
    const QString kept = ui->sharedDownloads->currentData().toString();
    QSignalBlocker blocker(ui->sharedDownloads);
    ui->sharedDownloads->clear();
    ui->sharedDownloads->addItem(QObject::tr("This instance's own folder"), QString());
    if (type != CreateInstanceDialog::Isolated) {
      const QString game = m_dlg.rawCreationInfo().game->gameName();
      for (const auto& i : existingInstances()) {
        if (i.game.compare(game, Qt::CaseInsensitive) == 0 && !i.downloads.isEmpty()) {
          ui->sharedDownloads->addItem(QObject::tr("Shared with %1").arg(i.name),
                                       i.downloads);
          ui->sharedDownloads->setItemData(ui->sharedDownloads->count() - 1,
                                           i.downloads, Qt::ToolTipRole);
        }
      }
    }
    const int index = ui->sharedDownloads->findData(kept);
    ui->sharedDownloads->setCurrentIndex(qMax(index, 0));
    ui->sharedDownloads->parentWidget()->setVisible(ui->sharedDownloads->count() > 1);
  }

  // generating and paths
  setPaths(name, changed);
  checkPaths();
  fitStackedWidgetToCurrentPage(ui->pathPages);
  ui->widget_13->updateGeometry();

  updateNavigation();

  if (type == CreateInstanceDialog::Isolated) {
    ui->pathsLabel->setText(QObject::tr(
        "These folders belong to the separate MO2 copy for %1. Its application files and settings will be kept together in that folder.")
                                 .arg(m_dlg.rawCreationInfo().game->gameName()));
  } else {
    m_label.setText(m_dlg.rawCreationInfo().game->gameName());
  }
  m_lastInstanceName = name;
  m_lastType         = type;

  if (firstTime) {
    ui->location->setFocus();
  }
}

CreateInstanceDialog::Paths PathsPage::selectedPaths() const
{
  CreateInstanceDialog::Paths p;

  // trimmed as the checks trim them: a leading space would make a relative
  // path that fails only when the instance is created
  if (ui->advancedPathOptions->isChecked()) {
    p.base      = ui->base->text().trimmed();
    p.downloads = ui->downloads->text().trimmed();
    p.mods      = ui->mods->text().trimmed();
    p.profiles  = ui->profiles->text().trimmed();
    p.overwrite = ui->overwrite->text().trimmed();
  } else {
    p.base      = ui->location->text().trimmed();
    p.downloads = ui->sharedDownloads->currentData().toString();
  }

  return p;
}

void PathsPage::onChanged()
{
  checkPaths();
  fitStackedWidgetToCurrentPage(ui->pathPages);
  ui->widget_13->updateGeometry();
  updateNavigation();
}

void PathsPage::browse(QLineEdit* e)
{
  const auto s = QFileDialog::getExistingDirectory(&m_dlg, {}, e->text());
  if (s.isNull() || s.isEmpty()) {
    return;
  }

  e->setText(QDir::toNativeSeparators(s));
}

void PathsPage::updateDiskSpace()
{
  // the nearest folder that exists, on the drive the data goes to
  QString path = ui->advancedPathOptions->isChecked() ? ui->base->text() : ui->location->text();
  QDir dir(QDir::fromNativeSeparators(path.trimmed()));
  while (!dir.exists() && dir.cdUp()) {
  }

  const QStorageInfo storage(dir);
  if (path.trimmed().isEmpty() || !storage.isValid() || !storage.isReady()) {
    ui->diskSpace->clear();
    ui->diskBar->hide();
    return;
  }
  ui->diskSpace->setText(QObject::tr("%1 free on %2")
                             .arg(localizedByteSize(storage.bytesAvailable()))
                             .arg(QDir::toNativeSeparators(storage.rootPath())));
  // how full the drive is
  const qint64 total = storage.bytesTotal();
  ui->diskBar->setVisible(total > 0);
  if (total > 0) {
    ui->diskBar->setValue(
        static_cast<int>((total - storage.bytesAvailable()) * 1000 / total));
    ui->diskBar->setToolTip(QObject::tr("%1 of %2 used")
                                .arg(localizedByteSize(total - storage.bytesAvailable()))
                                .arg(localizedByteSize(total)));
  }
}

void PathsPage::checkPaths()
{
  updateDiskSpace();

  if (ui->advancedPathOptions->isChecked()) {
    // checking advanced paths
    m_okay = checkAdvancedPath(ui->base->text()) &&
             checkAdvancedPath(resolve(ui->downloads->text())) &&
             checkAdvancedPath(resolve(ui->mods->text())) &&
             checkAdvancedPath(resolve(ui->profiles->text())) &&
             checkAdvancedPath(resolve(ui->overwrite->text()));
  } else {
    // checking simple path
    m_okay = checkSimplePath(ui->location->text());

    // the folders that go inside the location, and where the downloads go
    // when they're shared
    const QString base   = QDir::toNativeSeparators(ui->location->text().trimmed());
    const QString shared = ui->sharedDownloads->currentData().toString();
    QStringList inside   = {QStringLiteral("mods"), QStringLiteral("profiles"),
                            QStringLiteral("overwrite")};
    if (shared.isEmpty()) {
      inside.prepend(QStringLiteral("downloads"));
    }
    QString text = QObject::tr("%1 in %2").arg(inside.join(QStringLiteral(", ")), base);
    if (!shared.isEmpty()) {
      text += QStringLiteral("\n") + QObject::tr("Downloads from %1").arg(shared);
    }
    ui->folderPreview->setText(text);
  }
}

bool PathsPage::checkSimplePath(const QString& path)
{
  return checkPath(path, m_simpleExists, m_simpleInvalid);
}

bool PathsPage::checkAdvancedPath(const QString& path)
{
  return checkPath(path, m_advancedExists, m_advancedInvalid);
}

QString PathsPage::resolve(const QString& path) const
{
  return PathSettings::resolve(path, ui->base->text());
}

void PathsPage::onAdvanced()
{
  // the base/location textboxes are different widgets but they represent the
  // same base path value, so they're synced between pages

  if (ui->advancedPathOptions->isChecked()) {
    ui->base->setText(ui->location->text());
    ui->pathPages->setCurrentIndex(1);
  } else {
    ui->location->setText(ui->base->text());
    ui->pathPages->setCurrentIndex(0);
  }

  checkPaths();
  fitStackedWidgetToCurrentPage(ui->pathPages);
  ui->widget_13->updateGeometry();
}

void PathsPage::setPaths(const QString& name, bool force)
{
  QString basePath;

  const auto& manager = InstanceManager::singleton();
  if (m_dlg.rawCreationInfo().type == CreateInstanceDialog::Portable) {
    basePath = name.isEmpty() ? manager.portablePath()
                              : manager.portableInstancePath(name);
  } else if (m_dlg.rawCreationInfo().type == CreateInstanceDialog::Isolated) {
    basePath = manager.isolatedInstancePath(name);
  } else {
    const auto root = manager.globalInstancesRootPath();
    basePath        = root + "/" + name;
  }

  basePath = QDir::toNativeSeparators(QDir::cleanPath(basePath));

  // all paths are set regardless of advanced checkbox

  setIfEmpty(ui->location, basePath, force);
  setIfEmpty(ui->base, basePath, force);

  setIfEmpty(ui->downloads, makeDefaultPath(AppConfig::downloadPath()), force);
  setIfEmpty(ui->mods, makeDefaultPath(AppConfig::modsPath()), force);
  setIfEmpty(ui->profiles, makeDefaultPath(AppConfig::profilesPath()), force);
  setIfEmpty(ui->overwrite, makeDefaultPath(AppConfig::overwritePath()), force);
}

void PathsPage::setIfEmpty(QLineEdit* e, const QString& path, bool force)
{
  if (e->text().isEmpty() || force) {
    e->setText(path);
  }
}

bool PathsPage::checkPath(QString path, PlaceholderLabel& existsLabel,
                          PlaceholderLabel& invalidLabel)
{
  auto& m = InstanceManager::singleton();

  bool exists  = false;
  bool invalid = false;
  bool empty   = false;

  path = QDir::toNativeSeparators(path.trimmed());

  if (path.isEmpty()) {
    empty = true;
  } else {
    const QDir d(path);

    if (MOBase::validFileName(d.dirName())) {
      if (m_dlg.rawCreationInfo().type == CreateInstanceDialog::Portable &&
          m_dlg.rawCreationInfo().instanceName.isEmpty()) {
        // An in-place setup of the legacy portable instance uses the existing
        // MO2 installation directory.
        const auto normalizedPath =
            QDir::cleanPath(QDir::fromNativeSeparators(path));
        const auto normalizedRoot =
            QDir::cleanPath(QDir::fromNativeSeparators(m.portablePath()));
        if (normalizedPath.compare(normalizedRoot, Qt::CaseInsensitive) != 0) {
          exists = QDir(path).exists();
        }
      } else {
        exists = QDir(path).exists();
      }
    } else {
      invalid = true;
    }
  }

  bool okay = true;

  if (invalid) {
    okay = false;
    existsLabel.setVisible(false);
    invalidLabel.setVisible(true);
    invalidLabel.setText(path);
  } else if (empty) {
    okay = false;
    existsLabel.setVisible(false);
    invalidLabel.setVisible(false);
  } else if (exists) {
    // this is just a warning
    existsLabel.setVisible(true);
    existsLabel.setText(path);
    invalidLabel.setVisible(false);
  } else {
    okay = true;
    existsLabel.setVisible(false);
    invalidLabel.setVisible(false);
  }

  return okay;
}

NexusPage::NexusPage(CreateInstanceDialog& dlg) : Page(dlg), m_skip(false)
{
  m_connectionUI.reset(new NexusConnectionUI(&m_dlg, dlg.settings(), ui->nexusConnect,
                                             nullptr, ui->nexusManual, ui->nexusLog));

  // just check it once, or connecting and then going back and forth would skip
  // the page, which would be unexpected
  m_skip = GlobalSettings::hasNexusApiKey();

  QObject::connect(ui->nexusLog->model(), &QAbstractItemModel::rowsInserted, &m_dlg, [this] {
    if (ui->nexusLog->parentWidget()->isHidden()) {
      ui->nexusLog->parentWidget()->show();
    }
  });
}

NexusPage::~NexusPage() = default;

bool NexusPage::ready() const
{
  // this page is optional
  return true;
}

bool NexusPage::doSkip() const
{
  return m_skip;
}

ConfirmationPage::ConfirmationPage(CreateInstanceDialog& dlg) : Page(dlg)
{
  // The isolated-copy instructions are longer than the default sentence.
  ui->label_17->setWordWrap(true);
}

void ConfirmationPage::doActivated(bool)
{
  // one row per value: the caption, the value and Change, which goes back to
  // the step it came from
  auto* rows = ui->review->layout();
  while (auto* item = rows->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  for (const auto& [caption, value, step] : makeReview()) {
    auto* row = new QFrame(ui->review);
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(20, 9, 12, 9);
    rowLayout->setSpacing(16);
    auto* captionLabel = new QLabel(caption, row);
    captionLabel->setObjectName(QStringLiteral("wizardReviewKey"));
    captionLabel->setFixedWidth(120);
    captionLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto* valueLabel = new QLabel(value, row);
    valueLabel->setObjectName(QStringLiteral("wizardReviewValue"));
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rowLayout->addWidget(captionLabel);
    rowLayout->addWidget(valueLabel, 1);
    if (step != nullptr && !m_dlg.isSinglePage()) {
      auto* change = new QToolButton(row);
      change->setObjectName(QStringLiteral("wizardChangeButton"));
      change->setText(QObject::tr("Change"));
      change->setAutoRaise(true);
      change->setCursor(Qt::PointingHandCursor);
      change->setAccessibleName(QObject::tr("Change %1").arg(caption));
      QObject::connect(change, &QToolButton::clicked, &m_dlg, [this, step = step] {
        m_dlg.selectPageOf(step);
      });
      rowLayout->addWidget(change, 0, Qt::AlignTop);
    }
    rows->addWidget(row);
  }

  ui->creationLog->clear();
  ui->creationLog->hide();
  ui->creationLog->parentWidget()->hide();
  ui->widget_19->updateGeometry();

  const bool isolated = m_dlg.rawCreationInfo().type == CreateInstanceDialog::Isolated;
  if (isolated) {
    ui->label_17->setText(QObject::tr(
        "MO2 will copy its application files into a separate folder for this game. If you start the copy after setup, this MO2 closes first."));
    ui->launch->setText(QObject::tr("Start the isolated copy after setup"));
  } else {
    ui->label_17->setText(QObject::tr(
        "Check the details, then select Create instance. Nothing is written "
        "before that."));
    ui->launch->setText(QObject::tr("Launch the new instance"));
  }
}

std::vector<ConfirmationPage::ReviewRow> ConfirmationPage::makeReview() const
{
  std::vector<ReviewRow> rows;

  const auto ci = m_dlg.rawCreationInfo();
  const Page* typePage    = m_dlg.getPage<TypePage>();
  const Page* gamePage    = m_dlg.getPage<GamePage>();
  const Page* namePage    = m_dlg.getPage<NamePage>();
  const Page* profilePage = m_dlg.getPage<ProfilePage>();
  const Page* pathsPage   = m_dlg.getPage<PathsPage>();
  auto yesNo = [](bool b) {
    return b ? QObject::tr("yes") : QObject::tr("no");
  };

  // game
  QString name = ci.game->gameName();
  if (!ci.gameVariant.isEmpty()) {
    name += " (" + ci.gameVariant + ")";
  }
  rows.push_back({QObject::tr("Game"), name, gamePage});
  rows.push_back({QObject::tr("Game folder"),
                  inProtectedFolder(ci.gameLocation)
                      ? ci.gameLocation + QStringLiteral(" \u00B7 ") +
                            QObject::tr("In Program Files")
                      : ci.gameLocation,
                  gamePage});

  rows.push_back({QObject::tr("Storage"), toLocalizedString(ci.type), typePage});
  if (!ci.instanceName.isEmpty()) {
    rows.push_back({QObject::tr("Name"), ci.instanceName, namePage});
  }
  if (ci.type != CreateInstanceDialog::Isolated) {
    const QString group = ui->instanceGroup->currentText().trimmed();
    if (!group.isEmpty()) {
      rows.push_back({QObject::tr("Group"), group, namePage});
    }
    const QString notes = ui->instanceNotes->text().trimmed();
    if (!notes.isEmpty()) {
      rows.push_back({QObject::tr("Notes"), notes, namePage});
    }
  }
  rows.push_back({QObject::tr("Instance folder"), ci.dataPath, pathsPage});

  if (!ui->advancedPathOptions->isChecked()) {
    // simple settings
    if (ci.paths.base != ci.dataPath) {
      rows.push_back({QObject::tr("Base directory"), ci.paths.base, pathsPage});
    }
    if (!ci.paths.downloads.isEmpty()) {
      rows.push_back({QObject::tr("Downloads"),
                      ui->sharedDownloads->currentText() + QStringLiteral(" \u00B7 ") +
                          ci.paths.downloads,
                      pathsPage});
    }
  } else {
    // advanced settings
    rows.push_back({QObject::tr("Base directory"), ci.paths.base, pathsPage});
    rows.push_back({QObject::tr("Downloads"), ci.paths.downloads, pathsPage});
    rows.push_back({QObject::tr("Mods"), ci.paths.mods, pathsPage});
    rows.push_back({QObject::tr("Profiles"), ci.paths.profiles, pathsPage});
    rows.push_back({QObject::tr("Overwrite"), ci.paths.overwrite, pathsPage});
  }
  if (!ui->diskSpace->text().isEmpty()) {
    rows.push_back({QObject::tr("Disk"), ui->diskSpace->text(), pathsPage});
  }
  {
    QStringList programs;
    for (auto* box : ui->detectedPrograms->findChildren<QCheckBox*>()) {
      if (box->isChecked() && !ui->detectedPrograms->isHidden()) {
        programs.push_back(box->property("toolTitle").toString());
      }
    }
    if (!programs.isEmpty()) {
      rows.push_back({QObject::tr("Programs"), programs.join(QStringLiteral(", ")),
                      namePage});
    }
  }
  if (ci.type != CreateInstanceDialog::Isolated && ui->desktopShortcut->isChecked()) {
    rows.push_back({QObject::tr("Shortcut"), QObject::tr("On the desktop"), namePage});
  }

  if (EldenRingSaveSettings::supports(ci.game)) {
    rows.push_back({QObject::tr("Saves"),
                    EldenRingSaveSettings::modeName(ci.profileSettings.eldenRingSaveMode),
                    profilePage});
  } else {
    rows.push_back(
        {QObject::tr("Profiles"),
         QObject::tr("Local INIs: %1").arg(yesNo(ci.profileSettings.localInis)) +
             QStringLiteral(" \u00B7 ") +
             QObject::tr("Local Saves: %1").arg(yesNo(ci.profileSettings.localSaves)) +
             QStringLiteral(" \u00B7 ") +
             QObject::tr("Automatic Archive Invalidation: %1")
                 .arg(yesNo(ci.profileSettings.archiveInvalidation)),
         profilePage});
  }

  return rows;
}

}  // namespace cid
