#ifndef VIEWMARKINGSCROLLBAR_H
#define VIEWMARKINGSCROLLBAR_H

#include <QPointer>
#include <QScrollBar>
#include <QTreeView>

#include <vector>

class ViewMarkingScrollBar : public QScrollBar
{
public:
  ViewMarkingScrollBar(QTreeView* view, int role);

  // the marks are read again at the next paint; the bar notices changes in
  // the view's rows and expanded items by itself
  void invalidateMarks();

protected:
  void paintEvent(QPaintEvent* event) override;

  // retrieve the color of the marker for the given index
  //
  virtual QColor color(const QModelIndex& index) const;

private:
  struct Mark
  {
    int row;
    QColor color;
  };

  QTreeView* m_view;
  int m_role;

  // going through every row on each paint is too slow, and the bar repaints
  // whenever the thumb moves or animates
  std::vector<Mark> m_marks;
  int m_rowCount    = 0;
  bool m_marksValid = false;
  QPointer<QAbstractItemModel> m_watchedModel;

  void watchModel();
  void readMarks();
};

#endif  // VIEWMARKINGSCROLLBAR_H
