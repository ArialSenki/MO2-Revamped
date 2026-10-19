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

#include "settingsdialog.h"
#include "fluenticons.h"
#include "fluentnavigation.h"
#include "fluentsettings.h"
#include "settingsdialogdiagnostics.h"
#include "settingsdialoggeneral.h"
#include "settingsdialogmodlist.h"
#include "settingsdialognexus.h"
#include "settingsdialogpaths.h"
#include "settingsdialogplugins.h"
#include "settingsdialogtheme.h"
#include "settingsdialogworkarounds.h"
#include "plugincontainer.h"
#include "ui_settingsdialog.h"

#include "game_features.h"
#include <bsainvalidation.h>
#include <dataarchives.h>
#include <gameplugins.h>
#include <unmanagedmods.h>
#include <iplugingame.h>

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QPalette>
#include <QScreen>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>

using namespace MOBase;

namespace
{

class SettingsComboPopupDelegate final : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override
  {
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    // Keep the item geometry in step with the vertical padding in the Light
    // theme. Without this, the popup can paint one row over the next one.
    hint.setHeight(qMax(hint.height(), 32));
    return hint;
  }
};

void configureSettingsComboPopups(QWidget* root, bool lightStyle)
{
  if (!root || !lightStyle) {
    return;
  }

  for (QComboBox* combo : root->findChildren<QComboBox*>()) {
    QAbstractItemView* popupView = combo->view();
    if (!popupView) {
      continue;
    }

    if (combo->objectName() != QStringLiteral("languageBox")) {
      popupView->setItemDelegate(new SettingsComboPopupDelegate(popupView));
    }

    combo->setMaxVisibleItems(8);
    popupView->setMinimumWidth(qMax(320, combo->width()));
    popupView->setMinimumHeight(0);
    popupView->setMaximumHeight(260);
    popupView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QPalette popupPalette = popupView->palette();
    popupPalette.setColor(QPalette::Base, QColor("#FFFFFF"));
    popupPalette.setColor(QPalette::Window, QColor("#FFFFFF"));
    popupPalette.setColor(QPalette::Text, QColor("#293847"));
    popupPalette.setColor(QPalette::WindowText, QColor("#293847"));
    popupPalette.setColor(QPalette::Highlight, QColor("#D7EAF9"));
    popupPalette.setColor(QPalette::HighlightedText, QColor("#183E5F"));

    popupView->setPalette(popupPalette);
    popupView->viewport()->setPalette(popupPalette);
    // Let the rounded popup frame provide the background. An opaque view or
    // viewport paints a rectangular layer over its rounded corners.
    popupView->setAutoFillBackground(false);
    popupView->viewport()->setAutoFillBackground(false);
    popupView->setFrameShape(QFrame::NoFrame);
    QWidget* popupFrame = popupView->window();
    if (popupFrame && popupFrame != popupView &&
        popupFrame != combo->window()) {
      popupFrame->setMaximumHeight(270);
      popupFrame->setPalette(popupPalette);
      popupFrame->setAutoFillBackground(true);
      popupFrame->setAttribute(Qt::WA_StyledBackground, true);
      popupFrame->setStyleSheet(
          "QFrame { color: #293847; background-color: #FFFFFF; "
          "border: 1px solid #C7D3DD; border-radius: 8px; }");
    }
  }
}

}  // namespace

SettingsDialog::SettingsDialog(PluginContainer* pluginContainer, Settings& settings,
                               QWidget* parent)
    : TutorableDialog("SettingsDialog", parent), ui(new Ui::SettingsDialog),
      m_settings(settings), m_exit(Exit::None), m_pluginContainer(pluginContainer)
{
  ui->setupUi(this);
  hideUnsupportedOptions();

  // Give every Settings page more room while keeping the dialog inside the
  // available display area. The minimum also upgrades previously saved small
  // geometries when GeometrySaver restores them in exec().
  const QSize availableSize = screen() ? screen()->availableGeometry().size()
                                      : QSize(1920, 1080);
  const int targetWidth = qMax(320, qMin(1000, availableSize.width() - 48));
  const int targetHeight = qMax(320, qMin(740, availableSize.height() - 32));
  setMinimumSize(qMin(980, targetWidth), qMin(700, targetHeight));
  resize(targetWidth, targetHeight);

  QSizePolicy tabPolicy = ui->tabWidget->sizePolicy();
  tabPolicy.setVerticalPolicy(QSizePolicy::Ignored);
  ui->tabWidget->setSizePolicy(tabPolicy);

  // Fluent v4: pages listed on the left, like Windows 11 Settings
  FluentNavigation::attach(ui->tabWidget, {
                                              0xE713,  // General: Settings
                                              0xE790,  // Theme: Color
                                              0xE8FD,  // Mod List: BulletedList
                                              0xE8B7,  // Paths: Folder
                                              0xE774,  // Nexus: Globe
                                              0xEA86,  // Plugins: Puzzle
                                              0xE90F,  // Workarounds: Repair
                                              0xE9D9,  // Diagnostics: Diagnostic
                                          });
  // room for the navigation rail, still inside the available screen width
  const int maxWidth = qMax(320, availableSize.width() - 48);
  setMinimumWidth(qMin(minimumWidth() + 248, maxWidth));
  resize(qMin(width() + 248, maxWidth), height());

  // Build less commonly used pages the first time they are opened. Plugins
  // scans installed plugins and Workarounds reads saved Steam credentials, so
  // keep both lazy but finish their setup before their first paint.
  m_tabFactories = {
      [this] { return std::make_unique<GeneralSettingsTab>(m_settings, *this); },
      [this] { return std::make_unique<ThemeSettingsTab>(m_settings, *this); },
      [this] { return std::make_unique<ModListSettingsTab>(m_settings, *this); },
      [this] { return std::make_unique<PathsSettingsTab>(m_settings, *this); },
      [this] { return std::make_unique<NexusSettingsTab>(m_settings, *this); },
      [this] {
        return std::make_unique<PluginsSettingsTab>(m_settings, m_pluginContainer,
                                                    *this);
      },
      [this] {
        return std::make_unique<WorkaroundsSettingsTab>(m_settings, *this);
      },
      [this] {
        return std::make_unique<DiagnosticsSettingsTab>(m_settings, *this);
      },
  };
  m_tabs.resize(m_tabFactories.size());
  constexpr int PluginsTabIndex = 5;
  constexpr int WorkaroundsTabIndex = 6;
  connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == PluginsTabIndex || index == WorkaroundsTabIndex) {
      initializeTab(index);
      return;
    }

    // Let the dialog paint the selected page before building it. This keeps
    // other lazy pages from delaying Settings when first opened.
    QTimer::singleShot(0, this, [this, index] {
      if (ui->tabWidget->currentIndex() == index) {
        initializeTab(index);
      }
    });
  });
  // the Plugins page is built on its first visit and has no scroll area; its
  // full height would otherwise set the dialog's minimum height
  FluentSettings::wrapInScrollArea(ui->tabWidget->widget(PluginsTabIndex));
  initializeTab(ui->tabWidget->currentIndex());

  // These pages add circular headings and rebuild part of their layout at
  // runtime. Prepare them while Settings is hidden to avoid a visible jump on
  // the first visit.
  constexpr int ThemeTabIndex = 1;
  constexpr int ModListTabIndex = 2;
  constexpr int PathsTabIndex = 3;
  constexpr int NexusTabIndex = 4;
  constexpr int DiagnosticsTabIndex = 7;
  initializeTab(ThemeTabIndex);
  initializeTab(ModListTabIndex);
  initializeTab(PathsTabIndex);
  initializeTab(NexusTabIndex);
  initializeTab(DiagnosticsTabIndex);
}

void SettingsDialog::hideUnsupportedOptions()
{
  // options that only matter for games with plugins, archives or INI files
  // (the Bethesda games) are hidden when the managed game has none of these,
  // as for Elden Ring
  const MOBase::IPluginGame* game = m_settings.game().plugin();
  if (game == nullptr || m_pluginContainer == nullptr) {
    return;
  }

  auto& features         = m_pluginContainer->gameFeatures();
  const bool hasPlugins  = features.gameFeature<MOBase::GamePlugins>() != nullptr;
  const bool hasArchives = features.gameFeature<MOBase::DataArchives>() != nullptr;
  const bool hasInvalidation =
      features.gameFeature<MOBase::BSAInvalidation>() != nullptr;
  const bool hasUnmanaged = features.gameFeature<MOBase::UnmanagedMods>() != nullptr;

  // the Elden Ring plugin manages save isolation itself (Save Isolation tool)
  const bool pluginOwnsSaves =
      game->gameShortName().compare(QStringLiteral("eldenring"), Qt::CaseInsensitive) == 0;

  ui->localINIs->setVisible(!game->iniFiles().isEmpty());
  ui->localSaves->setVisible(!pluginOwnsSaves);
  ui->automaticArchiveInvalidation->setVisible(hasInvalidation);
  ui->groupBox_8->setVisible(!ui->localINIs->isHidden() || !ui->localSaves->isHidden() ||
                             !ui->automaticArchiveInvalidation->isHidden());

  ui->displayForeignBox->setVisible(hasUnmanaged);
  if (!hasPlugins) {
    ui->collapsibleSeparatorsHighlightLabel->setText(tr("Highlights"));
    ui->collapsibleSeparatorsHighlightFromBox->setToolTip(
        tr("When you select a collapsed separator, highlight the mods it contains."));
  }
  ui->forceEnableBox->setVisible(hasPlugins || hasArchives);
  ui->enableArchiveParsingBox->setVisible(hasArchives);
  ui->bsaDateBtn->setVisible(hasArchives && hasInvalidation);
  if (game->sortMechanism() == MOBase::IPluginGame::SortMechanism::NONE) {
    ui->diagnosticsLootGroup->hide();
    ui->diagnosticsPageSubtitle->setText(
        tr("Choose how much troubleshooting information MO2 collects. The "
           "recommended levels are suitable for everyday use."));
  }
}

void SettingsDialog::initializeTab(int index)
{
  if (index < 0 || static_cast<size_t>(index) >= m_tabs.size() || m_tabs[index]) {
    return;
  }

  m_tabs[index] = m_tabFactories[index]();
  FluentSettings::restyle(ui->tabWidget->widget(index));
  const QString activeStyle = m_settings.interface().styleName().value_or("");
  configureSettingsComboPopups(
      ui->tabWidget->widget(index),
      activeStyle.compare("Light.qss", Qt::CaseInsensitive) == 0 &&
          !FluentIcons::active());
}

PluginContainer* SettingsDialog::pluginContainer()
{
  return m_pluginContainer;
}

QWidget* SettingsDialog::parentWidgetForDialogs()
{
  if (isVisible()) {
    return this;
  } else {
    return parentWidget();
  }
}

void SettingsDialog::setExitNeeded(ExitFlags e)
{
  m_exit = e;
}

ExitFlags SettingsDialog::exitNeeded() const
{
  return m_exit;
}

int SettingsDialog::exec()
{
  GeometrySaver gs(m_settings, this);

  m_settings.widgets().restoreIndex(ui->tabWidget);

  auto ret = TutorableDialog::exec();

  m_settings.widgets().saveIndex(ui->tabWidget);

  if (ret == QDialog::Accepted) {
    for (auto&& tab : m_tabs) {
      if (tab) {
        tab->closing();
      }
    }

    // update settings for each tab
    for (std::unique_ptr<SettingsTab> const& tab : m_tabs) {
      if (tab) {
        tab->update();
      }
    }
  }

  return ret;
}

SettingsDialog::~SettingsDialog()
{
  disconnect(this);
  delete ui;
}

void SettingsDialog::accept()
{
  // The Paths page is loaded lazily. Its line edits are empty until that page
  // is opened, so comparing them unconditionally falsely warns when the user
  // only changed a theme, language, or another unrelated setting.
  constexpr std::size_t PathsTabIndex = 3;
  if (m_tabs.size() > PathsTabIndex && m_tabs[PathsTabIndex]) {
    QString newModPath = ui->modDirEdit->text();
    newModPath = PathSettings::resolve(newModPath, ui->baseDirEdit->text());

    if ((QDir::fromNativeSeparators(newModPath) !=
         QDir::fromNativeSeparators(Settings::instance().paths().mods(true))) &&
        (QMessageBox::question(
             parentWidgetForDialogs(), tr("Confirm"),
             tr("Changing the mod directory affects all your profiles! "
                "Mods not present (or named differently) in the new location "
                "will be disabled in all profiles. "
                "There is no way to undo this unless you backed up your "
                "profiles manually. Proceed?"),
             QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)) {
      return;
    }
  }

  TutorableDialog::accept();
}

SettingsTab::SettingsTab(Settings& s, SettingsDialog& d)
    : ui(d.ui), m_settings(s), m_dialog(d)
{}

SettingsTab::~SettingsTab() = default;

Settings& SettingsTab::settings()
{
  return m_settings;
}

SettingsDialog& SettingsTab::dialog()
{
  return m_dialog;
}

QWidget* SettingsTab::parentWidget()
{
  return m_dialog.parentWidgetForDialogs();
}
