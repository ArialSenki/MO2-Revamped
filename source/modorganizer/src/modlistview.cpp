#include "modlistview.h"
#include "fluentstatusdelegate.h"
#include <QApplication>
#include <QHash>
#include <QMimeData>
#include <QCursor>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QProxyStyle>
#include <QSettings>
#include <QUrl>
#include <QVariantAnimation>

#include <widgetutility.h>

#include "filesystemutilities.h"
#include <report.h>

#include "ui_mainwindow.h"

#include "copyeventfilter.h"
#include "filterlist.h"
#include "fluentmotion.h"
#include "genericicondelegate.h"
#include "log.h"
#include "mainwindow.h"
#include "fluenticons.h"
#include "fluentswitch.h"
#include "modconflicticondelegate.h"
#include "modcontenticondelegate.h"
#include "modelutils.h"
#include "modflagicondelegate.h"
#include "modlist.h"
#include "modlistbypriorityproxy.h"
#include "modlistcontextmenu.h"
#include "modlistdropinfo.h"
#include "modlistsortproxy.h"
#include "modlistversiondelegate.h"
#include "modlistviewactions.h"
#include "organizercore.h"
#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"

using namespace MOBase;
using namespace MOShared;

// delegate to remove indentation for mods when using collapsible
// separator
//
// the delegate works by removing the indentation of the child items
// before drawing, but unfortunately this normally breaks event
// handling (e.g. checkbox, edit, etc.), so we also need to override
// the visualRect() function from the mod list view.
//
class ModListStyledItemDelegate : public QStyledItemDelegate
{
  ModListView* m_view;

public:
  ModListStyledItemDelegate(ModListView* view) : QStyledItemDelegate(view), m_view(view)
  {}

  void initStyleOption(QStyleOptionViewItem* option,
                       const QModelIndex& index) const override
  {
    // the parent version always overwrite the background brush, so
    // we need to save it and restore it
    auto backgroundColor = option->backgroundBrush.color();
    QStyledItemDelegate::initStyleOption(option, index);

    if (backgroundColor.isValid()) {
      option->backgroundBrush = backgroundColor;
    }

    if (index.column() == ModList::COL_NAME) {
      alignWithSwitches(option);
    }
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    QStyleOptionViewItem opt(option);

    // remove items indentaiton when using collapsible separators
    opt.rect.adjust(-m_view->removedIndentation(index), 0, 0, 0);

    // compute required color from children, otherwise fallback to the
    // color from the model, and draw the background here
    auto color = m_view->markerColor(index);
    if (!color.isValid()) {
      color = index.data(Qt::BackgroundRole).value<QColor>();
    }
    opt.backgroundBrush = color;

    // we need to find the background color to compute the ideal text color
    // but the mod list view uses alternate color so we need to find the
    // right color
    auto bg = opt.palette.base().color();
    if (opt.features & QStyleOptionViewItem::Alternate) {
      bg = opt.palette.alternateBase().color();
    }

    // compute ideal foreground color for some rows
    if (color.isValid()) {
      if (((index.column() == ModList::COL_NAME ||
            index.column() == ModList::COL_PRIORITY) &&
           ModInfo::getByIndex(index.data(ModList::IndexRole).toInt())
               ->isSeparator()) ||
          index.column() == ModList::COL_NOTES) {

        // combine the color with the background and then find the "ideal" text color
        const auto a = color.alpha() / 255.;
        int r        = (1 - a) * bg.red() + a * color.red(),
            g        = (1 - a) * bg.green() + a * color.green(),
            b        = (1 - a) * bg.blue() + a * color.blue();
        opt.palette.setBrush(QPalette::Text,
                             ColorSettings::idealTextColor(QColor(r, g, b)));
      }
    }

    // Fluent: the priority is a quiet number next to the switch, the name
    // carries the row
    if (index.column() == ModList::COL_PRIORITY && !color.isValid()) {
      QColor muted = opt.palette.color(QPalette::Text);
      muted.setAlphaF(0.6);
      opt.palette.setColor(QPalette::Text, muted);
    }

    // the theme styles every row, and a styled row ignores the option's
    // background: the separator or conflict color is painted here. The
    // theme's hover and selection are opaque, so a colored row brightens its
    // own color instead
    const bool hovered  = opt.state & QStyle::State_MouseOver;
    const bool selected = opt.state & QStyle::State_Selected;
    if (color.isValid()) {
      m_view->paintColorBand(painter, opt.rect, index, color, hovered, selected);
      opt.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected);
    }

    // after the color fill: it stays inside the cell
    opt.rect.adjust(-textShift(index), 0, 0, 0);
    QStyledItemDelegate::paint(painter, opt, index);
    opt.state.setFlag(QStyle::State_MouseOver, hovered);
    paintSwitch(painter, opt, index);
  }

  void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const override
  {
    QStyledItemDelegate::updateEditorGeometry(editor, option, index);

    // centered on the tall row, its text where the name's text was (the
    // editor adds its own padding), and a gap before the column's end
    const int height = std::min(option.rect.height() - 2, 26);
    QRect rect       = editor->geometry();
    rect.setHeight(height);
    rect.moveTop(option.rect.top() + (option.rect.height() - height) / 2);
    rect.setLeft(rect.left() - 6 - textShift(index));
    // clear of the switch
    if (index.column() == ModList::COL_NAME && index.data(Qt::CheckStateRole).isValid()) {
      rect.setLeft(rect.left() + 10);
    }
    rect.setRight(std::min(rect.right(), option.rect.right() - 8));
    editor->setGeometry(rect);
  }

  // a refresh renumbers the mods: the states painted before belong to
  // other mods now, which would animate as if toggled
  void forgetSwitchStates()
  {
    for (auto* animation : std::as_const(m_switchAnimations)) {
      animation->stop();
      animation->deleteLater();
    }
    m_switchAnimations.clear();
    m_paintedStates.clear();
  }

private:
  // keyed by mod index: toggling a mod makes the list refresh its rows, so
  // row indexes would not survive until the next frame
  QHash<int, QVariantAnimation*> m_switchAnimations;

  // the state each switch was last painted in; a mod painted in a new state
  // was toggled (a click, the keyboard, a menu, "enable all"...) and slides
  // over instead of jumping
  mutable QHash<int, bool> m_paintedStates;

  // a name with an icon and no switch (Overwrite) gets the switch's width for
  // its icon, so its text starts where the mods' names start
  static void alignWithSwitches(QStyleOptionViewItem* option)
  {
    if ((option->features & QStyleOptionViewItem::HasCheckIndicator) ||
        !(option->features & QStyleOptionViewItem::HasDecoration)) {
      return;
    }

    const QWidget* widget = option->widget;
    QStyle* style         = widget ? widget->style() : QApplication::style();
    QStyleOptionViewItem withSwitch(*option);
    withSwitch.features.setFlag(QStyleOptionViewItem::HasCheckIndicator, true);
    withSwitch.features.setFlag(QStyleOptionViewItem::HasDecoration, false);
    withSwitch.checkState = Qt::Unchecked;

    const int shift =
        style->subElementRect(QStyle::SE_ItemViewItemText, &withSwitch, widget).left() -
        style->subElementRect(QStyle::SE_ItemViewItemText, option, widget).left();
    if (shift > 0) {
      option->decorationSize.rwidth() += shift;
    }
  }

  // the text columns start where their headings do: the row padding suits the
  // Name column, which keeps the switch after it
  static int textShift(const QModelIndex& index)
  {
    switch (index.column()) {
    case ModList::COL_PRIORITY:
    case ModList::COL_VERSION:
    case ModList::COL_CATEGORY:
    case ModList::COL_GAME:
    case ModList::COL_INSTALLTIME:
    case ModList::COL_NOTES:
      return 4;
    default:
      return 0;
    }
  }

  static int modKey(const QModelIndex& index)
  {
    bool ok       = false;
    const int mod = index.data(ModList::IndexRole).toInt(&ok);
    return ok ? mod : -1;
  }

  // switch position for the mod, 0 is off and 1 is on
  qreal switchPosition(int key, bool checked) const
  {
    auto itor = m_switchAnimations.find(key);
    if (itor != m_switchAnimations.end()) {
      return (*itor)->currentValue().toReal();
    }

    return checked ? 1.0 : 0.0;
  }

  void animateSwitch(int key, bool checked)
  {
    if (!FluentMotion::enabled() || !m_view->switchColors().isValid()) {
      return;
    }

    if (auto* old = m_switchAnimations.take(key)) {
      old->stop();
      old->deleteLater();
    }

    auto* animation = FluentMotion::createAnimation(this);
    m_switchAnimations.insert(key, animation);

    // the switch's column only: cheaper than finding the mod's row again on
    // every frame, and the rest of the list doesn't change
    connect(animation, &QVariantAnimation::valueChanged, this, [this] {
      m_view->viewport()->update(m_view->columnViewportPosition(ModList::COL_NAME), 0,
                                 m_view->columnWidth(ModList::COL_NAME),
                                 m_view->viewport()->height());
    });

    connect(animation, &QVariantAnimation::finished, this, [this, key, animation] {
      m_switchAnimations.remove(key);
      animation->deleteLater();
      m_view->viewport()->update();
    });

    FluentMotion::start(animation, checked ? 0.0 : 1.0, checked ? 1.0 : 0.0, 200);
  }

  // draws the switch over the indicator image from the stylesheet, so the
  // knob can move and the track can fade between the two states
  void paintSwitch(QPainter* painter, const QStyleOptionViewItem& viewOption,
                   const QModelIndex& index) const
  {
    // only the name column has a switch: the rest skip the work below
    if (index.column() != ModList::COL_NAME) {
      return;
    }

    // paint() hands over the view's option; the check state and the
    // indicator only appear once the option is filled in for the index
    QStyleOptionViewItem option(viewOption);
    initStyleOption(&option, index);
    if (!(option.features & QStyleOptionViewItem::HasCheckIndicator)) {
      return;
    }

    const auto colors = m_view->switchColors();
    if (!colors.isValid()) {
      return;
    }

    const QWidget* widget = option.widget;
    QStyle* style         = widget ? widget->style() : QApplication::style();
    const QRectF track =
        style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, widget);
    if (track.isEmpty()) {
      return;
    }

    const bool checked = (option.checkState != Qt::Unchecked);
    const int key      = modKey(index);
    auto painted       = m_paintedStates.find(key);
    if (painted == m_paintedStates.end()) {
      m_paintedStates.insert(key, checked);
    } else if (*painted != checked) {
      *painted = checked;
      if (key >= 0) {
        const_cast<ModListStyledItemDelegate*>(this)->animateSwitch(key, checked);
      }
    }

    const qreal hover = (option.state & QStyle::State_MouseOver) ? 1.0 : 0.0;
    FluentSwitch::paint(painter, track, switchPosition(key, checked), hover,
                        {colors.on, colors.off, colors.border, colors.knobOn,
                         colors.knobOff});
  }
};

class ModListViewMarkingScrollBar : public ViewMarkingScrollBar
{
  ModListView* m_view;

public:
  ModListViewMarkingScrollBar(ModListView* view)
      : ViewMarkingScrollBar(view, ModList::ScrollMarkRole), m_view(view)
  {}

  QColor color(const QModelIndex& index) const override
  {
    auto color = m_view->markerColor(index);
    if (!color.isValid()) {
      color = ViewMarkingScrollBar::color(index);
    }
    return color;
  }
};

ModListView::ModListView(QWidget* parent)
    : QTreeView(parent), m_core(nullptr), m_sortProxy(nullptr),
      m_byPriorityProxy(nullptr), m_byCategoryProxy(nullptr), m_byNexusIdProxy(nullptr),
      m_markers{{}, {}, {}, {}, {}, {}},
      m_scrollbar(new ModListViewMarkingScrollBar(this))
{
  setVerticalScrollBar(m_scrollbar);
  MOBase::setCustomizableColumns(this);
  setAutoExpandDelay(750);

  setItemDelegate(new ModListStyledItemDelegate(this));

  connect(this, &ModListView::doubleClicked, this, &ModListView::onDoubleClicked);
  connect(this, &ModListView::customContextMenuRequested, this,
          &ModListView::onCustomContextMenuRequested);

  // the timeout is pretty small because its main purpose is to avoid
  // refreshing multiple times when calling expandAll() or collapseAll()
  // which emit a lots of expanded/collapsed signals in a very small
  // time window
  m_refreshMarkersTimer.setInterval(50);
  m_refreshMarkersTimer.setSingleShot(true);
  connect(&m_refreshMarkersTimer, &QTimer::timeout, [=] {
    refreshMarkersAndPlugins();
  });

  installEventFilter(new CopyEventFilter(this, [=](auto& index) {
    QVariant mIndex = index.data(ModList::IndexRole);
    QString name    = index.data(Qt::DisplayRole).toString();
    if (mIndex.isValid() && hasCollapsibleSeparators()) {
      ModInfo::Ptr info = ModInfo::getByIndex(mIndex.toInt());
      if (info->isSeparator()) {
        name = "[" + name + "]";
      }
    } else if (model()->hasChildren(index)) {
      name = "[" + name + "]";
    }
    return name;
  }));
}

void ModListView::refresh()
{
  // the colors and options behind the markers may have changed
  clearMarkerColors();
  updateGroupByProxy();
}

void ModListView::onProfileChanged(Profile* oldProfile, Profile* newProfile)
{
  const auto perProfileSeparators =
      m_core->settings().interface().collapsibleSeparatorsPerProfile();

  // save expanded/collapsed state of separators
  if (oldProfile && perProfileSeparators) {
    auto& collapsed = m_collapsed[m_byPriorityProxy];
    oldProfile->storeSetting("UserInterface", "collapsed_separators",
                             QStringList(collapsed.begin(), collapsed.end()));
  }

  m_sortProxy->setProfile(newProfile);
  m_byPriorityProxy->setProfile(newProfile);

  if (newProfile && perProfileSeparators) {
    auto collapsed =
        newProfile->setting("UserInterface", "collapsed_separators", QStringList())
            .toStringList();
    m_collapsed[m_byPriorityProxy] = {collapsed.begin(), collapsed.end()};
  }
}

bool ModListView::hasCollapsibleSeparators() const
{
  return groupByMode() == GroupByMode::SEPARATOR;
}

int ModListView::sortColumn() const
{
  return m_sortProxy ? m_sortProxy->sortColumn() : -1;
}

Qt::SortOrder ModListView::sortOrder() const
{
  return m_sortProxy ? m_sortProxy->sortOrder() : Qt::AscendingOrder;
}

bool ModListView::isFilterActive() const
{
  return m_sortProxy && m_sortProxy->isFilterActive();
}

ModListView::GroupByMode ModListView::groupByMode() const
{
  if (m_sortProxy == nullptr) {
    return GroupByMode::NONE;
  } else if (m_sortProxy->sourceModel() == m_byPriorityProxy) {
    return GroupByMode::SEPARATOR;
  } else if (m_sortProxy->sourceModel() == m_byCategoryProxy) {
    return GroupByMode::CATEGORY;
  } else if (m_sortProxy->sourceModel() == m_byNexusIdProxy) {
    return GroupByMode::NEXUS_ID;
  } else {
    return GroupByMode::NONE;
  }
}

ModListViewActions& ModListView::actions() const
{
  return *m_actions;
}

std::optional<unsigned int> ModListView::nextMod(unsigned int modIndex) const
{
  const QModelIndex start = indexModelToView(m_core->modList()->index(modIndex, 0));

  auto index = start;

  for (;;) {
    index = nextIndex(index);

    if (index == start || !index.isValid()) {
      // wrapped around, give up
      break;
    }

    modIndex = index.data(ModList::IndexRole).toInt();

    ModInfo::Ptr mod = ModInfo::getByIndex(modIndex);

    // skip overwrite, backups and separators
    if (mod->isOverwrite() || mod->isBackup() || mod->isSeparator()) {
      continue;
    }

    return modIndex;
  }

  return {};
}

std::optional<unsigned int> ModListView::prevMod(unsigned int modIndex) const
{
  const QModelIndex start = indexModelToView(m_core->modList()->index(modIndex, 0));

  auto index = start;

  for (;;) {
    index = prevIndex(index);

    if (index == start || !index.isValid()) {
      // wrapped around, give up
      break;
    }

    modIndex = index.data(ModList::IndexRole).toInt();

    // skip overwrite, backups and separators
    ModInfo::Ptr mod = ModInfo::getByIndex(modIndex);
    if (mod->isOverwrite() || mod->isBackup() || mod->isSeparator()) {
      continue;
    }

    return modIndex;
  }

  return {};
}

void ModListView::invalidateFilter()
{
  m_sortProxy->invalidate();
}

void ModListView::setFilterCriteria(
    const std::vector<ModListSortProxy::Criteria>& criteria)
{
  m_sortProxy->setCriteria(criteria);
}

void ModListView::setFilterOptions(ModListSortProxy::FilterMode mode,
                                   ModListSortProxy::SeparatorsMode sep)
{
  m_sortProxy->setOptions(mode, sep);
}

bool ModListView::isModVisible(unsigned int index) const
{
  return m_sortProxy->filterMatchesMod(ModInfo::getByIndex(index),
                                       m_core->currentProfile()->modEnabled(index));
}

bool ModListView::isModVisible(ModInfo::Ptr mod) const
{
  return m_sortProxy->filterMatchesMod(
      mod, m_core->currentProfile()->modEnabled(ModInfo::getIndex(mod->name())));
}

QModelIndex ModListView::indexModelToView(const QModelIndex& index) const
{
  return ::indexModelToView(index, this);
}

QModelIndexList ModListView::indexModelToView(const QModelIndexList& index) const
{
  return ::indexModelToView(index, this);
}

QModelIndex ModListView::indexViewToModel(const QModelIndex& index) const
{
  return ::indexViewToModel(index, m_core->modList());
}

QModelIndexList ModListView::indexViewToModel(const QModelIndexList& index) const
{
  return ::indexViewToModel(index, m_core->modList());
}

QModelIndex ModListView::nextIndex(const QModelIndex& index) const
{
  auto* model = index.model();
  if (!model) {
    return {};
  }

  if (model->rowCount(index) > 0) {
    return model->index(0, index.column(), index);
  }

  if (index.parent().isValid()) {
    if (index.row() + 1 < model->rowCount(index.parent())) {
      return index.model()->index(index.row() + 1, index.column(), index.parent());
    } else {
      return index.model()->index((index.parent().row() + 1) %
                                      model->rowCount(index.parent().parent()),
                                  index.column(), index.parent().parent());
      ;
    }
  } else {
    return index.model()->index((index.row() + 1) % model->rowCount(index.parent()),
                                index.column(), index.parent());
  }
}

QModelIndex ModListView::prevIndex(const QModelIndex& index) const
{
  if (index.row() == 0 && index.parent().isValid()) {
    return index.parent();
  }

  auto* model = index.model();
  if (!model) {
    return {};
  }

  auto prev = model->index((index.row() - 1) % model->rowCount(index.parent()),
                           index.column(), index.parent());

  if (model->rowCount(prev) > 0) {
    return model->index(model->rowCount(prev) - 1, index.column(), prev);
  }

  return prev;
}

std::pair<QModelIndex, QModelIndexList> ModListView::selected() const
{
  return {indexViewToModel(currentIndex()),
          indexViewToModel(selectionModel()->selectedRows())};
}

void ModListView::setSelected(const QModelIndex& current,
                              const QModelIndexList& selected)
{
  setCurrentIndex(indexModelToView(current));
  for (auto idx : selected) {
    selectionModel()->select(indexModelToView(idx),
                             QItemSelectionModel::Select | QItemSelectionModel::Rows);
  }
}

void ModListView::scrollToAndSelect(const QModelIndex& index)
{
  scrollToAndSelect(QModelIndexList{index});
}

void ModListView::scrollToAndSelect(const QModelIndexList& indexes,
                                    const QModelIndex& current)
{
  // focus, scroll to and select
  if (!current.isValid() && indexes.isEmpty()) {
    return;
  }
  scrollTo(current.isValid() ? current : indexes.first());
  setCurrentIndex(current.isValid() ? current : indexes.first());
  QItemSelection selection;
  for (auto& idx : indexes) {
    selection.select(idx, idx);
  }
  selectionModel()->select(selection,
                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
  QTimer::singleShot(50, [=] {
    setFocus();
  });
}

void ModListView::refreshExpandedItems()
{
  auto* model = m_sortProxy->sourceModel();
  for (auto i = 0; i < model->rowCount(); ++i) {
    auto idx = model->index(i, 0);
    if (!m_collapsed[model].contains(idx.data(Qt::DisplayRole).toString())) {
      setExpanded(m_sortProxy->mapFromSource(idx), true);
    }
  }
}

void ModListView::onModPrioritiesChanged(const QModelIndexList& indices)
{
  // expand separator whose priority has changed and parents
  for (auto index : indices) {
    auto idx = indexModelToView(index);
    if (hasCollapsibleSeparators() && model()->hasChildren(idx)) {
      setExpanded(idx, true);
    }
    if (idx.parent().isValid()) {
      setExpanded(idx.parent(), true);
    }
  }

  setOverwriteMarkers(selectionModel()->selectedRows());
}

void ModListView::onModInstalled(const QString& modName)
{
  unsigned int index = ModInfo::getIndex(modName);

  if (index == UINT_MAX) {
    return;
  }

  QModelIndex qIndex = indexModelToView(m_core->modList()->index(index, 0));

  if (hasCollapsibleSeparators() && qIndex.parent().isValid()) {
    setExpanded(qIndex.parent(), true);
  }

  scrollToAndSelect(qIndex);
}

void ModListView::onModFilterActive(bool filterActive)
{
  ui.clearFilters->setVisible(filterActive);

  // Fluent: the theme draws an accent edge around the Mods card while a search,
  // quick filter or category filter hides mods
  for (QWidget* w = parentWidget(); w != nullptr; w = w->parentWidget()) {
    if (w->property("fluentCard").toBool()) {
      if (w->property("filterActive").toBool() != filterActive) {
        w->setProperty("filterActive", filterActive);
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
      }
      break;
    }
  }
}

ModListView::ModCounters ModListView::counters() const
{
  ModCounters c;

  for (unsigned int index = 0; index < ModInfo::getNumMods(); ++index) {
    auto info        = ModInfo::getByIndex(index);

    const bool enabled = m_core->currentProfile()->modEnabled(index);
    const bool visible = m_sortProxy->filterMatchesMod(info, enabled);

    if (info->isBackup()) {
      c.backup++;
      if (visible)
        c.visible.backup++;
    } else if (info->isForeign()) {
      c.foreign++;
      if (visible)
        c.visible.foreign++;
    } else if (info->isSeparator()) {
      c.separator++;
      if (visible)
        c.visible.separator++;
    } else if (!info->isOverwrite()) {
      c.regular++;
      if (visible)
        c.visible.regular++;
      if (enabled) {
        c.active++;
        if (visible)
          c.visible.active++;
      }
    }
  }

  return c;
}

void ModListView::updateModCount()
{
  const auto c = counters();

  ui.counter->display(c.visible.active);
  ui.counter->setDisplayText(
      QStringLiteral("%1/%2").arg(c.visible.active).arg(c.visible.regular));
  ui.counter->setToolTip(tr("<table cellspacing=\"5\">"
                            "<tr><th>Type</th><th>All</th><th>Visible</th>"
                            "<tr><td>Enabled mods:&emsp;</td><td align=right>%1 / "
                            "%2</td><td align=right>%3 / %4</td></tr>"
                            "<tr><td>Unmanaged/DLCs:&emsp;</td><td "
                            "align=right>%5</td><td align=right>%6</td></tr>"
                            "<tr><td>Mod backups:&emsp;</td><td align=right>%7</td><td "
                            "align=right>%8</td></tr>"
                            "<tr><td>Separators:&emsp;</td><td align=right>%9</td><td "
                            "align=right>%10</td></tr>"
                            "</table>")
                             .arg(c.active)
                             .arg(c.regular)
                             .arg(c.visible.active)
                             .arg(c.visible.regular)
                             .arg(c.foreign)
                             .arg(c.visible.foreign)
                             .arg(c.backup)
                             .arg(c.visible.backup)
                             .arg(c.separator)
                             .arg(c.visible.separator));

  emit modCountChanged();
}

void ModListView::setQuickFilter(int specialCategory)
{
  std::vector<ModListSortProxy::Criteria> criteria;
  if (specialCategory >= 0) {
    criteria.push_back({ModListSortProxy::TypeSpecial, specialCategory, false});
  }
  m_filters->select(criteria);
}

void ModListView::refreshFilters()
{
  auto [current, sourceRows] = selected();

  setCurrentIndex(QModelIndex());
  m_filters->refresh();

  setSelected(current, sourceRows);
}

void ModListView::onExternalFolderDropped(const QUrl& url, int priority)
{
  setWindowState(Qt::WindowActive);

  QFileInfo fileInfo(url.toLocalFile());

  GuessedValue<QString> name;
  name.setFilter(&fixDirectoryName);
  name.update(fileInfo.fileName(), GUESS_PRESET);

  do {
    bool ok;
    name.update(
        QInputDialog::getText(this, tr("Copy folder..."),
                              tr("This will copy the content of %1 to a new mod.\n"
                                 "Please enter the name:")
                                  .arg(fileInfo.fileName()),
                              QLineEdit::Normal, name, &ok),
        GUESS_USER);
    if (!ok) {
      return;
    }
  } while (name->isEmpty());

  if (m_core->modList()->getMod(name) != nullptr) {
    reportError(tr("A mod with this name already exists."));
    return;
  }

  IModInterface* newMod = m_core->createMod(name);
  if (!newMod) {
    return;
  }

  // TODO: this is currently a silent copy, which can take some time, but there is
  // no clean method to do this in uibase
  if (!copyDir(fileInfo.absoluteFilePath(), newMod->absolutePath(), true)) {
    return;
  }

  m_core->refresh();

  const auto index = ModInfo::getIndex(name);
  if (priority != -1) {
    m_core->modList()->changeModPriority(index, priority);
  }

  scrollToAndSelect(indexModelToView(m_core->modList()->index(index, 0)));
}

bool ModListView::moveSelection(int key)
{
  auto rows = selectionModel()->selectedRows();
  const QPersistentModelIndex current(key == Qt::Key_Up ? rows.first() : rows.last());

  int offset = key == Qt::Key_Up ? -1 : 1;
  if (m_sortProxy->sortOrder() == Qt::DescendingOrder) {
    offset = -offset;
  }

  m_core->modList()->shiftModsPriority(indexViewToModel(rows), offset);
  selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
  scrollTo(current);

  return true;
}

bool ModListView::removeSelection()
{
  m_actions->removeMods(indexViewToModel(selectionModel()->selectedRows()));
  return true;
}

bool ModListView::toggleSelectionState()
{
  if (!selectionModel()->hasSelection()) {
    return true;
  }
  return m_core->modList()->toggleState(
      indexViewToModel(selectionModel()->selectedRows()));
}

void ModListView::updateGroupByProxy()
{
  int groupIndex      = ui.groupBy->currentIndex();
  auto* previousModel = m_sortProxy->sourceModel();

  QAbstractItemModel* nextModel = m_core->modList();
  if (groupIndex == GroupBy::CATEGORY) {
    nextModel = m_byCategoryProxy;
  } else if (groupIndex == GroupBy::NEXUS_ID) {
    nextModel = m_byNexusIdProxy;
  } else if (m_core->settings().interface().collapsibleSeparators(
                 m_sortProxy->sortOrder()) &&
             m_sortProxy->sortColumn() == ModList::COL_PRIORITY) {
    m_byPriorityProxy->setSortOrder(m_sortProxy->sortOrder());
    nextModel = m_byPriorityProxy;
  }

  if (nextModel != previousModel) {

    if (auto* proxy = dynamic_cast<QAbstractProxyModel*>(nextModel)) {
      proxy->setSourceModel(m_core->modList());
    }
    m_sortProxy->setSourceModel(nextModel);

    // reset the source model of the old proxy because we do not want to
    // react to signals
    //
    if (auto* proxy = qobject_cast<QAbstractProxyModel*>(previousModel)) {
      proxy->setSourceModel(nullptr);
    }

    // expand items previously expanded
    refreshExpandedItems();

    if (hasCollapsibleSeparators()) {
      ui.filterSeparators->setCurrentIndex(ModListSortProxy::SeparatorFilter);
      ui.filterSeparators->setEnabled(false);
    } else {
      ui.filterSeparators->setEnabled(true);
    }
  }

  updateBranchDecoration();
}

void ModListView::updateBranchDecoration()
{
  // a list with nothing to open (no grouping, or collapsible separators but no
  // separator) keeps no branch slot: every row showed an empty one between
  // the priority and the switch
  const QAbstractItemModel* model = m_sortProxy->sourceModel();
  if (model == nullptr) {
    return;
  }

  bool branches = (model != m_core->modList());
  if (model == m_byPriorityProxy) {
    branches = false;
    for (unsigned int i = 0; i < ModInfo::getNumMods() && !branches; ++i) {
      branches = ModInfo::getByIndex(i)->isSeparator();
    }
  } else if (branches) {
    // grouped, but with no group to open
    branches = false;
    for (int i = 0; i < model->rowCount() && !branches; ++i) {
      branches = model->hasChildren(model->index(i, 0));
    }
  }
  setRootIsDecorated(branches);
}

void ModListView::setup(OrganizerCore& core, CategoryFactory& factory, MainWindow* mw,
                        Ui::MainWindow* mwui)
{
  // attributes
  m_core = &core;
  m_filters.reset(new FilterList(mwui, core, factory));
  m_categories = &factory;
  m_actions =
      new ModListViewActions(core, *m_filters, factory, this, mwui->espList, mw);
  ui = {mwui->groupCombo,
        mwui->activeModsCounter,
        mwui->modFilterEdit,
        mwui->currentCategoryLabel,
        mwui->clearFiltersButton,
        mwui->filtersSeparators,
        mwui->espList};

  connect(m_core, &OrganizerCore::modInstalled, [=](auto&& name) {
    onModInstalled(name);
  });
  connect(m_core, &OrganizerCore::profileChanged, this, &ModListView::onProfileChanged);
  connect(core.modList(), &ModList::modPrioritiesChanged, [=](auto&& indices) {
    onModPrioritiesChanged(indices);
  });
  connect(core.modList(), &ModList::clearOverwrite, [=] {
    m_actions->clearOverwrite();
  });
  connect(core.modList(), &ModList::modStatesChanged, [=] {
    updateModCount();
    setOverwriteMarkers(selectionModel()->selectedRows());
  });
  connect(core.modList(), &ModList::modelReset, [=] {
    clearOverwriteMarkers();
    if (auto* delegate = dynamic_cast<ModListStyledItemDelegate*>(itemDelegate())) {
      delegate->forgetSwitchStates();
    }
  });

  // proxy for various group by
  m_byPriorityProxy = new ModListByPriorityProxy(core.currentProfile(), core, this);
  m_byCategoryProxy = new QtGroupingProxy(QModelIndex(), ModList::COL_CATEGORY,
                                          ModList::GroupingRole, 0, ModList::AggrRole);
  m_byNexusIdProxy  = new QtGroupingProxy(
      QModelIndex(), ModList::COL_MODID, ModList::GroupingRole,
      QtGroupingProxy::FLAG_NOGROUPNAME | QtGroupingProxy::FLAG_NOSINGLE,
      ModList::AggrRole);

  // we need to store the expanded/collapsed state of all items and restore them 1) when
  // switching proxies, 2) when filtering and 3) when reseting the mod list.
  connect(this, &QTreeView::expanded, [=](const QModelIndex& index) {
    auto it = m_collapsed[m_sortProxy->sourceModel()].find(
        index.data(Qt::DisplayRole).toString());
    if (it != m_collapsed[m_sortProxy->sourceModel()].end()) {
      m_collapsed[m_sortProxy->sourceModel()].erase(it);
    }
  });
  connect(this, &QTreeView::collapsed, [=](const QModelIndex& index) {
    m_collapsed[m_sortProxy->sourceModel()].insert(
        index.data(Qt::DisplayRole).toString());
  });

  // the top-level proxy
  m_sortProxy = new ModListSortProxy(core.currentProfile(), &core);
  setModel(m_sortProxy);
  connect(m_sortProxy, &ModList::modelReset, [=] {
    // the first separator was created or the last one removed
    updateBranchDecoration();
    refreshExpandedItems();
  });

  // update the proxy when changing the sort column/direction and the group
  connect(m_sortProxy, &QAbstractItemModel::layoutAboutToBeChanged,
          [this](auto&& parents, auto&& hint) {
            if (hint == QAbstractItemModel::VerticalSortHint) {
              updateGroupByProxy();
            }
          });
  connect(ui.groupBy, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [=](int index) {
            updateGroupByProxy();
            onModFilterActive(m_sortProxy->isFilterActive());
          });
  sortByColumn(ModList::COL_PRIORITY, Qt::AscendingOrder);

  // inform the mod list about the type of item being dropped at the beginning of a drag
  // and the position of the drop indicator at the end (only for by-priority)
  connect(this, &ModListView::dragEntered, core.modList(), &ModList::onDragEnter);
  connect(this, &ModListView::dropEntered, m_byPriorityProxy,
          &ModListByPriorityProxy::onDropEnter);

  connect(m_sortProxy, &ModListSortProxy::filterInvalidated, this,
          &ModListView::updateModCount);

  connect(header(), &QHeaderView::sortIndicatorChanged, [=](int, Qt::SortOrder) {
    verticalScrollBar()->update();
  });
  connect(header(), &QHeaderView::sectionResized,
          [=](int logicalIndex, int oldSize, int newSize) {
            m_sortProxy->setColumnVisible(logicalIndex, newSize != 0);
          });

  setItemDelegateForColumn(ModList::COL_FLAGS,
                           new ModFlagIconDelegate(this, ModList::COL_FLAGS, 120));
    setItemDelegateForColumn(ModList::COL_CONFLICTFLAGS, new FluentStatusDelegate(this));

  setItemDelegateForColumn(ModList::COL_CONTENT,
                           new ModContentIconDelegate(this, ModList::COL_CONTENT, 150));
  setItemDelegateForColumn(ModList::COL_VERSION,
                           new ModListVersionDelegate(this, core.settings()));

  if (m_core->settings().geometry().restoreState(header())) {
    // hack: force the resize-signal to be triggered because restoreState doesn't seem
    // to do that
    for (int column = 0; column <= ModList::COL_LASTCOLUMN; ++column) {
      int sectionSize = header()->sectionSize(column);
      header()->resizeSection(column, sectionSize + 1);
      header()->resizeSection(column, sectionSize);
    }
  } else {
    // hide these columns by default
    header()->setSectionHidden(ModList::COL_CONTENT, true);
    header()->setSectionHidden(ModList::COL_MODID, true);
    header()->setSectionHidden(ModList::COL_GAME, true);
    header()->setSectionHidden(ModList::COL_INSTALLTIME, true);
    header()->setSectionHidden(ModList::COL_NOTES, true);

    // resize mod list to fit content
    for (int i = 0; i < header()->count(); ++i) {
      header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }

    header()->setSectionResizeMode(ModList::COL_NAME, QHeaderView::Stretch);
  }

  // prevent the name-column from being hidden
  header()->setSectionHidden(ModList::COL_NAME, false);

  // we need QueuedConnection for the download/archive dropped otherwise the
  // installation starts within the drop-event and it's not possible to drag&drop
  // in the manual installer
  connect(
      m_core->modList(), &ModList::downloadArchiveDropped, this,
      [=](int row, int priority) {
        m_core->installDownload(row, priority);
      },
      Qt::QueuedConnection);
  connect(
      m_core->modList(), &ModList::externalArchiveDropped, this,
      [=](const QUrl& url, int priority) {
        setWindowState(Qt::WindowActive);
        m_core->installArchive(url.toLocalFile(), priority, false, nullptr);
      },
      Qt::QueuedConnection);
  connect(m_core->modList(), &ModList::externalFolderDropped, this,
          &ModListView::onExternalFolderDropped);

  connect(selectionModel(), &QItemSelectionModel::selectionChanged, [=] {
    m_refreshMarkersTimer.start();
  });
  // what a row's marker depends on, besides the markers themselves
  const auto clearColors = [this] {
    clearMarkerColors();
  };
  connect(m_sortProxy, &QAbstractItemModel::modelReset, this, clearColors);
  connect(m_sortProxy, &QAbstractItemModel::layoutChanged, this, clearColors);
  connect(m_sortProxy, &QAbstractItemModel::rowsInserted, this, clearColors);
  connect(m_sortProxy, &QAbstractItemModel::rowsRemoved, this, clearColors);
  connect(m_sortProxy, &QAbstractItemModel::rowsMoved, this, clearColors);
  connect(this, &QTreeView::collapsed, this, clearColors);
  connect(this, &QTreeView::expanded, this, clearColors);

  connect(this, &QTreeView::collapsed, [=] {
    m_refreshMarkersTimer.start();
  });
  connect(this, &QTreeView::expanded, [=] {
    m_refreshMarkersTimer.start();
  });

  // filters
  connect(m_sortProxy, &ModListSortProxy::filterActive, this,
          &ModListView::onModFilterActive);
  connect(m_filters.get(), &FilterList::criteriaChanged, [=](auto&& v) {
    onFiltersCriteria(v);

    int quick = v.empty() ? -1 : -2;
    if (v.size() == 1 && v[0].type == ModListSortProxy::TypeSpecial && !v[0].inverse) {
      quick = v[0].id;
    }
    emit quickFilterChanged(quick);
  });
  connect(m_filters.get(), &FilterList::optionsChanged, [=](auto&& mode, auto&& sep) {
    setFilterOptions(mode, sep);
  });
  connect(ui.filter, &QLineEdit::textChanged, m_sortProxy,
          &ModListSortProxy::updateFilter);
  connect(ui.clearFilters, &QPushButton::clicked, [=]() {
    ui.filter->clear();
    m_filters->clearSelection();
  });
  connect(m_sortProxy, &ModListSortProxy::filterInvalidated, [=]() {
    if (hasCollapsibleSeparators()) {
      refreshExpandedItems();
    }
  });
}

void ModListView::restoreState(const Settings& s)
{
  s.geometry().restoreState(header());

  // Fluent v4 table, applied once over the saved layout: # · name · flags ·
  // status · version. Every other column stays one right-click away in the
  // header; a layout saved after this first run is never touched again.
  QSettings layout(s.filename(), QSettings::IniFormat);
  constexpr auto columnsAppliedKey = "Settings/fluent_v4_mod_columns_applied";
  if (!layout.value(columnsAppliedKey, false).toBool()) {
    QHeaderView* h = header();
    for (int column : {ModList::COL_CONTENT, ModList::COL_CATEGORY, ModList::COL_MODID,
                       ModList::COL_GAME, ModList::COL_INSTALLTIME, ModList::COL_NOTES}) {
      h->setSectionHidden(column, true);
    }
    const int order[] = {ModList::COL_PRIORITY, ModList::COL_NAME, ModList::COL_FLAGS,
                         ModList::COL_CONFLICTFLAGS, ModList::COL_VERSION};
    for (int position = 0; position < static_cast<int>(std::size(order)); ++position) {
      h->setSectionHidden(order[position], false);
      h->moveSection(h->visualIndex(order[position]), position);
    }
    for (int column = 0; column < h->count(); ++column) {
      h->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    h->resizeSection(ModList::COL_PRIORITY, 52);
    h->resizeSection(ModList::COL_FLAGS, 72);
    h->resizeSection(ModList::COL_CONFLICTFLAGS, 170);
    h->resizeSection(ModList::COL_VERSION, 140);
    h->setSectionResizeMode(ModList::COL_NAME, QHeaderView::Stretch);

    layout.setValue(columnsAppliedKey, true);
    layout.sync();
  }

  // the first Fluent layouts hid Flags (invalid game data, hidden files, notes);
  // show it once to them too, after that hiding it is the user's choice
  constexpr auto flagsShownKey = "Settings/fluent_v4_mod_flags_shown";
  if (!layout.value(flagsShownKey, false).toBool()) {
    QHeaderView* h = header();
    if (h->isSectionHidden(ModList::COL_FLAGS)) {
      h->setSectionHidden(ModList::COL_FLAGS, false);
      h->moveSection(h->visualIndex(ModList::COL_FLAGS),
                     h->visualIndex(ModList::COL_NAME) + 1);
      h->resizeSection(ModList::COL_FLAGS, 72);
    }
    layout.setValue(flagsShownKey, true);
    layout.sync();
  }

  s.widgets().restoreIndex(ui.groupBy);
  s.widgets().restoreTreeExpandState(this);

  m_filters->restoreState(s);
}

void ModListView::saveState(Settings& s) const
{
  s.geometry().saveState(header());

  s.widgets().saveIndex(ui.groupBy);
  s.widgets().saveTreeExpandState(this);

  m_filters->saveState(s);
}

QRect ModListView::visualRect(const QModelIndex& index) const
{
  // this shift the visualRect() from QTreeView to match the new actual
  // zone after removing indentation (see the ModListStyledItemDelegate)
  QRect rect = QTreeView::visualRect(index);
  rect.adjust(-removedIndentation(index), 0, 0, 0);
  return rect;
}

int ModListView::removedIndentation(const QModelIndex& index) const
{
  if (!index.isValid() || index.column() != 0 || !hasCollapsibleSeparators()) {
    return 0;
  }
  const QVariant mod = index.data(ModList::IndexRole);
  if (!mod.isValid() || ModInfo::getByIndex(mod.toInt())->isSeparator()) {
    return 0;
  }
  // the mods stay where they are without separators: only a separator keeps
  // the gutter, for its arrow
  return indentation() * (index.parent().isValid() ? 2 : 1);
}

void ModListView::paintColorBand(QPainter* painter, const QRect& cell,
                                 const QModelIndex& index, const QColor& color,
                                 bool hovered, bool selected) const
{
  // a separator's color is what it says, so it stays: an outline shows the state
  const QVariant mod = index.data(ModList::IndexRole);
  const bool outlined =
      mod.isValid() && ModInfo::getByIndex(mod.toInt())->isSeparator();
  const QColor fill = outlined || !(hovered || selected)
                          ? color
                          : color.lighter(selected ? SelectedLighter : HoverLighter);

  // one rounded shape across the row; each cell paints its slice of it
  const QRect row = colorBand(
      QRect(2 - horizontalOffset(), cell.top(), header()->length() - 4, cell.height()));
  painter->save();
  painter->setClipRect(cell, Qt::IntersectClip);
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setPen(Qt::NoPen);
  painter->setBrush(fill);
  painter->drawRoundedRect(row, 5, 5);

  if (outlined && (hovered || selected)) {
    QColor ink = FluentIcons::color(FluentIcons::Tone::Ink);
    ink.setAlpha(selected ? 255 : 130);
    const qreal width = selected ? 2.0 : 1.0;
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(ink, width));
    painter->drawRoundedRect(
        QRectF(row).adjusted(width / 2, width / 2, -width / 2, -width / 2), 5, 5);
  }
  painter->restore();
}

void ModListView::drawBranches(QPainter* painter, const QRect& rect,
                               const QModelIndex& index) const
{
  // the branches are the small indicator left to the row (there are none in the default
  // style, and the VS dark style only has background for these)
  //
  // the branches are not shifted left with the visualRect() change and since MO2 uses
  // stylesheet, it is not possible to shift those in the proxy style so we have to
  // shift it here.
  //
  QRect r(rect);
  if (hasCollapsibleSeparators() && index.parent().isValid()) {
    r.adjust(-indentation(), 0, 0 - indentation(), 0);
  }

  // the gap before the name of a colored separator or marked mod; the
  // delegate only paints the cells
  QColor color = markerColor(index);
  if (!color.isValid()) {
    color = index.data(Qt::BackgroundRole).value<QColor>();
  }
  if (color.isValid()) {
    const int cursor = viewport()->mapFromGlobal(QCursor::pos()).y();
    const bool hovered =
        viewport()->underMouse() && cursor >= rect.top() && cursor <= rect.bottom();
    const bool selected = selectionModel()->isSelected(index);
    if (hovered || selected) {
      // the view has already filled the whole gutter with the row's hover or
      // selection: above and below the band the row stays as it was
      const int row = (rect.top() + verticalOffset()) / std::max(rect.height(), 1);
      painter->fillRect(rect, alternatingRowColors() && (row & 1) != 0
                                  ? palette().alternateBase()
                                  : palette().base());
    }
    paintColorBand(painter, rect, index, color, hovered, selected);
  }
  QTreeView::drawBranches(painter, r, index);
}

void ModListView::commitData(QWidget* editor)
{
  // maintain the selection when changing priority
  if (currentIndex().column() == ModList::COL_PRIORITY) {
    auto [current, selected] = this->selected();
    QTreeView::commitData(editor);
    setSelected(current, selected);
  } else {
    QTreeView::commitData(editor);
  }
}

QModelIndexList ModListView::selectedIndexes() const
{
  // during drag&drop events, we fake the return value of selectedIndexes()
  // to allow drag&drop of a parent into its children
  //
  // this is only "active" during the actual dragXXXEvent and dropEvent method,
  // not during the whole drag&drop event
  //
  // selectedIndexes() is a protected method from QTreeView which is little
  // used so this should not break anything
  //
  return m_inDragMoveEvent ? QModelIndexList() : QTreeView::selectedIndexes();
}

void ModListView::onCustomContextMenuRequested(const QPoint& pos)
{
  try {
    QModelIndex contextIdx = indexViewToModel(indexAt(pos));

    if (!contextIdx.isValid()) {
      // no selection
      ModListGlobalContextMenu(*m_core, this).exec(viewport()->mapToGlobal(pos));
    } else {
      ModListContextMenu(contextIdx, *m_core, m_categories, this)
          .exec(viewport()->mapToGlobal(pos));
    }
  } catch (const std::exception& e) {
    reportError(tr("Exception: %1").arg(e.what()));
  } catch (...) {
    reportError(tr("Unknown exception"));
  }
}

void ModListView::onDoubleClicked(const QModelIndex& index)
{
  if (!index.isValid()) {
    return;
  }

  if (m_core->modList()->timeElapsedSinceLastChecked() <=
      QApplication::doubleClickInterval()) {
    // don't interpret double click if we only just checked a mod
    return;
  }

  bool indexOk = false;
  int modIndex = index.data(ModList::IndexRole).toInt(&indexOk);

  if (!indexOk || modIndex < 0 || modIndex >= ModInfo::getNumMods()) {
    return;
  }

  ModInfo::Ptr modInfo = ModInfo::getByIndex(modIndex);

  const auto modifiers = QApplication::queryKeyboardModifiers();
  if (modifiers.testFlag(Qt::ControlModifier)) {
    try {
      shell::Explore(modInfo->absolutePath());
    } catch (const std::exception& e) {
      reportError(e.what());
    }
  } else if (modifiers.testFlag(Qt::ShiftModifier)) {
    try {
      actions().visitNexusOrWebPage({indexViewToModel(index)});
    } catch (const std::exception& e) {
      reportError(e.what());
    }
  } else if (hasCollapsibleSeparators() && modInfo->isSeparator()) {
    // only the first column knows whether the row is expanded
    const QModelIndex first = index.siblingAtColumn(0);
    setExpanded(first, !isExpanded(first));
  } else {
    try {
      auto tab = ModInfoTabIDs::None;

      switch (index.column()) {
      case ModList::COL_NOTES:
        tab = ModInfoTabIDs::Notes;
        break;
      case ModList::COL_VERSION:
        tab = ModInfoTabIDs::Nexus;
        break;
      case ModList::COL_MODID:
        tab = ModInfoTabIDs::Nexus;
        break;
      case ModList::COL_GAME:
        tab = ModInfoTabIDs::Nexus;
        break;
      case ModList::COL_CATEGORY:
        tab = ModInfoTabIDs::Categories;
        break;
      case ModList::COL_CONFLICTFLAGS:
        tab = ModInfoTabIDs::Conflicts;
        break;
      }

      actions().displayModInformation(modIndex, tab);
    } catch (const std::exception& e) {
      reportError(e.what());
    }
  }

  // workaround to cancel the editor that might have opened because of
  // selection-click
  closePersistentEditor(index);
}

void ModListView::clearOverwriteMarkers()
{
  m_markers.overwrite.clear();
  m_markers.overwritten.clear();
  m_markers.archiveOverwrite.clear();
  m_markers.archiveOverwritten.clear();
  m_markers.archiveLooseOverwrite.clear();
  m_markers.archiveLooseOverwritten.clear();
  clearMarkerColors();
}

void ModListView::setOverwriteMarkers(const QModelIndexList& indexes)
{
  const auto insert = [](auto& dest, const auto& from) {
    dest.insert(from.begin(), from.end());
  };
  clearOverwriteMarkers();
  for (auto& idx : indexes) {
    auto mIndex = idx.data(ModList::IndexRole);
    if (mIndex.isValid()) {
      auto info = ModInfo::getByIndex(mIndex.toInt());
      insert(m_markers.overwrite, info->getModOverwrite());
      insert(m_markers.overwritten, info->getModOverwritten());
      insert(m_markers.archiveOverwrite, info->getModArchiveOverwrite());
      insert(m_markers.archiveOverwritten, info->getModArchiveOverwritten());
      insert(m_markers.archiveLooseOverwrite, info->getModArchiveLooseOverwrite());
      insert(m_markers.archiveLooseOverwritten, info->getModArchiveLooseOverwritten());
    }
  }
  viewport()->update();
  clearMarkerColors();
}

void ModListView::refreshMarkersAndPlugins()
{
  QModelIndexList indexes = selectionModel()->selectedRows();

  if (m_core->settings().interface().collapsibleSeparatorsHighlightFrom()) {
    for (auto& idx : selectionModel()->selectedRows()) {
      if (hasCollapsibleSeparators() && model()->hasChildren(idx) && !isExpanded(idx)) {
        for (int i = 0; i < model()->rowCount(idx); ++i) {
          indexes.append(model()->index(i, idx.column(), idx));
        }
      }
    }
  }

  setOverwriteMarkers(indexes);

  // highligth plugins
  std::vector<unsigned int> modIndices;
  for (auto& idx : indexes) {
    modIndices.push_back(idx.data(ModList::IndexRole).toInt());
  }
  m_core->pluginList()->highlightPlugins(modIndices, m_core->directoryStructure());
  // highlighted plugins are its marks too
  if (auto* bar = dynamic_cast<ViewMarkingScrollBar*>(ui.pluginList->verticalScrollBar())) {
    bar->invalidateMarks();
  } else {
    ui.pluginList->verticalScrollBar()->update();
  }
}

void ModListView::setHighlightedMods(const std::vector<unsigned int>& pluginIndices)
{
  m_markers.highlight.clear();
  auto& directoryEntry = *m_core->directoryStructure();
  for (auto idx : pluginIndices) {
    QString pluginName = m_core->pluginList()->getName(idx);

    const MOShared::FileEntryPtr fileEntry =
        directoryEntry.findFile(pluginName.toStdWString());
    if (fileEntry.get() != nullptr) {
      QString originName = QString::fromStdWString(
          directoryEntry.getOriginByID(fileEntry->getOrigin()).getName());
      const auto index = ModInfo::getIndex(originName);
      if (index != UINT_MAX) {
        m_markers.highlight.insert(index);
      }
    }
  }
  viewport()->update();
  clearMarkerColors();
}

void ModListView::clearMarkerColors() const
{
  m_markerColors.clear();
  m_scrollbar->invalidateMarks();
}

QColor ModListView::markerColor(const QModelIndex& index) const
{
  // the same for every cell of a row, and each row paints many
  const QModelIndex rowIndex = index.sibling(index.row(), 0);
  auto cached                = m_markerColors.constFind(rowIndex);
  if (cached != m_markerColors.constEnd()) {
    return *cached;
  }

  const QColor color = computeMarkerColor(rowIndex);
  m_markerColors.insert(rowIndex, color);
  return color;
}

QColor ModListView::computeMarkerColor(const QModelIndex& index) const
{
  // a group heading (by category or Nexus ID) is no mod: without the check it
  // would light up with the first mod's conflict marker
  const QVariant modData = index.data(ModList::IndexRole);
  if (!modData.isValid()) {
    return {};
  }
  unsigned int modIndex = modData.toInt();
  bool highligth = m_markers.highlight.find(modIndex) != m_markers.highlight.end();
  bool overwrite = m_markers.overwrite.find(modIndex) != m_markers.overwrite.end();
  bool archiveOverwrite =
      m_markers.archiveOverwrite.find(modIndex) != m_markers.archiveOverwrite.end();
  bool archiveLooseOverwrite = m_markers.archiveLooseOverwrite.find(modIndex) !=
                               m_markers.archiveLooseOverwrite.end();
  bool overwritten =
      m_markers.overwritten.find(modIndex) != m_markers.overwritten.end();
  bool archiveOverwritten =
      m_markers.archiveOverwritten.find(modIndex) != m_markers.archiveOverwritten.end();
  bool archiveLooseOverwritten = m_markers.archiveLooseOverwritten.find(modIndex) !=
                                 m_markers.archiveLooseOverwritten.end();

  if (highligth) {
    return Settings::instance().colors().modlistContainsPlugin();
  } else if (overwritten || archiveLooseOverwritten) {
    return Settings::instance().colors().modlistOverwritingLoose();
  } else if (overwrite || archiveLooseOverwrite) {
    return Settings::instance().colors().modlistOverwrittenLoose();
  } else if (archiveOverwritten) {
    return Settings::instance().colors().modlistOverwritingArchive();
  } else if (archiveOverwrite) {
    return Settings::instance().colors().modlistOverwrittenArchive();
  }

  // collapsed separator
  auto rowIndex = index.sibling(index.row(), 0);
  if (model()->hasChildren(rowIndex) && !isExpanded(rowIndex) &&
      hasCollapsibleSeparators() &&
      m_core->settings().interface().collapsibleSeparatorsHighlightTo()) {

    std::vector<QColor> colors;
    for (int i = 0; i < model()->rowCount(rowIndex); ++i) {
      auto childColor = markerColor(model()->index(i, index.column(), rowIndex));
      if (childColor.isValid()) {
        colors.push_back(childColor);
      }
    }

    if (colors.empty()) {
      return QColor();
    }

    int r = 0, g = 0, b = 0, a = 0;
    for (auto& color : colors) {
      r += color.red();
      g += color.green();
      b += color.blue();
      a += color.alpha();
    }

    return QColor(r / colors.size(), g / colors.size(), b / colors.size(),
                  a / colors.size());
  }

  return QColor();
}

std::vector<ModInfo::EFlag> ModListView::modFlags(const QModelIndex& index,
                                                  bool* forceCompact) const
{
  // group headings have no mod
  if (!index.data(ModList::IndexRole).isValid()) {
    if (forceCompact) {
      *forceCompact = true;
    }
    return {};
  }
  ModInfo::Ptr info = ModInfo::getByIndex(index.data(ModList::IndexRole).toInt());

  auto flags   = info->getFlags();
  bool compact = false;
  if (info->isSeparator() && hasCollapsibleSeparators() &&
      m_core->settings().interface().collapsibleSeparatorsIcons(ModList::COL_FLAGS) &&
      !isExpanded(index.sibling(index.row(), 0))) {

    // combine the child conflicts
    std::set eFlags(flags.begin(), flags.end());
    for (int i = 0; i < model()->rowCount(index); ++i) {
      auto cIndex =
          model()->index(i, index.column(), index).data(ModList::IndexRole).toInt();
      auto cFlags = ModInfo::getByIndex(cIndex)->getFlags();
      eFlags.insert(cFlags.begin(), cFlags.end());
    }
    flags = {eFlags.begin(), eFlags.end()};

    // force compact because there can be a lots of flags here
    compact = true;
  }

  if (forceCompact) {
    *forceCompact = true;
  }

  return flags;
}

std::vector<ModInfo::EConflictFlag> ModListView::conflictFlags(const QModelIndex& index,
                                                               bool* forceCompact) const
{
  // group headings have no mod
  if (!index.data(ModList::IndexRole).isValid()) {
    if (forceCompact) {
      *forceCompact = true;
    }
    return {};
  }
  ModInfo::Ptr info = ModInfo::getByIndex(index.data(ModList::IndexRole).toInt());

  auto flags   = info->getConflictFlags();
  bool compact = false;
  if (info->isSeparator() && hasCollapsibleSeparators() &&
      m_core->settings().interface().collapsibleSeparatorsIcons(
          ModList::COL_CONFLICTFLAGS) &&
      !isExpanded(index.sibling(index.row(), 0))) {

    // combine the child conflicts
    std::set<ModInfo::EConflictFlag> eFlags(flags.begin(), flags.end());
    for (int i = 0; i < model()->rowCount(index); ++i) {
      auto cIndex =
          model()->index(i, index.column(), index).data(ModList::IndexRole).toInt();
      auto cFlags = ModInfo::getByIndex(cIndex)->getConflictFlags();
      eFlags.insert(cFlags.begin(), cFlags.end());
    }
    flags = {eFlags.begin(), eFlags.end()};

    // force compact because there can be a lots of flags here
    compact = true;
  }

  if (forceCompact) {
    *forceCompact = true;
  }

  return flags;
}

std::set<int> ModListView::contents(const QModelIndex& index,
                                    bool* includeChildren) const
{
  auto modIndex = index.data(ModList::IndexRole);
  if (!modIndex.isValid()) {
    return {};
  }
  ModInfo::Ptr info = ModInfo::getByIndex(index.data(ModList::IndexRole).toInt());
  auto contents     = info->getContents();
  bool children     = false;

  if (info->isSeparator() && hasCollapsibleSeparators() &&
      m_core->settings().interface().collapsibleSeparatorsIcons(ModList::COL_CONTENT) &&
      !isExpanded(index.sibling(index.row(), 0))) {

    // combine the child contents
    std::set eContents(contents.begin(), contents.end());
    for (int i = 0; i < model()->rowCount(index); ++i) {
      auto cIndex =
          model()->index(i, index.column(), index).data(ModList::IndexRole).toInt();
      auto cContents = ModInfo::getByIndex(cIndex)->getContents();
      eContents.insert(cContents.begin(), cContents.end());
    }
    contents = {eContents.begin(), eContents.end()};
    children = true;
  }

  if (includeChildren) {
    *includeChildren = children;
  }

  return contents;
}

QList<QString> ModListView::contentsIcons(const QModelIndex& index,
                                          bool* forceCompact) const
{
  auto contents = this->contents(index, forceCompact);
  QList<QString> result;
  m_core->modDataContents().forEachContentInOrOut(
      contents,
      [&result](auto const& content) {
        result.append(content.icon());
      },
      [&result](auto const&) {
        result.append(QString());
      });
  return result;
}

QString ModListView::contentsTooltip(const QModelIndex& index) const
{
  auto contents = this->contents(index, nullptr);
  if (contents.empty()) {
    return {};
  }
  QString result("<table cellspacing=7>");
  m_core->modDataContents().forEachContentIn(contents, [&result](auto const& content) {
    result.append(QString("<tr><td><img src=\"%1\" width=32/></td>"
                          "<td valign=\"middle\">%2</td></tr>")
                      .arg(content.icon())
                      .arg(content.name()));
  });
  result.append("</table>");
  return result;
}

void ModListView::onFiltersCriteria(
    const std::vector<ModListSortProxy::Criteria>& criteria)
{
  setFilterCriteria(criteria);

  // each filter by name, saying which ones exclude
  QStringList names;
  for (const auto& c : criteria) {
    QString name;
    if (c.type == ModListSortProxy::TypeContent) {
      const auto* content = m_core->modDataContents().findById(c.id);
      name                = content ? content->name() : QString();
    } else {
      name = m_categories->getCategoryNameByID(c.id);
    }

    if (name.isEmpty()) {
      log::error("category {}:{} not found", c.type, c.id);
      continue;
    }
    names.push_back(c.inverse ? tr("Not %1").arg(name) : name);
  }

  // several can be long: the whole text is in the tooltip
  constexpr int LabelWidth = 360;
  const QString label      = names.join(QStringLiteral(", "));
  ui.currentCategory->setText(
      ui.currentCategory->fontMetrics().elidedText(label, Qt::ElideRight, LabelWidth));
  ui.currentCategory->setToolTip(label);
}

void ModListView::dragEnterEvent(QDragEnterEvent* event)
{
  // this event is used by the modlist to check if we are draggin
  // to a mod (local files) or to a priority (mods, downloads, external
  // files)
  emit dragEntered(event->mimeData());
  QTreeView::dragEnterEvent(event);

  // there is no drop event for invalid data since canDropMimeData
  // returns false, so we notify user on drag enter
  ModListDropInfo dropInfo(event->mimeData(), *m_core);

  if (dropInfo.isValid() && !dropInfo.isLocalFileDrop() &&
      sortColumn() != ModList::COL_PRIORITY) {
    log::warn("Drag&Drop is only supported when sorting by priority.");
  }
}

void ModListView::dragMoveEvent(QDragMoveEvent* event)
{
  // this replace the openTimer from QTreeView to prevent
  // auto-collapse of items
  if (autoExpandDelay() >= 0) {
    m_openTimer.start(autoExpandDelay(), this);
  }

  // see selectedIndexes()
  m_inDragMoveEvent = true;
  QAbstractItemView::dragMoveEvent(event);
  m_inDragMoveEvent = false;
}

void ModListView::dropEvent(QDropEvent* event)
{
  // from Qt source
  QModelIndex index;
  if (viewport()->rect().contains(event->pos())) {
    index = indexAt(event->pos());
    if (!index.isValid() || !visualRect(index).contains(event->pos()))
      index = QModelIndex();
  }

  // this event is used by the byPriorityProxy to know if allow
  // dropping mod between a separator and its first mod (there
  // is no way to deduce this except using dropIndicatorPosition())
  emit dropEntered(event->mimeData(), isExpanded(index),
                   static_cast<DropPosition>(dropIndicatorPosition()));

  // see selectedIndexes()
  m_inDragMoveEvent = true;
  QTreeView::dropEvent(event);
  m_inDragMoveEvent = false;
}

void ModListView::timerEvent(QTimerEvent* event)
{
  // prevent auto-collapse, see dragMoveEvent()
  if (event->timerId() == m_openTimer.timerId()) {
    QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    if (state() == QAbstractItemView::DraggingState &&
        viewport()->rect().contains(pos)) {
      QModelIndex index = indexAt(pos);
      setExpanded(index, !m_core->settings().interface().autoCollapseOnHover() ||
                             !isExpanded(index));
    }
    m_openTimer.stop();
  } else {
    QTreeView::timerEvent(event);
  }
}

void ModListView::mousePressEvent(QMouseEvent* event)
{
  // allow alt+click to select all mods inside a separator
  // when using collapsible separators
  //
  // similar code is also present in mouseReleaseEvent to
  // avoid missing events

  // disable edit if Alt is pressed
  auto triggers = editTriggers();
  if (event->modifiers() & Qt::AltModifier) {
    setEditTriggers(NoEditTriggers);
  }

  // we call the parent class first so that we can use the actual
  // selection state of the item after
  QTreeView::mousePressEvent(event);

  // restore triggers
  setEditTriggers(triggers);

  const auto index = indexAt(event->pos());

  if (event->isAccepted() && hasCollapsibleSeparators() && index.isValid() &&
      model()->hasChildren(indexAt(event->pos())) &&
      (event->modifiers() & Qt::AltModifier)) {

    const auto flag = selectionModel()->isSelected(index)
                          ? QItemSelectionModel::Select
                          : QItemSelectionModel::Deselect;
    const QItemSelection selection(
        model()->index(0, index.column(), index),
        model()->index(model()->rowCount(index) - 1, index.column(), index));
    selectionModel()->select(selection, flag | QItemSelectionModel::Rows);
  }
}

void ModListView::mouseReleaseEvent(QMouseEvent* event)
{
  // this is a duplicate of mousePressEvent because for some reason
  // the selection is not always triggered in mousePressEvent and only
  // doing it here create a small lag between the selection of the
  // separator and the children

  // disable edit if Alt is pressed
  auto triggers = editTriggers();
  if (event->modifiers() & Qt::AltModifier) {
    setEditTriggers(NoEditTriggers);
  }

  // we call the parent class first so that we can use the actual
  // selection state of the item after
  QTreeView::mouseReleaseEvent(event);

  const auto index = indexAt(event->pos());

  // restore triggers
  setEditTriggers(triggers);

  if (event->isAccepted() && hasCollapsibleSeparators() && index.isValid() &&
      model()->hasChildren(indexAt(event->pos())) &&
      (event->modifiers() & Qt::AltModifier)) {

    const auto flag = selectionModel()->isSelected(index)
                          ? QItemSelectionModel::Select
                          : QItemSelectionModel::Deselect;
    const QItemSelection selection(
        model()->index(0, index.column(), index),
        model()->index(model()->rowCount(index) - 1, index.column(), index));
    selectionModel()->select(selection, flag | QItemSelectionModel::Rows);
  }
}

bool ModListView::event(QEvent* event)
{
  if (event->type() == QEvent::KeyPress && m_core->currentProfile() &&
      selectionModel()->hasSelection()) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

    auto index = selectionModel()->currentIndex();

    if (keyEvent->modifiers() == Qt::ControlModifier) {
      // ctrl+enter open explorer
      if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        if (selectionModel()->selectedRows().count() == 1) {
          m_actions->openExplorer({indexViewToModel(index)});
          return true;
        }
      }
      // ctrl+up/down move selection
      else if (sortColumn() == ModList::COL_PRIORITY &&
               (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)) {
        return moveSelection(keyEvent->key());
      }
    } else if (keyEvent->modifiers() == Qt::ShiftModifier) {
      // shift+enter expand
      if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
          selectionModel()->selectedRows().count() == 1) {
        const QModelIndex first = index.siblingAtColumn(0);
        if (model()->hasChildren(first)) {
          setExpanded(first, !isExpanded(first));
        } else if (index.parent().isValid()) {
          setExpanded(index.parent(), false);
          selectionModel()->select(index.parent(), QItemSelectionModel::SelectCurrent |
                                                       QItemSelectionModel::Rows);
          setCurrentIndex(index.parent());
        }
      }
    } else {
      if (keyEvent->key() == Qt::Key_Delete) {
        return removeSelection();
      } else if (keyEvent->key() == Qt::Key_Space) {
        return toggleSelectionState();
      }
    }
    return QTreeView::event(event);
  }
  return QTreeView::event(event);
}
