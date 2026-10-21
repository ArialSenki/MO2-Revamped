#ifndef PROBLEMSDIALOG_H
#define PROBLEMSDIALOG_H

#include <QDialog>
#include <QUrl>
#include <iplugindiagnose.h>

class PluginContainer;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

// the Notifications window: the active notifications in a list card on the
// left, the selected one's text and actions in a details card on the right,
// laid out like the instance manager
class ProblemsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ProblemsDialog(PluginContainer const& pluginContainer, QWidget* parent = 0);
  ~ProblemsDialog();

  // also saves and restores geometry
  //
  int exec() override;

  bool hasProblems() const;

signals:
  void manageOverwriteRequested();

private:
  void setupUi();
  QWidget* createListCard();
  QWidget* createDetails();

  void runDiagnosis();
  void selectionChanged();
  void urlClicked(const QUrl& url);
  void startFix();
  void copyText();

  const PluginContainer& m_PluginContainer;
  bool m_hasProblems;
  unsigned int m_problemCount;

  QListWidget* m_list;
  QLabel* m_count;
  QStackedWidget* m_details;
  QLabel* m_title;
  QLabel* m_caption;
  QLabel* m_context;
  QTextBrowser* m_description;
  QPushButton* m_fix;
  QPushButton* m_manageOverwrite;
  QPushButton* m_copy;
};

#endif  // PROBLEMSDIALOG_H
