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

#include "baincomplexinstallerdialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringDecoder>
#include <QStyle>
#include <QVBoxLayout>

#include <set>

using namespace MOBase;

namespace
{

// package.txt as text: UTF-8 when it is valid UTF-8, else the system's code
// page, as BAIN packages were written on Windows
QString readPackageText(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  // a package.txt is a few kilobytes; a huge one is cut
  const QByteArray data = file.read(1024 * 1024);
  // a byte order mark says it (Notepad's "Unicode" is UTF-16)
  if (const auto bom = QStringConverter::encodingForData(data)) {
    QStringDecoder decoder(*bom);
    return decoder.decode(data);
  }
  QStringDecoder utf8(QStringDecoder::Utf8);
  QString text = utf8.decode(data);
  // a file cut at 1 MB may end inside a character: only an error before the
  // end sends it to the code page
  if (utf8.hasError() && file.atEnd()) {
    text = QString::fromLocal8Bit(data);
  }
  return text;
}

}  // namespace

BainComplexInstallerDialog::BainComplexInstallerDialog(
    const std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>& subpackages,
    const GuessedValue<QString>& modName, const QStringList& defaultOptions,
    const QString& packageTXT, QWidget* parent)
    : RevampedFluent::ContentDialog(
          parent, QStringLiteral("BainComplexInstallerDialog"),
          tr("BAIN package"),
          tr("This archive is a BAIN package, split into numbered parts. Choose the ones "
             "you want; a later part's files replace an earlier one's. Parts starting "
             "with 00 are usually the base, and the package.txt, when there is one, "
             "describes each part."))
{
  setHeading(tr("Choose the parts to install"));
  const QString packageText = packageTXT.isEmpty() ? QString() : readPackageText(packageTXT);
  setMinimumSize(packageText.isEmpty() ? 480 : 720, 480);
  resize(packageText.isEmpty() ? 540 : 900, 600);
  setSizeGripEnabled(true);
  setWindowFlag(Qt::WindowMaximizeButtonHint, true);

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

  auto* columns = new QHBoxLayout;
  columns->setSpacing(16);

  auto* left = new QVBoxLayout;
  left->setSpacing(8);
  m_parts = new QListWidget(this);
  m_parts->setObjectName(QStringLiteral("optionsList"));
  left->addWidget(RevampedFluent::fieldLabel(tr("Parts"), m_parts, this));
  m_parts->setSelectionMode(QAbstractItemView::SingleSelection);
  m_parts->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_parts->setWordWrap(true);
  m_parts->setResizeMode(QListView::Adjust);
  for (const auto& subpackage : subpackages) {
    const QString name = subpackage->name();
    auto* item         = new QListWidgetItem(name, m_parts);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setToolTip(name);
    item->setCheckState(name.startsWith(QStringLiteral("00")) ||
                                defaultOptions.contains(name, Qt::CaseInsensitive)
                            ? Qt::Checked
                            : Qt::Unchecked);
  }
  left->addWidget(m_parts, 1);

  auto* selection = new QHBoxLayout;
  selection->setSpacing(8);
  auto* all  = RevampedFluent::contentButton(tr("Choose all"), this);
  auto* none = RevampedFluent::contentButton(tr("Choose none"), this);
  selection->addWidget(all);
  selection->addWidget(none);
  selection->addStretch(1);
  left->addLayout(selection);
  // its own line: it wraps, and turns to the error tone when nothing is chosen
  m_count = new QLabel(this);
  m_count->setWordWrap(true);
  left->addWidget(m_count);
  columns->addLayout(left, 2);

  QPlainTextEdit* notes = nullptr;
  if (!packageText.isEmpty()) {
    auto* right = new QVBoxLayout;
    right->setSpacing(8);
    notes = new QPlainTextEdit(this);
    notes->setObjectName(QStringLiteral("bainPackageText"));
    notes->setReadOnly(true);
    notes->setPlainText(packageText);
    notes->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    right->addWidget(RevampedFluent::fieldLabel(tr("package.txt"), notes, this));
    right->addWidget(notes, 1);
    columns->addLayout(right, 3);
  }
  body()->addLayout(columns, 1);

  m_install    = addButton(tr("Install"), true);
  auto* manual = addButton(tr("Install manually"));
  manual->setToolTip(tr("Choose which of the archive's files to install, and where"));
  auto* cancel = addButton(tr("Cancel"));

  auto setAll = [this](Qt::CheckState state) {
    for (int i = 0; i < m_parts->count(); ++i) {
      m_parts->item(i)->setCheckState(state);
    }
  };
  connect(all, &QPushButton::clicked, this, [setAll] {
    setAll(Qt::Checked);
  });
  connect(none, &QPushButton::clicked, this, [setAll] {
    setAll(Qt::Unchecked);
  });
  connect(m_parts, &QListWidget::itemChanged, this, [this] {
    updateState();
  });
  connect(m_install, &QPushButton::clicked, this, [this] {
    if (m_install->isEnabled()) {
      accept();
    }
  });
  connect(manual, &QPushButton::clicked, this, [this] {
    m_Manual = true;
    reject();
  });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_name, &QComboBox::editTextChanged, this, [this] {
    m_nameEdited = true;
    updateState();
  });

  setTabOrder(m_name, m_parts);
  setTabOrder(m_parts, all);
  setTabOrder(all, none);
  QWidget* last = none;
  if (notes != nullptr) {
    setTabOrder(none, notes);
    last = notes;
  }
  setTabOrder(last, m_install);
  setTabOrder(m_install, manual);
  setTabOrder(manual, cancel);
  if (m_parts->count() > 0) {
    m_parts->setCurrentRow(0);
  }
  m_parts->setFocus();
  updateState();
}

QString BainComplexInstallerDialog::getName() const
{
  return m_name->currentText().trimmed();
}

void BainComplexInstallerDialog::updateState()
{
  int checked = 0;
  for (int i = 0; i < m_parts->count(); ++i) {
    if (m_parts->item(i)->checkState() == Qt::Checked) {
      ++checked;
    }
  }
  m_count->setText(checked == 0
                       ? tr("Choose at least one part to install.")
                       : tr("%1 of %2 parts chosen").arg(checked).arg(m_parts->count()));
  m_count->setObjectName(checked == 0 ? QStringLiteral("instanceNameValidation")
                                      : QStringLiteral("fluentSettingDescription"));
  m_count->style()->unpolish(m_count);
  m_count->style()->polish(m_count);
  RevampedFluent::setDescription(m_parts, m_count->text());

  const bool emptyName = getName().isEmpty();
  m_nameProblem->setVisible(emptyName && m_nameEdited);
  m_name->setAccessibleDescription(emptyName ? m_nameProblem->text() : QString());
  // 2.5.2 installed an empty mod when no part was chosen
  m_install->setEnabled(!emptyName && checked > 0);
}

QStringList BainComplexInstallerDialog::updateTree(std::shared_ptr<IFileTree>& tree)
{
  std::set<QString, FileNameComparator> selectedNames;
  for (int i = 0; i < m_parts->count(); ++i) {
    QListWidgetItem* item = m_parts->item(i);
    if (item->checkState() == Qt::Checked) {
      selectedNames.insert(item->text());
    }
  }

  // a new tree with the chosen parts merged in the archive's order, so a later
  // part's files replace an earlier one's
  auto newTree = tree->createOrphanTree();
  for (auto& entry : *tree) {
    if (entry->isDir() && selectedNames.count(entry->name()) > 0) {
      newTree->merge(entry->astree());
    }
  }

  tree = newTree;

  return QStringList(selectedNames.begin(), selectedNames.end());
}
