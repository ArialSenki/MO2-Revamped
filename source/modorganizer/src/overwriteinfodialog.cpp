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

#include "overwriteinfodialog.h"
#include "menuutils.h"
#include "fluenticons.h"
#include "mo2foldericonprovider.h"
#include "organizercore.h"
#include "report.h"
#include "ui_overwriteinfodialog.h"
#include "utility.h"
#include <QDirIterator>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QShortcut>
#include <QVariant>
#include <Shlwapi.h>

using namespace MOBase;

namespace
{
class CenteredOverwriteFileSystemModel final : public QFileSystemModel
{
public:
  explicit CenteredOverwriteFileSystemModel(QObject* parent = nullptr)
      : QFileSystemModel(parent)
  {}

  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override
  {
    // Fluent headings start where their column's text starts, like every
    // other list; classic themes keep them centered
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
      return static_cast<int>(
          (FluentIcons::active() ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignVCenter);
    }

    return QFileSystemModel::headerData(section, orientation, role);
  }
};
}  // namespace

OverwriteInfoDialog::OverwriteInfoDialog(ModInfo::Ptr modInfo, QWidget* parent)
    : QDialog(parent), ui(new Ui::OverwriteInfoDialog), m_FileSystemModel(nullptr),
      m_DeleteAction(nullptr), m_RenameAction(nullptr), m_OpenAction(nullptr)
{
  ui->setupUi(this);

  this->setWindowModality(Qt::NonModal);

  m_FileSystemModel = new CenteredOverwriteFileSystemModel(this);
  m_FileSystemModel->setIconProvider(mo2FolderIconProvider());
  m_FileSystemModel->setReadOnly(false);
  setModInfo(modInfo);
  ui->filesView->setModel(m_FileSystemModel);
  ui->filesView->setRootIndex(m_FileSystemModel->index(modInfo->absolutePath()));
  ui->filesView->setColumnWidth(0, 250);
  ui->explorerButton->setToolTip(
      tr("Open the Overwrite folder in Windows File Explorer."));
  if (FluentIcons::active()) {
    // a regular command button at the heading's right, with the folder glyph
    ui->explorerButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    ui->explorerButton->setIcon(FluentIcons::glyph(0xE838));  // OpenFolderHorizontal
  }

  m_DeleteAction    = new QAction(tr("&Delete…"), ui->filesView);
  m_RenameAction    = new QAction(tr("&Rename"), ui->filesView);
  m_OpenAction      = new QAction(tr("&Open file"), ui->filesView);
  m_NewFolderAction = new QAction(tr("&New folder"), ui->filesView);
  m_NewFolderAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/files/folder.svg"));
  m_RenameAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/rename.svg"));
  m_OpenAction->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/files/file.svg"));
  m_DeleteAction->setIcon(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"));
  m_NewFolderAction->setToolTip(tr("Create a folder inside Overwrite."));
  m_OpenAction->setToolTip(tr("Open the selected file with its associated application."));
  m_RenameAction->setToolTip(tr("Rename the selected file or folder."));
  m_DeleteAction->setToolTip(
      tr("Permanently remove the selected file or folder from Overwrite."));

  new QShortcut(QKeySequence::Delete, this, SLOT(delete_activated()));

  QObject::connect(m_DeleteAction, SIGNAL(triggered()), this, SLOT(deleteTriggered()));
  QObject::connect(m_RenameAction, SIGNAL(triggered()), this, SLOT(renameTriggered()));
  QObject::connect(m_OpenAction, SIGNAL(triggered()), this, SLOT(openTriggered()));
  QObject::connect(m_NewFolderAction, SIGNAL(triggered()), this,
                   SLOT(createDirectoryTriggered()));

  if (FluentIcons::active()) {
    buildFluentLayout();
  }
}

void OverwriteInfoDialog::buildFluentLayout()
{
  auto* root = qobject_cast<QVBoxLayout*>(layout());
  if (root == nullptr) {
    return;
  }

  // the intro panel's title and text become the card's header
  ui->overwriteIntroPanel->hide();
  ui->overwriteSectionHeading->hide();
  for (QWidget* w : {static_cast<QWidget*>(ui->overwriteIntroPanel),
                     static_cast<QWidget*>(ui->overwriteSectionHeading),
                     static_cast<QWidget*>(ui->filesView),
                     static_cast<QWidget*>(ui->overwriteContextHint),
                     static_cast<QWidget*>(ui->buttonBox)}) {
    root->removeWidget(w);
  }
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("overwriteCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* head = new QHBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(10);
  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  auto* title = new QLabel(tr("Overwrite"), card);
  title->setObjectName(QStringLiteral("instanceDetailName"));
  m_summary = new QLabel(card);
  m_summary->setObjectName(QStringLiteral("instanceDetailCaption"));
  names->addWidget(title);
  names->addWidget(m_summary);
  head->addLayout(names, 1);
  // what to do with the files: keep them in a mod, or look at them
  m_createMod = new QPushButton(FluentIcons::glyph(0xE8F4), tr("Create mod..."), card);
  m_createMod->setToolTip(tr("Move all files from Overwrite into a new regular mod"));
  m_createMod->setAutoDefault(false);
  m_moveToMod = new QPushButton(FluentIcons::glyph(0xEA3C), tr("Move to a mod..."), card);
  m_moveToMod->setToolTip(tr("Choose a regular mod and move all files from Overwrite into it"));
  m_moveToMod->setAutoDefault(false);
  head->addWidget(m_createMod, 0, Qt::AlignVCenter);
  head->addWidget(m_moveToMod, 0, Qt::AlignVCenter);
  connect(m_createMod, &QPushButton::clicked, this, &OverwriteInfoDialog::createModRequested);
  connect(m_moveToMod, &QPushButton::clicked, this, &OverwriteInfoDialog::moveToModRequested);
  ui->explorerButton->setParent(card);
  head->addWidget(ui->explorerButton, 0, Qt::AlignVCenter);
  ui->explorerButton->show();
  layout->addLayout(head);

  auto* files = new QFrame(card);
  files->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* filesLayout = new QVBoxLayout(files);
  filesLayout->setContentsMargins(8, 4, 8, 4);
  ui->filesView->setParent(files);
  ui->filesView->setFrameShape(QFrame::NoFrame);
  filesLayout->addWidget(ui->filesView);
  ui->filesView->show();
  layout->addWidget(files, 1);

  // the hint, as the card's last row with an info glyph
  auto* hint = new QFrame(card);
  hint->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* hintLayout = new QHBoxLayout(hint);
  hintLayout->setContentsMargins(18, 10, 18, 10);
  hintLayout->setSpacing(12);
  auto* glyph = new QLabel(QString(QChar(0xE946)), hint);
  glyph->setObjectName(QStringLiteral("instanceRowGlyph"));
  glyph->setFont(FluentIcons::font(16));
  hintLayout->addWidget(glyph, 0, Qt::AlignTop);
  ui->overwriteContextHint->setParent(hint);
  ui->overwriteContextHint->setObjectName(QStringLiteral("fluentSettingDescription"));
  ui->overwriteContextHint->setWordWrap(true);
  hintLayout->addWidget(ui->overwriteContextHint, 1);
  ui->overwriteContextHint->show();
  layout->addWidget(hint);

  root->addWidget(card, 1);
  root->addWidget(ui->buttonBox);

  // the summary follows what the model sees, a moment after it changes
  auto* timer = new QTimer(this);
  timer->setSingleShot(true);
  timer->setInterval(150);
  connect(timer, &QTimer::timeout, this, &OverwriteInfoDialog::updateSummary);
  for (auto signal : {&QFileSystemModel::rowsInserted, &QFileSystemModel::rowsRemoved}) {
    connect(m_FileSystemModel, signal, timer, qOverload<>(&QTimer::start));
  }
  connect(m_FileSystemModel, &QFileSystemModel::fileRenamed, timer,
          qOverload<>(&QTimer::start));
  updateSummary();
}

void OverwriteInfoDialog::updateSummary()
{
  if (m_summary == nullptr || !m_ModInfo) {
    return;
  }

  qint64 files = 0, folders = 0, bytes = 0;
  QDirIterator it(m_ModInfo->absolutePath(),
                  QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                      QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (it.fileInfo().isDir()) {
      ++folders;
    } else {
      ++files;
      bytes += it.fileInfo().size();
    }
  }

  // nothing to keep without files
  if (m_createMod != nullptr) {
    m_createMod->setEnabled(files > 0);
    m_moveToMod->setEnabled(files > 0);
  }

  // the same words as the main window's Overwrite card
  if (files == 0 && folders == 0) {
    m_summary->setText(tr("Empty: nothing here belongs outside the mods"));
  } else if (files == 0) {
    m_summary->setText(tr("Only empty folders, safe to clear"));
  } else {
    m_summary->setText(tr("%n file(s), %1, not part of any mod", nullptr,
                          static_cast<int>(files))
                           .arg(QLocale().formattedDataSize(bytes)));
  }
}

OverwriteInfoDialog::~OverwriteInfoDialog()
{
  delete ui;
}

void OverwriteInfoDialog::showEvent(QShowEvent* e)
{
  const auto& s = Settings::instance();

  s.geometry().restoreGeometry(this);

  if (!s.geometry().restoreState(ui->filesView->header())) {
    ui->filesView->sortByColumn(0, Qt::AscendingOrder);
  }
  QDialog::showEvent(e);
}

void OverwriteInfoDialog::done(int r)
{
  auto& s = Settings::instance();

  s.geometry().saveGeometry(this);
  s.geometry().saveState(ui->filesView->header());

  QDialog::done(r);
}

void OverwriteInfoDialog::setModInfo(ModInfo::Ptr modInfo)
{
  m_ModInfo = modInfo;
  if (QDir(modInfo->absolutePath()).exists()) {
    m_FileSystemModel->setRootPath(modInfo->absolutePath());
  } else {
    throw MyException(
        tr("mod not found: %1").arg(qUtf8Printable(modInfo->absolutePath())));
  }
}

bool OverwriteInfoDialog::recursiveDelete(const QModelIndex& index)
{
  for (int childRow = 0; childRow < m_FileSystemModel->rowCount(index); ++childRow) {
    QModelIndex childIndex = m_FileSystemModel->index(childRow, 0, index);
    if (m_FileSystemModel->isDir(childIndex)) {
      if (!recursiveDelete(childIndex)) {
        log::error("failed to delete {}", m_FileSystemModel->fileName(childIndex));
        return false;
      }
    } else {
      if (!m_FileSystemModel->remove(childIndex)) {
        log::error("failed to delete {}", m_FileSystemModel->fileName(childIndex));
        return false;
      }
    }
  }
  if (!m_FileSystemModel->remove(index)) {
    log::error("failed to delete {}", m_FileSystemModel->fileName(index));
    return false;
  }
  return true;
}

void OverwriteInfoDialog::deleteFile(const QModelIndex& index)
{

  bool res = m_FileSystemModel->isDir(index) ? recursiveDelete(index)
                                             : m_FileSystemModel->remove(index);
  if (!res) {
    QString fileName = m_FileSystemModel->fileName(index);
    reportError(tr("Failed to delete \"%1\"").arg(fileName));
  }
}

void OverwriteInfoDialog::delete_activated()
{
  if (ui->filesView->hasFocus()) {
    QItemSelectionModel* selection = ui->filesView->selectionModel();

    if (selection->hasSelection() && selection->selectedRows().count() >= 1) {

      if (selection->selectedRows().count() == 0) {
        return;
      } else if (selection->selectedRows().count() == 1) {
        QString fileName = m_FileSystemModel->fileName(selection->selectedRows().at(0));
        if (QMessageBox::question(
                this, tr("Confirm"),
                tr("Are you sure you want to delete \"%1\"?").arg(fileName),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
          return;
        }
      } else {
        if (QMessageBox::question(
                this, tr("Confirm"),
                tr("Are you sure you want to delete the selected files?"),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
          return;
        }
      }

      foreach (QModelIndex index, selection->selectedRows()) {
        deleteFile(index);
      }
    }
  }
}

void OverwriteInfoDialog::deleteTriggered()
{
  if (m_FileSelection.count() == 0) {
    return;
  } else if (m_FileSelection.count() == 1) {
    QString fileName = m_FileSystemModel->fileName(m_FileSelection.at(0));
    if (QMessageBox::question(
            this, tr("Confirm"),
            tr("Are you sure you want to delete \"%1\"?").arg(fileName),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
  } else {
    if (QMessageBox::question(this, tr("Confirm"),
                              tr("Are you sure you want to delete the selected files?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
  }

  foreach (QModelIndex index, m_FileSelection) {
    deleteFile(index);
  }
}

void OverwriteInfoDialog::renameTriggered()
{
  QModelIndex selection = m_FileSelection.at(0);
  QModelIndex index     = selection.sibling(selection.row(), 0);
  if (!index.isValid() || m_FileSystemModel->isReadOnly()) {
    return;
  }

  ui->filesView->edit(index);
}

void OverwriteInfoDialog::openFile(const QModelIndex& index)
{
  shell::Open(m_FileSystemModel->filePath(index));
}

void OverwriteInfoDialog::openTriggered()
{
  foreach (QModelIndex idx, m_FileSelection) {
    openFile(idx);
  }
}

void OverwriteInfoDialog::createDirectoryTriggered()
{
  QModelIndex selection = m_FileSelection.at(0);

  QModelIndex index =
      m_FileSystemModel->isDir(selection) ? selection : selection.parent();
  index = index.sibling(index.row(), 0);

  QString name = tr("New Folder");
  QString path = m_FileSystemModel->filePath(index).append("/");

  QModelIndex existingIndex = m_FileSystemModel->index(path + name);
  int suffix                = 1;
  while (existingIndex.isValid()) {
    name          = tr("New Folder") + QString::number(suffix++);
    existingIndex = m_FileSystemModel->index(path + name);
  }

  QModelIndex newIndex = m_FileSystemModel->mkdir(index, name);
  if (!newIndex.isValid()) {
    reportError(tr("Failed to create \"%1\"").arg(name));
    return;
  }

  ui->filesView->setCurrentIndex(newIndex);
  ui->filesView->edit(newIndex);
}

void OverwriteInfoDialog::on_explorerButton_clicked()
{
  shell::Explore(m_ModInfo->absolutePath());
}

void OverwriteInfoDialog::on_filesView_customContextMenuRequested(const QPoint& pos)
{
  QItemSelectionModel* selectionModel = ui->filesView->selectionModel();
  m_FileSelection                     = selectionModel->selectedRows(0);

  QMenu menu(ui->filesView);
  menu.setToolTipsVisible(true);

  menu.addAction(m_NewFolderAction);

  bool hasFiles = false;

  foreach (QModelIndex idx, m_FileSelection) {
    if (m_FileSystemModel->fileInfo(idx).isFile()) {
      hasFiles = true;
      break;
    }
  }

  if (selectionModel->hasSelection()) {
    menu.addSeparator();
    if (hasFiles) {
      menu.addAction(m_OpenAction);
    }
    if (m_FileSelection.size() == 1) {
      const QString path = m_FileSystemModel->filePath(m_FileSelection[0]);
      menu.addAction(menuGlyph(MenuGlyphs::Folder), tr("Show in folder"), [path] {
        shell::Explore(path);
      });
      addCopyMenu(&menu, {{tr("Name"), QFileInfo(path).fileName()},
                          {tr("Full path"), nativePath(path)}});
    }
    menu.addAction(m_RenameAction);
    menu.addSeparator();
    menu.addAction(m_DeleteAction);
  } else {
    m_FileSelection.clear();
    m_FileSelection.append(m_FileSystemModel->index(m_FileSystemModel->rootPath(), 0));
  }

  menu.exec(ui->filesView->viewport()->mapToGlobal(pos));
}
