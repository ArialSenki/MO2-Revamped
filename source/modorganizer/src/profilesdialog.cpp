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
#include <QAbstractButton>
#include <QBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QWhatsThis>

#include <Windows.h>

#include <exception>

using namespace MOBase;
using namespace MOShared;

Q_DECLARE_METATYPE(Profile::Ptr)

namespace
{
void addCircularHeadingIcon(QBoxLayout* layout, QLabel* heading,
                            const QString& iconPath,
                            const QString& iconObjectName, int size)
{
  if (layout == nullptr || heading == nullptr) {
    return;
  }

  const int index = layout->indexOf(heading);
  if (index < 0) {
    return;
  }

  layout->removeWidget(heading);
  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* icon = new QLabel(heading->parentWidget());
  icon->setObjectName(iconObjectName);
  icon->setAlignment(Qt::AlignCenter);
  icon->setFixedSize(size, size);
  const QPixmap pixmap = QIcon(iconPath).pixmap(size / 2, size / 2);
  if (!pixmap.isNull()) {
    icon->setPixmap(pixmap);
  }

  row->addWidget(icon, 0, Qt::AlignVCenter);
  row->addWidget(heading, 1, Qt::AlignVCenter);
  layout->insertLayout(index, row);
}

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
}  // namespace

ProfilesDialog::ProfilesDialog(const QString& profileName, OrganizerCore& organizer,
                               QWidget* parent)
    : TutorableDialog("Profiles", parent), ui(new Ui::ProfilesDialog),
      m_GameFeatures(organizer.gameFeatures()), m_FailState(false),
      m_Game(organizer.managedGame()), m_ActiveProfileName("")
{
  ui->setupUi(this);
  addCircularHeadingIcon(ui->profilesHeaderLayout, ui->profilesPageTitle,
                         ":/MO/gui/mainwindow/profile.svg",
                         "profilesHeaderIcon", 32);
  addCircularHeadingIcon(ui->profilesActionsLayout, ui->profilesSectionTitle,
                         ":/MO/gui/mainwindow/profile.svg",
                         "profilesSectionIcon", 24);
  addCircularHeadingIcon(ui->profileOptionsPanelLayout,
                         ui->profileOptionsTitle,
                         ":/MO/gui/mainwindow/settings.svg",
                         "profileOptionsIcon", 22);

  if (EldenRingSaveSettings::supports(m_Game)) {
    ui->profileOptionsPanel->hide();
    ui->profilesPageSubtitle->setText(
        tr("Elden Ring save routing is managed in Tools > Elden Ring Save Isolation."));
    ui->localSavesBox->hide();
    // Elden Ring routes saves through its dedicated Tools dialog. Keep the
    // legacy transfer action available for games that still use MO2's native
    // profile-local save handling, but do not show it for Elden Ring.
    ui->transferButton->hide();
    ui->localIniFilesBox->hide();
    ui->invalidationBox->hide();
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
    const Profile::Ptr currentProfile =
        ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();
    createProfile(name, *currentProfile);
  } catch (const std::exception& e) {
    reportError(tr("failed to copy profile: %1").arg(e.what()));
  }
}

void ProfilesDialog::on_removeProfileButton_clicked()
{
  QListWidgetItem* selectedItem = ui->profilesList->currentItem();
  Profile::Ptr profileToDelete =
      selectedItem->data(Qt::UserRole).value<Profile::Ptr>();
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
      profilePath = Settings::instance().paths().profiles() + "/" +
                    profileName;
      QMessageBox brokenProfileBox(
          QMessageBox::Warning, "MO2 Revamped",
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
    QListWidgetItem* item = ui->profilesList->takeItem(ui->profilesList->row(selectedItem));
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

    emit profileRemoved(profileName);
  }
}

void ProfilesDialog::on_renameButton_clicked()
{
  Profile::Ptr currentProfile =
      ui->profilesList->currentItem()->data(Qt::UserRole).value<Profile::Ptr>();

  if (currentProfile->name() == m_ActiveProfileName) {
    showProfileNotice(this, QMessageBox::Warning,
                      tr("This profile is currently active"),
                      tr("Switch to another profile before renaming it."));
    return;
  }

  ProfileInputDialog dialog(this, ProfileInputDialog::Purpose::Rename,
                            currentProfile->name());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QString name = dialog.getName();
  if (!fixDirectoryName(name) || name.isEmpty()) {
    showProfileNotice(this, QMessageBox::Warning, tr("Invalid profile name"),
                      tr("Choose a valid name for this profile."));
    return;
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
  }
}

void ProfilesDialog::on_profilesList_itemActivated(QListWidgetItem* item)
{
  on_select_clicked();
}

void ProfilesDialog::on_localSavesBox_stateChanged(int state)
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  if (item == nullptr) {
    return;
  }

  Profile::Ptr currentProfile = item->data(Qt::UserRole).value<Profile::Ptr>();
  if (!currentProfile) {
    return;
  }

  const bool enable = state == Qt::Checked;
  if (currentProfile->enableLocalSaves(enable)) {
    ui->transferButton->setEnabled(enable);
  } else {
    // revert checkbox-state
    ui->localSavesBox->setChecked(state != Qt::Checked);
  }
}

void ProfilesDialog::on_transferButton_clicked()
{
  if (EldenRingSaveSettings::supports(m_Game)) {
    return;
  }

  const QListWidgetItem* currentItem = ui->profilesList->currentItem();
  if (currentItem == nullptr) {
    return;
  }
  const Profile::Ptr currentProfile =
      currentItem->data(Qt::UserRole).value<Profile::Ptr>();
  if (!currentProfile) {
    return;
  }
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
