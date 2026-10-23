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

#ifndef MESSAGEDIALOG_H
#define MESSAGEDIALOG_H

#include <QColor>
#include <QPointer>
#include <QWidget>

class QTimer;

/**
 * a Windows 11 style notice: a small card with an info glyph and the text
 * that rises in over the bottom of a window, never takes the focus, and fades
 * away by itself after a moment (it waits while the mouse is over it; a click
 * dismisses it). The theme sets its colors (qproperty-cardColor and so on);
 * without them it follows the palette.
 **/
class MessageDialog : public QWidget
{
  Q_OBJECT
  Q_PROPERTY(QColor cardColor MEMBER m_card)
  Q_PROPERTY(QColor lineColor MEMBER m_line)
  Q_PROPERTY(QColor textColor MEMBER m_ink)

public:
  /**
   * @brief constructor
   *
   * @param text the message to display
   * @param reference widget the message is shown over, at its bottom center;
   *        nullptr shows it at the bottom of the screen
   **/
  explicit MessageDialog(const QString& text, QWidget* reference);

  ~MessageDialog() override;

  /**
   * factory function for message dialogs. This can be used as a fire-and-forget. The
   *message will automatically positioned to the reference dialog and get a reasonable
   *view time
   *
   * @param text the text to display. The length of this text is used to determine how
   *long the dialog is to be shown
   * @param reference the reference widget on top of which the message should be
   *displayed; the main window when nullptr, the screen when there is none
   * @param true if the message should bring MO to front to ensure this message is
   *visible
   **/
  static void showMessage(const QString& text, QWidget* reference,
                          bool bringToFront = true);

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  QString m_text;
  QPointer<QWidget> m_reference;
  QTimer* m_timer;
  QColor m_card, m_line, m_ink;
  bool m_closing = false;

  // the text's height when wrapped to width
  int textHeight(int width) const;
  QRect cardRect() const;
  void place();
  void dismiss();
};

#endif  // MESSAGEDIALOG_H
