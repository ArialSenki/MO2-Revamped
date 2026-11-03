#include "savetextasdialog.h"
#include "ui_savetextasdialog.h"
#include <QClipboard>
#include <QFileDialog>
#include <QTimer>
#include <report.h>

using MOBase::reportError;

namespace
{
// the button says what it did for a moment
void showDone(QPushButton* button, const QString& text)
{
  const QString original = button->property("idleText").toString().isEmpty()
                               ? button->text()
                               : button->property("idleText").toString();
  button->setProperty("idleText", original);
  button->setText(text);
  QTimer::singleShot(2000, button, [button, original] {
    button->setText(original);
  });
}
}  // namespace

SaveTextAsDialog::SaveTextAsDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::SaveTextAsDialog)
{
  ui->setupUi(this);
}

SaveTextAsDialog::~SaveTextAsDialog()
{
  delete ui;
}

void SaveTextAsDialog::setText(const QString& text)
{
  ui->textEdit->setPlainText(text);
}

void SaveTextAsDialog::on_closeBtn_clicked()
{
  this->close();
}

void SaveTextAsDialog::on_clipboardBtn_clicked()
{
  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(ui->textEdit->toPlainText());
  showDone(ui->clipboardBtn, tr("Copied"));
}

void SaveTextAsDialog::on_saveAsBtn_clicked()
{
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save text output"), QString(),
      tr("Text and CSV files") + " (*.txt *.csv)");
  if (!fileName.isEmpty()) {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
      reportError(tr("failed to open \"%1\" for writing").arg(fileName));
      return;
    }

    const QByteArray text = ui->textEdit->toPlainText().toUtf8();
    if (file.write(text) != text.size() || !file.flush()) {
      reportError(tr("\"%1\" could not be written completely. It may be missing part "
                     "of the text.")
                      .arg(fileName));
      return;
    }
    showDone(ui->saveAsBtn, tr("Saved"));
  }
}
