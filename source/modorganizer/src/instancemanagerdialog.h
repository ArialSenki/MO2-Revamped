#ifndef MODORGANIZER_INSTANCEMANAGERDIALOG_INCLUDED
#define MODORGANIZER_INSTANCEMANAGERDIALOG_INCLUDED

#include <QDialog>
#include <atomic>
#include <memory>
#include <vector>

class Instance;
class PluginContainer;
class QLabel;
class QLineEdit;
class QListView;
class QPoint;
class QPushButton;
class QStandardItemModel;
class QStackedWidget;
class QToolButton;

// a dialog to manage existing instances: the instances in a card on the left,
// the selected one's folders and actions in a card on the right, and Switch
// and Close below
//
class InstanceManagerDialog : public QDialog
{
  Q_OBJECT

public:
  explicit InstanceManagerDialog(PluginContainer& pc, QWidget* parent = nullptr);

  ~InstanceManagerDialog();

  // selects the instance having the given index in the list
  //
  void select(std::size_t i);

  // selects the instance by name
  //
  void select(const QString& name);

  // select the instance that is currently in use in MO
  //
  void selectActiveInstance();

  // switches to the selected instance; restarts MO, unless
  // was called setRestartOnSelect(false)
  //
  void openSelectedInstance();

  // asks the user before Mod Organizer restarts with another instance; also
  // used by the command bar's instance menu
  //
  static bool confirmSwitch(QWidget* parent, const Instance& to);

  // renames the currently selected instance
  //
  void rename();

  // explores the directory of the selected instance
  //
  void exploreLocation();

  // explores the base directory of the selected instance
  //
  void exploreBaseDirectory();

  // explores the game directory of the selected instance
  //
  void exploreGame();

  // opens the ini of the selected instance in the shell
  //
  void openINI();

  // deletes the selected instance
  //
  void deleteInstance();

  // copies the selected instance, with its profiles, mods and settings, to a
  // new instance
  //
  void duplicateInstance();

  // adds a desktop shortcut that opens the selected instance, or removes it
  //
  void toggleShortcut();

  // puts the selected instance in one of the user's groups, or in none
  //
  void changeGroup();

  // edits the selected instance's notes
  //
  void editNotes();

  // sets whether the dialog should restart MO when selecting an instance; this
  // is false on startup when no instances exist
  //
  void setRestartOnSelect(bool b);

  // saves geometry
  //
  void done(int r) override;

protected:
  // restores geometry
  //
  void showEvent(QShowEvent* e) override;

private:
  static const std::size_t NoSelection = -1;

  // a row of the details card: the value and, if any, the button that opens it
  struct DetailRow
  {
    QLabel* value       = nullptr;
    QToolButton* action = nullptr;
  };

  PluginContainer& m_pc;
  std::vector<std::unique_ptr<Instance>> m_instances;
  QStandardItemModel* m_model;
  bool m_restartOnSelect;

  // how the list is grouped ("game", "custom", "storage", "none") and sorted
  // ("name", "used"); remembered in the global settings
  QString m_groupBy;
  QString m_sortBy;

  QLabel* m_listCount;
  QToolButton* m_view;
  QLineEdit* m_filter;
  QListView* m_list;
  QPushButton* m_new;

  QStackedWidget* m_detailsStack;
  QLabel* m_emptyText;
  QLabel* m_name;
  QLabel* m_caption;
  QToolButton* m_rename;
  DetailRow m_locationRow;
  DetailRow m_baseRow;
  DetailRow m_gameRow;
  DetailRow m_iniRow;
  DetailRow m_profilesRow;
  DetailRow m_sizeRow;
  DetailRow m_storageRow;
  DetailRow m_groupRow;
  DetailRow m_notesRow;
  QPushButton* m_duplicate;
  QPushButton* m_shortcut;
  QPushButton* m_delete;

  // set to stop the folder size scan of the previous selection
  std::shared_ptr<std::atomic_bool> m_sizeScan;
  QPushButton* m_switch;

  // builds the window: the list card, the details card and the buttons
  //
  void setupUi();
  QWidget* createListCard();
  QWidget* createDetails();
  DetailRow addDetailRow(QWidget* parent, char16_t glyph, const QString& title,
                         const QString& tip, char16_t actionGlyph = 0xE838);

  // shows the actions for the instance under the mouse
  //
  void showListMenu(const QPoint& pos);

  // shows the actions for one of the user's groups (rename, remove) at the
  // given screen position
  //
  void showGroupMenu(const QString& group, const QPoint& globalPos);

  // counts the selected instance's mods, downloads and overwrite in the
  // background and fills the size row when done
  //
  void startSizeScan(const Instance& ii);

  // the desktop shortcut for the selected instance, and whether it exists
  //
  bool shortcutExists(const Instance& ii) const;

  // refreshes the list instances from disk
  //
  void updateInstances();

  // updates the ui for the selected instance
  //
  void onSelection();

  // hides the rows that don't match the search text
  //
  void applyFilter();

  // opens the create instance dialog
  //
  void createNew();


  // the list row of the instance with the given index
  //
  int rowOf(std::size_t index) const;

  // returns the index of selected instance, NoSelection if none
  //
  std::size_t singleSelectionIndex() const;

  // returns the InstanceInfo associated with the selected instance, null if
  // none
  //
  const Instance* singleSelection() const;

  // fills the instance list on the ui
  //
  void updateList();

  // fills the ui for the selected instance
  //
  void fillData(const Instance& ii);

  // clears the ui when there's no selection
  //
  void clearData();

  // enables/disables the actions that need a selected instance
  //
  void setButtonsEnabled(bool b);

  // deletes the given files, returns false on error
  //
  bool doDelete(const QStringList& files, bool recycle);
};

#endif  // MODORGANIZER_INSTANCEMANAGERDIALOG_INCLUDED
