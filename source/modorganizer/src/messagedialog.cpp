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

#include "messagedialog.h"
#include "fluentmotion.h"
#include "ui_messagedialog.h"
#include <QResizeEvent>
#include <QTimer>
#include <Windows.h>
#include <log.h>

using namespace MOBase;

MessageDialog::MessageDialog(const QString& text, QWidget* reference)
    : QDialog(reference), ui(new Ui::MessageDialog)
{
  ui->setupUi(this);

  // very crude way to ensure no single word in the test is wider than the message
  // window. ellide in the center if necessary
  QFontMetrics metrics(ui->message->font());
  QString restrictedText;
  QStringList lines = text.split("\n");
  foreach (const QString& line, lines) {
    QString newLine;
    QStringList words = line.split(" ");
    foreach (const QString& word, words) {
      if (word.length() > 10) {
        newLine +=
            "<span style=\"nobreak\">" +
            metrics.elidedText(word, Qt::ElideMiddle, ui->message->maximumWidth()) +
            "</span>";
      } else {
        newLine += word;
      }
      newLine += " ";
    }
    restrictedText += newLine + "\n";
  }

  ui->message->setText(restrictedText);
  this->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  this->setAttribute(Qt::WA_TranslucentBackground);
  this->setMinimumWidth(280);
  this->setFocusPolicy(Qt::NoFocus);
  this->setAttribute(Qt::WA_ShowWithoutActivating);
  this->setAttribute(Qt::WA_DeleteOnClose);
  QTimer::singleShot(1000 + (text.length() * 40), this, [this] {
    // closing deletes the toast; it used to stay hidden in memory
    FluentMotion::fadeOut(this, [this] {
      close();
    });
  });
  if (reference != nullptr) {
    QPoint position =
        reference->mapToGlobal(QPoint(reference->width() / 2, reference->height()));
    position.rx() -= this->width() / 2;
    position.ry() -= this->height() + 5;
    move(position);
  }
}

MessageDialog::~MessageDialog()
{
  delete ui;
}

void MessageDialog::resizeEvent(QResizeEvent* event)
{
  QWidget* par = parentWidget();
  if (par != nullptr) {
    QPoint position = par->mapToGlobal(QPoint(par->width() / 2, par->height()));
    position.rx() -= event->size().width() / 2;
    position.ry() -= event->size().height() + 5;
    move(position);
  }
}

void MessageDialog::showMessage(const QString& text, QWidget* reference,
                                bool bringToFront)
{
  log::debug("{}", text);

  if (!reference) {
    for (QWidget* w : qApp->topLevelWidgets()) {
      if (dynamic_cast<QMainWindow*>(w)) {
        reference = w;
        break;
      }
    }
  }

  if (!reference) {
    return;
  }

  MessageDialog* dialog = new MessageDialog(text, reference);
  // the toast rises into place over the bottom of the window; it starts
  // transparent so its first frame does not flash at full opacity
  const bool animate = qApp->property("revampedFluent").toBool() && FluentMotion::enabled();
  if (animate) {
    dialog->setWindowOpacity(0.0);
  }
  dialog->show();
  if (animate) {
    FluentMotion::popIn(dialog, -6);
  }

  if (bringToFront) {
    reference->activateWindow();
  }
}
