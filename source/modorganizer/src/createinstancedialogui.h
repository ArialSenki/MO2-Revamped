#ifndef MODORGANIZER_CREATEINSTANCEDIALOGUI_INCLUDED
#define MODORGANIZER_CREATEINSTANCEDIALOGUI_INCLUDED

// The widgets of the create instance wizard, laid out like the instance
// manager: one card with a thin progress line at the top and the current step
// below it (its title, its text, then rows divided by hairlines), and the step
// count with Back, Next and Cancel under the card.
//
// This used to come from a Designer file; the widget names stay the same
// because the pages and the Elden Ring plugin look them up by name ("pages",
// "games", "profileSavesCheckbox"...).

#include <QString>

class QCheckBox;
class QComboBox;
class QCommandLinkButton;
class QDialog;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace Ui
{

class CreateInstanceDialog
{
public:
  void setupUi(QDialog* dialog);

  // the progress line and the pages
  QProgressBar* progress;
  QStackedWidget* pages;

  // intro
  QWidget* page_1;
  QLabel* introHeading;
  QLabel* label_15;
  // how many instances there are already, by storage and by game
  QLabel* introInstances;
  QCheckBox* hideIntro;

  // storage type
  QWidget* page_2;
  QLabel* label_2;
  QLabel* typeSelectionHint;
  QWidget* widget_15;
  QCommandLinkButton* createGlobal;
  QCommandLinkButton* createPortable;
  QCommandLinkButton* createIsolated;
  QLabel* portableExistsLabel;
  // what changes between the three kinds, side by side
  QWidget* typeComparison;

  // game
  QWidget* page_3;
  QLabel* label_3;
  QLabel* gameSelectionHint;
  QLineEdit* gamesFilter;
  QPushButton* browseGameFolderButton;
  QCheckBox* showAllGames;
  QScrollArea* scrollArea_3;
  QWidget* games;
  // where to look when a game isn't listed
  QLabel* gameMissingHint;

  // edition
  QWidget* page_4;
  QLabel* label_4;
  QLabel* label_14;
  QDialogButtonBox* editions;

  // name, and the group and notes the manager shows
  QWidget* page_5;
  QLabel* instanceNameLabel;
  QLabel* instanceNameHint;
  QWidget* widget_9;
  QLineEdit* instanceName;
  QLabel* instanceNamePath;
  QLabel* instanceNameContext;
  QLabel* instanceNameExists;
  QLabel* instanceNameInvalid;
  QComboBox* instanceGroup;
  QLineEdit* instanceNotes;
  // the instances that already manage this game; its row hides with none
  QLabel* instanceSiblings;
  QCheckBox* desktopShortcut;

  // profile settings
  QWidget* page;
  QLabel* profileSettingsLabel;
  QLabel* profileSettingsHint;
  QWidget* widget_25;
  QCheckBox* profileInisCheckbox;
  QLabel* profileInisDescription;
  QCheckBox* profileSavesCheckbox;
  QLabel* profileSavesDescription;
  QCheckBox* archiveInvalidationCheckbox;
  QLabel* archiveInvalidationDescription;
  // where the game keeps its INI files and saves now, under the switches
  QLabel* gameInisPath;
  QLabel* gameSavesPath;

  // folders
  QWidget* page_6;
  QLabel* label_5;
  QLabel* pathsLabel;
  QWidget* widget_13;
  QStackedWidget* pathPages;
  QLabel* label_6;
  QLineEdit* location;
  QToolButton* browseLocation;
  QLabel* locationExists;
  QLabel* locationInvalid;
  QLabel* diskSpace;
  // how full that drive is
  QProgressBar* diskBar;
  // the downloads folder: this instance's own, or another instance's
  QComboBox* sharedDownloads;
  // the folders that will be made inside the location
  QLabel* folderPreview;
  QLabel* label_8;
  QLineEdit* base;
  QToolButton* browseBase;
  QLabel* label_9;
  QLineEdit* downloads;
  QToolButton* browseDownloads;
  QLabel* label_10;
  QLineEdit* mods;
  QToolButton* browseMods;
  QLabel* label_12;
  QLineEdit* profiles;
  QToolButton* browseProfiles;
  QLabel* label_13;
  QLineEdit* overwrite;
  QToolButton* browseOverwrite;
  QLabel* label_11;
  QLabel* advancedDirExists;
  QLabel* advancedDirInvalid;
  QCheckBox* advancedPathOptions;

  // Nexus
  QWidget* page_7;
  QLabel* label_7;
  QLabel* label_18;
  QPushButton* nexusConnect;
  QPushButton* nexusManual;
  QListWidget* nexusLog;

  // review
  QWidget* page_8;
  QLabel* label_16;
  QLabel* label_17;
  QWidget* widget_19;
  QWidget* review;
  QTextEdit* creationLog;
  QCheckBox* launch;

  // under the card: the step (or the title in single page mode) and the
  // buttons
  QLabel* title;
  QPushButton* back;
  QPushButton* next;
  QPushButton* cancel;
};

}  // namespace Ui

namespace cid
{
// a page heading without the <h3> markup the translated strings carry
QString plainHeading(const QString& text);
}  // namespace cid

#endif  // MODORGANIZER_CREATEINSTANCEDIALOGUI_INCLUDED
