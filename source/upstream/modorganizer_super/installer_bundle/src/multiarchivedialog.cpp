#include "multiarchivedialog.h"

#include <algorithm>

#include <QFileIconProvider>
#include <QListWidget>
#include <QPushButton>

using namespace MOBase;

MultiArchiveDialog::MultiArchiveDialog(
    std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> entries, QWidget* parent)
    : RevampedFluent::ContentDialog(
          parent, QStringLiteral("MultiArchiveDialog"), tr("Bundled archives"),
          tr("This archive holds several archives that install separately. Choose the "
             "one to install.")),
      m_Entries(std::move(entries))
{
  setHeading(tr("Choose an archive to install"));
  setMinimumWidth(500);

  m_list = new QListWidget(this);
  m_list->setObjectName(QStringLiteral("bundledArchives"));
  m_list->setAccessibleName(tr("Bundled archives"));
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setWordWrap(true);
  m_list->setResizeMode(QListView::Adjust);
  m_list->setIconSize(QSize(16, 16));
  // an archive's icon, by its extension: the entries aren't on the disk
  QFileIconProvider icons;
  const QIcon archiveIcon = icons.icon(QFileIconProvider::File);
  for (const auto& entry : m_Entries) {
    auto* item = new QListWidgetItem(archiveIcon, entry->name(), m_list);
    item->setToolTip(entry->path());
  }
  // as tall as its archives, up to eight; more scroll
  const int rows   = std::clamp<int>(static_cast<int>(m_Entries.size()), 2, 8);
  const int height = rows * (fontMetrics().height() + 12) + 8;
  m_list->setMinimumHeight(height);
  if (m_Entries.size() <= 8) {
    m_list->setMaximumHeight(height);
  }
  body()->addWidget(m_list, 1);

  m_install    = addButton(tr("Install"), true);
  auto* manual = addButton(tr("Install manually"));
  manual->setToolTip(tr("Choose which of the bundle's files to install, and where"));
  auto* cancel = addButton(tr("Cancel"));

  connect(m_install, &QPushButton::clicked, this, &MultiArchiveDialog::install);
  connect(manual, &QPushButton::clicked, this, [this] {
    m_Manual = true;
    reject();
  });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  // a double click installs the archive, as the buttons of MO2 2.5.2 did;
  // Enter goes to the default button, Install
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this] {
    install();
  });
  // the selection, not the current row: assistive tools select an item
  // without making it current
  connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
    m_install->setEnabled(!m_list->selectedItems().isEmpty());
  });

  setTabOrder(m_list, m_install);
  setTabOrder(m_install, manual);
  setTabOrder(manual, cancel);
  if (m_list->count() > 0) {
    m_list->setCurrentRow(0);
  }
  m_install->setEnabled(!m_list->selectedItems().isEmpty());
  m_list->setFocus();
}

void MultiArchiveDialog::install()
{
  const auto selected = m_list->selectedItems();
  const int row       = selected.isEmpty() ? -1 : m_list->row(selected.first());
  if (row < 0 || row >= static_cast<int>(m_Entries.size())) {
    return;
  }
  m_SelectedEntry = m_Entries[static_cast<std::size_t>(row)];
  accept();
}
