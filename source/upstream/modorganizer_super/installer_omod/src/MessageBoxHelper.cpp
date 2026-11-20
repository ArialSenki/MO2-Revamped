#include "MessageBoxHelper.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QPushButton>

#include "fluentcontentdialog.h"

MessageBoxHelper::MessageBoxHelper()
{
  moveToThread(QApplication::instance()->thread());

  connect(this, &MessageBoxHelper::criticalMessageBoxSignal, this, &MessageBoxHelper::criticalMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
  connect(this, &MessageBoxHelper::informationMessageBoxSignal, this, &MessageBoxHelper::informationMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
  connect(this, &MessageBoxHelper::questionMessageBoxSignal, this, &MessageBoxHelper::questionMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
  connect(this, &MessageBoxHelper::warningMessageBoxSignal, this, &MessageBoxHelper::warningMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
}

QMessageBox::StandardButton MessageBoxHelper::critical(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit criticalMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

QMessageBox::StandardButton MessageBoxHelper::information(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit informationMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

QMessageBox::StandardButton MessageBoxHelper::question(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit questionMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

QMessageBox::StandardButton MessageBoxHelper::warning(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit warningMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

namespace
{

// MO2 Revamped: an OMOD script's message or question is MO2's content dialog
// over the installer's window, with the script's own title and words; 2.5.2
// showed Qt's message boxes. Without a title, a plain one says where the text
// comes from.
QMessageBox::StandardButton showScriptMessage(QWidget* parent, const QString& title,
                                              const QString& text,
                                              QMessageBox::StandardButtons buttons,
                                              QMessageBox::StandardButton defaultButton)
{
  const QString heading =
      title.trimmed().isEmpty()
          ? QCoreApplication::translate("RevampedOmodScript", "Message from the installer")
          : title;
  RevampedFluent::ContentDialog dialog(parent, QStringLiteral("omodScriptMessage"), heading,
                                       text);
  dialog.setMinimumWidth(460);

  struct Choice
  {
    QMessageBox::StandardButton button;
    const char* text;
  };
  static const Choice choices[] = {
      {QMessageBox::Yes, "Yes"},       {QMessageBox::No, "No"},
      {QMessageBox::Ok, "OK"},         {QMessageBox::Retry, "Retry"},
      {QMessageBox::Ignore, "Ignore"}, {QMessageBox::Abort, "Abort"},
      {QMessageBox::Cancel, "Cancel"},
  };
  if (buttons == QMessageBox::NoButton) {
    buttons = QMessageBox::Ok;
  }
  QMessageBox::StandardButton primary = defaultButton;
  if (primary == QMessageBox::NoButton || !buttons.testFlag(primary)) {
    primary = buttons.testFlag(QMessageBox::Yes) ? QMessageBox::Yes
              : buttons.testFlag(QMessageBox::Ok) ? QMessageBox::Ok
                                            : QMessageBox::NoButton;
  }

  QButtonGroup group(&dialog);
  for (const Choice& choice : choices) {
    if (buttons.testFlag(choice.button)) {
      QPushButton* button = dialog.addButton(
          QCoreApplication::translate("RevampedOmodScript", choice.text),
          choice.button == primary);
      group.addButton(button, static_cast<int>(choice.button));
    }
  }
  // done(id) ends the dialog with the clicked button
  QObject::connect(&group, SIGNAL(idClicked(int)), &dialog, SLOT(done(int)));

  const int result = dialog.exec();
  if (result != QDialog::Rejected) {
    return static_cast<QMessageBox::StandardButton>(result);
  }
  // closed with Esc or the title bar: the answer that changes nothing
  for (QMessageBox::StandardButton escape : {QMessageBox::Cancel, QMessageBox::No, QMessageBox::Abort, QMessageBox::Ignore, QMessageBox::Ok}) {
    if (buttons.testFlag(escape)) {
      return escape;
    }
  }
  return QMessageBox::NoButton;
}

}  // namespace

void MessageBoxHelper::criticalMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = showScriptMessage(parent, title, text, buttons, defaultButton);
}

void MessageBoxHelper::informationMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = showScriptMessage(parent, title, text, buttons, defaultButton);
}

void MessageBoxHelper::questionMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = showScriptMessage(parent, title, text, buttons, defaultButton);
}

void MessageBoxHelper::warningMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = showScriptMessage(parent, title, text, buttons, defaultButton);
}
