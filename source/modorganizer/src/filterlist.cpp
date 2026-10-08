#include "filterlist.h"
#include "categories.h"
#include "categoriesdialog.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "settings.h"
#include "ui_mainwindow.h"
#include <QHeaderView>
#include <utility.h>

using namespace MOBase;
using CriteriaType = ModListSortProxy::CriteriaType;
using Criteria     = ModListSortProxy::Criteria;

class FilterList::CriteriaItem : public QTreeWidgetItem
{

  static constexpr int IDRole   = Qt::UserRole;
  static constexpr int TypeRole = Qt::UserRole + 1;

public:
  static constexpr int StateRole = Qt::UserRole + 2;

  enum States
  {
    FirstState = 0,

    Inactive = FirstState,
    Active,
    Inverted,

    LastState = Inverted
  };

  CriteriaItem(FilterList* list, QString name, CriteriaType type, int id)
      : QTreeWidgetItem({"", name}), m_list(list), m_state(Inactive)
  {
    setTextAlignment(0, Qt::AlignCenter);
    setData(0, Qt::ToolTipRole,
            QStringLiteral("%1\n%2")
                .arg(name,
                     QObject::tr("Left-click cycles through off, include, and exclude. "
                                 "Right-click reverses; Space advances.")));
    setData(0, TypeRole, type);
    setData(0, IDRole, id);
    setData(0, Qt::DecorationRole, QIcon(":/MO/gui/unchecked-checkbox"));
  }

  CriteriaType type() const
  {
    return static_cast<CriteriaType>(data(0, TypeRole).toInt());
  }

  int id() const { return data(0, IDRole).toInt(); }

  States state() const { return m_state; }

  void setState(States s)
  {
    if (m_state != s) {
      m_state = s;
      updateState();
    }
  }

  void nextState()
  {
    auto s = static_cast<States>(m_state + 1);
    if (s > LastState) {
      s = FirstState;
    }
    setState(s);
  }

  void previousState()
  {
    auto s = static_cast<States>(m_state - 1);
    if (s < FirstState) {
      s = LastState;
    }
    setState(s);
  }

  QVariant data(int column, int role) const
  {
    if (role == StateRole) {
      return m_state;
    }
    return QTreeWidgetItem::data(column, role);
  }

  void setData(int column, int role, const QVariant& value)
  {
    if (role == StateRole) {
      setState(static_cast<States>(value.toInt()));
    } else {
      QTreeWidgetItem::setData(column, role, value);
    }
  }

private:
  FilterList* m_list;
  States m_state;

  void updateState()
  {
    QIcon i;

    switch (m_state) {
    case Inactive: {
      i = QIcon(":/MO/gui/unchecked-checkbox");
      break;
    }

    case Active: {
      i = QIcon(":/MO/gui/checked-checkbox");
      break;
    }

    case Inverted: {
      i = QIcon(":/MO/gui/indeterminate-checkbox");
      break;
    }
    }
    setData(0, Qt::DecorationRole, i);
  }
};

class CriteriaItemFilter : public QObject
{
public:
  using Callback = std::function<bool(QTreeWidgetItem*, int)>;

  CriteriaItemFilter(QTreeWidget* tree, Callback f)
      : QObject(tree), m_tree(tree), m_f(std::move(f))
  {}

  bool eventFilter(QObject* o, QEvent* e) override
  {
    // careful: this filter is installed on both the tree and the viewport
    //
    // no check is currently necessary because mouse events originate from the
    // viewport only and keyboard events from the tree only

    if (m_f) {
      if (e->type() == QEvent::MouseButtonPress ||
          e->type() == QEvent::MouseButtonDblClick) {
        if (handleMouse(static_cast<QMouseEvent*>(e))) {
          return true;
        }
      } else if (e->type() == QEvent::KeyPress) {
        if (handleKeyboard(static_cast<QKeyEvent*>(e))) {
          return true;
        }
      }
    }

    return QObject::eventFilter(o, e);
  }

private:
  QTreeWidget* m_tree;
  Callback m_f;

  bool handleMouse(QMouseEvent* e)
  {
    auto* item = m_tree->itemAt(e->pos());
    if (!item) {
      return false;
    }

    m_tree->setCurrentItem(item);

    const auto dir = (e->button() == Qt::LeftButton ? 1 : -1);

    return m_f(item, dir);
  }

  bool handleKeyboard(QKeyEvent* e)
  {
    if (e->key() == Qt::Key_Space) {
      auto* item = m_tree->currentItem();
      if (!item) {
        return false;
      }

      const auto shiftPressed = (e->modifiers() & Qt::ShiftModifier);
      const auto dir          = (shiftPressed ? -1 : 1);

      return m_f(item, dir);
    }

    return false;
  }
};

FilterList::FilterList(Ui::MainWindow* ui, OrganizerCore& core,
                       CategoryFactory& factory)
    : ui(ui), m_core(core), m_factory(factory)
{
  auto* eventFilter = new CriteriaItemFilter(ui->filters, [&](auto* item, int dir) {
    return cycleItem(item, dir);
  });

  ui->filters->installEventFilter(eventFilter);
  ui->filters->viewport()->installEventFilter(eventFilter);

  connect(ui->filtersClear, &QPushButton::clicked, [&] {
    clearSelection();
  });
  connect(ui->filtersEdit, &QPushButton::clicked, [&] {
    editCategories();
  });
  connect(ui->filtersAnd, &QCheckBox::toggled, [&] {
    onOptionsChanged();
  });
  connect(ui->filtersOr, &QCheckBox::toggled, [&] {
    onOptionsChanged();
  });

  ui->filtersClear->setText(tr("Clear filters"));
  ui->filtersClear->setToolTip(tr("Remove every active filter."));
  ui->filtersEdit->setText(tr("Manage categories"));
  ui->filtersEdit->setToolTip(
      tr("Create, rename, organize, and map categories for this game."));
  ui->filtersAnd->setText(tr("All"));
  ui->filtersAnd->setToolTip(tr("Show mods that match every active filter."));
  ui->filtersOr->setText(tr("Any"));
  ui->filtersOr->setToolTip(
      tr("Show mods that match at least one active filter."));

  connect(ui->filtersSeparators, qOverload<int>(&QComboBox::currentIndexChanged), [&] {
    onOptionsChanged();
  });

  ui->filters->header()->setMinimumSectionSize(0);
  // The first column contains a full checkbox-state icon. Leave enough room
  // for the icon and its padding so it cannot collide with the filter text.
  ui->filters->header()->setSectionResizeMode(0, QHeaderView::Fixed);
  ui->filters->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  ui->filters->setIndentation(0);
  ui->filters->header()->resizeSection(0, 32);

  ui->filtersSeparators->addItem(tr("Follow filters"),
                                 ModListSortProxy::SeparatorFilter);
  ui->filtersSeparators->setItemData(
      0, tr("Show separators only when they match the active filters."),
      Qt::ToolTipRole);
  ui->filtersSeparators->addItem(tr("Always show separators"),
                                 ModListSortProxy::SeparatorShow);
  ui->filtersSeparators->setItemData(
      1, tr("Keep separators visible even when they do not match the filters."),
      Qt::ToolTipRole);
  ui->filtersSeparators->addItem(tr("Always hide separators"),
                                 ModListSortProxy::SeparatorHide);
  ui->filtersSeparators->setItemData(
      2, tr("Hide all separators from the mod list."), Qt::ToolTipRole);
}

void FilterList::restoreState(const Settings& s)
{
  s.widgets().restoreIndex(ui->filtersSeparators);
  s.widgets().restoreChecked(ui->filtersAnd);
  s.widgets().restoreChecked(ui->filtersOr);

  if (m_core.settings().interface().saveFilters()) {
    s.widgets().restoreTreeCheckState(ui->filters, CriteriaItem::StateRole);
  }
  checkCriteria();
}

void FilterList::saveState(Settings& s) const
{
  s.widgets().saveTreeCheckState(ui->filters, CriteriaItem::StateRole);
  s.widgets().saveChecked(ui->filtersAnd);
  s.widgets().saveChecked(ui->filtersOr);
  s.widgets().saveIndex(ui->filtersSeparators);
}

QTreeWidgetItem* FilterList::addCriteriaItem(QTreeWidgetItem* root, const QString& name,
                                             int categoryID, CriteriaType type)
{
  auto* item = new CriteriaItem(this, name, type, categoryID);

  // For now list all categories flatly without nestling them as there is
  // no way to espand nodes in the filter view since clicking changes state.
  ui->filters->addTopLevelItem(item);

  return item;
}

void FilterList::addContentCriteria()
{
  m_core.modDataContents().forEachContent(
      [this](auto const& content) {
        addCriteriaItem(nullptr,
                        QString("<%1>").arg(tr("Contains %1").arg(content.name())),
                        content.id(), ModListSortProxy::TypeContent);
      },
      true);
}

void FilterList::addCategoryCriteria(QTreeWidgetItem* root,
                                     const std::set<int>& categoriesUsed, int targetID)
{
  const auto count = static_cast<unsigned int>(m_factory.numCategories());
  for (unsigned int i = 1; i < count; ++i) {
    if (m_factory.getParentID(i) == targetID) {
      int categoryID = m_factory.getCategoryID(i);
      if (categoriesUsed.find(categoryID) != categoriesUsed.end()) {
        QTreeWidgetItem* item =
            addCriteriaItem(root, m_factory.getCategoryName(i), categoryID,
                            ModListSortProxy::TypeCategory);
        if (m_factory.hasChildren(i)) {
          addCategoryCriteria(item, categoriesUsed, categoryID);
        }
      }
    }
  }
}

void FilterList::addSpecialCriteria(int type)
{
  const auto sc = static_cast<CategoryFactory::SpecialCategories>(type);

  addCriteriaItem(nullptr, m_factory.getSpecialCategoryName(sc), type,
                  ModListSortProxy::TypeSpecial);
}

void FilterList::refresh()
{
  const auto oldSelection = selectedCriteria();

  ui->filters->clear();

  using F = CategoryFactory;
  addSpecialCriteria(F::Checked);
  addSpecialCriteria(F::UpdateAvailable);
  addSpecialCriteria(F::Backup);
  addSpecialCriteria(F::Managed);
  addSpecialCriteria(F::HasCategory);
  addSpecialCriteria(F::Conflict);
  addSpecialCriteria(F::HasHiddenFiles);
  addSpecialCriteria(F::Endorsed);
  addSpecialCriteria(F::Tracked);
  addSpecialCriteria(F::HasNexusID);
  addSpecialCriteria(F::HasGameData);

  addContentCriteria();

  std::set<int> categoriesUsed;
  for (unsigned int modIdx = 0; modIdx < ModInfo::getNumMods(); ++modIdx) {
    ModInfo::Ptr modInfo = ModInfo::getByIndex(modIdx);
    for (int categoryID : modInfo->getCategories()) {
      int currentID = categoryID;
      std::set<int> cycleTest;
      // also add parents so they show up in the tree
      while (currentID != 0) {
        categoriesUsed.insert(currentID);
        if (!cycleTest.insert(currentID).second) {
          log::warn("cycle in categories: {}", SetJoin(cycleTest, ", "));
          break;
        }
        currentID = m_factory.getParentID(m_factory.getCategoryIndex(currentID));
      }
    }
  }

  addCategoryCriteria(nullptr, categoriesUsed, 0);
  setSelection(oldSelection);
}

void FilterList::setSelection(const std::vector<Criteria>& criteria)
{
  for (int i = 0; i < ui->filters->topLevelItemCount(); ++i) {
    auto* item = dynamic_cast<CriteriaItem*>(ui->filters->topLevelItem(i));
    if (!item) {
      continue;
    }

    bool found = false;

    for (auto&& c : criteria) {
      if (item->type() == c.type && item->id() == c.id) {
        item->setState(c.inverse ? CriteriaItem::Inverted : CriteriaItem::Active);
        found = true;
        break;
      }
    }

    if (!found) {
      item->setState(CriteriaItem::Inactive);
    }
  }
}

void FilterList::clearSelection()
{
  for (int i = 0; i < ui->filters->topLevelItemCount(); ++i) {
    auto* ci = dynamic_cast<CriteriaItem*>(ui->filters->topLevelItem(i));
    if (!ci) {
      continue;
    }

    ci->setState(CriteriaItem::Inactive);
  }

  checkCriteria();
}

bool FilterList::cycleItem(QTreeWidgetItem* item, int direction)
{
  auto* ci = dynamic_cast<CriteriaItem*>(item);
  if (!ci) {
    return false;
  }

  if (direction > 0) {
    ci->nextState();
  } else if (direction < 0) {
    ci->previousState();
  } else {
    return false;
  }

  checkCriteria();
  return true;
}

std::vector<ModListSortProxy::Criteria> FilterList::selectedCriteria() const
{
  std::vector<Criteria> criteria;

  for (int i = 0; i < ui->filters->topLevelItemCount(); ++i) {
    const auto* ci = dynamic_cast<CriteriaItem*>(ui->filters->topLevelItem(i));
    if (!ci) {
      continue;
    }

    if (ci->state() != CriteriaItem::Inactive) {
      criteria.push_back(
          {ci->type(), ci->id(), (ci->state() == CriteriaItem::Inverted)});
    }
  }

  return criteria;
}

void FilterList::checkCriteria()
{
  emit criteriaChanged(selectedCriteria());
}

void FilterList::editCategories()
{
  CategoriesDialog dialog(qApp->activeWindow());

  if (dialog.exec() == QDialog::Accepted) {
    dialog.commitChanges();
    refresh();
  }
}

void FilterList::onOptionsChanged()
{
  const auto mode = ui->filtersAnd->isChecked() ? ModListSortProxy::FilterAnd
                                                : ModListSortProxy::FilterOr;

  const auto separators = static_cast<ModListSortProxy::SeparatorsMode>(
      ui->filtersSeparators->currentData().toInt());

  emit optionsChanged(mode, separators);
}
