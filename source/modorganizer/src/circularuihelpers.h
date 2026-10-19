#pragma once

#include <algorithm>
#include <vector>

#include <QBoxLayout>
#include <QCoreApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

namespace MOBase::CircularUi {

inline QLabel* createBadge(QWidget* parent, const QString& objectName,
                           const QString& iconPath, int size,
                           const QString& fallback = {})
{
  auto* badge = new QLabel(parent);
  badge->setObjectName(objectName);
  badge->setAlignment(Qt::AlignCenter);
  badge->setFixedSize(size, size);

  // Fluent v4 headings are plain text, like Windows 11 Settings
  if (const auto* app = QCoreApplication::instance();
      app != nullptr && app->property("revampedFluent").toBool()) {
    badge->hide();
    return badge;
  }

  const int iconSize = std::max(12, size / 2);
  const QPixmap pixmap = QIcon(iconPath).pixmap(QSize(iconSize, iconSize));
  if (pixmap.isNull()) {
    badge->setText(fallback);
  } else {
    badge->setPixmap(pixmap);
  }
  return badge;
}

inline QHBoxLayout* createHeadingRow(QWidget* parent, QLabel* heading,
                                     const QString& iconPath,
                                     const QString& iconObjectName, int size,
                                     const QString& fallback = {})
{
  auto* row = new QHBoxLayout;
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  row->addWidget(createBadge(parent, iconObjectName, iconPath, size, fallback),
                 0, Qt::AlignVCenter);
  row->addWidget(heading, 1, Qt::AlignVCenter);
  return row;
}

inline void addIconToHeading(QLabel* heading, const QString& iconPath,
                             const QString& iconObjectName, int size,
                             const QString& fallback = {})
{
  if (!heading || !heading->parentWidget()) {
    return;
  }

  auto* layout = qobject_cast<QBoxLayout*>(heading->parentWidget()->layout());
  if (!layout) {
    return;
  }

  const int index = layout->indexOf(heading);
  if (index < 0) {
    return;
  }

  layout->removeWidget(heading);
  layout->insertLayout(index,
                       createHeadingRow(heading->parentWidget(), heading,
                                        iconPath, iconObjectName, size,
                                        fallback));
}

inline void addGroupHeading(QGroupBox* group, const QString& iconPath,
                            const QString& iconObjectName, int size,
                            const QString& titleObjectName,
                            const QString& fallback = {})
{
  if (!group || !group->layout() || group->title().isEmpty()) {
    return;
  }

  const QString title = group->title();
  auto* titleLabel = new QLabel(title, group);
  titleLabel->setObjectName(titleObjectName);
  auto* headingRow = createHeadingRow(group, titleLabel, iconPath,
                                      iconObjectName, size, fallback);

  QLayout* layout = group->layout();
  group->setTitle(QString());
  if (auto* boxLayout = qobject_cast<QBoxLayout*>(layout)) {
    boxLayout->insertLayout(0, headingRow);
    return;
  }

  auto* gridLayout = qobject_cast<QGridLayout*>(layout);
  if (!gridLayout) {
    delete headingRow;
    delete titleLabel;
    group->setTitle(title);
    return;
  }

  struct GridItem
  {
    QLayoutItem* item;
    int row;
    int column;
    int rowSpan;
    int columnSpan;
  };

  std::vector<GridItem> items;
  for (int index = gridLayout->count() - 1; index >= 0; --index) {
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    gridLayout->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
    items.push_back({gridLayout->takeAt(index), row, column, rowSpan,
                     columnSpan});
  }
  std::reverse(items.begin(), items.end());

  gridLayout->addLayout(headingRow, 0, 0, 1,
                        std::max(1, gridLayout->columnCount()));
  for (const GridItem& item : items) {
    gridLayout->addItem(item.item, item.row + 1, item.column, item.rowSpan,
                        item.columnSpan);
  }
}

}  // namespace MOBase::CircularUi
