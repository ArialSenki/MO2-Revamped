/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "profilesdialog.h"
#include "fluentdialogs.h"
#include "fluenticons.h"
#include "menuutils.h"

#include "bsainvalidation.h"
#include "eldenringsavesettings.h"
#include "filesystemutilities.h"
#include "game_features.h"
#include "iplugingame.h"
#include "localsavegames.h"
#include "modinfo.h"
#include "organizercore.h"
#include "profile.h"
#include "profileinputdialog.h"
#include "report.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "transfersavesdialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWhatsThis>

#include <Windows.h>

#include <exception>

using namespace MOBase;
using namespace MOShared;

Q_DECLARE_METATYPE(Profile::Ptr)

namespace
{
// Segoe Fluent Icons code points
constexpr char16_t AddGlyph      = 0xE710;
constexpr char16_t RenameGlyph   = 0xE8AC;
constexpr char16_t CopyGlyph     = 0xE8C8;
constexpr char16_t DeleteGlyph   = 0xE74D;
constexpr char16_t HelpGlyph     = 0xE897;
constexpr char16_t PersonGlyph   = 0xE77B;  // Contact
constexpr char16_t ModsGlyph     = 0xE8F1;  // Library
constexpr char16_t FolderGlyph   = 0xE8B7;
constexpr char16_t ClockGlyph    = 0xE823;  // Recent
constexpr char16_t SaveGlyph     = 0xE74E;
constexpr char16_t SettingsGlyph = 0xE713;
constexpr char16_t ArchiveGlyph  = 0xF012;  // Zip folder
constexpr char16_t TransferGlyph = 0xE8AB;
constexpr char16_t PlayGlyph     = 0xE768;

void styleProfilePrompt(QMessageBox& prompt,
                        QMessageBox::StandardButton confirmButton = QMessageBox::No,
                        bool destructive = false)
{
  prompt.setObjectName("revampedProfileActionPrompt");
  prompt.setWindowTitle("MO2 Revamped");
  prompt.setTextFormat(Qt::PlainText);

  if (QLabel* label = prompt.findChild<QLabel*>("qt_msgbox_label")) {
    label->setWordWrap(true);
  }
  if (QLabel* label = prompt.findChild<QLabel*>("qt_msgbox_informativelabel")) {
    label->setWordWrap(true);
  }

  if (QAbstractButton* button = prompt.button(confirmButton)) {
    button->setObjectName(destructive ? "profileDestructiveButton"
                                      : "profilePromptConfirmButton");
  }
  if (QAbstractButton* button = prompt.button(QMessageBox::No)) {
    button->setObjectName("profileCancelButton");
  }
  if (QAbstractButton* button = prompt.button(QMessageBox::Cancel)) {
    button->setObjectName("profileCancelButton");
  }
}

void showProfileNotice(QWidget* parent, QMessageBox::Icon icon,
                       const QString& heading, const QString& details)
{
  QMessageBox prompt(icon, "MO2 Revamped", heading, QMessageBox::Ok, parent);
  prompt.setInformativeText(details);
  styleProfilePrompt(prompt, QMessageBox::Ok);
  prompt.exec();
}

// the regular mods a profile has enabled, and how many there are
std::pair<int, int> enabledMods(const Profile& profile)
{
  int enabled = 0, total = 0;
  const unsigned int count =
      std::min<unsigned int>(static_cast<unsigned int>(profile.numMods()),
                             ModInfo::getNumMods());
  for (unsigned int i = 0; i < count; ++i) {
    const auto info = ModInfo::getByIndex(i);
    if (info->isSeparator() || info->isForeign() || info->isOverwrite() ||
        info->isBackup()) {
      continue;
    }
    ++total;
    if (profile.modEnabled(i)) {
      ++enabled;
    }
  }
  return {enabled, total};
}

// "today at 13:20", "yesterday at 9:05" or the date, like the instance list
QString whenText(const QDateTime& time)
{
  if (!time.isValid()) {
    return QObject::tr("Unknown");
  }
  const QLocale locale;
  const QDate today = QDate::currentDate();
  const QString clock = locale.toString(time.time(), QLocale::ShortFormat);
  if (time.date() == today) {
    return ProfilesDialog::tr("Today at %1").arg(clock);
  }
  if (time.date() == today.addDays(-1)) {
    return ProfilesDialog::tr("Yesterday at %1").arg(clock);
  }
  return locale.toString(time.date(), QLocale::ShortFormat);
}
}  // namespace

ProfilesDialog::ProfilesDialog(const QString& profileName, OrganizerCore& organizer,
                               QWidget* parent)
    : TutorableDialog("Profiles", parent), m_GameFeatures(organizer.gameFeatures()),
      m_FailState(false), m_Game(organizer.managedGame()), m_ActiveProfileName(""),
      m_eldenRing(EldenRingSaveSettings::supports(m_Game))
{
  setupUi();

  QDir profilesDir(Settings::instance().paths().profiles());
  profilesDir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

  QDirIterator profileIter(profilesDir);

  // the list is filled before the first selection, so the current profile's
  // row is known when its caption is written
  m_ActiveProfileName = profileName;
  QListWidgetItem* active = nullptr;
  while (profileIter.hasNext()) {
    profileIter.next();
    QListWidgetItem* item = addItem(profileIter.filePath());
    if (profileName == profileIter.fileName()) {
      active = item;
    }
  }
  m_list->sortItems();
  for (int i = 0; i < m_list->count(); ++i) {
    updateItem(m_list->item(i));
  }
  if (active != nullptr) {
    m_list->setCurrentItem(active);
  } else {
    m_ActiveProfileName.clear();
    onCurrentChanged(m_list->currentItem());
  }

  connect(this, &ProfilesDialog::profileCreated, &organizer,
          &OrganizerCore::profileCreated);
  connect(this, &ProfilesDialog::profileRenamed, &organizer,
          &OrganizerCore::profileRenamed);
  connect(this, &ProfilesDialog::profileRemoved, &organizer,
          &OrganizerCore::profileRemoved);
}

ProfilesDialog::~ProfilesDialog() = default;

void ProfilesDialog::setupUi()
{
  setObjectName(QStringLiteral("ProfilesDialog"));
  setWindowTitle(tr("Profiles"));
  setMinimumSize(820, 520);
  resize(940, 620);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  auto* body = new QHBoxLayout;
  body->setSpacing(8);
  body->addWidget(createListCard());
  body->addWidget(createDetails(), 1);
  layout->addLayout(body, 1);

  // help on the left, Use and Close on the right
  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  auto* help = new QToolButton(this);
  help->setObjectName(QStringLiteral("instanceHelpButton"));
  help->setText(tr("What is a profile?"));
  help->setIcon(FluentIcons::glyph(HelpGlyph, FluentIcons::Tone::Muted));
  help->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  help->setAutoRaise(true);
  help->setCursor(Qt::PointingHandCursor);
  footer->addWidget(help);
  footer->addStretch(1);

  m_select = new QPushButton(this);
  m_select->setObjectName(QStringLiteral("select"));
  m_select->setProperty("fluentPrimary", true);
  m_select->setAutoDefault(false);
  auto* close = new QPushButton(tr("Close"), this);
  close->setObjectName(QStringLiteral("close"));
  close->setAutoDefault(false);
  footer->addWidget(m_select);
  footer->addWidget(close);
  layout->addLayout(footer);

  connect(help, &QToolButton::clicked, this, &ProfilesDialog::showHelp);
  connect(m_select, &QPushButton::clicked, this, &ProfilesDialog::selectProfile);
  connect(close, &QPushButton::clicked, this, &QDialog::close);

  // keyboard: Enter uses the profile (the list does that), F2 renames,
  // Delete removes, Ctrl+D duplicates and Ctrl+N creates; each only does what
  // its button could do right now
  auto shortcut = [this](const QKeySequence& keys, QAbstractButton* button,
                         void (ProfilesDialog::*slot)()) {
    auto* sc = new QShortcut(keys, this);
    sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc, &QShortcut::activated, this, [this, button, slot] {
      if (button->isEnabled() && !button->isHidden()) {
        (this->*slot)();
      }
    });
  };
  shortcut(QKeySequence(Qt::Key_F2), m_rename, &ProfilesDialog::renameProfile);
  shortcut(QKeySequence(Qt::Key_Delete), m_remove, &ProfilesDialog::removeProfile);
  shortcut(QKeySequence(Qt::CTRL | Qt::Key_D), m_copy, &ProfilesDialog::copyProfile);
  shortcut(QKeySequence(Qt::CTRL | Qt::Key_N), m_new, &ProfilesDialog::addProfile);
}

QWidget* ProfilesDialog::createListCard()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("profileListCard"));
  card->setProperty("fluentCard", true);
  card->setFixedWidth(260);

  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Profiles"), card);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_count = new QLabel(card);
  m_count->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_count);
  layout->addLayout(title);

  // a QListWidget named profilesList, as plugins expect it
  m_list = new QListWidget(card);
  m_list->setObjectName(QStringLiteral("profilesList"));
  m_list->setItemDelegate(new FluentPanels::RowDelegate(m_list, 56, 24));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setMouseTracking(true);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setAccessibleName(tr("Profiles"));
  m_list->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(m_list, 1);

  m_new = new QPushButton(FluentIcons::glyph(AddGlyph), tr("New profile"), card);
  m_new->setObjectName(QStringLiteral("addProfileButton"));
  m_new->setAutoDefault(false);
  m_new->setToolTip(tr("Create a profile with its own mod list (Ctrl+N)"));
  layout->addWidget(m_new);

  connect(m_new, &QPushButton::clicked, this, &ProfilesDialog::addProfile);
  connect(m_list, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current) {
            onCurrentChanged(current);
          });
  connect(m_list, &QListWidget::itemActivated, this, &ProfilesDialog::selectProfile);
  connect(m_list, &QListWidget::customContextMenuRequested, this,
          &ProfilesDialog::showListMenu);

  return card;
}

QWidget* ProfilesDialog::createDetails()
{
  m_details = new QStackedWidget(this);
  m_details->setObjectName(QStringLiteral("profileDetailsStack"));

  // nothing selected
  auto* empty = new QFrame(m_details);
  empty->setProperty("fluentCard", true);
  auto* emptyLayout = new QVBoxLayout(empty);
  auto* emptyText = new QLabel(tr("Select a profile to see its mods and options."), empty);
  emptyText->setObjectName(QStringLiteral("instanceEmptyText"));
  emptyText->setAlignment(Qt::AlignCenter);
  emptyText->setWordWrap(true);
  emptyLayout->addWidget(emptyText);
  m_details->addWidget(empty);

  auto* card = new QFrame(m_details);
  card->setObjectName(QStringLiteral("profileDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // the name, whether it is in use, and the pencil to rename it
  auto* head = new QHBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(10);
  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  m_name = new QLabel(card);
  m_name->setObjectName(QStringLiteral("instanceDetailName"));
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  names->addWidget(m_name);
  names->addWidget(m_caption);
  head->addLayout(names, 1);
  m_rename = FluentPanels::iconButton(RenameGlyph, tr("Rename"), card);
  m_rename->setObjectName(QStringLiteral("renameButton"));
  head->addWidget(m_rename, 0, Qt::AlignVCenter);
  layout->addLayout(head);
  connect(m_rename, &QToolButton::clicked, this, &ProfilesDialog::renameProfile);

  // the rows scroll when the window is too short for all of them
  auto* scroll = new QScrollArea(card);
  scroll->setObjectName(QStringLiteral("instanceDetailsScroll"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* rows = new QWidget(scroll);
  rows->setObjectName(QStringLiteral("instanceDetailRows"));
  auto* rowsLayout = new QVBoxLayout(rows);
  rowsLayout->setContentsMargins(0, 0, 0, 0);
  rowsLayout->setSpacing(0);
  scroll->setWidget(rows);
  layout->addWidget(scroll, 1);

  m_modsRow    = FluentPanels::addDetailRow(rows, ModsGlyph, tr("Mods"));
  m_folderRow  = FluentPanels::addDetailRow(rows, FolderGlyph, tr("Profile folder"),
                                            tr("Open the profile folder"));
  m_changedRow = FluentPanels::addDetailRow(rows, ClockGlyph, tr("Mod list changed"));
  connect(m_folderRow.action, &QToolButton::clicked, this, &ProfilesDialog::exploreProfile);

  // Elden Ring saves follow the Save Isolation tool, so the row only says so;
  // other games get MO2's own switches
  m_savesRow = FluentPanels::addDetailRow(rows, SaveGlyph, tr("Saves"));
  m_savesRow.frame->setVisible(m_eldenRing);

  m_localSaves = new QCheckBox(rows);
  m_localSaves->setObjectName(QStringLiteral("localSavesBox"));
  FluentPanels::addSwitchRow(rows, SaveGlyph, tr("Profile-specific saves"),
                             tr("This profile keeps its own save games instead of "
                                "sharing the game's."),
                             m_localSaves);

  m_localIniFiles = new QCheckBox(rows);
  m_localIniFiles->setObjectName(QStringLiteral("localIniFilesBox"));
  FluentPanels::addSwitchRow(rows, SettingsGlyph, tr("Profile-specific game INI files"),
                             tr("This profile keeps its own copy of the game's INI "
                                "files, so its settings don't change other profiles."),
                             m_localIniFiles);

  m_invalidation = new QCheckBox(rows);
  m_invalidation->setObjectName(QStringLiteral("invalidationBox"));
  FluentPanels::addSwitchRow(rows, ArchiveGlyph, tr("Automatic archive invalidation"),
                             tr("Makes the game load loose files over the ones in its "
                                "archives."),
                             m_invalidation);
  rowsLayout->addStretch(1);

  if (m_eldenRing) {
    // Elden Ring routes saves through its dedicated Tools dialog
    m_localSaves->hide();
    m_localIniFiles->hide();
    m_invalidation->hide();
  } else {
    if (m_GameFeatures.gameFeature<BSAInvalidation>() == nullptr) {
      m_invalidation->hide();
    }
    if (!m_GameFeatures.gameFeature<LocalSavegames>()) {
      m_localSaves->hide();
    }
  }

  connect(m_localSaves, &QCheckBox::toggled, this, &ProfilesDialog::onLocalSavesChanged);
  connect(m_localIniFiles, &QCheckBox::toggled, this,
          &ProfilesDialog::onLocalIniFilesChanged);
  connect(m_invalidation, &QCheckBox::toggled, this,
          &ProfilesDialog::onInvalidationChanged);

  // duplicate and transfer on the left, delete on the right
  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  m_copy = new QPushButton(FluentIcons::glyph(CopyGlyph), tr("Duplicate"), actions);
  m_copy->setObjectName(QStringLiteral("copyProfileButton"));
  m_copy->setAutoDefault(false);
  m_copy->setToolTip(tr("Create a profile with the same mods and options (Ctrl+D)"));
  m_transfer =
      new QPushButton(FluentIcons::glyph(TransferGlyph), tr("Transfer saves..."), actions);
  m_transfer->setObjectName(QStringLiteral("transferButton"));
  m_transfer->setAutoDefault(false);
  m_transfer->setToolTip(tr("Copy or move saves between this profile and the game"));
  m_transfer->setVisible(!m_eldenRing);
  m_remove = new QPushButton(FluentIcons::glyph(DeleteGlyph, FluentIcons::Tone::Critical),
                             tr("Delete profile"), actions);
  m_remove->setObjectName(QStringLiteral("removeProfileButton"));
  m_remove->setAutoDefault(false);
  actionsLayout->addWidget(m_copy);
  actionsLayout->addWidget(m_transfer);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_remove);
  layout->addWidget(actions);

  connect(m_copy, &QPushButton::clicked, this, &ProfilesDialog::copyProfile);
  connect(m_transfer, &QPushButton::clicked, this, &ProfilesDialog::transferSaves);
  connect(m_remove, &QPushButton::clicked, this, &ProfilesDialog::removeProfile);

  m_details->addWidget(card);
  return m_details;
}

int ProfilesDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);
  return QDialog::exec();
}

void ProfilesDialog::showEvent(QShowEvent* event)
{
  TutorableDialog::showEvent(event);

  if (m_list->count() == 0) {
    QPoint pos = m_list->mapToGlobal(QPoint(0, 0));
    pos.rx() += m_list->width() / 2;
    pos.ry() += (m_list->height() / 2) - 20;
    QWhatsThis::showText(
        pos,
        QObject::tr(
            "Before you can use ModOrganizer, you need to create at least one profile. "
            "ATTENTION: Run the game at least once before creating a profile!"),
        m_list);
  }
}

std::optional<QString> ProfilesDialog::selectedProfile() const
{
  return m_Selected;
}

Profile::Ptr ProfilesDialog::currentProfile() const
{
  const QListWidgetItem* item = m_list->currentItem();
  if (item == nullptr) {
    return {};
  }
  return item->data(Qt::UserRole).value<Profile::Ptr>();
}

QListWidgetItem* ProfilesDialog::addItem(const QString& name)
{
  QDir profileDir(name);
  QListWidgetItem* newItem = new QListWidgetItem(profileDir.dirName(), m_list);
  try {
    newItem->setData(Qt::UserRole, QVariant::fromValue(Profile::Ptr(new Profile(
                                       profileDir, m_Game, m_GameFeatures))));
    m_FailState = false;
  } catch (const std::exception& e) {
    reportError(tr("failed to create profile: %1").arg(e.what()));
  }
  return newItem;
}

void ProfilesDialog::updateItem(QListWidgetItem* item)
{
  if (item == nullptr) {
    return;
  }

  const bool active = item->text() == m_ActiveProfileName;
  item->setIcon(FluentIcons::glyph(PersonGlyph, active ? FluentIcons::Tone::Accent
                                                        : FluentIcons::Tone::Muted));

  const Profile::Ptr profile = item->data(Qt::UserRole).value<Profile::Ptr>();
  QString caption;
  if (!profile) {
    caption = tr("Can't be read");
  } else {
    const auto [enabled, total] = enabledMods(*profile);
    caption = tr("%1 of %2 mods").arg(enabled).arg(total);
    if (active) {
      caption = tr("In use") + QStringLiteral(" · ") + caption;
    }
  }
  item->setData(FluentPanels::CaptionRole, caption);
  m_count->setText(QString::number(m_list->count()));
}

void ProfilesDialog::createProfile(const QString& name, bool useDefaultSettings)
{
  try {
    auto profile =
        Profile::Ptr(new Profile(name, m_Game, m_GameFeatures, useDefaultSettings));
    QListWidgetItem* newItem = new QListWidgetItem(name, m_list);
    newItem->setData(Qt::UserRole, QVariant::fromValue(profile));
    m_list->addItem(newItem);
    m_FailState = false;
    updateItem(newItem);
    m_list->setCurrentItem(newItem);
    emit profileCreated(profile.get());
  } catch (const std::exception&) {
    m_FailState = true;
    throw;
  }
}

void ProfilesDialog::createProfile(const QString& name, const Profile& reference)
{
  try {
    auto profile = Profile::Ptr(Profile::createPtrFrom(name, reference, m_Game));
    QListWidgetItem* newItem = new QListWidgetItem(name, m_list);
    newItem->setData(Qt::UserRole, QVariant::fromValue(profile));
    m_list->addItem(newItem);
    m_FailState = false;
    updateItem(newItem);
    m_list->setCurrentItem(newItem);
    emit profileCreated(profile.get());
  } catch (const std::exception&) {
    m_FailState = true;
    throw;
  }
}

void ProfilesDialog::fillDetails()
{
  const Profile::Ptr profile = currentProfile();
  QListWidgetItem* item      = m_list->currentItem();
  if (item == nullptr) {
    m_details->setCurrentIndex(0);
    m_copy->setEnabled(false);
    m_remove->setEnabled(false);
    m_rename->setEnabled(false);
    m_select->setEnabled(false);
    m_select->setText(tr("Use this profile"));
    return;
  }

  m_details->setCurrentIndex(1);
  const bool active = item->text() == m_ActiveProfileName;
  m_name->setText(item->text());
  m_caption->setText(active ? tr("In use by the main window")
                            : tr("Not in use"));

  m_select->setText(active ? tr("Current profile") : tr("Use this profile"));
  m_select->setEnabled(profile && !active);
  m_select->setToolTip(active ? QString() : tr("Switch the main window to this profile"));

  // the profile in use can't be renamed or removed while MO2 has it open
  m_rename->setEnabled(profile && !active);
  m_rename->setToolTip(active ? tr("Switch to another profile before renaming this one")
                              : tr("Rename (F2)"));
  m_remove->setEnabled(!active);
  m_remove->setToolTip(active ? tr("Switch to another profile before deleting this one")
                              : tr("Delete the profile and its own saves (Delete)"));
  m_copy->setEnabled(static_cast<bool>(profile));

  if (!profile) {
    m_modsRow.value->setFullText(tr("This profile can't be read"));
    m_folderRow.value->setFullText(QDir::toNativeSeparators(
        Settings::instance().paths().profiles() + "/" + item->text()));
    m_changedRow.value->setFullText(tr("Unknown"));
    return;
  }

  const auto [enabled, total] = enabledMods(*profile);
  m_modsRow.value->setFullText(tr("%1 of %2 mods enabled").arg(enabled).arg(total));
  m_folderRow.value->setFullText(QDir::toNativeSeparators(profile->absolutePath()));
  m_changedRow.value->setFullText(
      whenText(QFileInfo(profile->getModlistFileName()).lastModified()));
  m_savesRow.value->setFullText(tr("Chosen in Tools > Save Isolation"));

  try {
    bool invalidationSupported = false;
    QSignalBlocker invalidation(m_invalidation);
    m_invalidation->setChecked(profile->invalidationActive(&invalidationSupported));
    m_invalidation->setEnabled(invalidationSupported);

    // blocked, otherwise the saves switch could think local saves were turned
    // off and delete the files or rename the folder
    const bool localSaves = profile->localSavesEnabled();
    QSignalBlocker saves(m_localSaves);
    m_localSaves->setChecked(localSaves);
    m_transfer->setEnabled(localSaves);
    m_transfer->setToolTip(localSaves
                               ? tr("Copy or move saves between this profile and the game")
                               : tr("Turn on profile-specific saves to transfer saves"));

    QSignalBlocker iniFiles(m_localIniFiles);
    m_localIniFiles->setChecked(profile->localSettingsEnabled());
  } catch (const std::exception& e) {
    reportError(tr("failed to determine if invalidation is active: %1").arg(e.what()));
    m_copy->setEnabled(false);
    m_remove->setEnabled(false);
    m_rename->setEnabled(false);
  }
}

void ProfilesDialog::onCurrentChanged(QListWidgetItem*)
{
  fillDetails();
}

void ProfilesDialog::showListMenu(const QPoint& pos)
{
  QListWidgetItem* item = m_list->itemAt(pos);
  if (item == nullptr) {
    QMenu menu(this);
    menu.addAction(FluentIcons::glyph(AddGlyph), tr("New profile..."), this,
                   &ProfilesDialog::addProfile);
    menu.addAction(menuGlyph(MenuGlyphs::Folder), tr("Open profiles folder"), this, [] {
      shell::Explore(Settings::instance().paths().profiles());
    });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
    return;
  }
  m_list->setCurrentItem(item);

  QMenu menu(this);
  auto add = [&](char16_t glyph, const QString& text, QAbstractButton* mirror,
                 void (ProfilesDialog::*slot)(), const QKeySequence& keys = {}) {
    auto* action = menu.addAction(FluentIcons::glyph(glyph), text, this, slot);
    action->setShortcut(keys);
    if (mirror != nullptr) {
      action->setEnabled(mirror->isEnabled());
      action->setVisible(!mirror->isHidden());
    }
    return action;
  };

  setDefaultMenuAction(&menu, add(PlayGlyph, tr("Use this profile"), m_select,
                                  &ProfilesDialog::selectProfile));
  menu.addSeparator();
  add(RenameGlyph, tr("Rename..."), m_rename, &ProfilesDialog::renameProfile,
      QKeySequence(Qt::Key_F2));
  add(CopyGlyph, tr("Duplicate..."), m_copy, &ProfilesDialog::copyProfile,
      QKeySequence(Qt::CTRL | Qt::Key_D));
  if (!m_eldenRing) {
    add(TransferGlyph, tr("Transfer saves..."), m_transfer, &ProfilesDialog::transferSaves);
  }
  menu.addSeparator();

  const Profile::Ptr profile = currentProfile();
  const QString folder       = profile ? profile->absolutePath()
                                       : Settings::instance().paths().profiles() + "/" +
                                       item->text();
  menu.addAction(menuGlyph(MenuGlyphs::Folder), tr("Open folder"), this,
                 &ProfilesDialog::exploreProfile);
  addCopyMenu(&menu, {{tr("Name"), item->text()}, {tr("Folder path"), nativePath(folder)}});
  menu.addSeparator();
  add(DeleteGlyph, tr("Delete profile..."), m_remove, &ProfilesDialog::removeProfile,
      QKeySequence(Qt::Key_Delete));

  menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void ProfilesDialog::showHelp()
{
  FluentContentDialog dlg(
      this, QStringLiteral("ProfileHelpDialog"), tr("What is a profile?"),
      tr("A profile is one setup of the same instance: which mods are enabled, their "
         "order and, if you want, its own saves and game INI files. Profiles share "
         "the instance's mods and downloads, so a second profile costs no disk "
         "space.\n\nUse one per playthrough or per mod list you want to try. The "
         "profile in use is chosen next to Program in the main window."));
  auto* ok = dlg.addButton(tr("OK"), true);
  connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  dlg.resize(520, dlg.sizeHint().height());
  dlg.exec();
}

void ProfilesDialog::selectProfile()
{
  const Profile::Ptr profile = currentProfile();
  if (!profile || !m_select->isEnabled()) {
    return;
  }

  m_Selected = profile->name();
  close();
}

void ProfilesDialog::addProfile()
{
  ProfileInputDialog dialog(this);
  bool okClicked = dialog.exec();
  QString name   = dialog.getName();

  if (okClicked && (name.size() > 0)) {
    try {
      createProfile(name, dialog.getPreferDefaultSettings());
    } catch (const std::exception& e) {
      reportError(tr("failed to create profile: %1").arg(e.what()));
    }
  }
}

void ProfilesDialog::copyProfile()
{
  const Profile::Ptr profile = currentProfile();
  if (!profile) {
    return;
  }

  ProfileInputDialog dialog(this, ProfileInputDialog::Purpose::Copy);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QString name = dialog.getName();
  if (!fixDirectoryName(name) || name.isEmpty()) {
    showProfileNotice(this, QMessageBox::Warning, tr("Invalid profile name"),
                      tr("Choose a valid name for this profile."));
    return;
  }

  try {
    createProfile(name, *profile);
  } catch (const std::exception& e) {
    reportError(tr("failed to copy profile: %1").arg(e.what()));
  }
}

void ProfilesDialog::removeProfile()
{
  QListWidgetItem* selectedItem = m_list->currentItem();
  if (selectedItem == nullptr) {
    return;
  }
  Profile::Ptr profileToDelete = selectedItem->data(Qt::UserRole).value<Profile::Ptr>();
  const QString profileName =
      profileToDelete ? profileToDelete->name() : selectedItem->text();
  if (profileToDelete && profileName == m_ActiveProfileName) {
    showProfileNotice(this, QMessageBox::Warning,
                      tr("This profile is currently active"),
                      tr("Switch to another profile before removing it."));
    return;
  }

  QMessageBox confirmBox(QMessageBox::Warning, "MO2 Revamped",
                         tr("Remove profile \"%1\"?").arg(profileName),
                         QMessageBox::Yes | QMessageBox::No, this);
  confirmBox.setDefaultButton(QMessageBox::No);
  confirmBox.setEscapeButton(QMessageBox::No);
  confirmBox.setInformativeText(
      tr("This removes the profile, including any profile-specific save games. "
         "This action cannot be undone."));
  styleProfilePrompt(confirmBox, QMessageBox::Yes, true);

  if (confirmBox.exec() == QMessageBox::Yes) {
    QString profilePath;
    if (profileToDelete.get() == nullptr) {
      profilePath = Settings::instance().paths().profiles() + "/" + profileName;
      QMessageBox brokenProfileBox(QMessageBox::Warning, "MO2 Revamped",
                                   tr("This profile appears to be broken."),
                                   QMessageBox::Yes | QMessageBox::No, this);
      brokenProfileBox.setDefaultButton(QMessageBox::No);
      brokenProfileBox.setEscapeButton(QMessageBox::No);
      brokenProfileBox.setInformativeText(
          tr("The folder below will be deleted:\n%1\n\nProceed?").arg(profilePath));
      styleProfilePrompt(brokenProfileBox, QMessageBox::Yes, true);
      if (brokenProfileBox.exec() != QMessageBox::Yes) {
        return;
      }
    } else {
      // on destruction, the profile object would write the profile.ini file again, so
      // we have to get rid of the it before deleting the directory
      profilePath = profileToDelete->absolutePath();
    }
    QListWidgetItem* item = m_list->takeItem(m_list->row(selectedItem));
    if (item != nullptr) {
      delete item;
    }
    if (!shellDelete(QStringList(profilePath))) {
      log::warn("Failed to shell-delete \"{}\" (errorcode {}), trying regular delete",
                profilePath, ::GetLastError());
      if (!removeDir(profilePath)) {
        log::warn("regular delete failed too");
      }
    }

    m_count->setText(QString::number(m_list->count()));
    emit profileRemoved(profileName);
  }
}

void ProfilesDialog::renameProfile()
{
  Profile::Ptr profile = currentProfile();
  if (!profile) {
    return;
  }

  if (profile->name() == m_ActiveProfileName) {
    showProfileNotice(this, QMessageBox::Warning,
                      tr("This profile is currently active"),
                      tr("Switch to another profile before renaming it."));
    return;
  }

  ProfileInputDialog dialog(this, ProfileInputDialog::Purpose::Rename, profile->name());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QString name = dialog.getName();
  if (!fixDirectoryName(name) || name.isEmpty()) {
    showProfileNotice(this, QMessageBox::Warning, tr("Invalid profile name"),
                      tr("Choose a valid name for this profile."));
    return;
  }

  m_list->currentItem()->setText(name);

  QString oldName = profile->name();
  profile->rename(name);

  updateItem(m_list->currentItem());
  fillDetails();
  emit profileRenamed(profile.get(), oldName, name);
}

void ProfilesDialog::exploreProfile()
{
  const Profile::Ptr profile = currentProfile();
  if (profile) {
    shell::Explore(profile->absolutePath());
  } else if (const auto* item = m_list->currentItem()) {
    shell::Explore(Settings::instance().paths().profiles() + "/" + item->text());
  }
}

void ProfilesDialog::onInvalidationChanged(bool checked)
{
  const Profile::Ptr profile = currentProfile();
  if (!profile || !m_invalidation->isEnabled()) {
    return;
  }
  try {
    if (checked) {
      profile->activateInvalidation();
    } else {
      profile->deactivateInvalidation();
    }
  } catch (const std::exception& e) {
    reportError(tr("failed to change archive invalidation state: %1").arg(e.what()));
  }
}

void ProfilesDialog::onLocalSavesChanged(bool checked)
{
  const Profile::Ptr profile = currentProfile();
  if (!profile) {
    return;
  }

  if (profile->enableLocalSaves(checked)) {
    m_transfer->setEnabled(checked);
  } else {
    // revert the switch
    QSignalBlocker blocker(m_localSaves);
    m_localSaves->setChecked(!checked);
  }
}

void ProfilesDialog::transferSaves()
{
  if (m_eldenRing) {
    return;
  }

  const Profile::Ptr profile = currentProfile();
  if (!profile) {
    return;
  }
  TransferSavesDialog transferDialog(*profile, m_Game, this);
  transferDialog.exec();
}

void ProfilesDialog::onLocalIniFilesChanged(bool checked)
{
  const Profile::Ptr profile = currentProfile();
  if (!profile) {
    return;
  }

  if (!profile->enableLocalSettings(checked)) {
    // revert the switch
    QSignalBlocker blocker(m_localIniFiles);
    m_localIniFiles->setChecked(!checked);
  }
}
