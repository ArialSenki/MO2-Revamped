#ifndef MULTIARCHIVEDIALOG_H
#define MULTIARCHIVEDIALOG_H

#include <memory>
#include <vector>

#include "fluentcontentdialog.h"
#include "ifiletree.h"

class QListWidget;
class QPushButton;

/**
 * @brief Asks which of the archives bundled in an archive to install. MO2
 * Revamped draws it as a Windows 11 content dialog: the archives in a list,
 * and Install, Install manually and Cancel.
 **/
class MultiArchiveDialog : public RevampedFluent::ContentDialog
{
  Q_OBJECT

public:
  /**
   * @param entries the bundled archives, in the archive's order
   * @param parent parent widget
   **/
  explicit MultiArchiveDialog(
      std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> entries,
      QWidget* parent);

  /**
   * @return true if the user asked for the manual installation dialog
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return the archive to install; only valid if exec() returned Accepted
   **/
  auto selectedEntry() const { return m_SelectedEntry; }

private:
  void install();

  std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> m_Entries;
  QListWidget* m_list;
  QPushButton* m_install;
  bool m_Manual = false;
  std::shared_ptr<const MOBase::FileTreeEntry> m_SelectedEntry;
};

#endif  // MULTIARCHIVEDIALOG_H
