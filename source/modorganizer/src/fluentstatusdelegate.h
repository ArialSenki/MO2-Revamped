#ifndef FLUENTSTATUSDELEGATE_H
#define FLUENTSTATUSDELEGATE_H

// Fluent v4 "Status" column of the mod list: short pills per mod
// ("New version", "2 conflicts", "Fully overwritten") instead of the row of
// conflict icons. An arrow and the tone say whether the mod wins (up), loses
// (down) or both (up-down); an available update is shown next to the conflict
// state. The column's tooltip still lists every conflict flag.

#include "modinfo.h"

#include <QStyledItemDelegate>

class FluentStatusDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  // sort key matching the pills: update first, then conflict severity
  // (none < wins < mixed < loses < fully overwritten), then conflict count
  static quint64 sortKey(const ModInfo::Ptr& mod);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};

#endif  // FLUENTSTATUSDELEGATE_H
