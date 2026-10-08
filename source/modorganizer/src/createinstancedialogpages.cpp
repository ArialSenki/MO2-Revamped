#include "createinstancedialogpages.h"
#include "eldenringsavesettings.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include "plugincontainer.h"
#include "settings.h"
#include "settingsdialognexus.h"
#include "shared/appconfig.h"
#include "ui_createinstancedialog.h"
#include <iplugingame.h>
#include <report.h>
#include <utility.h>

#include <QComboBox>
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
#include <QSet>
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

enum class WizardIcon
{
  Global,
  Portable,
  Isolated,
  Browse,
  Edition,
};

QIcon makeWizardIcon(const QWidget* widget, WizardIcon type)
{
  constexpr int iconSize = 40;
  QPixmap pixmap(iconSize, iconSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const QColor accent = widget->palette().color(QPalette::Highlight);
  const QColor ink = widget->palette().color(QPalette::Text);
  const QColor surface = widget->palette().color(QPalette::Base);
  QColor tint = accent;
  tint.setAlpha(28);
  painter.setPen(Qt::NoPen);
  painter.setBrush(tint);
  painter.drawRoundedRect(QRectF(1, 1, 38, 38), 10, 10);

  const QPen outline(accent, 2.1, Qt::SolidLine, Qt::RoundCap,
                     Qt::RoundJoin);
  const QPen detail(ink, 1.7, Qt::SolidLine, Qt::RoundCap,
                    Qt::RoundJoin);
  painter.setPen(outline);
  painter.setBrush(surface);

  auto drawFolder = [&] {
    QPainterPath folder;
    folder.moveTo(8, 14);
    folder.lineTo(16, 14);
    folder.lineTo(19, 17);
    folder.lineTo(32, 17);
    folder.lineTo(32, 29);
    folder.quadTo(32, 31, 30, 31);
    folder.lineTo(10, 31);
    folder.quadTo(8, 31, 8, 29);
    folder.closeSubpath();
    painter.drawPath(folder);
    painter.drawLine(QPointF(8, 19), QPointF(32, 19));
  };

  switch (type) {
  case WizardIcon::Global: {
    // User-space storage: a compact home-folder symbol.
    QPainterPath home;
    home.moveTo(8, 19);
    home.lineTo(20, 9);
    home.lineTo(32, 19);
    home.lineTo(29, 19);
    home.lineTo(29, 31);
    home.lineTo(22, 31);
    home.lineTo(22, 24);
    home.lineTo(18, 24);
    home.lineTo(18, 31);
    home.lineTo(11, 31);
    home.lineTo(11, 19);
    home.closeSubpath();
    painter.drawPath(home);
    break;
  }
  case WizardIcon::Portable: {
    drawFolder();
    painter.setPen(detail);
    painter.drawLine(QPointF(17, 24), QPointF(27, 24));
    painter.drawLine(QPointF(24, 21), QPointF(27, 24));
    painter.drawLine(QPointF(24, 27), QPointF(27, 24));
    break;
  }
  case WizardIcon::Isolated: {
    painter.drawRoundedRect(QRectF(8, 8, 24, 20), 4, 4);
    painter.drawLine(QPointF(8, 14), QPointF(32, 14));
    painter.setPen(detail);
    painter.drawEllipse(QPointF(12, 11), 0.8, 0.8);
    painter.drawEllipse(QPointF(15, 11), 0.8, 0.8);
    painter.setPen(outline);
    painter.setBrush(surface);
    painter.drawRoundedRect(QRectF(21, 23, 12, 10), 3, 3);
    painter.drawArc(QRectF(24, 18, 6, 10), 0, 180 * 16);
    break;
  }
  case WizardIcon::Browse: {
    drawFolder();
    painter.setPen(QPen(accent, 2.3, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(surface);
    painter.drawEllipse(QRectF(22, 21, 9, 9));
    painter.drawLine(QPointF(29, 29), QPointF(34, 34));
    break;
  }
  case WizardIcon::Edition: {
    painter.drawRoundedRect(QRectF(10, 7, 19, 25), 3, 3);
    painter.setPen(detail);
    painter.drawLine(QPointF(15, 15), QPointF(24, 15));
    painter.drawLine(QPointF(15, 20), QPointF(24, 20));
    painter.drawLine(QPointF(15, 25), QPointF(21, 25));
    break;
  }
  }

  painter.end();
  return QIcon(pixmap);
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
  if (stacked == nullptr || stacked->currentWidget() == nullptr) {
    return;
  }

  QWidget* currentPage = stacked->currentWidget();
  currentPage->ensurePolished();
  if (QLayout* layout = currentPage->layout()) {
    layout->activate();
  }

  const int height = currentPage->sizeHint()
                         .expandedTo(currentPage->minimumSizeHint())
                         .height();
  if (height <= 0) {
    return;
  }

  stacked->setMinimumHeight(height);
  stacked->setMaximumHeight(height);
  stacked->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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

  // This legacy note only described Portable storage and was misleading for
  // Global and Isolated choices. The cards now contain the relevant details.
  ui->portableExistsLabel->setVisible(false);
  ui->createGlobal->setAutoDefault(false);
  ui->createPortable->setAutoDefault(false);
  ui->createIsolated->setAutoDefault(false);
  ui->createGlobal->setIcon(makeWizardIcon(ui->createGlobal, WizardIcon::Global));
  ui->createPortable->setIcon(makeWizardIcon(ui->createPortable, WizardIcon::Portable));
  ui->createIsolated->setIcon(makeWizardIcon(ui->createIsolated, WizardIcon::Isolated));
  ui->createGlobal->setIconSize(QSize(34, 34));
  ui->createPortable->setIconSize(QSize(34, 34));
  ui->createIsolated->setIconSize(QSize(34, 34));
  ui->createGlobal->setMinimumHeight(80);
  ui->createPortable->setMinimumHeight(80);
  ui->createIsolated->setMinimumHeight(80);

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

    // Keep selection tools attached to the game list instead of in a footer
    // that floats below the active cards.
    if (auto* panelLayout = qobject_cast<QBoxLayout*>(ui->widget_6->layout())) {
      panelLayout->removeWidget(ui->widget_21);
      panelLayout->removeWidget(ui->widget_23);
      panelLayout->insertWidget(0, ui->widget_21);
      panelLayout->insertWidget(1, ui->widget_23, 1);
      panelLayout->setStretch(0, 0);
      panelLayout->setStretch(1, 1);
      panelLayout->setContentsMargins(12, 12, 12, 12);
      panelLayout->setSpacing(10);
    }
    if (auto* toolbarLayout = qobject_cast<QBoxLayout*>(ui->widget_21->layout())) {
      toolbarLayout->setContentsMargins(0, 0, 0, 0);
      toolbarLayout->setSpacing(10);
    }
    ui->scrollArea_3->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    ui->scrollArea_3->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    if (ui->games != nullptr) {
      ui->games->setAttribute(Qt::WA_StyledBackground, true);
    }

    // A new wizard always starts with detected installations only. Keep the
    // filter state in GamePage so list rebuilding never depends on a stale UI
    // reference or an implicit Designer default.
    m_showAllGames = false;
    if (ui->showAllGames != nullptr) {
      ui->showAllGames->setChecked(false);
    }

    if (ui->browseGameFolderButton != nullptr) {
      ui->browseGameFolderButton->setAutoDefault(false);
      ui->browseGameFolderButton->setIcon(
          makeWizardIcon(ui->browseGameFolderButton, WizardIcon::Browse));
      ui->browseGameFolderButton->setIconSize(QSize(20, 20));
      ui->browseGameFolderButton->setMinimumHeight(34);
      QObject::connect(ui->browseGameFolderButton, &QPushButton::clicked,
                       &m_dlg, [this] { selectCustom(); });
    } else {
      log::error("Create Instance wizard: game-folder browse button is missing");
    }

    if (ui->gamesFilter != nullptr) {
      ui->gamesFilter->setMinimumWidth(190);
      ui->gamesFilter->setMaximumWidth(260);
      ui->gamesFilter->setFixedHeight(34);
      ui->gamesFilter->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Fixed);
      ui->gamesFilter->setToolTip(
          QObject::tr("Filter supported games by name."));
      QObject::connect(ui->gamesFilter, &QLineEdit::textChanged, &m_dlg,
                       [this] { fillList(); });
    } else {
      log::error("Create Instance wizard: game filter is missing");
    }

    if (ui->showAllGames != nullptr) {
      ui->showAllGames->setTristate(false);
      ui->showAllGames->setMinimumHeight(34);
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
  g->button->setMinimumHeight(78);
  g->button->setIconSize(QSize(32, 32));

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
    gameIcon = makeWizardIcon(g->button, WizardIcon::Edition);
  }
  g->button->setIcon(gameIcon);

  if (g->installed) {
    g->button->setDescription(g->dir);
  } else {
    g->button->setDescription(QObject::tr("No installation found"));
  }
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
    b->setIcon(makeWizardIcon(b, WizardIcon::Edition));
    b->setIconSize(QSize(28, 28));
    b->setMinimumHeight(56);

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
  QObject::connect(ui->instanceName, &QLineEdit::textEdited, [&] {
    onChanged();
  });

  QObject::connect(ui->instanceName, &QLineEdit::returnPressed, [&] {
    next();
  });
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

  if (auto* nameLayout =
          qobject_cast<QVBoxLayout*>(ui->widget_9->layout())) {
    if (nameLayout->count() > 0) {
      if (auto* spacer = nameLayout->itemAt(nameLayout->count() - 1)
                              ->spacerItem()) {
        spacer->changeSize(20, 0, QSizePolicy::Minimum,
                           QSizePolicy::Fixed);
        nameLayout->invalidate();
        nameLayout->activate();
        ui->widget_9->updateGeometry();
      }
    }
  }

  const auto type = m_dlg.rawCreationInfo().type;
  ui->instanceNameContext->setVisible(true);
  if (type == CreateInstanceDialog::Isolated) {
    ui->instanceNameLabel->setText(
        QCoreApplication::translate(
            "cid::NamePage",
            "<h3>Name your isolated MO2 copy for %1.</h3>")
            .arg(g->gameName().toHtmlEscaped()));
  } else {
    const QString typeName =
        type == CreateInstanceDialog::Portable
            ? QCoreApplication::translate("cid::NamePage", "portable")
            : QCoreApplication::translate("cid::NamePage", "global");
    ui->instanceNameLabel->setText(
        QCoreApplication::translate("cid::NamePage",
                                     "<h3>Name this %1 instance for %2.</h3>")
            .arg(typeName.toHtmlEscaped(), g->gameName().toHtmlEscaped()));
  }

  const auto& manager = InstanceManager::singleton();
  if (manager.isEldenRingOnlyPortableMode()) {
    ui->instanceNameLabel->setText(
        QCoreApplication::translate(
            "cid::NamePage",
            "<h3>This setup will create one isolated portable instance for %1.</h3>")
            .arg(g->gameName().toHtmlEscaped()));
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
  m_eldenRingSaveGroup->setTitle(
      QCoreApplication::translate("cid::ProfilePage", "Elden Ring Save Isolation"));
  m_eldenRingSaveGroup->setVisible(false);

  auto* groupLayout = new QVBoxLayout(m_eldenRingSaveGroup);
  groupLayout->setContentsMargins(14, 12, 14, 12);
  groupLayout->setSpacing(8);

  m_eldenRingSaveScope->setText(QCoreApplication::translate(
      "cid::ProfilePage",
      "This choice applies only to Elden Ring profiles in the instance being created."));
  m_eldenRingSaveScope->setWordWrap(true);
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
  // The descriptions for the three modes have different lengths. Reserve
  // enough lines so changing the mode cannot resize the wizard page.
  m_eldenRingSaveDescription->setMinimumHeight(
      m_eldenRingSaveDescription->fontMetrics().lineSpacing() * 4);
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

  // The save-isolation plugin provides the visible Elden Ring controls. Keep
  // this native group hidden as a model for its selected mode; showing both
  // groups briefly creates the legacy panel overlay during page changes.
  m_eldenRingSaveGroup->hide();

  // The heading also changes for Elden Ring; preserve its larger height in
  // both states so it cannot alter the page geometry on activation.
  const QString eldenRingHeading = QCoreApplication::translate(
      "cid::ProfilePage", "<h3>Choose how Elden Ring saves are handled.</h3>");
  auto* headingLayout = ui->widget_24->layout();
  ui->profileSettingsLabel->setText(eldenRingHeading);
  if (headingLayout != nullptr) {
    headingLayout->activate();
  }
  const int eldenRingHeadingHeight = ui->widget_24->sizeHint().height();

  ui->profileSettingsLabel->setText(m_defaultHeading);
  if (headingLayout != nullptr) {
    headingLayout->activate();
  }
  const int standardHeadingHeight = ui->widget_24->sizeHint().height();
  ui->widget_24->setMinimumHeight(qMax(eldenRingHeadingHeight, standardHeadingHeight));
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

  if (auto* contentLayout =
          qobject_cast<QVBoxLayout*>(ui->widget_25->layout())) {
    const int margin = isEldenRing ? 0 : 9;
    contentLayout->setContentsMargins(margin, margin, margin, margin);
    contentLayout->setSpacing(isEldenRing ? 0 : -1);
    if (contentLayout->count() > 0) {
      if (auto* spacer = contentLayout->itemAt(contentLayout->count() - 1)
                             ->spacerItem()) {
        spacer->changeSize(
            20, isEldenRing ? 0 : 40, QSizePolicy::Minimum,
            isEldenRing ? QSizePolicy::Fixed : QSizePolicy::Expanding);
      }
    }
    contentLayout->invalidate();
  }

  ui->widget_25->setStyleSheet(
      isEldenRing
          ? QStringLiteral(
                "QWidget#widget_25 { background-color: transparent; "
                "border: none; border-radius: 0; padding: 0; }")
          : QString());

  if (auto* pageLayout = qobject_cast<QVBoxLayout*>(ui->page->layout())) {
    ui->widget_25->setSizePolicy(
        QSizePolicy::Expanding,
        isEldenRing ? QSizePolicy::Maximum : QSizePolicy::Preferred);
    pageLayout->setAlignment(
        ui->widget_25,
        isEldenRing ? Qt::Alignment(Qt::AlignTop) : Qt::Alignment());
    pageLayout->invalidate();
    pageLayout->activate();
  }

  m_eldenRingSaveGroup->hide();
  ui->profileInisCheckbox->setVisible(!isEldenRing);
  ui->profileSavesCheckbox->setVisible(!isEldenRing);
  ui->archiveInvalidationCheckbox->setVisible(!isEldenRing);
  ui->profileSettingsLabel->setText(
      isEldenRing
          ? QCoreApplication::translate(
                "cid::ProfilePage", "<h3>Choose how Elden Ring saves are handled.</h3>")
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

  ui->pathPages->setCurrentIndex(0);

  if (auto* panelLayout = qobject_cast<QVBoxLayout*>(ui->widget_13->layout())) {
    panelLayout->setContentsMargins(14, 12, 14, 12);
    panelLayout->setSpacing(0);
  }

  QLabel* pathLabels[] = {ui->label_6, ui->label_8, ui->label_9,
                          ui->label_10, ui->label_12, ui->label_13};
  int pathLabelWidth = 0;
  for (QLabel* label : pathLabels) {
    pathLabelWidth = qMax(pathLabelWidth, label->sizeHint().width());
  }
  for (QLabel* label : pathLabels) {
    label->setMinimumWidth(pathLabelWidth);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }

  // Let both path-entry pages use the full width of the wizard. The simple
  // page otherwise sizes its location field only to its text-edit size hint,
  // which clips the generated instance path on narrow windows.
  for (QLineEdit* edit : {ui->location, ui->base, ui->downloads, ui->mods,
                           ui->profiles, ui->overwrite}) {
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* grid = qobject_cast<QGridLayout*>(edit->parentWidget()->layout());
    if (grid == nullptr) {
      continue;
    }

    int row = 0;
    int column = 0;
    int rowSpan = 0;
    int columnSpan = 0;
    const int itemIndex = grid->indexOf(edit);
    if (itemIndex >= 0) {
      grid->getItemPosition(itemIndex, &row, &column, &rowSpan, &columnSpan);
      grid->setColumnStretch(column, 1);
    }
  }

  QSize largestPathPageSize;
  for (int i = 0; i < ui->pathPages->count(); ++i) {
    QWidget* page = ui->pathPages->widget(i);
    page->ensurePolished();
    if (QLayout* pageLayout = page->layout()) {
      pageLayout->activate();
    }
    largestPathPageSize = largestPathPageSize.expandedTo(page->minimumSizeHint())
                              .expandedTo(page->sizeHint());
  }
  if (largestPathPageSize.isValid()) {
    // Keep the width stable for long paths. The active page's height is
    // adjusted when the wizard enters this step or the advanced toggle moves.
    ui->pathPages->setMinimumWidth(largestPathPageSize.width());
    ui->pathPages->setMinimumHeight(0);
    ui->pathPages->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->pathPages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
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

  if (ui->advancedPathOptions->isChecked()) {
    p.base      = ui->base->text();
    p.downloads = ui->downloads->text();
    p.mods      = ui->mods->text();
    p.profiles  = ui->profiles->text();
    p.overwrite = ui->overwrite->text();
  } else {
    p.base = ui->location->text();
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

void PathsPage::checkPaths()
{
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

ConfirmationPage::ConfirmationPage(CreateInstanceDialog& dlg) : Page(dlg) {}

void ConfirmationPage::doActivated(bool)
{
  ui->review->setPlainText(makeReview());
  ui->creationLog->clear();
  ui->creationLog->hide();
  ui->widget_19->updateGeometry();

  if (m_dlg.rawCreationInfo().type == CreateInstanceDialog::Isolated) {
    ui->label_17->setText(QObject::tr(
        "MO2 will copy its application files into a separate folder for this game. Close the current MO2 before starting the new copy."));
    ui->launch->setText(QObject::tr("Open the isolated copy's folder after setup"));
  } else {
    ui->label_17->setText(QObject::tr(
        "The instance is ready to be created. Review the details, then select Finish."));
    ui->launch->setText(QObject::tr("Launch the new instance"));
  }
}

QString ConfirmationPage::makeReview() const
{
  QStringList lines;

  const auto ci = m_dlg.rawCreationInfo();

  lines.push_back(QObject::tr("Instance type: %1").arg(toLocalizedString(ci.type)));

  lines.push_back(QObject::tr("Instance location: %1").arg(ci.dataPath));

  if (!ci.instanceName.isEmpty()) {
    lines.push_back(QObject::tr("Instance name: %1").arg(ci.instanceName));
  }

  lines.push_back(QObject::tr("Profile settings:"));
  if (EldenRingSaveSettings::supports(ci.game)) {
    lines.push_back(QObject::tr("  Elden Ring save mode: %1")
                        .arg(EldenRingSaveSettings::modeName(
                            ci.profileSettings.eldenRingSaveMode)));
  } else {
    lines.push_back(
        QObject::tr("  Local INIs: %1")
            .arg(ci.profileSettings.localInis ? QObject::tr("yes") : QObject::tr("no")));
    lines.push_back(QObject::tr("  Local Saves: %1")
                        .arg(ci.profileSettings.localSaves ? QObject::tr("yes")
                                                           : QObject::tr("no")));
    lines.push_back(QObject::tr("  Automatic Archive Invalidation: %1")
                        .arg(ci.profileSettings.archiveInvalidation
                                 ? QObject::tr("yes")
                                 : QObject::tr("no")));
  }

  if (ci.paths.downloads.isEmpty()) {
    // simple settings
    if (ci.paths.base != ci.dataPath) {
      lines.push_back(QObject::tr("Base directory: %1").arg(ci.paths.base));
    }
  } else {
    // advanced settings
    lines.push_back(QObject::tr("Base directory: %1").arg(ci.paths.base));
    lines.push_back(dirLine(QObject::tr("Downloads"), ci.paths.downloads));
    lines.push_back(dirLine(QObject::tr("Mods"), ci.paths.mods));
    lines.push_back(dirLine(QObject::tr("Profiles"), ci.paths.profiles));
    lines.push_back(dirLine(QObject::tr("Overwrite"), ci.paths.overwrite));
  }

  // game
  QString name = ci.game->gameName();
  if (!ci.gameVariant.isEmpty()) {
    name += " (" + ci.gameVariant + ")";
  }

  lines.push_back(QObject::tr("Game: %1").arg(name));
  lines.push_back(QObject::tr("Game location: %1").arg(ci.gameLocation));

  return lines.join("\n");
}

QString ConfirmationPage::dirLine(const QString& caption, const QString& path) const
{
  return QString("  - %1: %2").arg(caption).arg(path);
}

}  // namespace cid
