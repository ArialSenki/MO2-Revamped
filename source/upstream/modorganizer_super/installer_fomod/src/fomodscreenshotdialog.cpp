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

#include "fomodscreenshotdialog.h"

#include "scalelabel.h"

#include <QFont>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace
{

// Segoe Fluent Icons, with MDL2 for Windows 10
QFont glyphFont(int pixels)
{
  QFont font;
  font.setFamilies({QStringLiteral("Segoe Fluent Icons"),
                    QStringLiteral("Segoe MDL2 Assets")});
  font.setPixelSize(pixels);
  return font;
}

QPushButton* glyphButton(char16_t glyph, const QString& name, QWidget* parent)
{
  auto* button = new QPushButton(QString(QChar(glyph)), parent);
  button->setObjectName(QStringLiteral("fomodViewerButton"));
  button->setFont(glyphFont(16));
  button->setAccessibleName(name);
  button->setToolTip(name);
  button->setFixedSize(44, 44);
  button->setAutoDefault(false);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

// a thumbnail, read at its small size
QIcon thumbnail(const QString& path)
{
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  if (size.isValid()) {
    reader.setScaledSize(size.scaled(200, 150, Qt::KeepAspectRatio));
  }
  return QIcon(QPixmap::fromImage(reader.read()));
}

}  // namespace

FomodScreenshotDialog::FomodScreenshotDialog(
    QWidget* parent, std::vector<std::pair<QString, QString>> carouselImages,
    int carouselIndex)
    : QDialog(parent, Qt::FramelessWindowHint), m_carouselImages(carouselImages)
{
  setObjectName(QStringLiteral("FomodScreenshotDialog"));
  setWindowTitle(tr("Pictures"));
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_DeleteOnClose);
  // the installer waits: Next mustn't change the step behind the pictures
  setWindowModality(Qt::WindowModal);

  // full screen on the installer's screen, like a photo viewer; MO2 leaves
  // full-screen dialogs at their size (a fixed size was cut to 82% of it)
  QScreen* target = parent != nullptr ? parent->screen() : screen();
  setGeometry(target->geometry());
  setWindowState(Qt::WindowFullScreen);
  const QRect available = target->availableGeometry();

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(24, 16, 24, 20);
  root->setSpacing(12);

  auto* top = new QHBoxLayout;
  m_position = new QLabel(this);
  m_position->setObjectName(QStringLiteral("fomodViewerText"));
  top->addWidget(m_position);
  top->addStretch(1);
  auto* close = glyphButton(0xE711, tr("Close"), this);
  top->addWidget(close);
  root->addLayout(top);

  auto* middle = new QHBoxLayout;
  middle->setSpacing(16);
  m_previous = glyphButton(0xE76B, tr("Previous picture"), this);
  m_next     = glyphButton(0xE76C, tr("Next picture"), this);
  m_image    = new ScaleLabel(this);
  m_image->setAlignment(Qt::AlignCenter);
  m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  middle->addWidget(m_previous, 0, Qt::AlignVCenter);
  middle->addWidget(m_image, 1);
  middle->addWidget(m_next, 0, Qt::AlignVCenter);
  root->addLayout(middle, 1);

  m_title = new QLabel(this);
  m_title->setObjectName(QStringLiteral("fomodViewerTitle"));
  m_title->setTextFormat(Qt::PlainText);
  m_title->setAlignment(Qt::AlignCenter);
  m_title->setWordWrap(true);
  root->addWidget(m_title);

  m_strip = new QListWidget(this);
  m_strip->setObjectName(QStringLiteral("fomodViewerStrip"));
  m_strip->setAccessibleName(tr("Pictures"));
  m_strip->setViewMode(QListView::IconMode);
  m_strip->setFlow(QListView::LeftToRight);
  m_strip->setWrapping(false);
  m_strip->setMovement(QListView::Static);
  m_strip->setIconSize(QSize(100, 75));
  m_strip->setSpacing(6);
  m_strip->setFixedHeight(104);
  m_strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_strip->setFrameShape(QFrame::NoFrame);
  for (const auto& image : m_carouselImages) {
    auto* item = new QListWidgetItem(thumbnail(image.second), QString(), m_strip);
    item->setToolTip(image.first);
    item->setData(Qt::AccessibleTextRole, image.first);
    item->setSizeHint(QSize(112, 86));
  }
  root->addWidget(m_strip, 0, Qt::AlignHCenter);
  m_strip->setMaximumWidth(available.width() - 48);
  m_strip->setMinimumWidth(
      qMin(available.width() - 48, static_cast<int>(m_carouselImages.size()) * 124 + 8));
  m_strip->setVisible(m_carouselImages.size() > 1);

  for (auto* button : {close, m_previous, m_next}) {
    button->installEventFilter(this);
  }
  connect(close, &QPushButton::clicked, this, &QDialog::close);
  connect(m_previous, &QPushButton::clicked, this, [this] {
    showPicture(m_index - 1);
  });
  connect(m_next, &QPushButton::clicked, this, [this] {
    showPicture(m_index + 1);
  });
  connect(m_strip, &QListWidget::currentRowChanged, this, [this](int row) {
    showPicture(row);
  });

  showPicture(m_carouselImages.empty() ? -1 : qBound(0, carouselIndex,
                                               static_cast<int>(m_carouselImages.size()) - 1));
  m_strip->setFocus();
}

void FomodScreenshotDialog::showPicture(int index)
{
  if (m_carouselImages.empty()) {
    m_position->clear();
    m_title->setText(tr("This step has no pictures."));
    m_previous->setEnabled(false);
    m_next->setEnabled(false);
    return;
  }
  index = qBound(0, index, static_cast<int>(m_carouselImages.size()) - 1);
  if (index == m_index) {
    return;
  }
  m_index           = index;
  const auto& image = m_carouselImages[static_cast<std::size_t>(index)];
  m_image->setScalableResource(image.second);
  m_image->setAccessibleName(image.first);
  m_title->setText(image.first);
  m_position->setText(tr("%1 of %2").arg(index + 1).arg(m_carouselImages.size()));
  m_previous->setEnabled(index > 0);
  m_next->setEnabled(index + 1 < static_cast<int>(m_carouselImages.size()));
  if (m_strip->currentRow() != index) {
    m_strip->setCurrentRow(index);
  }
}

void FomodScreenshotDialog::paintEvent(QPaintEvent*)
{
  // the overlay is dark in both themes, as Windows' photo viewers are
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 215));
}

bool FomodScreenshotDialog::eventFilter(QObject* watched, QEvent* event)
{
  if (event->type() == QEvent::KeyPress) {
    const int key = static_cast<QKeyEvent*>(event)->key();
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home ||
        key == Qt::Key_End) {
      keyPressEvent(static_cast<QKeyEvent*>(event));
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void FomodScreenshotDialog::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
  case Qt::Key_Left:
    showPicture(m_index - 1);
    return;
  case Qt::Key_Right:
    showPicture(m_index + 1);
    return;
  case Qt::Key_Home:
    showPicture(0);
    return;
  case Qt::Key_End:
    showPicture(static_cast<int>(m_carouselImages.size()) - 1);
    return;
  default:
    QDialog::keyPressEvent(event);
  }
}
