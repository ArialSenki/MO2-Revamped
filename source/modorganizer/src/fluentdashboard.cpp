#include "fluentdashboard.h"

#include "categories.h"
#include "modinfo.h"
#include "modlistview.h"
#include "modlistviewactions.h"
#include "organizercore.h"
#include "profile.h"
#include "runmenutoolbutton.h"
#include "ui_mainwindow.h"

#include <iplugingame.h>

#include <QBoxLayout>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QLabel>
#include <QLocale>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QStyle>

namespace
{
constexpr char16_t ActiveGlyph    = 0xE73E;
constexpr char16_t ConflictGlyph  = 0xE7BA;
constexpr char16_t OverwriteGlyph = 0xE8B7;
constexpr char16_t UpdateGlyph    = 0xE898;

// Overwrite can hold a whole game's worth of files; the tile only needs to
// know there are some
constexpr int OverwriteCountLimit = 9999;

QFont iconFont(int pointSize)
{
  QFont f;
  f.setFamilies({QStringLiteral("Segoe Fluent Icons"),
                 QStringLiteral("Segoe MDL2 Assets")});
  f.setPointSize(pointSize);
  return f;
}

void repolish(QWidget* w)
{
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}

// Steam installs that may hold the artwork cache: the one the game lives in
// (…/steamapps/common/<game>) and the one registered for the user
QStringList steamRoots(const QDir& gameDirectory)
{
  QStringList roots;
  QDir dir = gameDirectory;
  while (dir.cdUp()) {
    if (dir.dirName().compare(QStringLiteral("steamapps"), Qt::CaseInsensitive) == 0) {
      if (dir.cdUp()) {
        roots << dir.absolutePath();
      }
      break;
    }
  }

  const QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                        QSettings::NativeFormat);
  const QString registered =
      QDir::fromNativeSeparators(steam.value(QStringLiteral("SteamPath")).toString());
  if (!registered.isEmpty() && !roots.contains(registered, Qt::CaseInsensitive)) {
    roots << registered;
  }
  return roots;
}

// Steam keeps library artwork per app, in a folder of its own on current
// clients and as <appid>_<name> files on older ones
QString steamArtwork(const QStringList& roots, const QString& appId,
                     const QString& name)
{
  for (const QString& root : roots) {
    const QString cache = root + QStringLiteral("/appcache/librarycache/");
    for (const QString& candidate :
         {cache + appId + QLatin1Char('/') + name, cache + appId + QLatin1Char('_') + name}) {
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
    }
  }
  return {};
}

QString modCountText(int count)
{
  return count == 1 ? QObject::tr("1 mod") : QObject::tr("%1 mods").arg(count);
}
}  // namespace

FluentHeroBanner::FluentHeroBanner(QWidget* parent) : QFrame(parent)
{
  setObjectName(QStringLiteral("fluentHero"));
}

void FluentHeroBanner::setArt(const QPixmap& art)
{
  m_art = art;
  setProperty("hasArt", !art.isNull());
  repolish(this);
}

void FluentHeroBanner::paintEvent(QPaintEvent* event)
{
  QFrame::paintEvent(event);
  if (m_art.isNull()) {
    return;
  }

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
  QPainterPath clip;
  clip.addRoundedRect(area, 7, 7);
  p.setClipPath(clip);

  // cover the banner, keeping the middle of the artwork
  const QSizeF art  = m_art.deviceIndependentSize();
  const qreal scale = std::max(area.width() / art.width(), area.height() / art.height());
  const QSizeF drawn = art * scale;
  const QRectF target(area.center().x() - drawn.width() / 2,
                      area.center().y() - drawn.height() / 2, drawn.width(),
                      drawn.height());
  p.drawPixmap(target, m_art, QRectF(QPointF(0, 0), m_art.size()));

  // a scrim from the left keeps the logo, labels and pickers readable on any
  // artwork, in both themes
  QLinearGradient scrim(area.topLeft(), area.topRight());
  scrim.setColorAt(0.0, QColor(0, 0, 0, 215));
  scrim.setColorAt(0.55, QColor(0, 0, 0, 150));
  scrim.setColorAt(1.0, QColor(0, 0, 0, 50));
  p.fillPath(clip, scrim);
}

FluentStatTile::FluentStatTile(char16_t glyph, const QString& caption, QWidget* parent)
    : QFrame(parent)
{
  setObjectName(QStringLiteral("fluentStatTile"));
  setFixedHeight(72);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(16, 12, 16, 12);
  m_layout->setSpacing(14);

  m_badge = new QLabel(QString(QChar(glyph)), this);
  m_badge->setObjectName(QStringLiteral("fluentStatBadge"));
  m_badge->setFont(iconFont(15));
  m_badge->setAlignment(Qt::AlignCenter);
  m_badge->setFixedSize(40, 40);
  m_layout->addWidget(m_badge);

  auto* text = new QVBoxLayout;
  text->setSpacing(0);
  auto* captionLabel = new QLabel(caption, this);
  captionLabel->setObjectName(QStringLiteral("fluentStatCaption"));
  m_value = new QLabel(this);
  m_value->setObjectName(QStringLiteral("fluentStatValue"));
  text->addWidget(captionLabel);
  text->addWidget(m_value);
  m_layout->addLayout(text, 1);
}

void FluentStatTile::setValue(const QString& value, const QString& tone)
{
  m_value->setText(value);
  if (m_badge->property("tone").toString() != tone) {
    m_badge->setProperty("tone", tone);
    repolish(m_badge);
  }
}

QPushButton* FluentStatTile::addAction(const QString& text)
{
  auto* button = new QPushButton(text, this);
  button->setObjectName(QStringLiteral("fluentStatAction"));
  button->setCursor(Qt::PointingHandCursor);
  m_layout->addWidget(button, 0, Qt::AlignVCenter);
  return button;
}

FluentDashboard::FluentDashboard(Ui::MainWindow* mainUi, OrganizerCore& core,
                                 QWidget* window)
    : QObject(window), ui(mainUi), m_core(core)
{
  QWidget* central = ui->centralWidget;
  setupBanner(central);
  setupStats(central);
  setupModsHeader();

  m_refreshTimer.setSingleShot(true);
  m_refreshTimer.setInterval(150);
  connect(&m_refreshTimer, &QTimer::timeout, this, &FluentDashboard::refreshStats);
  connect(ui->modList, &ModListView::modCountChanged, this,
          &FluentDashboard::scheduleRefresh);
  connect(ui->modList, &ModListView::quickFilterChanged, this,
          &FluentDashboard::onQuickFilterChanged);

  refreshGame();
  scheduleRefresh();
}

void FluentDashboard::setupBanner(QWidget* central)
{
  m_banner = new FluentHeroBanner(central);
  m_banner->setFixedHeight(150);

  auto* row = new QHBoxLayout(m_banner);
  row->setContentsMargins(28, 16, 32, 16);
  row->setSpacing(24);

  auto* left = new QVBoxLayout;
  left->setSpacing(12);
  left->addStretch();

  m_logo = new QLabel(m_banner);
  m_logo->setObjectName(QStringLiteral("fluentHeroLogo"));
  m_title = new QLabel(m_banner);
  m_title->setObjectName(QStringLiteral("fluentHeroTitle"));
  left->addWidget(m_logo, 0, Qt::AlignLeft | Qt::AlignVCenter);
  left->addWidget(m_title, 0, Qt::AlignLeft | Qt::AlignVCenter);

  auto* pickers = new QHBoxLayout;
  pickers->setSpacing(10);
  auto* profileLabel = new QLabel(tr("Profile"), m_banner);
  profileLabel->setObjectName(QStringLiteral("fluentHeroLabel"));
  pickers->addWidget(profileLabel);
  pickers->addWidget(ui->profileBox);
  ui->profileBox->setMinimumWidth(200);
  pickers->addSpacing(18);
  auto* programLabel = new QLabel(tr("Program"), m_banner);
  programLabel->setObjectName(QStringLiteral("fluentHeroLabel"));
  pickers->addWidget(programLabel);
  pickers->addWidget(ui->executablesListBox);
  ui->executablesListBox->setMinimumWidth(280);
  pickers->addSpacing(18);
  m_lastPlayed = new QLabel(m_banner);
  m_lastPlayed->setObjectName(QStringLiteral("fluentHeroLabel"));
  pickers->addWidget(m_lastPlayed);
  pickers->addStretch();
  left->addLayout(pickers);
  left->addStretch();
  row->addLayout(left, 1);

  // the button carries its own style sheet (see MainWindow), which wins over
  // the theme for its size; make it the banner's large call to action
  ui->startButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  ui->startButton->setMinimumWidth(220);
  ui->startButton->setStyleSheet(
      QStringLiteral(
          "QToolButton#startButton { padding: 0px 0px 0px %2px; min-height: 50px; "
          "max-height: 50px; border-radius: 6px; font-size: 12pt; }"
          "QToolButton#startButton::menu-button { width: %1px; "
          "border-top-right-radius: 6px; border-bottom-right-radius: 6px; }"
          "QToolButton#startButton[menuButtonActive=\"true\"]::menu-button { "
          "background-color: rgba(0, 0, 0, 38); }")
          .arg(RunMenuToolButton::menuButtonWidth())
          .arg(RunMenuToolButton::menuButtonWidth() + 13));
  row->addWidget(ui->startButton, 0, Qt::AlignVCenter);

  // the old homes of the pickers and Run stay in the .ui, empty
  ui->label_3->hide();
  ui->startGroup->hide();

  auto* layout = qobject_cast<QBoxLayout*>(central->layout());
  layout->insertWidget(0, m_banner);
}

void FluentDashboard::setupStats(QWidget* central)
{
  auto* stats = new QWidget(central);
  stats->setObjectName(QStringLiteral("fluentStats"));
  stats->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto* row = new QHBoxLayout(stats);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  m_active    = new FluentStatTile(ActiveGlyph, tr("Active mods"), stats);
  m_conflicts = new FluentStatTile(ConflictGlyph, tr("With conflicts"), stats);
  m_overwrite = new FluentStatTile(OverwriteGlyph, tr("Overwrite"), stats);
  m_updates   = new FluentStatTile(UpdateGlyph, tr("Updates"), stats);
  for (auto* tile : {m_active, m_conflicts, m_overwrite, m_updates}) {
    row->addWidget(tile, 1);
  }

  m_conflictsAction = m_conflicts->addAction(tr("Show"));
  connect(m_conflictsAction, &QPushButton::clicked, this, [this] {
    ui->modList->setQuickFilter(CategoryFactory::Conflict);
  });

  m_overwriteAction = m_overwrite->addAction(tr("Manage"));
  connect(m_overwriteAction, &QPushButton::clicked, this, [this] {
    ModInfo::Ptr overwrite = ModInfo::getOverwrite();
    if (overwrite) {
      ui->modList->actions().displayModInformation(ModInfo::getIndex(overwrite->name()));
    }
  });

  m_updatesAction = m_updates->addAction(tr("Show"));
  connect(m_updatesAction, &QPushButton::clicked, this, [this] {
    ui->modList->setQuickFilter(CategoryFactory::UpdateAvailable);
  });

  auto* layout = qobject_cast<QBoxLayout*>(central->layout());
  layout->insertWidget(1, stats);
}

void FluentDashboard::setupModsHeader()
{
  QBoxLayout* header = ui->horizontalLayout_6;

  auto* title = new QLabel(tr("Mods"), ui->layoutWidget);
  title->setObjectName(QStringLiteral("fluentCardTitle"));
  header->insertWidget(0, title);
  header->insertSpacing(1, 8);

  const std::pair<QString, int> filters[] = {
      {tr("All"), -1},
      {tr("Active"), CategoryFactory::Checked},
      {tr("Conflicts"), CategoryFactory::Conflict},
  };
  int index = 2;
  for (const auto& [text, id] : filters) {
    auto* chip = new QPushButton(text, ui->layoutWidget);
    chip->setObjectName(QStringLiteral("fluentFilterChip"));
    chip->setCheckable(true);
    chip->setChecked(id == -1);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setProperty("specialCategory", id);
    connect(chip, &QPushButton::clicked, this, [this, chip, id] {
      // a chip is a choice, not a toggle; the filter change sets the states
      chip->setChecked(!chip->isChecked());
      ui->modList->setQuickFilter(id);
    });
    header->insertWidget(index++, chip);
    m_chips << chip;
  }
  header->insertStretch(index++, 1);

  // the search box moves up next to the "more" button
  const int moreIndex = header->indexOf(ui->layoutWidget->findChild<QWidget*>(
      QStringLiteral("moreActionsBtn")));
  header->insertWidget(moreIndex < 0 ? index : moreIndex, ui->modFilterEdit);
  ui->modFilterEdit->setMinimumWidth(240);
  ui->modFilterEdit->setMaximumWidth(320);

  // the active count lives in the summary row now
  ui->activeModslabel->hide();
  ui->activeModsCounter->hide();

  // the Data tab's refresh joins the filter row, so the tree starts right
  // under the tabs
  ui->horizontalLayout_13->addWidget(ui->dataTabRefresh);
  ui->dataTabRefresh->setText(QString());
  ui->dataTabRefresh->setFixedWidth(32);
  ui->dataTabRefresh->setAccessibleName(tr("Refresh"));
  ui->horizontalSpacer_4->changeSize(0, 0, QSizePolicy::Ignored, QSizePolicy::Ignored);
  ui->horizontalLayout_12->invalidate();
}

void FluentDashboard::refreshGame()
{
  const MOBase::IPluginGame* game = m_core.managedGame();
  if (game == nullptr) {
    return;
  }

  m_title->setText(game->gameName());

  QPixmap art;
  QPixmap logo;
  const QString appId = game->steamAPPId();
  if (!appId.isEmpty()) {
    const QStringList roots = steamRoots(game->gameDirectory());
    art.load(steamArtwork(roots, appId, QStringLiteral("library_hero.jpg")));
    if (art.isNull()) {
      art.load(steamArtwork(roots, appId, QStringLiteral("header.jpg")));
    }
    logo.load(steamArtwork(roots, appId, QStringLiteral("logo.png")));
  }
  m_banner->setArt(art);

  if (!logo.isNull() && !art.isNull()) {
    const qreal ratio = m_logo->devicePixelRatioF();
    QPixmap scaled    = logo.scaled(QSize(320, 60) * ratio, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(ratio);
    m_logo->setPixmap(scaled);
    m_logo->setAccessibleName(game->gameName());
    m_logo->show();
    m_title->hide();
  } else {
    m_logo->hide();
    m_title->show();
  }
}

void FluentDashboard::scheduleRefresh()
{
  m_refreshTimer.start();
}

void FluentDashboard::refreshStats()
{
  // the old counter may have been wrapped in a frame (LCDNumber::useFrame)
  // after the header was set up; keep that frame out of the header too
  if (QWidget* frame = ui->activeModsCounter->parentWidget();
      frame != nullptr && frame->property("lcdFrame").toBool()) {
    frame->hide();
  }

  const Profile* profile = m_core.currentProfile();
  if (profile == nullptr) {
    return;
  }

  int total = 0, active = 0, conflicts = 0, updates = 0;
  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    ModInfo::Ptr mod = ModInfo::getByIndex(i);
    if (mod->isSeparator() || mod->isForeign() || mod->isBackup() || mod->isOverwrite()) {
      continue;
    }
    ++total;
    if (profile->modEnabled(i)) {
      ++active;
      if (!mod->getConflictFlags().empty()) {
        ++conflicts;
      }
    }
    if (mod->updateAvailable()) {
      ++updates;
    }
  }

  int overwriteFiles = 0;
  QDirIterator files(m_core.overwritePath(), QDir::Files | QDir::Hidden | QDir::System,
                     QDirIterator::Subdirectories);
  while (files.hasNext() && overwriteFiles < OverwriteCountLimit) {
    files.next();
    ++overwriteFiles;
  }
  // the list marks Overwrite as soon as it holds anything, folders included
  const bool overwriteFolders =
      overwriteFiles == 0 &&
      !QDir(m_core.overwritePath())
           .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)
           .isEmpty();

  m_active->setValue(tr("%1 of %2").arg(active).arg(total), QStringLiteral("accent"));
  m_conflicts->setValue(conflicts > 0 ? modCountText(conflicts) : tr("None"),
                        conflicts > 0 ? QStringLiteral("caution")
                                      : QStringLiteral("neutral"));
  m_conflictsAction->setVisible(conflicts > 0);

  QString overwriteText = overwriteFolders ? tr("Empty folders") : tr("Empty");
  if (overwriteFiles >= OverwriteCountLimit) {
    overwriteText = tr("%1+ files").arg(OverwriteCountLimit);
  } else if (overwriteFiles > 0) {
    overwriteText = overwriteFiles == 1 ? tr("1 file") : tr("%1 files").arg(overwriteFiles);
  }
  m_overwrite->setValue(overwriteText, overwriteFiles > 0 ? QStringLiteral("critical")
                                                          : QStringLiteral("neutral"));
  m_overwriteAction->setVisible(overwriteFiles > 0 || overwriteFolders);

  refreshLastPlayed();

  m_updates->setValue(updates > 0 ? tr("%1 available").arg(updates) : tr("Up to date"),
                      updates > 0 ? QStringLiteral("accent") : QStringLiteral("neutral"));
  m_updatesAction->setVisible(updates > 0);
}

void FluentDashboard::setSavesDirectory(std::function<QDir()> provider)
{
  m_savesDirectory = std::move(provider);
  scheduleRefresh();
}

void FluentDashboard::refreshLastPlayed()
{
  QDateTime newest;
  if (m_savesDirectory) {
    // saves sit at most a couple of folders deep (per account or slot); stop
    // early on folders that hold far more than saves
    int seen = 0;
    QDirIterator saves(m_savesDirectory().absolutePath(), QDir::Files,
                       QDirIterator::Subdirectories);
    while (saves.hasNext() && seen++ < 2000) {
      saves.next();
      const QDateTime modified = saves.fileInfo().lastModified();
      if (!newest.isValid() || modified > newest) {
        newest = modified;
      }
    }
  }

  if (!newest.isValid()) {
    m_lastPlayed->hide();
    return;
  }

  const qint64 days = newest.date().daysTo(QDate::currentDate());
  QString when;
  if (days == 0) {
    when = tr("today at %1").arg(QLocale().toString(newest.time(), QLocale::ShortFormat));
  } else if (days == 1) {
    when = tr("yesterday");
  } else if (days < 7) {
    when = tr("%1 days ago").arg(days);
  } else {
    when = QLocale().toString(newest.date(), QLocale::ShortFormat);
  }
  m_lastPlayed->setText(tr("Last played %1").arg(when));
  m_lastPlayed->show();
}

void FluentDashboard::onQuickFilterChanged(int specialCategory)
{
  for (QPushButton* chip : m_chips) {
    chip->setChecked(chip->property("specialCategory").toInt() == specialCategory);
  }
}
