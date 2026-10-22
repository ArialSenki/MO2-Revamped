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

#include "editexecutablesdialog.h"
#include "fluentdialogs.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "menuutils.h"
#include "filedialogmemory.h"
#include "envshortcut.h"
#include "forcedloaddialog.h"
#include "modlist.h"
#include "organizercore.h"
#include "spawn.h"
#include "ui_editexecutablesdialog.h"

#include <QTimer>
#include <QAbstractButton>
#include <QAction>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>
#include <Shellapi.h>
#include <algorithm>
#include <utility.h>

using namespace MOBase;
using namespace MOShared;

namespace
{
void addCircularHeadingIcon(QBoxLayout* layout, QLabel* heading,
                            const QString& iconPath,
                            const QString& iconObjectName, int size)
{
  // Fluent v4 headings are plain text, like Windows 11 Settings
  if (layout == nullptr || heading == nullptr || FluentIcons::active()) {
    return;
  }

  const int index = layout->indexOf(heading);
  if (index < 0) {
    return;
  }

  layout->removeWidget(heading);
  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* icon = new QLabel(heading->parentWidget());
  icon->setObjectName(iconObjectName);
  icon->setAlignment(Qt::AlignCenter);
  icon->setFixedSize(size, size);
  const QPixmap pixmap = FluentIcons::icon(iconPath).pixmap(size / 2, size / 2);
  if (!pixmap.isNull()) {
    icon->setPixmap(pixmap);
  }

  row->addWidget(icon, 0, Qt::AlignVCenter);
  row->addWidget(heading, 1, Qt::AlignVCenter);
  layout->insertLayout(index, row);
}

void addExecutableGroupHeading(QGroupBox* group, const QString& text,
                               const QString& iconPath,
                               const QString& iconObjectName)
{
  if (group == nullptr) {
    return;
  }

  auto* layout = qobject_cast<QVBoxLayout*>(group->layout());
  if (layout == nullptr) {
    return;
  }

  group->setTitle(QString());
  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* icon = new QLabel(group);
  icon->setObjectName(iconObjectName);
  icon->setAlignment(Qt::AlignCenter);
  icon->setFixedSize(26, 26);
  // Fluent v4 headings are plain text, like Windows 11 Settings
  if (FluentIcons::active()) {
    icon->hide();
  }
  const QPixmap pixmap = FluentIcons::icon(iconPath).pixmap(14, 14);
  if (!pixmap.isNull()) {
    icon->setPixmap(pixmap);
  }

  auto* heading = new QLabel(text, group);
  heading->setObjectName("executableSectionTitle");
  row->addWidget(icon, 0, Qt::AlignVCenter);
  row->addWidget(heading, 0, Qt::AlignVCenter);
  row->addStretch(1);
  layout->insertLayout(0, row);
}

void styleExecutablePrompt(QMessageBox& prompt,
                           QMessageBox::StandardButton confirmButton)
{
  prompt.setObjectName("revampedExecutableActionPrompt");
  prompt.setWindowTitle("MO2 Revamped");
  prompt.setTextFormat(Qt::PlainText);

  if (QLabel* label = prompt.findChild<QLabel*>("qt_msgbox_label")) {
    label->setWordWrap(true);
  }
  if (QLabel* label = prompt.findChild<QLabel*>("qt_msgbox_informativelabel")) {
    label->setWordWrap(true);
  }

  if (QAbstractButton* button = prompt.button(confirmButton)) {
    button->setObjectName("executablePromptPrimaryButton");
  }
  if (QAbstractButton* button = prompt.button(QMessageBox::Cancel)) {
    button->setObjectName("executablePromptCancelButton");
  }
}

void showExecutableNotice(QWidget* parent, QMessageBox::Icon icon,
                          const QString& heading, const QString& details)
{
  QMessageBox prompt(icon, "MO2 Revamped", heading, QMessageBox::Ok, parent);
  prompt.setInformativeText(details);
  styleExecutablePrompt(prompt, QMessageBox::Ok);
  prompt.exec();
}
}  // namespace

class IgnoreChanges
{
public:
  IgnoreChanges(EditExecutablesDialog* d) : m_dialog(d)
  {
    m_dialog->m_settingUI = true;
  }

  ~IgnoreChanges() { m_dialog->m_settingUI = false; }

  IgnoreChanges(const IgnoreChanges&)            = delete;
  IgnoreChanges& operator=(const IgnoreChanges&) = delete;

private:
  EditExecutablesDialog* m_dialog;
};

EditExecutablesDialog::EditExecutablesDialog(OrganizerCore& oc, int sel,
                                             QWidget* parent)
    : TutorableDialog("EditExecutables", parent), ui(new Ui::EditExecutablesDialog),
      m_organizerCore(oc), m_originalExecutables(*oc.executablesList()),
      m_executablesList(*oc.executablesList()), m_settingUI(false)
{
  ui->setupUi(this);
  addCircularHeadingIcon(ui->headerLayout, ui->dialogHeading,
                         ":/MO/gui/mainwindow/executables.svg",
                         "executableHeaderIcon", 32);
  addExecutableGroupHeading(
      ui->launchGroup, tr("Program"),
      ":/MO/gui/mainwindow/files/executable.svg", "executableProgramIcon");
  addExecutableGroupHeading(
      ui->optionsGroup, tr("Optional launch behaviour"),
      ":/MO/gui/mainwindow/settings.svg", "executableOptionsIcon");
  ui->add->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/action-add-light.svg"));
  ui->add->setIconSize(QSize(16, 16));

  // Tool buttons keep their size hint by default; let both action rows span
  // the list width evenly instead of leaving uneven gaps.
  for (QToolButton* button : {ui->add, ui->remove, ui->up, ui->down, ui->reset}) {
    button->setSizePolicy(QSizePolicy::Expanding, button->sizePolicy().verticalPolicy());
    button->setIconSize(QSize(16, 16));
  }
  if (ui->actionsSpacer != nullptr) {
    ui->orderActionsLayout->removeItem(ui->actionsSpacer);
    delete ui->actionsSpacer;
    ui->actionsSpacer = nullptr;
  }
  ui->browseBinary->setIcon(FluentIcons::icon(":/MO/gui/mainwindow/files/executable.svg"));
  ui->browseWorkingDirectory->setIcon(
      FluentIcons::icon(":/MO/gui/mainwindow/files/folder.svg"));
  ui->configureLibraries->setIcon(
      FluentIcons::icon(":/MO/gui/mainwindow/files/library.svg"));
  if (auto* button = ui->buttons->button(QDialogButtonBox::Ok)) {
    button->setObjectName("executableAcceptButton");
  }
  if (auto* button = ui->buttons->button(QDialogButtonBox::Apply)) {
    button->setObjectName("executableApplyButton");
  }
  if (auto* button = ui->buttons->button(QDialogButtonBox::Cancel)) {
    button->setObjectName("executableCancelButton");
  }
  ui->splitter->setSizes({265, 1});
  ui->splitter->setStretchFactor(0, 0);
  ui->splitter->setStretchFactor(1, 1);

  loadCustomOverwrites();
  loadForcedLibraries();

  QStringList modNames;

  for (auto&& m : m_organizerCore.modList()->allMods()) {
    auto mod = ModInfo::getByName(m);
    if (!mod->hasAnyOfTheseFlags({ModInfo::FLAG_FOREIGN, ModInfo::FLAG_BACKUP,
                                  ModInfo::FLAG_OVERWRITE, ModInfo::FLAG_SEPARATOR})) {
      ui->mods->addItem(m);
      modNames.push_back(m);
    }
  }

  auto* c = new QCompleter(modNames);
  c->setCaseSensitivity(Qt::CaseInsensitive);
  c->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  ui->mods->setCompleter(c);

  fillList();
  setDirty(false);

  if (sel >= 0 && sel < ui->list->count()) {
    selectIndex(sel);
  }

  auto* m = new QMenu(ui->add);
  m->setObjectName("executableAddMenu");
  auto* addFromFileAction = m->addAction(tr("Add from file..."));
  auto* addEmptyAction    = m->addAction(tr("Add empty"));
  auto* cloneAction       = m->addAction(tr("Clone selected"));
  cloneAction->setEnabled(selectedExe() != nullptr);
  connect(addFromFileAction, &QAction::triggered, this,
          [this]() { addFromFile(); });
  connect(addEmptyAction, &QAction::triggered, this,
          [this]() { addEmpty(); });
  connect(cloneAction, &QAction::triggered, this, [this]() { clone(); });
  connect(ui->list, &QListWidget::itemSelectionChanged, cloneAction,
          [this, cloneAction]() { cloneAction->setEnabled(selectedExe() != nullptr); });
  ui->add->setMenu(m);

  // some widgets need to do more than just save() and have their own handler
  connect(ui->binary, &QLineEdit::textChanged, [&] {
    save();
  });
  connect(ui->workingDirectory, &QLineEdit::textChanged, [&] {
    save();
  });
  connect(ui->arguments, &QLineEdit::textChanged, [&] {
    save();
  });
  connect(ui->steamAppID, &QLineEdit::textChanged, [&] {
    save();
  });
  connect(ui->mods, &QComboBox::currentTextChanged, [&] {
    save();
  });
  connect(ui->useApplicationIcon, &QCheckBox::toggled, [&] {
    save();
  });
  connect(ui->hide, &QCheckBox::toggled, [&] {
    save();
  });
  connect(ui->list->model(), &QAbstractItemModel::rowsMoved, [&] {
    saveOrder();
  });

  if (FluentIcons::active()) {
    buildFluentLayout();
  }
}

EditExecutablesDialog::~EditExecutablesDialog() = default;

int EditExecutablesDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);

  auto* targetScreen = screen();
  if (!targetScreen) {
    targetScreen = QGuiApplication::primaryScreen();
  }

  if (targetScreen) {
    const auto available = targetScreen->availableGeometry();
    const QSize maxReasonableSize(
        std::min(1280, std::max(800, available.width() - 64)),
        std::min(900, std::max(580, available.height() - 80)));

    if (width() > maxReasonableSize.width() ||
        height() > maxReasonableSize.height()) {
      const QSize compactSize = QSize(920, 700).boundedTo(maxReasonableSize);
      resize(compactSize);
      QDialog::move(available.center() - rect().center());
    }
  }

  return QDialog::exec();
}

void EditExecutablesDialog::loadCustomOverwrites()
{
  const auto* p = m_organizerCore.currentProfile();

  for (const auto& e : m_executablesList) {
    const auto s = p->setting("custom_overwrites", e.title()).toString();

    if (!s.isEmpty()) {
      m_customOverwrites.set(e.title(), true, s);
    }
  }
}

void EditExecutablesDialog::loadForcedLibraries()
{
  const auto* p = m_organizerCore.currentProfile();

  for (const auto& e : m_executablesList) {
    m_forcedLibraries.set(e.title(), p->forcedLibrariesEnabled(e.title()),
                          p->determineForcedLibraries(e.title()));
  }
}

ExecutablesList EditExecutablesDialog::getExecutablesList() const
{
  ExecutablesList newList;

  // make sure the executables are in the same order as in the list
  for (int i = 0; i < ui->list->count(); ++i) {
    const auto& title = ui->list->item(i)->text();

    auto itor = m_executablesList.find(title);

    if (itor == m_executablesList.end()) {
      log::warn("getExecutablesList(): executable '{}' not found", title);
      continue;
    }

    newList.setExecutable(*itor);
  }

  return newList;
}

const EditExecutablesDialog::CustomOverwrites&
EditExecutablesDialog::getCustomOverwrites() const
{
  return m_customOverwrites;
}

const EditExecutablesDialog::ForcedLibraries&
EditExecutablesDialog::getForcedLibraries() const
{
  return m_forcedLibraries;
}

bool EditExecutablesDialog::checkOutputMods(const ExecutablesList& exes)
{
  // make sure the output mods for exes exist since the combobox is editable
  //
  // it'd be convenient for users to automatically create mods here if they're
  // not found, but this is a can of worms: it would require a refresh
  // because getIndex() still won't find it after calling
  // OrganizerCore::createMod(), which is a problem if the user just clicked
  // Apply and continued doing things
  //
  // triggering a refresh while this dialog is up doesn't sound like a very
  // smart thing to do for now, so this just shows an error

  for (const auto& e : exes) {
    auto modName = m_customOverwrites.find(e.title());

    if (modName && modName->enabled) {
      if (modName->value.isEmpty()) {
        showExecutableNotice(
            this, QMessageBox::Warning, tr("Output mod is empty"),
            tr("Output-mod routing is enabled for \"%1\", but no mod is selected.")
                .arg(e.title()));

        return false;
      } else if (ModInfo::getIndex(modName->value) == UINT_MAX) {
        showExecutableNotice(
            this, QMessageBox::Warning, tr("Output mod not found"),
            tr("The mod \"%1\" assigned to \"%2\" does not exist.")
                .arg(modName->value, e.title()));

        return false;
      }
    }
  }

  return true;
}

bool EditExecutablesDialog::commitChanges()
{
  const auto newExecutables = getExecutablesList();

  if (!checkOutputMods(newExecutables)) {
    return false;
  }

  auto* profile = m_organizerCore.currentProfile();

  // remove all the custom overwrites and forced libraries
  for (const auto& e : m_originalExecutables) {
    profile->removeSetting("custom_overwrites", e.title());
    profile->removeForcedLibraries(e.title());
  }

  // set the new custom overwrites and forced libraries
  for (const auto& e : newExecutables) {
    if (auto modName = m_customOverwrites.find(e.title())) {
      if (modName && modName->enabled) {
        profile->storeSetting("custom_overwrites", e.title(), modName->value);
      }
    }

    if (auto libraryList = m_forcedLibraries.find(e.title())) {
      if (libraryList && !libraryList->value.empty()) {
        profile->setForcedLibrariesEnabled(e.title(), libraryList->enabled);
        profile->storeForcedLibraries(e.title(), libraryList->value);
      }
    }
  }

  // set the new executables list
  m_organizerCore.setExecutablesList(newExecutables);

  setDirty(false);

  return true;
}

void EditExecutablesDialog::setDirty(bool b)
{
  if (auto* button = ui->buttons->button(QDialogButtonBox::Apply)) {
    button->setEnabled(b);
  }
}

void EditExecutablesDialog::selectIndex(int i)
{
  if (i >= 0 && i < ui->list->count()) {
    ui->list->selectionModel()->setCurrentIndex(ui->list->model()->index(i, 0),
                                                QItemSelectionModel::ClearAndSelect);
  }
}

QListWidgetItem* EditExecutablesDialog::selectedItem()
{
  const auto selection = ui->list->selectedItems();

  if (selection.empty()) {
    return nullptr;
  }

  return selection[0];
}

Executable* EditExecutablesDialog::selectedExe()
{
  auto* item = selectedItem();
  if (!item) {
    return nullptr;
  }

  const auto& title = item->text();
  auto itor         = m_executablesList.find(title);

  if (itor == m_executablesList.end()) {
    return nullptr;
  }

  return &*itor;
}

void EditExecutablesDialog::fillList()
{
  ui->list->clear();

  for (const auto& exe : m_executablesList) {
    ui->list->addItem(createListItem(exe));
  }

  // select the first one in the list, if any
  if (ui->list->count() > 0) {
    selectIndex(0);
  } else {
    updateUI(nullptr, nullptr);
  }
}

QListWidgetItem* EditExecutablesDialog::createListItem(const Executable& exe)
{
  auto* item = new QListWidgetItem(exe.title());
  refreshFluentItem(item);
  return item;
}

void EditExecutablesDialog::updateUI(const QListWidgetItem* item, const Executable* e)
{
  // the ui is currently being set, ignore changes
  IgnoreChanges c(this);

  if (e) {
    setEdits(*e);
  } else {
    clearEdits();
  }

  setButtons(item, e);
  refreshFluent();
}

void EditExecutablesDialog::setButtons(const QListWidgetItem* item, const Executable* e)
{
  // add and remove are always enabled

  if (item) {
    ui->up->setEnabled(canMove(item, -1));
    ui->down->setEnabled(canMove(item, +1));
  } else {
    ui->up->setEnabled(false);
    ui->down->setEnabled(false);
  }
}

void EditExecutablesDialog::clearEdits()
{
  ui->title->clear();
  ui->title->setEnabled(false);
  ui->binary->clear();
  ui->binary->setEnabled(false);
  ui->browseBinary->setEnabled(false);
  ui->workingDirectory->clear();
  ui->workingDirectory->setEnabled(false);
  ui->browseWorkingDirectory->setEnabled(false);
  ui->arguments->clear();
  ui->arguments->setEnabled(false);
  ui->overwriteSteamAppID->setEnabled(false);
  ui->overwriteSteamAppID->setChecked(false);
  ui->steamAppID->setEnabled(false);
  ui->steamAppID->clear();
  ui->createFilesInMod->setEnabled(false);
  ui->createFilesInMod->setChecked(false);
  ui->mods->setEnabled(false);
  ui->mods->setCurrentIndex(-1);
  ui->forceLoadLibraries->setEnabled(false);
  ui->forceLoadLibraries->setChecked(false);
  ui->configureLibraries->setEnabled(false);
  ui->useApplicationIcon->setEnabled(false);
  ui->useApplicationIcon->setChecked(false);
  ui->hide->setEnabled(false);
  ui->hide->setChecked(false);

  m_lastGoodTitle = "";
}

void EditExecutablesDialog::setEdits(const Executable& e)
{
  ui->title->setText(e.title());
  ui->binary->setText(QDir::toNativeSeparators(e.binaryInfo().filePath()));
  ui->workingDirectory->setText(QDir::toNativeSeparators(e.workingDirectory()));
  ui->arguments->setText(e.arguments());
  // long paths show their drive and first folders, like the instance manager
  ui->binary->setCursorPosition(0);
  ui->workingDirectory->setCursorPosition(0);
  ui->overwriteSteamAppID->setChecked(!e.steamAppID().isEmpty());
  ui->steamAppID->setEnabled(!e.steamAppID().isEmpty());
  ui->steamAppID->setText(e.steamAppID());
  ui->useApplicationIcon->setChecked(e.usesOwnIcon());
  ui->hide->setChecked(e.hide());

  m_lastGoodTitle = e.title();

  {
    int modIndex = -1;

    const auto modName = m_customOverwrites.find(e.title());

    if (modName && !modName->value.isEmpty()) {
      modIndex = ui->mods->findText(modName->value);

      if (modIndex == -1) {
        log::warn("executable '{}' uses mod '{}' as a custom overwrite, but that mod "
                  "doesn't exist",
                  e.title(), modName->value);
      }
    }

    const bool hasCustomOverwrites = (modName && modName->enabled);

    ui->createFilesInMod->setChecked(hasCustomOverwrites);
    ui->mods->setEnabled(hasCustomOverwrites);
    ui->mods->setCurrentIndex(modIndex);
  }

  {
    const auto libraryList        = m_forcedLibraries.find(e.title());
    const bool hasForcedLibraries = (libraryList && libraryList->enabled);

    ui->forceLoadLibraries->setChecked(hasForcedLibraries);
    ui->configureLibraries->setEnabled(hasForcedLibraries);
  }

  // always enabled
  ui->title->setEnabled(true);
  ui->binary->setEnabled(true);
  ui->browseBinary->setEnabled(true);
  ui->workingDirectory->setEnabled(true);
  ui->browseWorkingDirectory->setEnabled(true);
  ui->arguments->setEnabled(true);
  ui->overwriteSteamAppID->setEnabled(true);
  ui->useApplicationIcon->setEnabled(true);
  ui->createFilesInMod->setEnabled(true);
  ui->forceLoadLibraries->setEnabled(true);
  ui->hide->setEnabled(true);
}

void EditExecutablesDialog::save()
{
  if (m_settingUI) {
    return;
  }

  auto* e = selectedExe();
  if (!e) {
    log::warn("trying to save but nothing is selected");
    return;
  }

  // title may have changed, start with the stuff using it

  // custom overwrites
  if (ui->createFilesInMod->isChecked()) {
    m_customOverwrites.set(e->title(), true, ui->mods->currentText());
  } else {
    m_customOverwrites.setEnabled(e->title(), false);
  }

  // forced libraries
  m_forcedLibraries.setEnabled(e->title(), ui->forceLoadLibraries->isChecked());

  // get the new title, but ignore it if it's conflicting with an already
  // existing executable
  QString newTitle = ui->title->text().trimmed();
  if (isTitleConflicting(newTitle)) {
    newTitle = e->title();
  }

  if (e->title() != newTitle) {
    // now rename both the custom overwrites and forced libraries if the title
    // is being changed
    m_customOverwrites.rename(e->title(), newTitle);
    m_forcedLibraries.rename(e->title(), newTitle);

    // save the new title
    e->title(newTitle);
  }

  e->binaryInfo(QFileInfo(ui->binary->text()));
  e->workingDirectory(ui->workingDirectory->text());
  e->arguments(ui->arguments->text());

  if (ui->overwriteSteamAppID->isChecked()) {
    e->steamAppID(ui->steamAppID->text());
  } else {
    e->steamAppID("");
  }

  if (ui->useApplicationIcon->isChecked()) {
    e->flags(e->flags() | Executable::UseApplicationIcon);
  } else {
    e->flags(e->flags() & (~Executable::UseApplicationIcon));
  }

  if (ui->hide->isChecked()) {
    e->flags(e->flags() | Executable::Hide);
  } else {
    e->flags(e->flags() & (~Executable::Hide));
  }

  setDirty(true);

  // the header checks the shortcut on disk and loads the icon: once after the
  // typing stops, not on every key
  if (!m_fluentRefreshQueued) {
    m_fluentRefreshQueued = true;
    QTimer::singleShot(200, this, [this] {
      m_fluentRefreshQueued = false;
      refreshFluent();
    });
  }
}

void EditExecutablesDialog::saveOrder()
{
  m_executablesList = getExecutablesList();
  setDirty(true);
}

bool EditExecutablesDialog::canMove(const QListWidgetItem* item, int direction)
{
  if (!item) {
    return false;
  }

  if (direction < 0) {
    // moving up
    return (ui->list->row(item) > 0);

  } else if (direction > 0) {
    // moving down
    return (ui->list->row(item) < (ui->list->count() - 1));
  }

  return false;
}

void EditExecutablesDialog::move(QListWidgetItem* item, int direction)
{
  if (!canMove(item, direction)) {
    return;
  }

  const auto oldRow = ui->list->row(item);
  const auto newRow = oldRow + (direction > 0 ? 1 : -1);

  // removing item
  ui->list->takeItem(oldRow);
  ui->list->insertItem(newRow, item);

  selectIndex(newRow);
  setDirty(true);
}

void EditExecutablesDialog::on_list_itemSelectionChanged()
{
  updateUI(selectedItem(), selectedExe());
}

void EditExecutablesDialog::on_reset_clicked()
{
  QMessageBox prompt(QMessageBox::Question, "MO2 Revamped",
                     tr("Restore the game-provided executables?"),
                     QMessageBox::Ok | QMessageBox::Cancel, this);
  prompt.setInformativeText(
      tr("MO2 will add the executables supplied by the game plugin. Existing "
         "entries with matching names will be renamed and kept."));
  styleExecutablePrompt(prompt, QMessageBox::Ok);
  if (QAbstractButton* button = prompt.button(QMessageBox::Ok)) {
    button->setText(tr("Restore"));
  }
  prompt.setDefaultButton(QMessageBox::Cancel);
  prompt.setEscapeButton(QMessageBox::Cancel);

  if (prompt.exec() != QMessageBox::Ok) {
    return;
  }

  m_executablesList.resetFromPlugin(m_organizerCore.managedGame());
  fillList();

  setDirty(true);
}

void EditExecutablesDialog::on_add_clicked()
{
  addFromFile();
}

void EditExecutablesDialog::on_remove_clicked()
{
  auto* item = selectedItem();
  if (!item) {
    log::warn("trying to remove entry but nothing is selected");
    return;
  }

  auto* exe = selectedExe();
  if (!exe) {
    log::warn("trying to remove entry but nothing is selected");
    return;
  }

  const int currentRow = ui->list->row(item);
  delete item;

  m_customOverwrites.remove(exe->title());
  m_forcedLibraries.remove(exe->title());

  // removing from main list, must be done last because it invalidates the
  // exe pointer
  m_executablesList.remove(exe->title());

  // reselecting the same row as before, or the last one
  if (currentRow >= ui->list->count()) {
    // that was the last item, select the new list item, if any
    if (ui->list->count() > 0) {
      selectIndex(ui->list->count() - 1);
    }
  } else {
    selectIndex(currentRow);
  }

  setDirty(true);
}

void EditExecutablesDialog::on_up_clicked()
{
  auto* item = selectedItem();
  if (!item) {
    return;
  }

  move(item, -1);
}

void EditExecutablesDialog::on_down_clicked()
{
  auto* item = selectedItem();
  if (!item) {
    return;
  }

  move(item, +1);
}

bool EditExecutablesDialog::isTitleConflicting(const QString& s)
{
  for (const auto& exe : m_executablesList) {
    if (exe.title() == s) {
      if (&exe != selectedExe()) {
        // found an executable that's not the current one with the same title
        return true;
      }
    }
  }

  return false;
}

void EditExecutablesDialog::on_title_textChanged(const QString& original)
{
  if (m_settingUI) {
    return;
  }

  auto s = original.trimmed();

  // disallow empty names
  if (s.isEmpty()) {
    return;
  }

  // disallow changing the title to something that already exists
  if (isTitleConflicting(s)) {
    return;
  }

  m_lastGoodTitle = s;

  // must save before modifying the item in the list widget because saving
  // relies on the item's text being the same as an item in m_executablesList
  save();

  // once the executable is saved, the list item must be changed to match the
  // new name
  if (auto* i = selectedItem()) {
    i->setText(s);
  }
  refreshFluent();
}

void EditExecutablesDialog::on_title_editingFinished()
{
  ui->title->setText(m_lastGoodTitle);
}

void EditExecutablesDialog::on_overwriteSteamAppID_toggled(bool checked)
{
  if (m_settingUI) {
    return;
  }

  ui->steamAppID->setEnabled(checked);
  save();
}

void EditExecutablesDialog::on_createFilesInMod_toggled(bool checked)
{
  if (m_settingUI) {
    return;
  }

  ui->mods->setEnabled(checked);
  save();

  if (checked) {
    ui->mods->lineEdit()->selectAll();
    ui->mods->setFocus();
  }
}

void EditExecutablesDialog::on_forceLoadLibraries_toggled(bool checked)
{
  if (m_settingUI) {
    return;
  }

  ui->configureLibraries->setEnabled(ui->forceLoadLibraries->isChecked());
  save();
}

void EditExecutablesDialog::on_browseBinary_clicked()
{
  const auto binaryName = browseBinary(ui->binary->text());
  if (binaryName.fileName().isEmpty()) {
    return;
  }

  setBinary(binaryName);
  save();
}

void EditExecutablesDialog::addFromFile()
{
  const auto binary = browseBinary(ui->binary->text());
  if (binary.fileName().isEmpty()) {
    return;
  }

  addNew(Executable(binary.completeBaseName()));
  setBinary(binary);
}

void EditExecutablesDialog::addEmpty()
{
  addNew(Executable(tr("New Executable")));
}

void EditExecutablesDialog::clone()
{
  auto* e = selectedExe();
  if (!e) {
    return;
  }

  addNew(*e);
}

void EditExecutablesDialog::addNew(Executable e)
{
  const auto fixedTitle = m_executablesList.makeNonConflictingTitle(e.title());
  if (!fixedTitle) {
    return;
  }

  e.title(*fixedTitle);

  m_executablesList.setExecutable(e);

  auto* item = createListItem(e);
  ui->list->addItem(item);

  selectIndex(ui->list->count() - 1);
  setDirty(true);
}

void EditExecutablesDialog::setBinary(const QFileInfo& binary)
{
  // setting binary
  if (binary.suffix().compare("jar", Qt::CaseInsensitive) == 0) {
    // special case for jar files, uses the system java installation
    setJarBinary(binary);
  } else {
    ui->binary->setText(QDir::toNativeSeparators(binary.absoluteFilePath()));
  }

  // setting title if some variation of "New Executable"
  if (ui->title->text().startsWith(tr("New Executable"), Qt::CaseInsensitive)) {
    const auto prefix   = binary.completeBaseName();
    const auto newTitle = m_executablesList.makeNonConflictingTitle(prefix);

    if (newTitle) {
      ui->title->setText(*newTitle);
    }
  }
}

void EditExecutablesDialog::on_browseWorkingDirectory_clicked()
{
  QString dirName = FileDialogMemory::getExistingDirectory(
      "editExecutableDirectory", this, tr("Select a directory"),
      ui->workingDirectory->text(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);

  if (dirName.isNull()) {
    // cancelled
    return;
  }

  ui->workingDirectory->setText(dirName);
}

void EditExecutablesDialog::on_configureLibraries_clicked()
{
  auto* e = selectedExe();
  if (!e) {
    log::warn("trying to configure libraries but nothing is selected");
    return;
  }

  ForcedLoadDialog dialog(m_organizerCore.managedGame(), this);

  if (auto libraryList = m_forcedLibraries.find(e->title())) {
    dialog.setValues(libraryList->value);
  }

  if (dialog.exec() == QDialog::Accepted) {
    m_forcedLibraries.setValue(e->title(), dialog.values());
    save();
  }
}

void EditExecutablesDialog::on_buttons_clicked(QAbstractButton* b)
{
  if (b == ui->buttons->button(QDialogButtonBox::Ok)) {
    if (commitChanges()) {
      accept();
    }
  } else if (b == ui->buttons->button(QDialogButtonBox::Apply)) {
    if (commitChanges()) {
      // a new or renamed program is saved now, so it can get a shortcut
      refreshFluent();
    }
  } else {
    reject();
  }
}

QFileInfo EditExecutablesDialog::browseBinary(const QString& initial)
{
  const QString Filters =
      tr("Executables (*.exe *.bat *.jar)") + ";;" + tr("All Files (*.*)");

  const auto f = FileDialogMemory::getOpenFileName(
      "editExecutableBinary", this, tr("Select an executable"), initial, Filters,
      nullptr, QFileDialog::DontUseNativeDialog);

  if (f.isNull()) {
    return {};
  }

  return QFileInfo(f);
}

void EditExecutablesDialog::setJarBinary(const QFileInfo& binary)
{
  auto java = spawn::findJavaInstallation(binary.absoluteFilePath());

  if (java.isEmpty()) {
    showExecutableNotice(
        this, QMessageBox::Information, tr("Java is required"),
        tr("MO needs Java to launch this JAR. If Java is already installed, "
           "browse to its javaw.exe file and select it as the binary."));
  }

  {
    // only save once
    IgnoreChanges c(this);

    ui->binary->setText(java);
    ui->workingDirectory->setText(QDir::toNativeSeparators(binary.absolutePath()));
    ui->arguments->setText("-jar \"" +
                           QDir::toNativeSeparators(binary.absoluteFilePath()) + "\"");
  }

  save();
}

void EditExecutablesDialog::buildFluentLayout()
{
  // the Designer layout stays for the classic themes; Fluent moves the same
  // widgets into the instance manager's list and details cards, so all the
  // handlers above keep working on them
  auto* root = qobject_cast<QVBoxLayout*>(layout());
  if (root == nullptr) {
    return;
  }

  ui->headerCard->hide();
  ui->splitter->hide();
  root->removeWidget(ui->headerCard);
  root->removeWidget(ui->splitter);
  root->removeWidget(ui->buttons);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* body = new QHBoxLayout;
  body->setSpacing(8);
  body->addWidget(createFluentListCard());
  body->addWidget(createFluentDetails(), 1);
  root->addLayout(body, 1);

  // help on the left, OK, Cancel and Apply on the right
  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  auto* help = new QToolButton(this);
  help->setObjectName(QStringLiteral("instanceHelpButton"));
  help->setText(tr("What can I add here?"));
  help->setIcon(FluentIcons::glyph(0xE897, FluentIcons::Tone::Muted));
  help->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  help->setAutoRaise(true);
  help->setCursor(Qt::PointingHandCursor);
  footer->addWidget(help);
  footer->addStretch(1);
  footer->addWidget(ui->buttons);
  root->addLayout(footer);
  connect(help, &QToolButton::clicked, this, &EditExecutablesDialog::showFluentHelp);

  setMinimumSize(900, 600);
  resize(std::max(width(), 1000), std::max(height(), 700));

  // only now: the refresh functions need the cards built above
  m_fluent = true;
  for (int i = 0; i < ui->list->count(); ++i) {
    refreshFluentItem(ui->list->item(i));
  }
  refreshFluent();
}

QWidget* EditExecutablesDialog::createFluentListCard()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("executableListCard"));
  card->setProperty("fluentCard", true);
  card->setFixedWidth(280);

  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Programs"), card);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_fluentCount = new QLabel(card);
  m_fluentCount->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_fluentCount);
  layout->addLayout(title);

  ui->list->setParent(card);
  ui->list->setItemDelegate(new FluentPanels::RowDelegate(ui->list, 56, 24));
  ui->list->setFrameShape(QFrame::NoFrame);
  ui->list->setMouseTracking(true);
  ui->list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  ui->list->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(ui->list, 1);
  ui->list->show();
  connect(ui->list, &QListWidget::customContextMenuRequested, this,
          &EditExecutablesDialog::showFluentListMenu);

  // Add keeps its menu (from a file, empty, a copy); restoring the game's
  // programs joins it, the order buttons move to the list's menu and drag
  auto* add = new QPushButton(FluentIcons::glyph(0xE710), tr("Add program"), card);
  add->setObjectName(QStringLiteral("executableAddButton"));
  add->setAutoDefault(false);
  if (QMenu* menu = ui->add->menu()) {
    menu->addSeparator();
    menu->addAction(tr("Restore the game's programs..."), this,
                    &EditExecutablesDialog::on_reset_clicked);
    add->setMenu(menu);
  }
  layout->addWidget(add);

  return card;
}

QWidget* EditExecutablesDialog::createFluentDetails()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("executableDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // the program's name and what it starts
  auto* head = new QHBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(12);
  m_fluentIcon = new QLabel(card);
  m_fluentIcon->setFixedSize(32, 32);
  head->addWidget(m_fluentIcon, 0, Qt::AlignVCenter);
  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  m_fluentName = new QLabel(card);
  m_fluentName->setObjectName(QStringLiteral("instanceDetailName"));
  m_fluentCaption = new QLabel(card);
  m_fluentCaption->setObjectName(QStringLiteral("instanceDetailCaption"));
  names->addWidget(m_fluentName);
  names->addWidget(m_fluentCaption);
  head->addLayout(names, 1);
  layout->addLayout(head);

  auto* scroll = new QScrollArea(card);
  scroll->setObjectName(QStringLiteral("instanceDetailsScroll"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* rows = new QWidget(scroll);
  rows->setObjectName(QStringLiteral("instanceDetailRows"));
  auto* rowsLayout = new QVBoxLayout(rows);
  rowsLayout->setContentsMargins(0, 0, 0, 0);
  rowsLayout->setSpacing(0);
  scroll->setWidget(rows);
  layout->addWidget(scroll, 1);

  // the browse buttons become glyph buttons beside their field
  ui->browseBinary->setAccessibleName(tr("Browse for the program"));
  ui->browseWorkingDirectory->setAccessibleName(tr("Browse for the start folder"));
  for (QPushButton* browse : {ui->browseBinary, ui->browseWorkingDirectory}) {
    browse->setText(QString());
    browse->setToolTip(browse->accessibleName());
    browse->setIcon(FluentIcons::glyph(0xE8DA));
    browse->setFixedWidth(40);
    browse->setProperty("fluentKeepIcon", true);
  }

  using FluentPanels::addEditRow;
  addEditRow(rows, 0xE8AC, tr("Title"), tr("The name in the Program list"), {ui->title});
  addEditRow(rows, 0xE756, tr("Program"), tr("The file Mod Organizer starts"),
             {ui->binary, ui->browseBinary});
  addEditRow(rows, 0xE8B7, tr("Start in"), tr("The folder the program runs from"),
             {ui->workingDirectory, ui->browseWorkingDirectory});
  addEditRow(rows, 0xE943, tr("Arguments"), tr("Passed to the program as it starts"),
             {ui->arguments});

  using FluentPanels::addSwitchRow;
  ui->steamAppIdLabel->hide();
  ui->steamAppID->setFixedWidth(140);
  ui->steamAppID->setPlaceholderText(tr("App ID"));
  addSwitchRow(rows, 0xE7FC, tr("Steam app ID"),
               tr("Starts the program as another Steam app, for tools that need it"),
               ui->overwriteSteamAppID, ui->steamAppID);
  ui->mods->setMinimumWidth(180);
  addSwitchRow(rows, 0xE74E, tr("Save new files to a mod"),
               tr("New files go here instead of Overwrite"),
               ui->createFilesInMod, ui->mods);
  ui->configureLibraries->setText(tr("Configure..."));
  addSwitchRow(rows, 0xE8F1, tr("Force load libraries"),
               tr("Loads extra DLLs into the program when it starts"),
               ui->forceLoadLibraries, ui->configureLibraries);
  addSwitchRow(rows, 0xE91B, tr("Use the program's icon"),
               tr("The Run button and shortcuts show the program's own icon"),
               ui->useApplicationIcon);
  addSwitchRow(rows, 0xED1A, tr("Hide from the Program list"),
               tr("Keeps the program here without listing it in the main window"),
               ui->hide);
  rowsLayout->addStretch(1);

  // duplicate and move on the left, remove on the right
  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  m_fluentDuplicate = new QPushButton(FluentIcons::glyph(0xE8C8), tr("Duplicate"), actions);
  m_fluentDuplicate->setAutoDefault(false);
  m_fluentRemove = new QPushButton(FluentIcons::glyph(0xE74D, FluentIcons::Tone::Critical),
                                   tr("Remove"), actions);
  m_fluentRemove->setAutoDefault(false);
  m_fluentRemove->setToolTip(tr("Remove the program from the list (Delete)"));
  m_fluentShortcut = new QPushButton(FluentIcons::glyph(0xE718), tr("Desktop shortcut"), actions);
  m_fluentShortcut->setAutoDefault(false);
  actionsLayout->addWidget(m_fluentDuplicate);
  actionsLayout->addWidget(m_fluentShortcut);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_fluentRemove);
  layout->addWidget(actions);

  connect(m_fluentDuplicate, &QPushButton::clicked, this, [this] {
    clone();
  });
  connect(m_fluentRemove, &QPushButton::clicked, this,
          &EditExecutablesDialog::on_remove_clicked);
  connect(m_fluentShortcut, &QPushButton::clicked, this, [this] {
    // the shortcut starts the saved program, so it uses the saved entry
    if (const Executable* exe = selectedExe()) {
      const ExecutablesList& committed = *m_organizerCore.executablesList();
      auto saved = committed.find(exe->title());
      if (saved != committed.end()) {
        env::Shortcut(*saved).toggle(env::Shortcut::Desktop);
        refreshFluent();
      }
    }
  });

  auto* del = new QShortcut(QKeySequence(Qt::Key_Delete), ui->list);
  del->setContext(Qt::WidgetShortcut);
  connect(del, &QShortcut::activated, this, [this] {
    if (selectedExe() != nullptr) {
      on_remove_clicked();
    }
  });

  return card;
}

void EditExecutablesDialog::refreshFluentItem(QListWidgetItem* item)
{
  if (!m_fluent || item == nullptr) {
    return;
  }

  auto itor = m_executablesList.find(item->text());
  if (itor == m_executablesList.end()) {
    return;
  }

  const Executable& exe = *itor;
  const QString file    = exe.binaryInfo().fileName();
  QString caption       = file.isEmpty() ? tr("No program file") : file;
  if (exe.hide()) {
    caption += QStringLiteral(" · ") + tr("Hidden");
  }
  item->setData(FluentPanels::CaptionRole, caption);
  item->setIcon(file.isEmpty() ? FluentIcons::glyph(0xE756, FluentIcons::Tone::Muted)
                               : iconForExecutable(exe.binaryInfo().filePath()));
}

void EditExecutablesDialog::refreshFluent()
{
  if (!m_fluent) {
    return;
  }

  m_fluentCount->setText(QString::number(ui->list->count()));

  const Executable* exe = selectedExe();
  m_fluentDuplicate->setEnabled(exe != nullptr);
  m_fluentRemove->setEnabled(exe != nullptr);

  // a shortcut needs the program saved under this title
  const ExecutablesList& committed = *m_organizerCore.executablesList();
  const bool saved = exe != nullptr && committed.find(exe->title()) != committed.end();
  const bool hasShortcut =
      saved && env::Shortcut(*committed.find(exe->title())).exists(env::Shortcut::Desktop);
  m_fluentShortcut->setEnabled(saved);
  m_fluentShortcut->setText(hasShortcut ? tr("Remove desktop shortcut")
                                        : tr("Desktop shortcut"));
  m_fluentShortcut->setToolTip(
      saved ? (hasShortcut ? tr("Delete the shortcut on the desktop")
                           : tr("Start this program with Mod Organizer from the desktop"))
            : tr("Choose OK or Apply first, so the shortcut finds the program"));
  if (exe == nullptr) {
    m_fluentName->setText(tr("No program selected"));
    m_fluentCaption->setText(tr("Add one, or pick one in the list"));
    m_fluentIcon->clear();
    return;
  }

  m_fluentName->setText(exe->title());
  const QString file = exe->binaryInfo().filePath();
  m_fluentCaption->setText(file.isEmpty()
                               ? tr("Choose the file it starts")
                               : QDir::toNativeSeparators(file));
  const QIcon icon = file.isEmpty() ? FluentIcons::glyph(0xE756, FluentIcons::Tone::Muted)
                                    : iconForExecutable(file);
  m_fluentIcon->setPixmap(icon.pixmap(32, 32));
  refreshFluentItem(selectedItem());
}

void EditExecutablesDialog::showFluentListMenu(const QPoint& pos)
{
  QListWidgetItem* item = ui->list->itemAt(pos);
  QMenu menu(this);

  if (item == nullptr) {
    menu.addAction(FluentIcons::glyph(0xE710), tr("Add from file..."), this,
                   [this] { addFromFile(); });
    menu.addAction(tr("Add empty"), this, [this] { addEmpty(); });
    menu.addSeparator();
    menu.addAction(FluentIcons::glyph(0xE777), tr("Restore the game's programs..."), this,
                   &EditExecutablesDialog::on_reset_clicked);
    menu.exec(ui->list->viewport()->mapToGlobal(pos));
    return;
  }

  ui->list->setCurrentItem(item);
  const Executable* exe = selectedExe();
  if (exe == nullptr) {
    return;
  }
  const QString file = exe->binaryInfo().filePath();

  menu.addAction(FluentIcons::glyph(0xE8C8), tr("Duplicate"), this, [this] { clone(); });
  auto* up = menu.addAction(FluentIcons::glyph(0xE74A), tr("Move up"), this,
                            &EditExecutablesDialog::on_up_clicked);
  up->setEnabled(canMove(item, -1));
  auto* down = menu.addAction(FluentIcons::glyph(0xE74B), tr("Move down"), this,
                              &EditExecutablesDialog::on_down_clicked);
  down->setEnabled(canMove(item, +1));
  menu.addSeparator();

  auto* show = menu.addAction(menuGlyph(MenuGlyphs::Folder), tr("Show in folder"), this,
                              [file] { shell::Explore(file); });
  show->setEnabled(!file.isEmpty() && QFileInfo::exists(file));
  addCopyMenu(&menu, {{tr("Title"), exe->title()},
                      {tr("Program path"), QDir::toNativeSeparators(file)},
                      {tr("Arguments"), exe->arguments()}});
  menu.addSeparator();
  // the key shows at the menu's right edge; the list's shortcut handles it
  menu.addAction(FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
                 tr("Remove") + QLatin1Char('	') +
                     QKeySequence(Qt::Key_Delete).toString(QKeySequence::NativeText),
                 this, &EditExecutablesDialog::on_remove_clicked);

  menu.exec(ui->list->viewport()->mapToGlobal(pos));
}

void EditExecutablesDialog::showFluentHelp()
{
  FluentContentDialog dlg(
      this, QStringLiteral("ExecutableHelpDialog"), tr("What can I add here?"),
      tr("Any program Mod Organizer should start with your mods in place: the game, its "
         "launcher, a script extender or tools like xEdit or BodySlide. Programs "
         "started from here see the virtual game folder, so they find the mods of "
         "the profile in use.\n\nThe programs that are not hidden appear in the "
         "main window's Program list, next to Run."));
  auto* ok = dlg.addButton(tr("OK"), true);
  connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  dlg.resize(520, dlg.sizeHint().height());
  dlg.exec();
}
