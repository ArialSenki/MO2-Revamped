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
#include <QHBoxLayout>
#include <QLayout>
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
