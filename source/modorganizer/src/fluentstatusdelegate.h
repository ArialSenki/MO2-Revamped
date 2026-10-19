#ifndef FLUENTSTATUSDELEGATE_H
#define FLUENTSTATUSDELEGATE_H

// Fluent v4 "Status" column of the mod list: one short label per mod
// ("New version", "2 conflicts", "Fully overwritten") instead of the row of
// conflict icons. The column's tooltip still lists every conflict flag.

#include <QStyledItemDelegate>

class FluentStatusDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};

#endif  // FLUENTSTATUSDELEGATE_H
