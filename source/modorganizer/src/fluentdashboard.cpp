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
#include <QPushButton>
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

QString modCountText(int count)
{
  return count == 1 ? QObject::tr("1 mod") : QObject::tr("%1 mods").arg(count);
}
}  // namespace

FluentHeroBanner::FluentHeroBanner(QWidget* parent) : QFrame(parent)
{
  setObjectName(QStringLiteral("fluentHero"));
}

FluentStatTile::FluentStatTile(char16_t glyph, const QString& caption, QWidget* parent)
    : QFrame(parent)
{
  setObjectName(QStringLiteral("fluentStatTile"));
  setFixedHeight(60);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(14, 8, 14, 8);
  m_layout->setSpacing(14);

  m_badge = new QLabel(QString(QChar(glyph)), this);
  m_badge->setObjectName(QStringLiteral("fluentStatBadge"));
  m_badge->setFont(iconFont(15));
  m_badge->setAlignment(Qt::AlignCenter);
  m_badge->setFixedSize(34, 34);
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
  // one compact bar: the game, the profile and program pickers, when it was
  // last played, and Run
  m_banner = new FluentHeroBanner(central);
  m_banner->setFixedHeight(56);

  auto* row = new QHBoxLayout(m_banner);
  row->setContentsMargins(16, 0, 10, 0);
  row->setSpacing(10);

  m_logo = new QLabel(m_banner);
  m_logo->setObjectName(QStringLiteral("fluentHeroLogo"));
  m_logo->setFixedSize(22, 22);
  m_title = new QLabel(m_banner);
  m_title->setObjectName(QStringLiteral("fluentHeroTitle"));
  row->addWidget(m_logo);
  row->addWidget(m_title);
  row->addSpacing(18);

  auto* profileLabel = new QLabel(tr("Profile"), m_banner);
  profileLabel->setObjectName(QStringLiteral("fluentHeroLabel"));
  row->addWidget(profileLabel);
  row->addWidget(ui->profileBox);
  ui->profileBox->setMinimumWidth(180);
  row->addSpacing(12);
  auto* programLabel = new QLabel(tr("Program"), m_banner);
  programLabel->setObjectName(QStringLiteral("fluentHeroLabel"));
  row->addWidget(programLabel);
  row->addWidget(ui->executablesListBox);
  ui->executablesListBox->setMinimumWidth(260);
  row->addSpacing(12);
  m_lastPlayed = new QLabel(m_banner);
  m_lastPlayed->setObjectName(QStringLiteral("fluentHeroLabel"));
  row->addWidget(m_lastPlayed);
  row->addStretch(1);

  // the button carries its own style sheet (see MainWindow), which wins over
  // the theme for its size
  ui->startButton->setProperty("fluentKeepIcon", true);
  ui->startButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  ui->startButton->setMinimumWidth(170);
  ui->startButton->setStyleSheet(
      QStringLiteral(
          "QToolButton#startButton { padding: 0px 0px 0px %2px; min-height: 34px; "
          "max-height: 34px; border-radius: 4px; }"
          "QToolButton#startButton::menu-button { width: %1px; "
          "border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
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

  // the same for the Downloads tab
  ui->horizontalLayout_16->addWidget(ui->btnRefreshDownloads);
  ui->btnRefreshDownloads->setText(QString());
  ui->btnRefreshDownloads->setFixedWidth(32);
  ui->btnRefreshDownloads->setAccessibleName(tr("Refresh"));
  ui->horizontalSpacer_7->changeSize(0, 0, QSizePolicy::Ignored, QSizePolicy::Ignored);
  ui->horizontalLayout->invalidate();
}

void FluentDashboard::refreshGame()
{
  const MOBase::IPluginGame* game = m_core.managedGame();
  if (game == nullptr) {
    return;
  }

  m_title->setText(game->gameName());

  const QIcon icon = game->gameIcon();
  m_logo->setPixmap(icon.pixmap(QSize(22, 22)));
  m_logo->setVisible(!icon.isNull());
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
