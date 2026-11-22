/****************************************************************************
**
** Copyright (c) 2007 Trolltech ASA <info@trolltech.com>
**
** Use, modification and distribution is allowed without limitation,
** warranty, liability or support of any kind.
**
****************************************************************************/

#include "lineeditclear.h"
#include <QStyle>
#include <QToolButton>

namespace MOBase
{

LineEditClear::LineEditClear(QWidget* parent) : QLineEdit(parent)
{
  clearButton = new QToolButton(this);
  // MO2 Revamped: MO2's own clear icon, which it draws as a Fluent glyph
  // (2.5.2's edit_clear picture is gone from the resources)
  clearButton->setIcon(QIcon(QStringLiteral(":/MO/gui/mainwindow/clear.svg")));
  clearButton->setIconSize(QSize(16, 16));
  clearButton->setCursor(Qt::ArrowCursor);
  clearButton->setStyleSheet("QToolButton { border: none; padding: 0px; }");
  clearButton->hide();
  connect(clearButton, SIGNAL(clicked()), this, SLOT(clear()));
  connect(this, SIGNAL(textChanged(const QString&)), this,
          SLOT(updateCloseButton(const QString&)));
  int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
  setStyleSheet(QString("QLineEdit { padding-right: %1px; } ")
                    .arg(clearButton->sizeHint().width() + frameWidth + 1));
  /*  QSize msz = minimumSizeHint();
    setMinimumSize(qMax(msz.width(), clearButton->sizeHint().height() + frameWidth * 2 +
    2), qMax(msz.height(), clearButton->sizeHint().height() + frameWidth * 2 + 2));*/
}

void LineEditClear::resizeEvent(QResizeEvent*)
{
  QSize sz       = clearButton->sizeHint();
  int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
  clearButton->move(rect().right() - frameWidth - sz.width(),
                    (rect().bottom() + 1 - sz.height()) / 2);
}

void LineEditClear::updateCloseButton(const QString& text)
{
  clearButton->setVisible(!text.isEmpty());
}

}  // namespace MOBase
