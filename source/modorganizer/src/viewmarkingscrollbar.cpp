#include "viewmarkingscrollbar.h"
#include "modelutils.h"
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

using namespace MOShared;

ViewMarkingScrollBar::ViewMarkingScrollBar(QTreeView* view, int role)
    : QScrollBar(view), m_view(view), m_role(role)
{
  // not implemented for horizontal sliders
  Q_ASSERT(this->orientation() == Qt::Vertical);

  connect(view, &QTreeView::expanded, this, &ViewMarkingScrollBar::invalidateMarks);
  connect(view, &QTreeView::collapsed, this, &ViewMarkingScrollBar::invalidateMarks);
}

void ViewMarkingScrollBar::invalidateMarks()
{
  m_marksValid = false;
  update();
}

void ViewMarkingScrollBar::watchModel()
{
  if (m_watchedModel != nullptr) {
    m_watchedModel->disconnect(this);
  }

  m_watchedModel = m_view->model();
  m_marksValid   = false;

  const auto* model = m_watchedModel.data();
  if (model == nullptr) {
    return;
  }

  connect(model, &QAbstractItemModel::dataChanged, this,
          &ViewMarkingScrollBar::invalidateMarks);
  connect(model, &QAbstractItemModel::modelReset, this,
          &ViewMarkingScrollBar::invalidateMarks);
  connect(model, &QAbstractItemModel::layoutChanged, this,
          &ViewMarkingScrollBar::invalidateMarks);
  connect(model, &QAbstractItemModel::rowsInserted, this,
          &ViewMarkingScrollBar::invalidateMarks);
  connect(model, &QAbstractItemModel::rowsRemoved, this,
          &ViewMarkingScrollBar::invalidateMarks);
  connect(model, &QAbstractItemModel::rowsMoved, this,
          &ViewMarkingScrollBar::invalidateMarks);
}

void ViewMarkingScrollBar::readMarks()
{
  const auto indices = visibleIndex(m_view, 0);

  m_marks.clear();
  m_rowCount = static_cast<int>(indices.size());
  for (int i = 0; i < m_rowCount; ++i) {
    const QColor markColor = color(indices[i]);
    if (markColor.isValid()) {
      m_marks.push_back({i, markColor});
    }
  }
  m_marksValid = true;
}

QColor ViewMarkingScrollBar::color(const QModelIndex& index) const
{
  auto data = index.data(m_role);
  if (data.canConvert<QColor>()) {
    return data.value<QColor>();
  }
  return QColor();
}

void ViewMarkingScrollBar::paintEvent(QPaintEvent* event)
{
  if (m_view->model() == nullptr) {
    return;
  }
  QScrollBar::paintEvent(event);

  QStyleOptionSlider styleOption;
  initStyleOption(&styleOption);

  QPainter painter(this);

  QRect handleRect = style()->subControlRect(QStyle::CC_ScrollBar, &styleOption,
                                             QStyle::SC_ScrollBarSlider, this);
  QRect innerRect  = style()->subControlRect(QStyle::CC_ScrollBar, &styleOption,
                                             QStyle::SC_ScrollBarGroove, this);

  if (m_watchedModel != m_view->model()) {
    watchModel();
  }
  if (!m_marksValid) {
    readMarks();
  }

  painter.translate(innerRect.topLeft() + QPoint(0, 3));
  qreal scale =
      static_cast<qreal>(innerRect.height() - 3) / static_cast<qreal>(m_rowCount);

  for (const auto& mark : m_marks) {
    painter.setPen(mark.color);
    painter.setBrush(mark.color);
    painter.drawRect(QRect(2, mark.row * scale - 2, handleRect.width() - 5, 3));
  }
}
