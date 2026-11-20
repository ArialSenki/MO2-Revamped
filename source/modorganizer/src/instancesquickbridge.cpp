#include "instancesquickbridge.h"

#include "fluentpanels.h"
#include "instancemanagerdialog.h"
#include "quickui.h"

#include <QAbstractButton>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>

namespace
{

constexpr int IndexRole = Qt::UserRole + 2;  // as in instancemanagerdialog.cpp
constexpr int GroupRole = Qt::UserRole + 4;

// shown unless hidden on purpose: the controls are never on screen in the Quick
// version, so isHidden() would be true for all of them
bool shown(const QWidget* w)
{
  return w != nullptr && !(w->testAttribute(Qt::WA_WState_ExplicitShowHide) &&
                           w->testAttribute(Qt::WA_WState_Hidden));
}

QString glyphText(char16_t glyph)
{
  return glyph == 0 ? QString() : QString(QChar(glyph));
}

QVariantMap buttonState(const QAbstractButton* b)
{
  if (b == nullptr) {
    return {{QStringLiteral("visible"), false}};
  }
  return {{QStringLiteral("text"), b->text()},
          {QStringLiteral("tip"), b->toolTip()},
          {QStringLiteral("enabled"), b->isEnabled()},
          {QStringLiteral("visible"), shown(b)}};
}

}  // namespace

InstancesQuickBridge::InstancesQuickBridge(InstanceManagerDialog* dialog)
    : QObject(dialog), m_dialog(dialog)
{
  connect(dialog->m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &InstancesQuickBridge::scheduleSync);
  connect(dialog->m_model, &QAbstractItemModel::modelReset, this,
          &InstancesQuickBridge::scheduleSync);
  connect(dialog->m_model, &QAbstractItemModel::rowsInserted, this,
          &InstancesQuickBridge::scheduleSync);
  connect(dialog->m_model, &QAbstractItemModel::rowsRemoved, this,
          &InstancesQuickBridge::scheduleSync);
  connect(dialog->m_model, &QAbstractItemModel::dataChanged, this,
          &InstancesQuickBridge::scheduleSync);
  sync();
}

void InstancesQuickBridge::scheduleSync()
{
  if (m_pending) {
    return;
  }
  m_pending = true;
  QTimer::singleShot(0, this, [this] {
    m_pending = false;
    sync();
  });
}

void InstancesQuickBridge::openSearch()
{
  m_searchOpen = true;
  sync();
  emit searchRequested();
}

void InstancesQuickBridge::sync()
{
  auto* d = m_dialog;

  // the list, without the rows the search hides
  QVariantList rows;
  int current           = -1;
  const auto selection  = d->m_list->selectionModel()->selectedIndexes();
  const int selectedRow = selection.size() == 1 ? selection[0].row() : -1;
  QString headerIcon;
  for (int row = 0; row < d->m_model->rowCount(); ++row) {
    if (d->m_list->isRowHidden(row)) {
      continue;
    }
    const QStandardItem* item = d->m_model->item(row);
    const bool header         = item->data(FluentPanels::HeaderRole).toBool();
    // by instance: the list makes new icons each time it is filled
    const QString icon =
        header ? QString()
               : QuickUi::iconUrl(item->icon(), QStringLiteral("instance/%1")
                                                    .arg(item->data(IndexRole).toInt()));
    if (row == selectedRow) {
      current    = rows.size();
      headerIcon = icon;
    }
    rows.push_back(QVariantMap{
        {QStringLiteral("row"), row},
        {QStringLiteral("header"), header},
        {QStringLiteral("text"), item->text()},
        {QStringLiteral("caption"), item->data(FluentPanels::CaptionRole).toString()},
        {QStringLiteral("tip"), item->toolTip()},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("index"), item->data(IndexRole).toInt()},
        {QStringLiteral("group"), item->data(GroupRole).toString()}});
  }

  // the details card's rows
  QVariantList details;
  auto addRow = [&](const QString& key, const InstanceManagerDialog::DetailRow& r) {
    auto* value = static_cast<FluentPanels::ElidedLabel*>(r.value);
    details.push_back(QVariantMap{
        {QStringLiteral("key"), key},
        {QStringLiteral("glyph"), glyphText(r.glyph)},
        {QStringLiteral("title"), r.title},
        {QStringLiteral("value"), value->fullText()},
        {QStringLiteral("tip"), value->toolTip()},
        {QStringLiteral("critical"),
         value->property("tone").toString() == QLatin1String("critical")},
        {QStringLiteral("action"), r.action != nullptr},
        {QStringLiteral("actionGlyph"), glyphText(r.actionGlyph)},
        {QStringLiteral("actionTip"), r.action ? r.action->toolTip() : QString()},
        {QStringLiteral("actionEnabled"), r.action && r.action->isEnabled()}});
  };
  addRow(QStringLiteral("location"), d->m_locationRow);
  addRow(QStringLiteral("base"), d->m_baseRow);
  addRow(QStringLiteral("game"), d->m_gameRow);
  addRow(QStringLiteral("profiles"), d->m_profilesRow);
  addRow(QStringLiteral("size"), d->m_sizeRow);
  addRow(QStringLiteral("ini"), d->m_iniRow);
  addRow(QStringLiteral("storage"), d->m_storageRow);
  addRow(QStringLiteral("group"), d->m_groupRow);
  addRow(QStringLiteral("notes"), d->m_notesRow);

  // the Nolvus card: its texts and buttons as they are now
  QVariantList nolvus;
  m_nolvusButtons.clear();
  if (d->m_nolvusRows != nullptr) {
    for (int i = 0; i < d->m_nolvusRows->count(); ++i) {
      QWidget* w = d->m_nolvusRows->itemAt(i)->widget();
      if (auto* b = qobject_cast<QAbstractButton*>(w)) {
        QVariantMap entry = buttonState(b);
        entry.insert(QStringLiteral("kind"), QStringLiteral("button"));
        entry.insert(QStringLiteral("id"), m_nolvusButtons.size());
        m_nolvusButtons.push_back(b);
        nolvus.push_back(entry);
      } else if (auto* label = qobject_cast<QLabel*>(w)) {
        nolvus.push_back(QVariantMap{{QStringLiteral("kind"), QStringLiteral("label")},
                                     {QStringLiteral("text"), label->text()},
                                     {QStringLiteral("tip"), label->toolTip()}});
      }
    }
  }

  // the search stays open while it has text or the focus (Ctrl+F opens it)
  const QString filterText = d->m_filter->text();
  if (!filterText.isEmpty()) {
    m_searchOpen = true;
  }

  QVariantMap state{
      {QStringLiteral("title"), InstanceManagerDialog::tr("Instances")},
      {QStringLiteral("count"), d->m_listCount->text()},
      {QStringLiteral("grouping"), d->m_view->text()},
      {QStringLiteral("groupingTip"), d->m_view->toolTip()},
      {QStringLiteral("groupingName"), d->m_view->accessibleName()},
      {QStringLiteral("filterVisible"), shown(d->m_filter) || m_searchOpen || m_searchFocus},
      {QStringLiteral("filterText"), filterText},
      {QStringLiteral("filterPlaceholder"), d->m_filter->placeholderText()},
      {QStringLiteral("hasSelection"), d->m_detailsStack->currentIndex() == 1},
      {QStringLiteral("selection"),
       selectedRow >= 0 ? d->m_model->item(selectedRow)->data(IndexRole).toInt() : -1},
      {QStringLiteral("emptyText"), d->m_emptyText->text()},
      {QStringLiteral("name"), d->m_name->text()},
      {QStringLiteral("caption"), d->m_caption->text()},
      {QStringLiteral("icon"), headerIcon},
      {QStringLiteral("details"), details},
      {QStringLiteral("rename"), buttonState(d->m_rename)},
      {QStringLiteral("duplicate"), buttonState(d->m_duplicate)},
      {QStringLiteral("shortcut"), buttonState(d->m_shortcut)},
      {QStringLiteral("delete"), buttonState(d->m_delete)},
      {QStringLiteral("open"), buttonState(d->m_switch)},
      {QStringLiteral("new"), buttonState(d->m_new)},
      {QStringLiteral("help"), buttonState(button(QStringLiteral("help")))},
      {QStringLiteral("close"), buttonState(button(QStringLiteral("close")))},
      {QStringLiteral("nolvusVisible"), d->m_nolvusCard != nullptr && shown(d->m_nolvusCard)},
      {QStringLiteral("nolvusTitle"), InstanceManagerDialog::tr("Found in Nolvus")},
      {QStringLiteral("nolvus"), nolvus}};

  // only what changed: a new list would rebuild the view's rows
  if (rows != m_rows) {
    m_rows = rows;
    emit rowsChanged();
  }
  if (state != m_state || current != m_currentRow) {
    m_state      = state;
    m_currentRow = current;
    emit stateChanged();
  }
}

QAbstractButton* InstancesQuickBridge::button(const QString& name) const
{
  auto* d = m_dialog;
  if (name == QLatin1String("rename")) {
    return d->m_rename;
  } else if (name == QLatin1String("duplicate")) {
    return d->m_duplicate;
  } else if (name == QLatin1String("shortcut")) {
    return d->m_shortcut;
  } else if (name == QLatin1String("delete")) {
    return d->m_delete;
  } else if (name == QLatin1String("open")) {
    return d->m_switch;
  } else if (name == QLatin1String("new")) {
    return d->m_new;
  } else if (name == QLatin1String("help")) {
    return d->findChild<QToolButton*>(QStringLiteral("instanceHelpButton"));
  } else if (name == QLatin1String("close")) {
    return d->findChild<QPushButton*>(QStringLiteral("close"));
  }
  return nullptr;
}

void InstancesQuickBridge::selectRow(int row)
{
  auto* d = m_dialog;
  if (row < 0 || row >= d->m_model->rowCount() ||
      d->m_model->item(row)->data(FluentPanels::HeaderRole).toBool()) {
    return;
  }
  d->m_list->setCurrentIndex(d->m_model->index(row, 0));
  d->m_list->selectionModel()->select(d->m_model->index(row, 0),
                                      QItemSelectionModel::ClearAndSelect);
}

void InstancesQuickBridge::later(std::function<void()> action)
{
  QMetaObject::invokeMethod(this, std::move(action), Qt::QueuedConnection);
}

void InstancesQuickBridge::activateRow(int row)
{
  selectRow(row);
  later([this] {
    m_dialog->openSelectedInstance();
  });
}

void InstancesQuickBridge::rowMenu(int row, qreal globalX, qreal globalY)
{
  auto* d = m_dialog;
  if (row < 0 || row >= d->m_model->rowCount()) {
    return;
  }
  const QPoint at(qRound(globalX), qRound(globalY));
  later([this, row, at] {
    auto* d = m_dialog;
    if (row < d->m_model->rowCount()) {
      d->showRowMenu(d->m_model->index(row, 0), at);
    }
  });
}

void InstancesQuickBridge::viewMenu(qreal globalX, qreal globalY)
{
  const QPoint at(qRound(globalX), qRound(globalY));
  later([this, at] {
    if (QMenu* menu = m_dialog->m_view->menu()) {
      // its entries are made on aboutToShow, as for the Widgets button
      menu->exec(at);
    }
  });
}

void InstancesQuickBridge::detailAction(const QString& key)
{
  auto* d = m_dialog;
  const InstanceManagerDialog::DetailRow* row =
      key == QLatin1String("location")   ? &d->m_locationRow
      : key == QLatin1String("base")     ? &d->m_baseRow
      : key == QLatin1String("game")     ? &d->m_gameRow
      : key == QLatin1String("profiles") ? &d->m_profilesRow
      : key == QLatin1String("ini")      ? &d->m_iniRow
      : key == QLatin1String("group")    ? &d->m_groupRow
      : key == QLatin1String("notes")    ? &d->m_notesRow
                                         : nullptr;
  if (row != nullptr && row->action != nullptr) {
    // click() does nothing on a disabled button, as on screen
    QPointer<QToolButton> action = row->action;
    later([action] {
      if (action) {
        action->click();
      }
    });
  }
}

void InstancesQuickBridge::press(const QString& name)
{
  QPointer<QAbstractButton> b = button(name);
  if (b && shown(b)) {
    later([b] {
      if (b) {
        b->click();
      }
    });
  }
}

void InstancesQuickBridge::nolvusPress(int id)
{
  // the button shown now: the list can be rebuilt before the click runs
  if (id >= 0 && id < m_nolvusButtons.size() && m_nolvusButtons[id]) {
    QPointer<QAbstractButton> b = m_nolvusButtons[id];
    later([b] {
      if (b) {
        b->click();
      }
    });
  }
}

void InstancesQuickBridge::setFilter(const QString& text)
{
  if (m_dialog->m_filter->text() != text) {
    // applyFilter() runs on textChanged
    m_dialog->m_filter->setText(text);
  }
  if (text.isEmpty()) {
    scheduleSync();
  }
}

void InstancesQuickBridge::setSearchFocus(bool focused)
{
  m_searchFocus = focused;
  scheduleSync();
}

void InstancesQuickBridge::listRefilled()
{
  // as updateList() decides for the Widgets box: a short list keeps its search
  // open only while it has the focus or text
  m_searchOpen = m_searchFocus || !m_dialog->m_filter->text().isEmpty();
  scheduleSync();
}
