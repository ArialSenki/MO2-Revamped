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

#ifndef PROFILESDIALOG_H
#define PROFILESDIALOG_H

#include <QObject>

#include "fluentpanels.h"
#include "profile.h"
#include "tutorabledialog.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QString;
class QToolButton;

namespace MOBase
{
class IPluginGame;
}

class GameFeatures;
class OrganizerCore;

/**
 * @brief Dialog that can be used to create/delete/modify profiles
 *
 * Laid out like the instance manager: the profiles in a list card on the left,
 * the selected profile's details and options as Settings rows on the right.
 * The widget names profilesList, localSavesBox, localIniFilesBox,
 * invalidationBox and transferButton are kept for plugins that look them up.
 **/
class ProfilesDialog : public MOBase::TutorableDialog
{
  Q_OBJECT

public:
  /**
   * @brief constructor
   *
   * @param profileName currently enabled profile
   * @param organizer
   * @param parent parent widget
   **/
  explicit ProfilesDialog(const QString& profileName, OrganizerCore& organizer,
                          QWidget* parent = 0);
  ~ProfilesDialog();

  // also saves and restores geometry
  //
  int exec() override;

  /**
   * @return true if creation of a new profile failed
   * @todo the notion of a fail state makes little sense in the current dialog
   **/
  bool failed() const { return m_FailState; }

  // if the dialog was closed with the 'select' button, returns the name of the
  // selected profile; if the dialog was closed with 'cancel', returns empty
  //
  std::optional<QString> selectedProfile() const;

signals:

  /**
   * @brief Signal emitted when a profile is created.
   */
  void profileCreated(Profile* profile);

  /**
   * @brief Signal emitted when a profile is renamed.
   */
  void profileRenamed(Profile* profile, QString const& oldName, QString const& newName);

  /**
   * @brief Signal emitted when a profile has been removed.
   */
  void profileRemoved(QString const& profileName);

protected:
  virtual void showEvent(QShowEvent* event);

private:
  void setupUi();
  QWidget* createListCard();
  QWidget* createDetails();

  QListWidgetItem* addItem(const QString& name);
  void createProfile(const QString& name, bool useDefaultSettings);
  void createProfile(const QString& name, const Profile& reference);

  // the selected profile, null without one or when it failed to load
  Profile::Ptr currentProfile() const;

  // the list row's caption and the details card follow the profile
  void updateItem(QListWidgetItem* item);
  void fillDetails();
  void showListMenu(const QPoint& pos);
  void showHelp();

  void addProfile();
  void copyProfile();
  void removeProfile();
  void renameProfile();
  void selectProfile();
  void transferSaves();
  void exploreProfile();

  void onCurrentChanged(QListWidgetItem* current);
  void onInvalidationChanged(bool checked);
  void onLocalSavesChanged(bool checked);
  void onLocalIniFilesChanged(bool checked);

  GameFeatures& m_GameFeatures;
  bool m_FailState;
  MOBase::IPluginGame const* m_Game;
  QString m_ActiveProfileName;
  std::optional<QString> m_Selected;
  bool m_eldenRing;

  QListWidget* m_list;
  QLabel* m_count;
  QPushButton* m_new;

  QStackedWidget* m_details;
  QLabel* m_name;
  QLabel* m_caption;
  QToolButton* m_rename;
  FluentPanels::DetailRow m_modsRow;
  FluentPanels::DetailRow m_folderRow;
  FluentPanels::DetailRow m_changedRow;
  FluentPanels::DetailRow m_savesRow;
  QCheckBox* m_localSaves;
  QCheckBox* m_localIniFiles;
  QCheckBox* m_invalidation;
  QPushButton* m_copy;
  QPushButton* m_transfer;
  QPushButton* m_remove;
  QPushButton* m_select;
};

#endif  // PROFILESDIALOG_H
