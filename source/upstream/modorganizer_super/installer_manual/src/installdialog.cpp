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

#include "installdialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStyle>

using namespace MOBase;

namespace
{

// a name Windows accepts for a folder
bool validFolderName(const QString& name)
{
  static const QString forbidden = QStringLiteral("<>:\"/\\|?*");
  if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
      name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
    return false;
  }
  for (const QChar c : name) {
    if (c.unicode() < 32 || forbidden.contains(c)) {
      return false;
    }
  }
  // the device names Windows keeps, with or without an extension
  static const QStringList reserved{"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2",
                                    "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                    "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                    "LPT6", "LPT7", "LPT8", "LPT9"};
  return !reserved.contains(name.section(QLatin1Char('.'), 0, 0).trimmed(),
                            Qt::CaseInsensitive);
}

void setTone(QLabel* label, const char* tone)
{
  label->setProperty("wizardInfoBar", QString::fromLatin1(tone));
  label->style()->unpolish(label);
  label->style()->polish(label);
}

}  // namespace

InstallDialog::InstallDialog(
    std::shared_ptr<IFileTree> tree, const GuessedValue<QString>& modName,
    std::shared_ptr<const MOBase::ModDataChecker> modDataChecker,
    const QString& dataName, QWidget* parent)
    : RevampedFluent::ContentDialog(parent, QStringLiteral("InstallDialog"),
                                    tr("Manual installation")),
      m_Checker(modDataChecker), m_DataFolderName(dataName)
{
  setHeading(tr("Choose what to install"));
  setText(tr("Drag files and folders to arrange them, and clear the check boxes of "
             "what you don't want. What is under %1 goes into the game's data "
             "folder; to use another folder for it, select that folder and choose "
             "Use as %1.")
              .arg(this->dataName()));
  setMinimumSize(620, 520);
  resize(760, 640);
  setSizeGripEnabled(true);
  setWindowFlag(Qt::WindowMaximizeButtonHint, true);

  // the mod's name
  m_name = new QComboBox(this);
  m_name->setEditable(true);
  m_name->setInsertPolicy(QComboBox::NoInsert);
  body()->addWidget(RevampedFluent::fieldLabel(tr("Mod name"), m_name, this));
  for (const QString& variant : modName.variants()) {
    m_name->addItem(variant);
  }
  if (m_name->completer() != nullptr) {
    m_name->completer()->setCaseSensitivity(Qt::CaseSensitive);
  }
  // the likeliest guess; typed in when it isn't one of the variants (MO2 2.5.2
  // showed an empty name then)
  const int presetIndex = m_name->findText(modName);
  if (presetIndex >= 0) {
    m_name->setCurrentIndex(presetIndex);
  } else {
    m_name->setEditText(modName);
  }
  body()->addWidget(m_name);
  m_nameProblem = RevampedFluent::problemLabel(tr("Enter a name for the mod."), this);
  body()->addWidget(m_nameProblem);
  body()->addSpacing(8);

  // what was only in the right-click menu, for the keyboard too
  auto* actions = new QHBoxLayout;
  actions->setSpacing(8);
  m_setData   = RevampedFluent::contentButton(tr("Use as %1").arg(this->dataName()), this);
  m_setData->setToolTip(tr("Install the selected folder's content as the game's data "
                           "folder"));
  m_unsetData = RevampedFluent::contentButton(tr("Reset %1").arg(this->dataName()), this);
  m_unsetData->setToolTip(tr("Go back to the archive's top folder as %1")
                              .arg(this->dataName()));
  m_newFolder = RevampedFluent::contentButton(tr("New folder…"), this);
  m_newFolder->setToolTip(tr("Create a folder inside the selected folder, or beside the "
                             "selected file (Ctrl+Shift+N)"));
  m_newFolder->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
  m_open = RevampedFluent::contentButton(tr("Open"), this);
  m_open->setToolTip(tr("Open the selected file"));
  // read aloud in words, not as "less than data greater than"
  m_setData->setAccessibleName(tr("Use as the game data folder"));
  m_unsetData->setAccessibleName(tr("Reset the game data folder"));
  for (auto* button : {m_setData, m_unsetData, m_newFolder, m_open}) {
    actions->addWidget(button);
  }
  actions->addStretch(1);
  body()->addLayout(actions);

  // the archive's content
  m_Tree = new ArchiveTreeWidget(this);
  m_Tree->setObjectName(QStringLiteral("treeContent"));
  m_Tree->setAccessibleName(tr("Content of the archive"));
  m_Tree->setContextMenuPolicy(Qt::CustomContextMenu);
  m_Tree->setDragEnabled(true);
  m_Tree->setDragDropMode(QAbstractItemView::DragDrop);
  m_Tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_Tree->setHeaderHidden(true);
  m_Tree->setColumnCount(1);
  body()->addWidget(m_Tree, 1);

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  body()->addWidget(m_status);

  m_install    = addButton(tr("Install"), true);
  auto* cancel = addButton(tr("Cancel"));

  m_TreeRoot = new ArchiveTreeWidgetItem(tree);
  m_Tree->setup(m_DataFolderName);
  connect(m_Tree, &ArchiveTreeWidget::treeChanged, this, [this] {
    updateProblems();
    updateActions();
  });
  connect(m_Tree, &QWidget::customContextMenuRequested, this,
          &InstallDialog::showContextMenu);
  connect(m_Tree, &QTreeWidget::currentItemChanged, this, [this] {
    updateActions();
  });
  // assistive tools select an item without making it current: with one item
  // selected, the current one follows it, since the buttons act on it
  connect(m_Tree, &QTreeWidget::itemSelectionChanged, this, [this] {
    const auto selected = m_Tree->selectedItems();
    if (selected.size() == 1 && selected.first() != m_Tree->currentItem()) {
      // NoUpdate: with Ctrl held, a plain setCurrentItem toggled the selection
      m_Tree->setCurrentItem(selected.first(), 0, QItemSelectionModel::NoUpdate);
    }
    updateActions();
  });
  m_Tree->setDataRoot(m_TreeRoot);
  // read aloud as words, not as "less than data greater than"
  m_Tree->root()->setData(0, Qt::AccessibleTextRole, tr("Game data folder"));

  connect(m_setData, &QPushButton::clicked, this, [this] {
    if (auto* item = currentItem(); item != nullptr && item != m_Tree->root() &&
                                    item->entry()->isDir()) {
      m_Tree->setDataRoot(item);
    }
  });
  connect(m_unsetData, &QPushButton::clicked, this, [this] {
    m_Tree->setDataRoot(m_TreeRoot);
  });
  connect(m_newFolder, &QPushButton::clicked, this, [this] {
    createDirectoryUnder(folderFor(currentItem()));
  });
  connect(m_open, &QPushButton::clicked, this, [this] {
    if (auto* item = currentItem(); item != nullptr && item->entry()->isFile()) {
      emit openFile(item->entry().get());
    }
  });
  connect(m_install, &QPushButton::clicked, this, &InstallDialog::install);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_name, &QComboBox::editTextChanged, this, [this] {
    m_nameEdited = true;
    updateActions();
  });

  setTabOrder(m_name, m_Tree);
  setTabOrder(m_Tree, m_setData);
  setTabOrder(m_setData, m_unsetData);
  setTabOrder(m_unsetData, m_newFolder);
  setTabOrder(m_newFolder, m_open);
  setTabOrder(m_open, m_install);
  setTabOrder(m_install, cancel);
  m_Tree->setCurrentItem(m_Tree->root());
  m_name->setFocus();
  updateActions();
}

QString InstallDialog::dataName() const
{
  return QStringLiteral("<%1>").arg(m_DataFolderName);
}

QString InstallDialog::getModName() const
{
  return m_name->currentText().trimmed();
}

std::shared_ptr<MOBase::IFileTree> InstallDialog::getModifiedTree() const
{
  return m_Tree->root()->entry()->astree();
}

ArchiveTreeWidgetItem* InstallDialog::currentItem() const
{
  return static_cast<ArchiveTreeWidgetItem*>(m_Tree->currentItem());
}

bool InstallDialog::testForProblem()
{
  if (!m_Checker) {
    return true;
  }
  return m_Checker->dataLooksValid(m_Tree->root()->entry()->astree()) ==
         ModDataChecker::CheckReturn::VALID;
}

void InstallDialog::updateProblems()
{
  // the themes' info bar tones, instead of 2.5.2's dark yellow, green and red
  // text and borders
  if (!m_Checker) {
    m_status->setText(tr("This game's plugin can't check the content of %1, so MO2 "
                         "can't tell whether the mod will work.")
                          .arg(dataName()));
    setTone(m_status, "caution");
  } else if (testForProblem()) {
    m_status->setText(
        tr("The content of %1 looks right for this game.").arg(dataName()));
    setTone(m_status, "success");
  } else {
    m_status->setText(tr("The content of %1 doesn't look right for this game: the "
                         "mod will probably not work.")
                          .arg(dataName()));
    setTone(m_status, "critical");
  }
  RevampedFluent::setDescription(m_Tree, m_status->text());
}

void InstallDialog::updateActions()
{
  auto* item       = currentItem();
  const bool isDir = item != nullptr && item->entry() != nullptr && item->entry()->isDir();
  m_setData->setEnabled(isDir && item != m_Tree->root());
  m_unsetData->setEnabled(m_Tree->root()->entry() != m_TreeRoot->entry());
  m_newFolder->setEnabled(true);
  m_open->setEnabled(item != nullptr && item->entry() != nullptr &&
                     item->entry()->isFile());

  const bool emptyName = getModName().isEmpty();
  m_install->setEnabled(!emptyName);
  m_nameProblem->setVisible(emptyName && m_nameEdited);
  m_name->setAccessibleDescription(emptyName ? m_nameProblem->text() : QString());
}

ArchiveTreeWidgetItem* InstallDialog::folderFor(ArchiveTreeWidgetItem* item) const
{
  // a file's folder; the top of the tree when nothing is selected
  while (item != nullptr && (item->entry() == nullptr || !item->entry()->isDir())) {
    item = item->parent();
  }
  return item != nullptr ? item : m_Tree->root();
}

void InstallDialog::createDirectoryUnder(ArchiveTreeWidgetItem* item)
{
  if (item == nullptr || !item->entry()->isDir()) {
    return;
  }
  auto fileTree = item->entry()->astree();

  RevampedFluent::ContentDialog dialog(this, QStringLiteral("installNewFolderDialog"),
                                       tr("New folder"));
  dialog.setText(tr("The folder goes inside '%1'.")
                     .arg(item == m_Tree->root() ? dataName() : item->text(0)));
  auto* field = new QLineEdit(&dialog);
  dialog.body()->addWidget(RevampedFluent::fieldLabel(tr("Name"), field, &dialog));
  dialog.body()->addWidget(field);
  auto* problem = RevampedFluent::problemLabel(QString(), &dialog);
  dialog.body()->addWidget(problem);
  auto* create = dialog.addButton(tr("Create"), true);
  auto* cancel = dialog.addButton(tr("Cancel"));
  connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(create, &QPushButton::clicked, &dialog, [&] {
    if (create->isEnabled()) {
      dialog.accept();
    }
  });

  auto check = [&] {
    const QString name = field->text().trimmed();
    QString text;
    if (!name.isEmpty() && !validFolderName(name)) {
      text = tr("A folder name can't have any of these: < > : \" / \\ | ? *, nor end "
                "with a dot or a space, nor be a name Windows keeps (CON, NUL, COM1…).");
    } else if (!name.isEmpty() && fileTree->exists(name)) {
      text = tr("There is already a file or folder named '%1' here.").arg(name);
    }
    problem->setText(text);
    problem->setVisible(!text.isEmpty());
    field->setAccessibleDescription(text);
    create->setEnabled(!name.isEmpty() && text.isEmpty());
  };
  connect(field, &QLineEdit::textChanged, &dialog, check);
  check();
  field->setFocus();

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  item->setExpanded(true);
  auto* newItem = m_Tree->addDirectory(item, field->text().trimmed());
  m_Tree->setCurrentItem(newItem);
  m_Tree->scrollToItem(newItem);
}

void InstallDialog::showContextMenu(QPoint pos)
{
  auto* selectedItem = static_cast<ArchiveTreeWidgetItem*>(m_Tree->itemAt(pos));
  // the menu key or Shift+F10: the current item, below it
  if (selectedItem == nullptr && currentItem() != nullptr) {
    selectedItem = currentItem();
    const QRect rect = m_Tree->visualItemRect(selectedItem);
    pos              = rect.bottomLeft() + QPoint(rect.height(), 0);
  }
  if (selectedItem == nullptr) {
    return;
  }

  QMenu menu(this);

  if (selectedItem != m_Tree->root() && selectedItem->entry()->isDir()) {
    menu.addAction(tr("Use as %1").arg(dataName()), [this, selectedItem]() {
      m_Tree->setDataRoot(selectedItem);
    });
  }

  if (m_Tree->root()->entry() != m_TreeRoot->entry()) {
    menu.addAction(tr("Reset %1").arg(dataName()), [this]() {
      m_Tree->setDataRoot(m_TreeRoot);
    });
  }

  if (!menu.isEmpty()) {
    menu.addSeparator();
  }

  if (selectedItem->entry()->isDir()) {
    menu.addAction(tr("New folder…"), [this, selectedItem]() {
      createDirectoryUnder(selectedItem);
    });
  } else {
    menu.addAction(tr("Open"), [this, selectedItem]() {
      emit openFile(selectedItem->entry().get());
    });
  }
  menu.exec(m_Tree->viewport()->mapToGlobal(pos));
}

void InstallDialog::install()
{
  if (getModName().isEmpty()) {
    return;
  }
  if (!testForProblem()) {
    RevampedFluent::ContentDialog question(
        this, QStringLiteral("installLayoutQuestion"), tr("Install anyway?"),
        tr("The content of %1 doesn't look right for this game, so the mod will "
           "probably not work. Fix the layout in the tree first, or install it as "
           "it is.")
            .arg(dataName()));
    auto* anyway = question.addButton(tr("Install anyway"), true);
    auto* back   = question.addButton(tr("Go back"));
    connect(anyway, &QPushButton::clicked, &question, &QDialog::accept);
    connect(back, &QPushButton::clicked, &question, &QDialog::reject);
    question.askFirst(back);
    if (question.exec() != QDialog::Accepted) {
      return;
    }
  }
  accept();
}
