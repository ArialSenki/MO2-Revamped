#include "settingsdialogplugins.h"
#include "circularuihelpers.h"
#include "noeditdelegate.h"
#include "ui_settingsdialog.h"
#include <iplugin.h>

#include "disableproxyplugindialog.h"
#include "fluentswitch.h"
#include "organizercore.h"
#include "game_features.h"
#include "plugincontainer.h"
#include <dataarchives.h>
#include <gameplugins.h>
#include <iplugingame.h>
#include "revampedtheme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QStyle>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaType>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <functional>

using namespace MOBase;

namespace
{

class PluginSettingValueDelegate final : public QStyledItemDelegate
{
public:
  PluginSettingValueDelegate(QWidget* colorsFrom, QObject* parent)
      : QStyledItemDelegate(parent), m_colorsFrom(colorsFrom)
  {}

  // a true or false option is a switch, as in every other settings page
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    if (index.data(Qt::EditRole).metaType().id() != QMetaType::Bool) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }
    QStyleOptionViewItem blank(option);
    initStyleOption(&blank, index);
    blank.text.clear();
    const QStyle* style = blank.widget != nullptr ? blank.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &blank, painter, blank.widget);

    // the theme's switch colors (FluentSwitchPainter in the stylesheets),
    // until a painter is found to read them from
    FluentSwitch::Colors colors =
        MOBase::RevampedTheme::isDark()
            ? FluentSwitch::Colors{QColor(0x4C, 0xC2, 0xFF), QColor(0x2F, 0x2F, 0x2F),
                                   QColor(0x9A, 0x9A, 0x9A), QColor(0, 0, 0),
                                   QColor(0xC5, 0xC5, 0xC5)}
            : FluentSwitch::Colors{QColor(0x00, 0x5F, 0xB8), QColor(0xF7, 0xF7, 0xF7),
                                   QColor(0x8A, 0x8A, 0x8A), QColor(0xFF, 0xFF, 0xFF),
                                   QColor(0x5D, 0x5D, 0x5D)};
    if (m_colorsFrom != nullptr) {
      if (auto* painterWidget = m_colorsFrom->findChild<FluentSwitchPainter*>()) {
        colors = {painterWidget->property("onColor").value<QColor>(),
                  painterWidget->property("offColor").value<QColor>(),
                  painterWidget->property("borderColor").value<QColor>(),
                  painterWidget->property("knobOnColor").value<QColor>(),
                  painterWidget->property("knobOffColor").value<QColor>()};
      }
    }
    const QRectF track(option.rect.left() + 6,
                       option.rect.center().y() - 9.5, 40, 20);
    FluentSwitch::paint(painter, track, index.data(Qt::EditRole).toBool() ? 1.0 : 0.0, 0.0,
                        colors);
  }

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const override
  {
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (index.data(Qt::EditRole).metaType().id() == QMetaType::Bool) {
      if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        combo->setMaxVisibleItems(2);
        QTimer::singleShot(0, combo, [combo] { combo->showPopup(); });
      }
    }
    return editor;
  }

private:
  QWidget* m_colorsFrom;
};

// check_font reads "Check font"; the plugin's own key stays with the row
QString readableOptionName(QString key)
{
  key.replace(QLatin1Char('_'), QLatin1Char(' '));
  // camelCase, without splitting an abbreviation
  static const QRegularExpression wordStart(QStringLiteral("(?<=[a-z0-9])(?=[A-Z])"));
  key.replace(wordStart, QStringLiteral(" "));
  key = key.simplified();
  QString result;
  for (const QString& word : key.split(QLatin1Char(' '))) {
    const bool abbreviation = word.size() > 1 && word == word.toUpper();
    result += (result.isEmpty() ? QString() : QStringLiteral(" ")) +
              (abbreviation ? word : word.toLower());
  }
  if (!result.isEmpty()) {
    result[0] = result[0].toUpper();
  }
  return result;
}

// the details are as tall as their text at their width: no less, or the enable
// row is cut off, and no more, or there's a gap above the options
class PluginDetailsHeight final : public QObject
{
public:
  using QObject::QObject;

  static void fit(QWidget* details)
  {
    auto* form = qobject_cast<QFormLayout*>(details->layout());
    if (form == nullptr) {
      return;
    }
    // row by row: with most of the form's rows hidden, its own sum isn't usable
    const QMargins margins = form->contentsMargins();
    const int width        = details->width() - margins.left() - margins.right();
    int height             = margins.top() + margins.bottom();
    int rows               = 0;
    for (int i = 0; i < form->count(); ++i) {
      QWidget* w = form->itemAt(i)->widget();
      if (w == nullptr || w->isHidden()) {
        continue;
      }
      w->ensurePolished();
      height += w->hasHeightForWidth() ? w->heightForWidth(width) : w->sizeHint().height();
      ++rows;
    }
    height += qMax(0, rows - 1) * form->verticalSpacing();
    if (details->minimumHeight() != height || details->maximumHeight() != height) {
      details->setFixedHeight(height);
    }
  }

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    // also once shown and laid out: the page can be built while it's being
    // opened, with its rows still being made
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
        event->type() == QEvent::LayoutRequest) {
      fit(static_cast<QWidget*>(watched));
    }
    return false;
  }
};

class PluginBooleanSingleClickFilter final : public QObject
{
public:
  explicit PluginBooleanSingleClickFilter(QTreeWidget* view)
      : QObject(view), m_view(view)
  {}

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    const auto type = event->type();
    if ((type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick ||
         type == QEvent::MouseButtonRelease) &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && !m_view.isNull()) {
      const auto* mouseEvent = static_cast<QMouseEvent*>(event);
      const QModelIndex index = m_view->indexAt(mouseEvent->position().toPoint());
      const bool isSwitch     = index.isValid() && index.column() == 1 &&
                            index.data(Qt::EditRole).metaType().id() == QMetaType::Bool;

      if (type == QEvent::MouseButtonRelease) {
        // it changes on the spot, as a switch does: a click that started on it
        if (isSwitch && m_pressed == index) {
          m_view->setCurrentIndex(index);
          m_view->model()->setData(index, !index.data(Qt::EditRole).toBool(), Qt::EditRole);
        }
        m_pressed = QPersistentModelIndex();
      } else {
        m_pressed = isSwitch ? QPersistentModelIndex(index) : QPersistentModelIndex();
        // a double click is two clicks here, not the list's editor
        if (isSwitch && type == QEvent::MouseButtonDblClick) {
          return true;
        }
      }
    }

    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QTreeWidget> m_view;
  QPersistentModelIndex m_pressed;
};

}  // namespace

PluginsSettingsTab::PluginsSettingsTab(Settings& s, PluginContainer* pluginContainer,
                                       SettingsDialog& d)
    : SettingsTab(s, d), m_pluginContainer(pluginContainer)
{
  CircularUi::addIconToHeading(
      ui->pluginPageTitle, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("pluginPageIcon"), 36, QStringLiteral("P"));
  CircularUi::addGroupHeading(
      ui->pluginBrowserGroup, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("pluginBrowserIcon"), 26,
      QStringLiteral("pluginSectionTitle"), QStringLiteral("P"));
  CircularUi::addGroupHeading(
      ui->pluginDetailsGroup, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("pluginDetailsIcon"), 26,
      QStringLiteral("pluginSectionTitle"), QStringLiteral("D"));
  CircularUi::addIconToHeading(
      ui->pluginSettingsTitle, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("pluginOptionsIcon"), 26, QStringLiteral("O"));
  CircularUi::addGroupHeading(
      ui->verticalGroupBox, ":/MO/gui/mainwindow/clear.svg",
      QStringLiteral("pluginBlacklistIcon"), 26,
      QStringLiteral("pluginSectionTitle"), QStringLiteral("B"));

  ui->pluginSettingsList->setAlternatingRowColors(true);
  ui->pluginsList->setAlternatingRowColors(true);
  ui->pluginBlacklist->setAlternatingRowColors(true);
  ui->pluginSettingsList->header()->setStretchLastSection(true);
  // Keep descriptive labels at their natural height. QLabel defaults to a
  // vertically preferred policy, which lets long, empty plugin panes stretch
  // the labels and push the filter and settings controls far apart.
  const auto keepNaturalHeight = [](QWidget* widget) {
    QSizePolicy policy = widget->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Maximum);
    widget->setSizePolicy(policy);
    widget->setMinimumHeight(0);
  };
  keepNaturalHeight(ui->pluginBrowserDescription);
  ui->pluginDescription->installEventFilter(new PluginDetailsHeight(ui->pluginDescription));
  keepNaturalHeight(ui->pluginSettingsTitle);
  keepNaturalHeight(ui->pluginSettingsDescription);
  keepNaturalHeight(ui->pluginBlacklistDescription);
  ui->verticalLayout_9->setContentsMargins(8, 6, 8, 8);
  ui->verticalLayout_9->setSpacing(8);
  ui->pluginsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ui->pluginSettingsList->setSizePolicy(QSizePolicy::Expanding,
                                        QSizePolicy::Expanding);
  ui->pluginBlacklist->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Expanding);
  // the gap between the cards of every other page: the top margin of the card
  // below it adds 4
  ui->splitter->setHandleWidth(13);
  ui->splitter->setStretchFactor(0, 4);
  ui->splitter->setStretchFactor(1, 1);
  ui->splitter_2->setHandleWidth(17);
  ui->splitter_2->setStretchFactor(0, 2);
  ui->splitter_2->setStretchFactor(1, 3);
  ui->verticalLayout_15->setContentsMargins(8, 6, 8, 8);
  ui->verticalLayout_15->setSpacing(8);
  ui->pluginDetailsLayout->setContentsMargins(8, 6, 8, 8);
  ui->pluginDetailsLayout->setSpacing(8);
  ui->verticalLayout_16->setContentsMargins(8, 6, 8, 8);
  ui->verticalLayout_16->setSpacing(8);
  ui->pluginFilterEdit->setMinimumHeight(32);
  ui->pluginFilterEdit->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Maximum);
  ui->pluginDescriptionLayout->setVerticalSpacing(6);
  ui->pluginDescriptionLayout->setHorizontalSpacing(12);
  // no card inside the card: the enable switch ends where the other pages' do
  ui->pluginDescriptionLayout->setContentsMargins(0, 0, 0, 0);
  ui->pluginSettingsList->setEditTriggers(QAbstractItemView::DoubleClicked |
                                          QAbstractItemView::EditKeyPressed);
  // the same height of row as the other pages' lists
  ui->pluginsList->setIndentation(14);
  ui->pluginSettingsList->viewport()->installEventFilter(
      new PluginBooleanSingleClickFilter(ui->pluginSettingsList));
  ui->pluginSettingsList->setItemDelegateForColumn(
      1, new PluginSettingValueDelegate(ui->enabledCheckbox, ui->pluginSettingsList));

  // the plugin's name heads its details, with its author and version under it
  m_pluginName = new QLabel(ui->pluginDescription);
  m_pluginName->setObjectName(QStringLiteral("pluginDetailsName"));
  m_pluginName->setWordWrap(true);
  m_pluginCaption = new QLabel(ui->pluginDescription);
  m_pluginCaption->setObjectName(QStringLiteral("pluginDetailsCaption"));
  m_pluginCaption->setWordWrap(true);
  ui->pluginDescriptionLayout->insertRow(0, m_pluginCaption);
  ui->pluginDescriptionLayout->insertRow(0, m_pluginName);
  for (QWidget* w : {static_cast<QWidget*>(ui->label_13), static_cast<QWidget*>(ui->label_15),
                     static_cast<QWidget*>(ui->label_14), static_cast<QWidget*>(ui->authorLabel),
                     static_cast<QWidget*>(ui->versionLabel)}) {
    w->hide();
  }
  ui->pluginDescriptionLayout->setHorizontalSpacing(0);

  // Create top-level tree widget:
  QStringList pluginInterfaces = m_pluginContainer->pluginInterfaces();
  pluginInterfaces.sort(Qt::CaseInsensitive);
  std::map<QString, QTreeWidgetItem*> topItems;
  for (QString interfaceName : pluginInterfaces) {
    // the names are QT_TR_NOOP() keys; translate them for display only
    auto* item = new QTreeWidgetItem(
        ui->pluginsList, {QCoreApplication::translate("PluginTypeName",
                                                      interfaceName.toUtf8().constData())});
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    auto font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    topItems[interfaceName] = item;
    item->setExpanded(true);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
  }
  ui->pluginsList->setHeaderHidden(true);

  // display plugin settings
  QSet<QString> handledNames;
  for (IPlugin* plugin : settings().plugins().plugins()) {
    if (handledNames.contains(plugin->name()) ||
        m_pluginContainer->requirements(plugin).master()) {
      continue;
    }

    QTreeWidgetItem* listItem = new QTreeWidgetItem(
        topItems.at(m_pluginContainer->topImplementedInterface(plugin)));
    listItem->setData(0, Qt::DisplayRole, plugin->localizedName());
    listItem->setData(0, PluginRole, QVariant::fromValue((void*)plugin));
    listItem->setData(0, SettingsRole, settings().plugins().settings(plugin->name()));
    listItem->setData(0, DescriptionsRole,
                      settings().plugins().descriptions(plugin->name()));

    // Handle child item:
    auto children = m_pluginContainer->requirements(plugin).children();
    for (auto* child : children) {
      QTreeWidgetItem* childItem = new QTreeWidgetItem(listItem);
      childItem->setData(0, Qt::DisplayRole, child->localizedName());
      childItem->setData(0, PluginRole, QVariant::fromValue((void*)child));
      childItem->setData(0, SettingsRole, settings().plugins().settings(child->name()));
      childItem->setData(0, DescriptionsRole,
                         settings().plugins().descriptions(child->name()));

      handledNames.insert(child->name());
    }

    handledNames.insert(plugin->name());
  }

  for (auto& [k, item] : topItems) {
    if (item->childCount() == 0) {
      item->setHidden(true);
    }
  }

  ui->pluginsList->sortByColumn(0, Qt::AscendingOrder);

  // display plugin blacklist
  for (const QString& pluginName : settings().plugins().blacklist()) {
    ui->pluginBlacklist->addItem(pluginName);
  }

  m_filter.setEdit(ui->pluginFilterEdit);

  QObject::connect(ui->pluginsList, &QTreeWidget::currentItemChanged,
                   [&](auto* current, auto* previous) {
                     on_pluginsList_currentItemChanged(current, previous);
                   });
  QObject::connect(ui->enabledCheckbox, &QCheckBox::clicked, [&](bool checked) {
    on_checkboxEnabled_clicked(checked);
  });

  QShortcut* delShortcut =
      new QShortcut(QKeySequence(Qt::Key_Delete), ui->pluginBlacklist);
  QObject::connect(delShortcut, &QShortcut::activated, &dialog(), [&] {
    deleteBlacklistItem();
  });
  QObject::connect(&m_filter, &FilterWidget::changed, [&] {
    filterPluginList();
  });

  updateListItems();
  filterPluginList();
}

void PluginsSettingsTab::updateListItems()
{
  for (auto i = 0; i < ui->pluginsList->topLevelItemCount(); ++i) {
    auto* topLevelItem = ui->pluginsList->topLevelItem(i);
    for (auto j = 0; j < topLevelItem->childCount(); ++j) {
      auto* item   = topLevelItem->child(j);
      auto* plugin = this->plugin(item);

      bool inactive = !m_pluginContainer->implementInterface<IPluginGame>(plugin) &&
                      !m_pluginContainer->isEnabled(plugin);

      auto font = item->font(0);
      font.setItalic(inactive);
      item->setFont(0, font);
      for (auto k = 0; k < item->childCount(); ++k) {
        item->child(k)->setFont(0, font);
      }
    }
  }
}

void PluginsSettingsTab::filterPluginList()
{
  auto selectedItems              = ui->pluginsList->selectedItems();
  QTreeWidgetItem* firstNotHidden = nullptr;

  // only what means something for this game is listed; hiding a row doesn't
  // change the plugin, which stays loaded and as it was
  const auto listed = [this](IPlugin* plugin) {
    return appliesToGame(plugin);
  };

  for (auto i = 0; i < ui->pluginsList->topLevelItemCount(); ++i) {
    auto* topLevelItem = ui->pluginsList->topLevelItem(i);

    bool found = false;
    for (auto j = 0; j < topLevelItem->childCount(); ++j) {
      auto* item   = topLevelItem->child(j);
      auto* plugin = this->plugin(item);

      // Check the item or the child - If any match (item or child), the whole
      // group is displayed.
      bool match = listed(plugin) && m_filter.matches([plugin](const QRegularExpression& regex) {
        return regex.match(plugin->localizedName()).hasMatch();
      });
      for (int k = 0; k < item->childCount(); ++k) {
        item->child(k)->setHidden(!listed(this->plugin(item->child(k))));
      }
      if (listed(plugin)) {
        for (auto* child : m_pluginContainer->requirements(plugin).children()) {
          match = match || (listed(child) &&
                            m_filter.matches([child](const QRegularExpression& regex) {
                              return regex.match(child->localizedName()).hasMatch();
                            }));
        }
      }

      if (match) {
        found = true;
        item->setHidden(false);

        if (firstNotHidden == nullptr) {
          firstNotHidden = item;
        }
      } else {
        item->setHidden(true);
      }
    }

    topLevelItem->setHidden(!found);
  }

  // Unselect item if hidden:
  if (firstNotHidden) {
    ui->pluginDescription->setVisible(true);
    ui->pluginSettingsTitle->setVisible(true);
    ui->pluginSettingsDescription->setVisible(true);
    ui->pluginSettingsList->setVisible(true);
    ui->noPluginLabel->setVisible(false);
    if (selectedItems.isEmpty()) {
      ui->pluginsList->setCurrentItem(firstNotHidden);
    } else if (selectedItems[0]->isHidden()) {
      ui->pluginsList->setCurrentItem(firstNotHidden);
    }
  } else {
    ui->pluginDescription->setVisible(false);
    ui->pluginSettingsTitle->setVisible(false);
    ui->pluginSettingsDescription->setVisible(false);
    ui->pluginSettingsList->setVisible(false);
    ui->noPluginLabel->setVisible(true);
  }
}

bool PluginsSettingsTab::appliesToGame(IPlugin* plugin)
{
  const IPluginGame* game = settings().game().plugin();
  if (m_pluginContainer->implementInterface<IPluginGame>(plugin)) {
    return game == nullptr || plugin->name() == game->name();
  }
  if (!m_pluginContainer->requirements(plugin).problems().empty()) {
    return false;
  }
  if (game == nullptr) {
    return true;
  }

  // these work on the Bethesda formats and have no requirement that says so
  const QString name = plugin->name();
  if (name == QLatin1String("BSA Packer") || name == QLatin1String("BSA Extractor") ||
      name.compare(QLatin1String("Preview BSA"), Qt::CaseInsensitive) == 0) {
    return m_pluginContainer->gameFeatures().gameFeature<DataArchives>() != nullptr;
  }
  if (name == QLatin1String("INI Bakery") || name == QLatin1String("INI Editor")) {
    return !game->iniFiles().filter(".ini", Qt::CaseInsensitive).isEmpty();
  }
  // packages with these formats only exist for the games with plugin files
  if (name == QLatin1String("BAIN Installer") ||
      name == QLatin1String("BAIN Wizard Installer") ||
      name.compare(QLatin1String("Omod Installer"), Qt::CaseInsensitive) == 0) {
    return m_pluginContainer->gameFeatures().gameFeature<GamePlugins>() != nullptr;
  }
  return true;
}

IPlugin* PluginsSettingsTab::plugin(QTreeWidgetItem* pluginItem) const
{
  return static_cast<IPlugin*>(qvariant_cast<void*>(pluginItem->data(0, PluginRole)));
}

void PluginsSettingsTab::update()
{
  // transfer plugin settings to in-memory structure, the plugins under another
  // plugin too
  const std::function<void(QTreeWidgetItem*)> transfer = [&](QTreeWidgetItem* parent) {
    for (int j = 0; j < parent->childCount(); ++j) {
      auto* item = parent->child(j);
      settings().plugins().setSettings(plugin(item)->name(),
                                       item->data(0, SettingsRole).toMap());
      transfer(item);
    }
  };
  for (int i = 0; i < ui->pluginsList->topLevelItemCount(); ++i) {
    transfer(ui->pluginsList->topLevelItem(i));
  }

  // set plugin blacklist
  QStringList names;
  for (QListWidgetItem* item : ui->pluginBlacklist->findItems("*", Qt::MatchWildcard)) {
    names.push_back(item->text());
  }

  settings().plugins().setBlacklist(names);

  settings().plugins().save();
}

void PluginsSettingsTab::closing()
{
  storeSettings(ui->pluginsList->currentItem());
}

void PluginsSettingsTab::on_checkboxEnabled_clicked(bool checked)
{
  // Retrieve the plugin:
  auto* item = ui->pluginsList->currentItem();
  if (!item || !item->data(0, PluginRole).isValid()) {
    return;
  }
  IPlugin* plugin          = this->plugin(item);
  const auto& requirements = m_pluginContainer->requirements(plugin);

  // User wants to enable:
  if (checked) {
    m_pluginContainer->setEnabled(plugin, true, false);
  } else {
    // Custom check for proxy + current game:
    if (m_pluginContainer->implementInterface<IPluginProxy>(plugin)) {

      // Current game:
      auto* game = m_pluginContainer->managedGame();
      if (m_pluginContainer->requirements(game).proxy() == plugin) {
        QMessageBox::warning(parentWidget(), QObject::tr("Cannot disable plugin"),
                             QObject::tr("The '%1' plugin is used by the current game "
                                         "plugin and cannot disabled.")
                                 .arg(plugin->localizedName()),
                             QMessageBox::Ok);
        ui->enabledCheckbox->setChecked(true);
        return;
      }

      // Check the proxied plugins:
      auto proxied = requirements.proxied();
      if (!proxied.empty()) {
        DisableProxyPluginDialog dialog(plugin, proxied, parentWidget());
        if (dialog.exec() != QDialog::Accepted) {
          ui->enabledCheckbox->setChecked(true);
          return;
        }
      }
    }

    // Check if the plugins is required for other plugins:
    auto requiredFor = requirements.requiredFor();
    if (!requiredFor.empty()) {
      QStringList pluginNames;
      for (auto& p : requiredFor) {
        pluginNames.append(p->localizedName());
      }
      pluginNames.sort();
      QString message =
          QObject::tr("<p>Disabling the '%1' plugin will also disable the following "
                      "plugins:</p><ul>%2</ul><p>Do you want to continue?</p>")
              .arg(plugin->localizedName())
              .arg("<li>" + pluginNames.join("</li><li>") + "</li>");
      if (QMessageBox::warning(parentWidget(), QObject::tr("Really disable plugin?"),
                               message,
                               QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        ui->enabledCheckbox->setChecked(true);
        return;
      }
    }
    m_pluginContainer->setEnabled(plugin, false, true);
  }

  // Proxy was disabled / enabled, need restart:
  if (m_pluginContainer->implementInterface<IPluginProxy>(plugin)) {
    dialog().setExitNeeded(Exit::Restart);
  }

  updateListItems();
  // a plugin that needs this one comes and goes with it
  filterPluginList();
}

void PluginsSettingsTab::on_pluginsList_currentItemChanged(QTreeWidgetItem* current,
                                                           QTreeWidgetItem* previous)
{
  storeSettings(previous);

  ui->pluginSettingsList->clear();
  if (current == nullptr || !current->data(0, PluginRole).isValid()) {
    // a group heading, or nothing after the filter emptied the list: the
    // previous plugin's options must go too, or edits to them would be lost
    ui->pluginSettingsList->setEnabled(false);
    ui->enabledCheckbox->setVisible(false);
    m_pluginName->setText(current != nullptr ? current->text(0) : QString());
    m_pluginCaption->clear();
    ui->authorLabel->clear();
    ui->versionLabel->clear();
    ui->descriptionLabel->clear();
    return;
  }

  IPlugin* plugin = this->plugin(current);
  ui->authorLabel->setText(plugin->author());
  ui->versionLabel->setText(plugin->version().canonicalString());
  ui->descriptionLabel->setText(plugin->description());
  m_pluginName->setText(plugin->localizedName());
  m_pluginCaption->setText(QStringLiteral("%1 \u00B7 %2")
                               .arg(plugin->author(), plugin->version().canonicalString()));
  ui->pluginDescription->updateGeometry();
  ui->pluginSettingsDescription->updateGeometry();

  // Checkbox, do not show for children or game plugins, disable
  // if the plugin cannot be enabled.
  ui->enabledCheckbox->setVisible(
      !m_pluginContainer->implementInterface<IPluginGame>(plugin) &&
      plugin->master().isEmpty());

  bool enabled       = m_pluginContainer->isEnabled(plugin);
  auto& requirements = m_pluginContainer->requirements(plugin);
  auto problems      = requirements.problems();

  if (m_pluginContainer->requirements(plugin).isCorePlugin()) {
    ui->enabledCheckbox->setDisabled(true);
    ui->enabledCheckbox->setChecked(true);
    ui->enabledCheckbox->setToolTip(
        QObject::tr("This plugin is required for Mod Organizer to work properly and "
                    "cannot be disabled."));
  }
  // Plugin is enable or can be enabled.
  else if (enabled || problems.empty()) {
    ui->enabledCheckbox->setDisabled(false);
    ui->enabledCheckbox->setToolTip("");
    ui->enabledCheckbox->setChecked(enabled);
  }
  // Plugin is disable and cannot be enabled.
  else {
    ui->enabledCheckbox->setDisabled(true);
    ui->enabledCheckbox->setChecked(false);
    if (problems.size() == 1) {
      ui->enabledCheckbox->setToolTip(problems[0].shortDescription());
    } else {
      QStringList descriptions;
      for (auto& problem : problems) {
        descriptions.append(problem.shortDescription());
      }
      ui->enabledCheckbox->setToolTip("<ul><li>" + descriptions.join("</li><li>") +
                                      "</li></ul>");
    }
  }

  // the switch sits in a Settings row: why it is locked shows over all of it
  // (not before the page's rows exist: the parent is then the whole panel)
  if (QWidget* row = ui->enabledCheckbox->parentWidget();
      row != nullptr && row->objectName() == QStringLiteral("fluentSettingRow")) {
    row->setToolTip(ui->enabledCheckbox->toolTip());
  }

  QVariantMap settings     = current->data(0, SettingsRole).toMap();
  QVariantMap descriptions = current->data(0, DescriptionsRole).toMap();
  ui->pluginSettingsList->setEnabled(settings.count() != 0);
  // the option names can't be edited; one delegate for the list, not a new
  // one without a parent for every option
  if (ui->pluginSettingsList->itemDelegateForColumn(0) == nullptr) {
    ui->pluginSettingsList->setItemDelegateForColumn(
        0, new NoEditDelegate(ui->pluginSettingsList));
  }
  for (auto iter = settings.begin(); iter != settings.end(); ++iter) {
    // masters only exist in games with plugin files; the value stays stored
    if (iter.key() == QLatin1String("check_missingmasters") &&
        !m_pluginContainer->gameFeatures().gameFeature<GamePlugins>()) {
      continue;
    }

    QTreeWidgetItem* newItem =
        new QTreeWidgetItem(QStringList(readableOptionName(iter.key())));
    newItem->setData(0, Qt::UserRole, iter.key());
    QVariant value = *iter;
    QString description;
    {
      auto descriptionIter = descriptions.find(iter.key());
      if (descriptionIter != descriptions.end()) {
        description = descriptionIter->toString();
      }
    }

    newItem->setData(1, Qt::DisplayRole, value);
    newItem->setData(1, Qt::EditRole, value);
    newItem->setToolTip(1, description);
    // the plugin's own name for it, as a mod's guide or the plugin's author would use
    newItem->setToolTip(0, description.isEmpty()
                               ? iter.key()
                               : QStringLiteral("%1\n\n%2").arg(iter.key(), description));

    newItem->setFlags(newItem->flags() | Qt::ItemIsEditable);
    ui->pluginSettingsList->addTopLevelItem(newItem);
  }

  ui->pluginSettingsList->resizeColumnToContents(0);
  ui->pluginSettingsList->resizeColumnToContents(1);

  PluginDetailsHeight::fit(ui->pluginDescription);
  QTimer::singleShot(0, ui->pluginDescription, [details = ui->pluginDescription] {
    PluginDetailsHeight::fit(details);
  });
}

void PluginsSettingsTab::deleteBlacklistItem()
{
  // takeItem() hands the item over; nothing else deletes it
  delete ui->pluginBlacklist->takeItem(ui->pluginBlacklist->currentIndex().row());
}

void PluginsSettingsTab::storeSettings(QTreeWidgetItem* pluginItem)
{
  if (pluginItem != nullptr && pluginItem->data(0, PluginRole).isValid()) {
    QVariantMap settings = pluginItem->data(0, SettingsRole).toMap();

    for (int i = 0; i < ui->pluginSettingsList->topLevelItemCount(); ++i) {
      const QTreeWidgetItem* item = ui->pluginSettingsList->topLevelItem(i);
      settings[item->data(0, Qt::UserRole).toString()] = item->data(1, Qt::DisplayRole);
    }

    pluginItem->setData(0, SettingsRole, settings);
  }
}
