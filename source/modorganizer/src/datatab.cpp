#include "datatab.h"
#include "filetree.h"
#include "filetreemodel.h"
#include "messagedialog.h"
#include "organizercore.h"
#include "settings.h"
#include "startupdiagnostics.h"
#include "ui_mainwindow.h"
#include <QHeaderView>
#include <QSettings>
#include <log.h>
#include <report.h>

using namespace MOShared;
using namespace MOBase;

// in mainwindow.cpp
QString UnmanagedModName();

DataTab::DataTab(OrganizerCore& core, PluginContainer& pc, QWidget* parent,
                 Ui::MainWindow* mwui)
    : m_core(core), m_pluginContainer(pc), m_parent(parent),
      ui{mwui->tabWidget,
         mwui->dataTab,
         mwui->dataTabRefresh,
         mwui->dataTree,
         mwui->dataTabShowOnlyConflicts,
         mwui->dataTabShowFromArchives},
      m_needUpdate(true)
{
  setStartupDiagnosticPhase("data_tab.file_tree.construct");
  m_filetree.reset(new FileTree(core, m_pluginContainer, ui.tree));
  setStartupDiagnosticPhase("data_tab.file_tree.construct.complete");
  setStartupDiagnosticPhase("data_tab.filter.set_source_sort");
  m_filter.setUseSourceSort(true);
  setStartupDiagnosticPhase("data_tab.filter.set_filter_column");
  m_filter.setFilterColumn(FileTreeModel::FileName);
  setStartupDiagnosticPhase("data_tab.filter.set_edit");
  m_filter.setEdit(mwui->dataTabFilter);
  setStartupDiagnosticPhase("data_tab.filter.set_list");
  m_filter.setList(mwui->dataTree);
  setStartupDiagnosticPhase("data_tab.filter.set_update_delay");
  m_filter.setUpdateDelay(true);

  if (auto* m = m_filter.proxyModel()) {
    setStartupDiagnosticPhase("data_tab.filter.configure_proxy");
    m->setDynamicSortFilter(false);
  }

  setStartupDiagnosticPhase("data_tab.connect_signals");
  connect(&m_filter, &FilterWidget::aboutToChange, [&] {
    ensureFullyLoaded();
  });

  connect(ui.refresh, &QPushButton::clicked, [&] {
    onRefresh();
  });

  connect(ui.conflicts, &QCheckBox::toggled, [&] {
    onConflicts();
  });

  connect(ui.archives, &QCheckBox::toggled, [&] {
    onArchives();
  });

  connect(m_filetree.get(), &FileTree::executablesChanged, this,
          &DataTab::executablesChanged);

  connect(m_filetree.get(), &FileTree::originModified, this, &DataTab::originModified);

  connect(m_filetree.get(), &FileTree::displayModInformation, this,
          &DataTab::displayModInformation);
}

void DataTab::saveState(Settings& s) const
{
  s.geometry().saveState(ui.tree->header());
  s.widgets().saveChecked(ui.conflicts);
  s.widgets().saveChecked(ui.archives);
}

void DataTab::restoreState(Settings& s)
{
  auto* header = ui.tree->header();
  s.geometry().restoreState(header);

  // Let the primary file-name column use the remaining space. Keep the other
  // columns interactive and preserve their saved widths.
  header->setStretchLastSection(false);
  for (int column = 0; column < header->count(); ++column) {
    header->setSectionResizeMode(column, QHeaderView::Interactive);
  }

  // Existing layouts may have saved the Date modified section while it was
  // stretching to fill the tree. Normalize that width once; later user sizing
  // is restored normally.
  QSettings layoutSettings(s.filename(), QSettings::IniFormat);
  constexpr auto layoutMigratedKey =
      "Settings/data_tree_column_layout_migrated";
  if (!layoutSettings.value(layoutMigratedKey, false).toBool()) {
    constexpr int minimumDateWidth = 145;
    constexpr int maximumDateWidth = 230;
    constexpr int preferredDateWidth = 170;
    const int dateWidth = header->sectionSize(FileTreeModel::LastModified);
    if (!header->isSectionHidden(FileTreeModel::LastModified) &&
        (dateWidth < minimumDateWidth || dateWidth > maximumDateWidth)) {
      header->resizeSection(FileTreeModel::LastModified, preferredDateWidth);
    }
    layoutSettings.setValue(layoutMigratedKey, true);
    layoutSettings.sync();
  }

  header->setSectionResizeMode(FileTreeModel::FileName, QHeaderView::Stretch);

  // prior to 2.3, the list was not sortable, and this remembered in the
  // widget state, for whatever reason
  ui.tree->setSortingEnabled(true);

  s.widgets().restoreChecked(ui.conflicts);
  s.widgets().restoreChecked(ui.archives);
}

void DataTab::activated()
{
  if (m_needUpdate) {
    updateTree();
  }
}

bool DataTab::isActive() const
{
  return ui.tabs->currentWidget() == ui.tab;
}

void DataTab::onRefresh()
{
  if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
    m_filetree->model()->setEnabled(false);
    m_filetree->clear();
  }

  m_core.refreshDirectoryStructure();
}

void DataTab::updateTree()
{
  if (isActive()) {
    doUpdateTree();
  } else {
    m_needUpdate = true;
  }
}

void DataTab::doUpdateTree()
{
  m_filetree->model()->setEnabled(true);
  m_filetree->refresh();

  if (!m_filter.empty()) {
    ensureFullyLoaded();

    if (auto* m = m_filter.proxyModel()) {
      m->invalidate();
    }
  }

  m_needUpdate = false;
}

void DataTab::ensureFullyLoaded()
{
  if (!m_filetree->fullyLoaded()) {
    m_filter.setFilteringEnabled(false);
    m_filetree->ensureFullyLoaded();
    m_filter.setFilteringEnabled(true);
  }
}

void DataTab::onConflicts()
{
  updateOptions();
}

void DataTab::onArchives()
{
  updateOptions();
}

void DataTab::updateOptions()
{
  using M = FileTreeModel;

  M::Flags flags = M::NoFlags;

  if (ui.conflicts->isChecked()) {
    flags |= M::ConflictsOnly | M::PruneDirectories;
  }

  if (ui.archives->isChecked()) {
    flags |= M::Archives;
  }

  m_filetree->model()->setFlags(flags);
  updateTree();
}
