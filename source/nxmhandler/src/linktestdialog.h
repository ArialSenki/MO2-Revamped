#ifndef NXMHANDLER_LINKTESTDIALOG_H
#define NXMHANDLER_LINKTESTDIALOG_H

// MO2 Revamped: "Test a Nexus link". Routes a made-up nxm link for a chosen
// game through the same lookup nxmhandler uses for real links
// (HandlerStorage::resolveHandler) and shows which program would get it.
// It is a dry run: nothing is downloaded and no program is started.

#include "handlerstorage.h"

#include <QDialog>
#include <list>

class QComboBox;
class QLabel;
class QFrame;

class LinkTestDialog : public QDialog
{
  Q_OBJECT

public:
  // handlers: the list as currently shown in the handler window (unsaved
  // changes included); registeredElsewhere: the program Windows sends links to
  // when it is not an nxmhandler, empty otherwise
  LinkTestDialog(const std::list<HandlerInfo>& handlers, const QString& registeredElsewhere,
                 QWidget* parent = nullptr);

private:
  void update();
  QString currentGameId() const;

  std::list<HandlerInfo> m_Handlers;
  QString m_RegisteredElsewhere;
  QComboBox* m_Game;
  QLabel* m_Link;
  QLabel* m_ResultTitle;
  QLabel* m_ResultValue;
  QLabel* m_ResultPath;
  QLabel* m_ResultReason;
  QLabel* m_Note;
  QFrame* m_Result;
};

#endif  // NXMHANDLER_LINKTESTDIALOG_H
