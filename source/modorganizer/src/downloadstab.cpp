#include "downloadstab.h"
#include "downloadlist.h"
#include "downloadlistview.h"
#include "organizercore.h"
#include "startupdiagnostics.h"
#include "ui_mainwindow.h"

DownloadsTab::DownloadsTab(OrganizerCore& core, Ui::MainWindow* mwui)
    : m_core(core), ui{mwui->btnRefreshDownloads, mwui->downloadView,
                       mwui->showHiddenBox, mwui->downloadFilterEdit}
{
  setStartupDiagnosticPhase("downloads_tab.construct.create_model");
  DownloadList* sourceModel = new DownloadList(m_core, ui.list);

  setStartupDiagnosticPhase("downloads_tab.construct.set_model");
  ui.list->setModel(sourceModel);
  setStartupDiagnosticPhase("downloads_tab.construct.set_manager");
  ui.list->setManager(m_core.downloadManager());
  setStartupDiagnosticPhase("downloads_tab.construct.set_delegate");
  ui.list->setItemDelegate(
      new DownloadProgressDelegate(m_core.downloadManager(), ui.list));

  setStartupDiagnosticPhase("downloads_tab.update.begin");
  update();
  setStartupDiagnosticPhase("downloads_tab.update.complete");

  m_filter.setEdit(ui.filter);
  m_filter.setList(ui.list);
  m_filter.setSortPredicate([sourceModel](auto&& left, auto&& right) {
    return sourceModel->lessThanPredicate(left, right);
  });

  connect(ui.refresh, &QPushButton::clicked, [&] {
    refresh();
  });
  connect(ui.list, SIGNAL(installDownload(int)), &m_core, SLOT(installDownload(int)));
  connect(ui.list, SIGNAL(queryInfo(int)), m_core.downloadManager(),
          SLOT(queryInfo(int)));
  connect(ui.list, SIGNAL(queryInfoMd5(int)), m_core.downloadManager(),
          SLOT(queryInfoMd5(int)));
  connect(ui.list, SIGNAL(visitOnNexus(int)), m_core.downloadManager(),
          SLOT(visitOnNexus(int)));
  connect(ui.list, SIGNAL(openFile(int)), m_core.downloadManager(),
          SLOT(openFile(int)));
  connect(ui.list, SIGNAL(openMetaFile(int)), m_core.downloadManager(),
          SLOT(openMetaFile(int)));
  connect(ui.list, SIGNAL(openInDownloadsFolder(int)), m_core.downloadManager(),
          SLOT(openInDownloadsFolder(int)));
  connect(ui.list, SIGNAL(removeDownload(int, bool)), m_core.downloadManager(),
          SLOT(removeDownload(int, bool)));
  connect(ui.list, SIGNAL(restoreDownload(int)), m_core.downloadManager(),
          SLOT(restoreDownload(int)));
  connect(ui.list, SIGNAL(cancelDownload(int)), m_core.downloadManager(),
          SLOT(cancelDownload(int)));
  connect(ui.list, SIGNAL(pauseDownload(int)), m_core.downloadManager(),
          SLOT(pauseDownload(int)));
  connect(ui.list, &DownloadListView::resumeDownload, [&](int i) {
    resumeDownload(i);
  });
}

void DownloadsTab::update()
{
  // this means downloadTab initialization hasn't happened yet
  if (ui.list->model() == nullptr) {
    return;
  }

  // set the view attribute and default row sizes
  if (m_core.settings().interface().compactDownloads()) {
    ui.list->setProperty("downloadView", "compact");
    ui.list->setStyleSheet("DownloadListView::item { padding: 4px 2px; }");
  } else {
    ui.list->setProperty("downloadView", "standard");
    ui.list->setStyleSheet("DownloadListView::item { padding: 16px 4px; }");
  }

  setStartupDiagnosticPhase("downloads_tab.update.style_unpolish");
  ui.list->style()->unpolish(ui.list);
  setStartupDiagnosticPhase("downloads_tab.update.style_polish");
  ui.list->style()->polish(ui.list);
  setStartupDiagnosticPhase("downloads_tab.update.resize_sections");
  auto* downloadHeader = qobject_cast<DownloadListHeader*>(ui.list->header());
  downloadHeader->customResizeSections();
  downloadHeader->ensureReadableSections();
  setStartupDiagnosticPhase("downloads_tab.update.resize_sections.complete");

  m_core.downloadManager()->refreshList();
}

void DownloadsTab::refresh()
{
  m_core.downloadManager()->refreshList();
}

void DownloadsTab::resumeDownload(int downloadIndex)
{
  m_core.loggedInAction(ui.list, [this, downloadIndex] {
    m_core.downloadManager()->resumeDownload(downloadIndex);
  });
}
