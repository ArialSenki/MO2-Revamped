/*
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

#ifndef LCDNUMBER_H
#define LCDNUMBER_H

#include <QLCDNumber>
#include <QMouseEvent>
#include <QWidget>

class LCDNumber : public QLCDNumber
{
  Q_OBJECT

public:
  LCDNumber(QWidget* parent = nullptr);
  void mousePressEvent(QMouseEvent* event);

  // QLCDNumber paints its digits across the whole widget and ignores the
  // stylesheet padding, so a themed border clips the outer digits. Moves the
  // counter into a frame named "<objectName>Frame" (property lcdFrame=true)
  // that carries the border and the inner margins instead.
  void useFrame();

  // calls useFrame() on every LCDNumber below root
  static void useFrames(QWidget* root);

  // Fluent counters show plain text such as "2/4" in the widget font instead
  // of seven-segment digits; an empty string restores the digits
  void setDisplayText(const QString& text);
  QString displayText() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

public slots:
  void showToolTip();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QPoint m_toolTipPosition;
  QString m_displayText;
};

#endif  // LCDNUMBER_H
