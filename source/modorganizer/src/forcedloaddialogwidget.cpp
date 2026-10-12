#include "forcedloaddialogwidget.h"
#include "executableinfo.h"
#include "ui_forcedloaddialogwidget.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStringList>
#include <log.h>

using namespace MOBase;

namespace
{
QString browseForExistingFile(QWidget* parent, const QString& caption,
                              const QString& directory, const QString& filter,
                              const QString& objectName)
{
  QFileDialog dialog(parent, caption, directory, filter);
  dialog.setObjectName(objectName);
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setOption(QFileDialog::ReadOnly);
  dialog.setOption(QFileDialog::DontUseNativeDialog);
  return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0)
                                            : QString();
}
}  // namespace

ForcedLoadDialogWidget::ForcedLoadDialogWidget(const IPluginGame* game, QWidget* parent)
    : QWidget(parent), ui(new Ui::ForcedLoadDialogWidget), m_Forced(false), m_GamePlugin(game)
{
  ui->setupUi(this);
  setMinimumHeight(42);
}

ForcedLoadDialogWidget::~ForcedLoadDialogWidget()
{
  delete ui;
}

bool ForcedLoadDialogWidget::getEnabled()
{
  return ui->enabledBox->isChecked();
}

bool ForcedLoadDialogWidget::getForced()
{
  return m_Forced;
}

QString ForcedLoadDialogWidget::getLibraryPath()
{
  return ui->libraryPathEdit->text();
}

QString ForcedLoadDialogWidget::getProcess()
{
  return ui->processEdit->text();
}

void ForcedLoadDialogWidget::setEnabled(bool enabled)
{
  ui->enabledBox->setChecked(enabled);
}

void ForcedLoadDialogWidget::setForced(bool forced)
{
  m_Forced = forced;
  ui->libraryPathBrowseButton->setEnabled(!forced);
  ui->libraryPathEdit->setEnabled(!forced);
  ui->processBrowseButton->setEnabled(!forced);
  ui->processEdit->setEnabled(!forced);
}

void ForcedLoadDialogWidget::setLibraryPath(const QString& path)
{
  ui->libraryPathEdit->setText(path);
}

void ForcedLoadDialogWidget::setProcess(const QString& name)
{
  ui->processEdit->setText(name);
}

void ForcedLoadDialogWidget::on_enabledBox_toggled()
{
  // anything to do?
}

void ForcedLoadDialogWidget::on_libraryPathBrowseButton_clicked()
{
  QDir gameDir(m_GamePlugin->gameDirectory());
  QString startPath = gameDir.absolutePath();
  QString result = browseForExistingFile(
      this, tr("Select a library"), startPath,
      tr("Dynamic link libraries (*.dll)"), "forcedLibraryFilePicker");
  if (!result.isEmpty()) {
    QFileInfo fileInfo(result);
    QString relativePath = gameDir.relativeFilePath(fileInfo.filePath());
    QString filePath     = fileInfo.filePath();
    if (!relativePath.startsWith("..")) {
      filePath = relativePath;
    }

    if (fileInfo.exists()) {
      ui->libraryPathEdit->setText(filePath);
    } else {
      log::error("{} does not exist", filePath);
    }
  }
}

void ForcedLoadDialogWidget::on_processBrowseButton_clicked()
{
  QDir gameDir(m_GamePlugin->gameDirectory());
  QString startPath = gameDir.absolutePath();
  QString result = browseForExistingFile(
      this, tr("Select a process"), startPath,
      tr("Executable files (*.exe)"), "forcedProcessFilePicker");
  if (!result.isEmpty()) {
    QFileInfo fileInfo(result);
    QString fileName = fileInfo.fileName();

    if (fileInfo.exists()) {
      ui->processEdit->setText(fileName);
    } else {
      log::error("{} does not exist", fileInfo.filePath());
    }
  }
}
