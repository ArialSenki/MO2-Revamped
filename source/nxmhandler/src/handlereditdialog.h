#ifndef NXMHANDLER_HANDLEREDITDIALOG_H
#define NXMHANDLER_HANDLEREDITDIALOG_H

// MO2 Revamped: the Fluent "Add a program" / "Edit program" dialog. It
// replaces upstream's AddBinaryDialog (addbinarydialog.ui) and also takes over
// the in-place editing the old table offered, including game IDs that are not
// in the known-games list (MO2 registers its plugins' own short names).

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class HandlerEditDialog : public QDialog
{
  Q_OBJECT

public:
  enum class Mode
  {
    Add,
    Edit
  };

  explicit HandlerEditDialog(Mode mode, QWidget* parent = nullptr);

  // fills the dialog; game IDs that are not known games go to "Other game IDs"
  void setValues(const QStringList& games, const QString& executable,
                 const QString& arguments);

  QStringList gameIDs() const;
  QString executable() const;
  QString arguments() const;

private:
  void browse();
  void updateState();

  QLineEdit* m_Executable;
  QLineEdit* m_Arguments;
  QListWidget* m_Games;
  QLineEdit* m_OtherGames;
  QLabel* m_Problem;
  QPushButton* m_Accept;
};

#endif  // NXMHANDLER_HANDLEREDITDIALOG_H
