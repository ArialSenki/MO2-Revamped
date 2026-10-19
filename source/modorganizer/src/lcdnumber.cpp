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

#include "lcdnumber.h"

#include <QFrame>

#include <algorithm>
#include <QHBoxLayout>
#include <QLayout>
#include <QPainter>
#include <QTimer>
#include <QToolTip>

LCDNumber::LCDNumber(QWidget* parent) : QLCDNumber(parent) {}

void LCDNumber::useFrame()
{
  QWidget* parent = parentWidget();
  if (parent == nullptr || parent->layout() == nullptr ||
      parent->property("lcdFrame").toBool()) {
    return;
  }

  auto* frame = new QFrame(parent);
  frame->setObjectName(objectName() + QStringLiteral("Frame"));
  frame->setProperty("lcdFrame", true);
  frame->setSizePolicy(sizePolicy());
  frame->setToolTip(toolTip());

  // replaceWidget() searches nested layouts as well
  if (parent->layout()->replaceWidget(this, frame) == nullptr) {
    delete frame;
    return;
  }

  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(7, 3, 7, 3);
  layout->setSpacing(0);
  layout->addWidget(this);

  setFrameShape(QFrame::NoFrame);
  setMinimumHeight(qMax(0, minimumHeight() - 6));
}

void LCDNumber::useFrames(QWidget* root)
{
  if (root == nullptr) {
    return;
  }

  for (LCDNumber* lcd : root->findChildren<LCDNumber*>()) {
    lcd->useFrame();
  }
}

void LCDNumber::mousePressEvent(QMouseEvent* event)
{
  m_toolTipPosition = mapToGlobal(event->pos());
  QTimer::singleShot(100, this, SLOT(showToolTip()));
}

void LCDNumber::showToolTip()
{
  QToolTip::showText(m_toolTipPosition, toolTip());
}

void LCDNumber::setDisplayText(const QString& text)
{
  if (m_displayText == text) {
    return;
  }

  m_displayText = text;
  updateGeometry();
  update();
}

QString LCDNumber::displayText() const
{
  return m_displayText;
}

QString LCDNumber::shownText() const
{
  // Fluent counters always read as text; without a set text, the number
  QString text = m_displayText;
  if (text.isEmpty()) {
    text = QString::number(intValue());
  }
  return text;
}

QSize LCDNumber::sizeHint() const
{
  QFont f = font();
  f.setWeight(QFont::DemiBold);
  const QFontMetrics fm(f);
  return QSize(std::max(fm.horizontalAdvance(shownText()) + 4, fm.height()),
               fm.height() + 2);
}

QSize LCDNumber::minimumSizeHint() const
{
  return sizeHint();
}

void LCDNumber::paintEvent(QPaintEvent*)
{
  // the stylesheet "color" ends up in the foreground role of the palette
  QPainter p(this);
  QFont f = font();
  f.setWeight(QFont::DemiBold);
  p.setFont(f);
  p.setPen(palette().color(foregroundRole()));
  p.drawText(rect(), Qt::AlignCenter, shownText());
}
