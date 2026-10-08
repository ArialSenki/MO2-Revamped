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
#include "settingsdialogdiagnostics.h"
#include "settingsdialoggeneral.h"
#include "settingsdialogmodlist.h"
#include "settingsdialognexus.h"
#include "settingsdialogpaths.h"
#include "settingsdialogplugins.h"
#include "settingsdialogtheme.h"
#include "settingsdialogworkarounds.h"
#include "ui_settingsdialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QPalette>
#include <QTimer>

using namespace MOBase;

namespace
{

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
    popupView->setStyleSheet(
        "QAbstractItemView { color: #293847; background-color: transparent; "
        "border: none; border-radius: 7px; padding: 3px; outline: none; "
        "selection-background-color: #D7EAF9; "
        "selection-color: #183E5F; } "
        "QAbstractItemView::item { color: #293847; background-color: "
        "transparent; min-height: 22px; padding: 4px 8px; border: none; } "
        "QAbstractItemView::item:hover { background-color: #F0F6FB; "
        "color: #183E5F; } "
        "QAbstractItemView::item:selected { background-color: #D7EAF9; "
        "color: #183E5F; }");

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

  // Build a page the first time it is opened. The Plugins page enumerates
  // every installed plugin and resolves its settings and descriptions, which
  // is unnecessary work when the user only opens general settings.
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
  connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    // Let the dialog paint the selected page before building it. This keeps
    // opening Settings responsive even when the last-used page is Plugins.
    QTimer::singleShot(0, this, [this, index] {
      if (ui->tabWidget->currentIndex() == index) {
        initializeTab(index);
      }
    });
  });
  initializeTab(ui->tabWidget->currentIndex());
}

void SettingsDialog::initializeTab(int index)
{
  if (index < 0 || static_cast<size_t>(index) >= m_tabs.size() || m_tabs[index]) {
    return;
  }

  m_tabs[index] = m_tabFactories[index]();
  const QString activeStyle = m_settings.interface().styleName().value_or("");
  configureSettingsComboPopups(
      ui->tabWidget->widget(index),
      activeStyle.compare("Light.qss", Qt::CaseInsensitive) == 0);
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

QString SettingsDialog::getColoredButtonStyleSheet() const
{
  return QString("QPushButton {"
                 "background-color: %1;"
                 "color: %2;"
                 "border: 1px solid;"
                 "padding: 3px;"
                 "}");
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
