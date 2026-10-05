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
#include "ui_profilesdialog.h"

#include "bsainvalidation.h"
#include "eldenringsavesettings.h"
#include "filesystemutilities.h"
#include "game_features.h"
#include "iplugingame.h"
#include "localsavegames.h"
#include "organizercore.h"
#include "profile.h"
#include "profileinputdialog.h"
#include "report.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "transfersavesdialog.h"

#include <QDir>
#include <QDirIterator>
#include <QComboBox>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWhatsThis>

#include <Windows.h>

#include <exception>

using namespace MOBase;
using namespace MOShared;

Q_DECLARE_METATYPE(Profile::Ptr)

ProfilesDialog::ProfilesDialog(const QString& profileName, OrganizerCore& organizer,
                               QWidget* parent)
    : TutorableDialog("Profiles", parent), ui(new Ui::ProfilesDialog),
      m_GameFeatures(organizer.gameFeatures()), m_FailState(false),
      m_Game(organizer.managedGame()),
      m_IsEldenRing(EldenRingSaveSettings::supports(m_Game)), m_ActiveProfileName("")
{
  ui->setupUi(this);

  if (m_IsEldenRing) {
    ui->localSavesBox->hide();
    ui->localIniFilesBox->hide();
    ui->invalidationBox->hide();

    QWidget* profileOptions = ui->profilesList->parentWidget();
    auto* profileLayout = qobject_cast<QVBoxLayout*>(profileOptions->layout());
    if (profileLayout != nullptr) {
      m_eldenRingSaveGroup = new QGroupBox(tr("Elden Ring Save Isolation"),
                                           profileOptions);
      m_eldenRingSaveGroup->setObjectName(
          QStringLiteral("eldenRingSaveIsolationGroup"));
      auto* saveLayout = new QVBoxLayout(m_eldenRingSaveGroup);
      saveLayout->setContentsMargins(14, 12, 14, 12);
      saveLayout->setSpacing(8);

      m_eldenRingProfileLabel = new QLabel(m_eldenRingSaveGroup);
      m_eldenRingProfileLabel->setWordWrap(true);
      saveLayout->addWidget(m_eldenRingProfileLabel);

      m_eldenRingSaveScope = new QLabel(
          tr("The save mode applies to the selected profile in this Elden Ring "
             "instance. Other game instances keep their own MO2 settings."),
          m_eldenRingSaveGroup);
      m_eldenRingSaveScope->setWordWrap(true);
      saveLayout->addWidget(m_eldenRingSaveScope);

      m_eldenRingSaveMode = new QComboBox(m_eldenRingSaveGroup);
      m_eldenRingSaveMode->addItem(
          EldenRingSaveSettings::modeName(EldenRingSaveSettings::ProfileIsolated),
          QString::fromLatin1(EldenRingSaveSettings::ProfileIsolated));
      m_eldenRingSaveMode->addItem(
          EldenRingSaveSettings::modeName(EldenRingSaveSettings::InstanceShared),
          QString::fromLatin1(EldenRingSaveSettings::InstanceShared));
      m_eldenRingSaveMode->addItem(
          EldenRingSaveSettings::modeName(EldenRingSaveSettings::GlobalShared),
          QString::fromLatin1(EldenRingSaveSettings::GlobalShared));
      saveLayout->addWidget(m_eldenRingSaveMode);

      m_eldenRingSaveDescription = new QLabel(m_eldenRingSaveGroup);
      m_eldenRingSaveDescription->setWordWrap(true);
      saveLayout->addWidget(m_eldenRingSaveDescription);

      m_eldenRingSaveStatus = new QLabel(m_eldenRingSaveGroup);
      m_eldenRingSaveStatus->setWordWrap(true);
      saveLayout->addWidget(m_eldenRingSaveStatus);

      m_eldenRingSaveModeSaveButton =
          new QPushButton(tr("Save mode"), m_eldenRingSaveGroup);
      saveLayout->addWidget(m_eldenRingSaveModeSaveButton);

      profileLayout->insertWidget(1, m_eldenRingSaveGroup);
      connect(m_eldenRingSaveModeSaveButton, &QPushButton::clicked, this,
              &ProfilesDialog::saveEldenRingSaveMode);
      connect(m_eldenRingSaveMode,
              qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
                const QString mode = m_eldenRingSaveMode->currentData().toString();
                m_eldenRingSaveDescription->setText(
                    EldenRingSaveSettings::modeDescription(mode));

                QListWidgetItem* item = ui->profilesList->currentItem();
                Profile::Ptr profile = item == nullptr
                                           ? Profile::Ptr()
                                           : item->data(Qt::UserRole)
                                                 .value<Profile::Ptr>();
                if (profile) {
                  const QString savedMode = EldenRingSaveSettings::readProfileMode(
                      profile->absolutePath(),
                      profile->localSavesEnabled()
                          ? QString::fromLatin1(EldenRingSaveSettings::ProfileIsolated)
                          : QString::fromLatin1(EldenRingSaveSettings::GlobalShared));
                  const bool expectsLocalSaves =
                      mode != QLatin1String(EldenRingSaveSettings::GlobalShared);
                  const bool localSavesMatch =
                      profile->localSavesEnabled() == expectsLocalSaves;
                  if (mode != savedMode) {
                    m_eldenRingSaveStatus->setText(
                        tr("Pending change: %1. Select Save mode to apply it.")
                            .arg(EldenRingSaveSettings::modeName(mode)));
                  } else if (!localSavesMatch) {
                    m_eldenRingSaveStatus->setText(
                        tr("MO2's save setting does not match this mode. Select "
                           "Save mode to synchronize it."));
                  } else {
                    m_eldenRingSaveStatus->setText(
                        tr("Current mode: %1")
                            .arg(EldenRingSaveSettings::modeName(savedMode)));
                  }
                  m_eldenRingSaveModeSaveButton->setEnabled(
                      mode != savedMode || !localSavesMatch);
                }
              });

      resize(760, 500);
    }
  }

  QDir profilesDir(Settings::instance().paths().profiles());
  profilesDir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

  QDirIterator profileIter(profilesDir);

  while (profileIter.hasNext()) {
    profileIter.next();
    QListWidgetItem* item = addItem(profileIter.filePath());
    if (profileName == profileIter.fileName()) {
      ui->profilesList->setCurrentItem(item);
      m_ActiveProfileName = profileName;
    }
  }

  auto invalidation = m_GameFeatures.gameFeature<BSAInvalidation>();
  if (invalidation == nullptr) {
    ui->invalidationBox->setToolTip(
        tr("Archive invalidation isn't required for this game."));
    ui->invalidationBox->setEnabled(false);
  }

  if (!m_GameFeatures.gameFeature<LocalSavegames>()) {
    ui->localSavesBox->setToolTip(
        tr("This game does not support profile-specific game saves."));
    ui->localSavesBox->setEnabled(false);
  }

  connect(this, &ProfilesDialog::profileCreated, &organizer,
          &OrganizerCore::profileCreated);
  connect(this, &ProfilesDialog::profileRenamed, &organizer,
          &OrganizerCore::profileRenamed);
  connect(this, &ProfilesDialog::profileRemoved, &organizer,
          &OrganizerCore::profileRemoved);
}

ProfilesDialog::~ProfilesDialog()
{
  delete ui;
}

int ProfilesDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);
  return QDialog::exec();
}

void ProfilesDialog::showEvent(QShowEvent* event)
{
  TutorableDialog::showEvent(event);

  if (ui->profilesList->count() == 0) {
    QPoint pos = ui->profilesList->mapToGlobal(QPoint(0, 0));
    pos.rx() += ui->profilesList->width() / 2;
    pos.ry() += (ui->profilesList->height() / 2) - 20;
    QWhatsThis::showText(
        pos,
        QObject::tr(
            "Before you can use ModOrganizer, you need to create at least one profile. "
            "ATTENTION: Run the game at least once before creating a profile!"),
        ui->profilesList);
  }
}

void ProfilesDialog::on_close_clicked()
{
  close();
}

void ProfilesDialog::on_select_clicked()
{
  const Profile::Ptr currentProfile =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();

  if (!currentProfile) {
    return;
  }

  m_Selected = currentProfile->name();
  close();
}

std::optional<QString> ProfilesDialog::selectedProfile() const
{
  return m_Selected;
}

QListWidgetItem* ProfilesDialog::addItem(const QString& name)
{
  QDir profileDir(name);
  QListWidgetItem* newItem =
      new QListWidgetItem(profileDir.dirName(), ui->profilesList);
  try {
    newItem->setData(Qt::UserRole, QVariant::fromValue(Profile::Ptr(new Profile(
                                       profileDir, m_Game, m_GameFeatures))));
    m_FailState = false;
  } catch (const std::exception& e) {
    reportError(tr("failed to create profile: %1").arg(e.what()));
  }
  return newItem;
}

void ProfilesDialog::createProfile(const QString& name, bool useDefaultSettings)
{
  try {
    auto profile =
        Profile::Ptr(new Profile(name, m_Game, m_GameFeatures, useDefaultSettings));
    QListWidgetItem* newItem = new QListWidgetItem(name, ui->profilesList);
    newItem->setData(Qt::UserRole, QVariant::fromValue(profile));
    ui->profilesList->addItem(newItem);
    m_FailState = false;
    ui->profilesList->setCurrentItem(newItem);
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
    QListWidgetItem* newItem = new QListWidgetItem(name, ui->profilesList);
    newItem->setData(Qt::UserRole, QVariant::fromValue(profile));
    ui->profilesList->addItem(newItem);
    m_FailState = false;
    ui->profilesList->setCurrentItem(newItem);
    emit profileCreated(profile.get());
  } catch (const std::exception&) {
    m_FailState = true;
    throw;
  }
}

void ProfilesDialog::on_addProfileButton_clicked()
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

void ProfilesDialog::on_copyProfileButton_clicked()
{
  bool okClicked;
  QString name = QInputDialog::getText(this, tr("Name"),
                                       tr("Please enter a name for the new profile"),
                                       QLineEdit::Normal, QString(), &okClicked);
  if (okClicked) {
    if (fixDirectoryName(name)) {
      try {
        const Profile::Ptr currentProfile =
            ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();
        createProfile(name, *currentProfile);
      } catch (const std::exception& e) {
        reportError(tr("failed to copy profile: %1").arg(e.what()));
      }
    } else {
      QMessageBox::warning(this, tr("Invalid name"), tr("Invalid profile name"));
    }
  }
}

void ProfilesDialog::on_removeProfileButton_clicked()
{
  Profile::Ptr profileToDelete =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();
  if (profileToDelete->name() == m_ActiveProfileName) {
    QMessageBox::warning(this, tr("Deleting active profile"),
                         tr("Unable to delete active profile.  Please change to a "
                            "different profile first."));
    return;
  }

  QMessageBox confirmBox(QMessageBox::Question, tr("Confirm"),
                         tr("Are you sure you want to remove this profile (including "
                            "profile-specific save games, if any)?"),
                         QMessageBox::Yes | QMessageBox::No, this);

  if (confirmBox.exec() == QMessageBox::Yes) {
    QString profilePath;
    if (profileToDelete.get() == nullptr) {
      profilePath = Settings::instance().paths().profiles() + "/" +
                    ui->profilesList->currentItem()->text();
      if (QMessageBox::question(
              this, tr("Profile broken"),
              tr("This profile you're about to delete seems to be broken or the path "
                 "is invalid. "
                 "I'm about to delete the following folder: \"%1\". Proceed?")
                  .arg(profilePath),
              QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        return;
      }
    } else {
      // on destruction, the profile object would write the profile.ini file again, so
      // we have to get rid of the it before deleting the directory
      profilePath = profileToDelete->absolutePath();
    }
    QListWidgetItem* item = ui->profilesList->takeItem(ui->profilesList->currentRow());
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

    emit profileRemoved(profileToDelete->name());
  }
}

void ProfilesDialog::on_renameButton_clicked()
{
  Profile::Ptr currentProfile =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();

  if (currentProfile->name() == m_ActiveProfileName) {
    QMessageBox::warning(this, tr("Renaming active profile"),
                         tr("The active profile cannot be renamed. Please change to a "
                            "different profile first."));
    return;
  }

  bool valid = false;
  QString name;

  while (!valid) {
    bool ok = false;
    name    = QInputDialog::getText(this, tr("Rename Profile"), tr("New Name"),
                                    QLineEdit::Normal, currentProfile->name(), &ok);
    valid   = fixDirectoryName(name);
    if (!ok) {
      return;
    }
  }

  ui->profilesList->currentItem()->setText(name);

  QString oldName = currentProfile->name();
  currentProfile->rename(name);

  emit profileRenamed(currentProfile.get(), oldName, name);
}

void ProfilesDialog::on_invalidationBox_stateChanged(int state)
{
  QListWidgetItem* currentItem = ui->profilesList->currentItem();
  if (currentItem == nullptr) {
    return;
  }
  if (!ui->invalidationBox->isEnabled()) {
    return;
  }
  try {
    QVariant currentProfileVariant = currentItem->data(Qt::UserRole);
    if (!currentProfileVariant.isValid() || currentProfileVariant.isNull()) {
      return;
    }
    const Profile::Ptr currentProfile =
        currentItem->data(Qt::UserRole).value<Profile::Ptr>();
    if (state == Qt::Unchecked) {
      currentProfile->deactivateInvalidation();
    } else {
      currentProfile->activateInvalidation();
    }
  } catch (const std::exception& e) {
    reportError(tr("failed to change archive invalidation state: %1").arg(e.what()));
  }
}

void ProfilesDialog::on_profilesList_currentItemChanged(QListWidgetItem* current,
                                                        QListWidgetItem*)
{
  if (current != nullptr) {
    if (!current->data(Qt::UserRole).isValid())
      return;
    const Profile::Ptr currentProfile =
        current->data(Qt::UserRole).value<Profile::Ptr>();

    try {
      bool invalidationSupported = false;
      ui->invalidationBox->blockSignals(true);
      ui->invalidationBox->setChecked(
          currentProfile->invalidationActive(&invalidationSupported));
      ui->invalidationBox->setEnabled(invalidationSupported);
      ui->invalidationBox->blockSignals(false);

      bool localSaves = currentProfile->localSavesEnabled();
      ui->transferButton->setEnabled(localSaves);
      // prevent the stateChanged-event for the saves-box from triggering, otherwise it
      // may think local saves were disabled and delete the files/rename the dir
      ui->localSavesBox->blockSignals(true);
      ui->localSavesBox->setChecked(localSaves);
      ui->localSavesBox->blockSignals(false);

      ui->copyProfileButton->setEnabled(true);
      ui->removeProfileButton->setEnabled(true);
      ui->renameButton->setEnabled(true);

      ui->localIniFilesBox->blockSignals(true);
      ui->localIniFilesBox->setChecked(currentProfile->localSettingsEnabled());
      ui->localIniFilesBox->blockSignals(false);

      if (m_IsEldenRing) {
        updateEldenRingSaveSettings(currentProfile.get());
      }
    } catch (const std::exception& E) {
      reportError(
          tr("failed to determine if invalidation is active: %1").arg(E.what()));
      ui->copyProfileButton->setEnabled(false);
      ui->removeProfileButton->setEnabled(false);
      ui->renameButton->setEnabled(false);
      ui->invalidationBox->setChecked(false);
    }
  } else {
    ui->invalidationBox->setChecked(false);
    ui->copyProfileButton->setEnabled(false);
    ui->removeProfileButton->setEnabled(false);
    ui->renameButton->setEnabled(false);
    if (m_IsEldenRing) {
      updateEldenRingSaveSettings(nullptr);
    }
  }
}

void ProfilesDialog::updateEldenRingSaveSettings(Profile* profile)
{
  if (!m_IsEldenRing || m_eldenRingSaveMode == nullptr) {
    return;
  }

  const bool hasProfile = profile != nullptr;
  m_eldenRingSaveMode->setEnabled(hasProfile);
  m_eldenRingSaveModeSaveButton->setEnabled(false);

  if (!hasProfile) {
    m_eldenRingProfileLabel->setText(tr("Select a profile to configure its saves."));
    m_eldenRingSaveDescription->clear();
    m_eldenRingSaveStatus->clear();
    return;
  }

  const QString fallback =
      profile->localSavesEnabled()
          ? QString::fromLatin1(EldenRingSaveSettings::ProfileIsolated)
          : QString::fromLatin1(EldenRingSaveSettings::GlobalShared);
  const QString mode = EldenRingSaveSettings::readProfileMode(
      profile->absolutePath(), fallback);

  m_eldenRingProfileLabel->setText(tr("<b>Profile:</b> %1").arg(profile->name()));
  m_eldenRingSaveMode->blockSignals(true);
  m_eldenRingSaveMode->setCurrentIndex(m_eldenRingSaveMode->findData(mode));
  m_eldenRingSaveMode->blockSignals(false);
  m_eldenRingSaveDescription->setText(
      EldenRingSaveSettings::modeDescription(mode));
  const bool expectsLocalSaves =
      mode != QLatin1String(EldenRingSaveSettings::GlobalShared);
  const bool localSavesMatch = profile->localSavesEnabled() == expectsLocalSaves;
  m_eldenRingSaveStatus->setText(
      localSavesMatch
          ? tr("Current mode: %1").arg(EldenRingSaveSettings::modeName(mode))
          : tr("MO2's save setting does not match this mode. Select Save mode to "
               "synchronize it."));
  m_eldenRingSaveModeSaveButton->setEnabled(!localSavesMatch);

  // MO2's built-in transfer dialog addresses the profile's own saves folder.
  // It does not address the instance-shared folder used by the shared mode.
  ui->transferButton->setEnabled(
      mode == QLatin1String(EldenRingSaveSettings::ProfileIsolated));
}

void ProfilesDialog::saveEldenRingSaveMode()
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  if (item == nullptr) {
    return;
  }

  const Profile::Ptr profile = item->data(Qt::UserRole).value<Profile::Ptr>();
  if (!profile) {
    return;
  }

  const QString mode =
      EldenRingSaveSettings::normalizeMode(m_eldenRingSaveMode->currentData().toString(),
                                           QString());
  if (mode.isEmpty()) {
    return;
  }

  const bool useLocalSaves =
      mode != QLatin1String(EldenRingSaveSettings::GlobalShared);
  if (!useLocalSaves) {
    const auto result = QMessageBox::warning(
        this, tr("Use the global Elden Ring save?"),
        tr("This mode shares the global save folder with Steam and other MO2 "
           "instances using the same path. Modded play can change the vanilla "
           "character. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes) {
      updateEldenRingSaveSettings(profile.get());
      return;
    }
  } else {
    QDir profileDirectory(profile->absolutePath());
    if (!profileDirectory.exists("saves") && !profileDirectory.mkpath("saves")) {
      QMessageBox::warning(this, tr("Could not create the save folder"),
                           profileDirectory.filePath("saves"));
      return;
    }
  }

  if (!EldenRingSaveSettings::writeProfileMode(profile->absolutePath(), mode)) {
    QMessageBox::warning(this, tr("Could not save Elden Ring save mode"),
                         tr("MO2 could not write the save mode for this profile."));
    updateEldenRingSaveSettings(profile.get());
    return;
  }

  // Update MO2's own local-save flag while preserving any existing profile saves.
  profile->storeSetting("", "LocalSaves", useLocalSaves);
  updateEldenRingSaveSettings(profile.get());
  m_eldenRingSaveStatus->setText(
      tr("Saved. If this is the active profile, switch to another profile and "
         "back, or restart MO2, before launching Elden Ring so the save route "
         "is reloaded."));
}

void ProfilesDialog::on_profilesList_itemActivated(QListWidgetItem* item)
{
  on_select_clicked();
}

void ProfilesDialog::on_localSavesBox_stateChanged(int state)
{
  Profile::Ptr currentProfile =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();

  if (currentProfile->enableLocalSaves(state == Qt::Checked)) {
    ui->transferButton->setEnabled(state == Qt::Checked);
  } else {
    // revert checkbox-state
    ui->localSavesBox->setChecked(state != Qt::Checked);
  }
}

void ProfilesDialog::on_transferButton_clicked()
{
  const Profile::Ptr currentProfile =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();
  TransferSavesDialog transferDialog(*currentProfile, m_Game, this);
  transferDialog.exec();
}

void ProfilesDialog::on_localIniFilesBox_stateChanged(int state)
{
  Profile::Ptr currentProfile =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();

  if (!currentProfile->enableLocalSettings(state == Qt::Checked)) {
    // revert checkbox-state
    ui->localIniFilesBox->setChecked(state != Qt::Checked);
  }
}
