#include "forcedloaddialog.h"
#include "fluenticons.h"
#include "ui_forcedloaddialog.h"

#include "forcedloaddialogwidget.h"

#include "fluentdialogs.h"
#include "fluentpanels.h"
#include "menuutils.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QToolButton>
#include <algorithm>
#include <functional>
#include <QIcon>
#include <QComboBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{
void addRulesGroupHeading(QGroupBox* group)
{
  if (group == nullptr) {
    return;
  }

  auto* layout = qobject_cast<QVBoxLayout*>(group->layout());
  if (layout == nullptr) {
    return;
  }

  const QString title = group->title();
  group->setTitle(QString());

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* icon = new QLabel(group);
  icon->setObjectName(QStringLiteral("rulesSectionIcon"));
  icon->setAlignment(Qt::AlignCenter);
  icon->setFixedSize(26, 26);
  const QPixmap pixmap = FluentIcons::icon(QStringLiteral(":/MO/gui/mainwindow/files/library.svg"))
                             .pixmap(18, 18);
  if (!pixmap.isNull()) {
    icon->setPixmap(pixmap);
  }

  auto* heading = new QLabel(title, group);
  heading->setObjectName(QStringLiteral("rulesSectionTitle"));
  row->addWidget(icon, 0, Qt::AlignVCenter);
  row->addWidget(heading, 0, Qt::AlignVCenter);
  row->addStretch(1);
  layout->insertLayout(0, row);
}
}  // namespace

ForcedLoadDialog::ForcedLoadDialog(const IPluginGame* game, QWidget* parent)
    : QDialog(parent), ui(new Ui::ForcedLoadDialog), m_GamePlugin(game)
{
  ui->setupUi(this);
  addRulesGroupHeading(ui->rulesGroup);
  resize(860, 520);
  if (FluentIcons::active()) {
    buildFluentLayout();
  }
}

ForcedLoadDialog::~ForcedLoadDialog()
{
  delete ui;
}

void ForcedLoadDialog::setValues(QList<MOBase::ExecutableForcedLoadSetting>& values)
{
  ui->tableWidget->clearContents();

  for (int i = 0; i < values.count(); i++) {
    ForcedLoadDialogWidget* item = new ForcedLoadDialogWidget(m_GamePlugin, this);
    item->setEnabled(values[i].enabled());
    item->setProcess(values[i].process());
    item->setLibraryPath(values[i].library());
    item->setForced(values[i].forced());

    ui->tableWidget->insertRow(i);
    ui->tableWidget->setCellWidget(i, 0, item);
  }

  ui->tableWidget->resizeRowsToContents();
  refreshFluent();
  if (m_fluent && m_list->count() > 0) {
    m_list->setCurrentRow(0);
  }
}

QList<MOBase::ExecutableForcedLoadSetting> ForcedLoadDialog::values()
{
  QList<MOBase::ExecutableForcedLoadSetting> results;
  for (int row = 0; row < ui->tableWidget->rowCount(); row++) {
    auto widget = (ForcedLoadDialogWidget*)ui->tableWidget->cellWidget(row, 0);
    results.append(
        ExecutableForcedLoadSetting(widget->getProcess(), widget->getLibraryPath())
            .withEnabled(widget->getEnabled())
            .withForced(widget->getForced()));
  }
  return results;
}

void ForcedLoadDialog::on_addRowButton_clicked()
{
  addRule();
}

ForcedLoadDialogWidget* ForcedLoadDialog::addRule()
{
  int row = ui->tableWidget->rowCount();
  ui->tableWidget->insertRow(row);
  ForcedLoadDialogWidget* item = new ForcedLoadDialogWidget(m_GamePlugin, this);
  ui->tableWidget->setCellWidget(row, 0, item);
  ui->tableWidget->resizeRowsToContents();
  if (m_fluent) {
    // a new rule is on by default, like the game's own ones
    item->setEnabled(true);
    refreshFluent();
    m_list->setCurrentRow(row);
    m_library->setFocus();
  }
  return item;
}

void ForcedLoadDialog::on_deleteRowButton_clicked()
{
  // bottom up, so removing a row does not move the ones still to remove
  QList<int> rows;
  for (auto rowIndex : ui->tableWidget->selectionModel()->selectedRows()) {
    rows.append(rowIndex.row());
  }
  if (m_fluent && m_list->currentRow() >= 0) {
    rows = {m_list->currentRow()};
  }
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (int row : rows) {
    auto widget = (ForcedLoadDialogWidget*)ui->tableWidget->cellWidget(row, 0);
    if (widget != nullptr && !widget->getForced()) {
      ui->tableWidget->removeRow(row);
    }
  }
  if (m_fluent) {
    const int current = rows.isEmpty() ? 0 : rows.last();
    refreshFluent();
    m_list->setCurrentRow(std::min(current, m_list->count() - 1));
  }
}

void ForcedLoadDialog::buildFluentLayout()
{
  // the Designer layout stays for the classic themes; Fluent keeps the table
  // of rule widgets hidden and edits the selected one from the details card
  auto* root = qobject_cast<QVBoxLayout*>(layout());
  if (root == nullptr) {
    return;
  }
  ui->headerPanel->hide();
  ui->rulesGroup->hide();
  ui->addRowButton->hide();
  ui->deleteRowButton->hide();
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* body = new QHBoxLayout;
  body->setSpacing(8);

  // the rules: the library, and the program it goes into below it
  auto* listCard = new QFrame(this);
  listCard->setObjectName(QStringLiteral("forcedLoadListCard"));
  listCard->setProperty("fluentCard", true);
  listCard->setFixedWidth(280);
  auto* listLayout = new QVBoxLayout(listCard);
  listLayout->setContentsMargins(8, 10, 8, 8);
  listLayout->setSpacing(6);
  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Libraries"), listCard);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_count = new QLabel(listCard);
  m_count->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_count);
  listLayout->addLayout(title);

  m_list = new QListWidget(listCard);
  m_list->setObjectName(QStringLiteral("forcedLoadList"));
  m_list->setItemDelegate(new FluentPanels::RowDelegate(m_list, 56, 24));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setMouseTracking(true);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setContextMenuPolicy(Qt::CustomContextMenu);
  m_list->setAccessibleName(tr("Libraries"));
  listLayout->addWidget(m_list, 1);
  connect(m_list, &QListWidget::currentRowChanged, this, [this] {
    if (!m_fluentSyncing) {
      refreshFluent();
    }
  });
  connect(m_list, &QListWidget::customContextMenuRequested, this,
          &ForcedLoadDialog::showFluentListMenu);

  auto* add = new QPushButton(FluentIcons::glyph(0xE710), tr("Add library"), listCard);
  add->setAutoDefault(false);
  add->setToolTip(tr("Load another DLL into a program (Ctrl+N)"));
  connect(add, &QPushButton::clicked, this, [this] { addRule(); });
  listLayout->addWidget(add);
  body->addWidget(listCard);

  // the selected rule
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("forcedLoadDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(0, 0, 0, 0);
  cardLayout->setSpacing(0);
  auto* head = new QHBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(12);
  auto* glyph = new QLabel(QString(QChar(char16_t(0xE8F1))), card);
  glyph->setObjectName(QStringLiteral("instanceRowGlyph"));
  glyph->setFont(FluentIcons::font(24));
  glyph->setFixedSize(32, 32);
  glyph->setAlignment(Qt::AlignCenter);
  head->addWidget(glyph, 0, Qt::AlignVCenter);
  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  m_name = new QLabel(card);
  m_name->setObjectName(QStringLiteral("instanceDetailName"));
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  names->addWidget(m_name);
  names->addWidget(m_caption);
  head->addLayout(names, 1);
  cardLayout->addLayout(head);

  auto* scroll = new QScrollArea(card);
  scroll->setObjectName(QStringLiteral("instanceDetailsScroll"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_rows = new QWidget(scroll);
  m_rows->setObjectName(QStringLiteral("instanceDetailRows"));
  auto* rows = new QVBoxLayout(m_rows);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(0);
  scroll->setWidget(m_rows);
  cardLayout->addWidget(scroll, 1);
  m_empty = new QLabel(tr("No library is forced into a program yet. Add one to load "
                          "a DLL that a program does not load by itself."),
                       card);
  m_empty->setObjectName(QStringLiteral("instanceEmptyText"));
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setWordWrap(true);
  cardLayout->addWidget(m_empty, 1);

  m_enabled = new QCheckBox(m_rows);
  FluentPanels::addSwitchRow(m_rows, 0xE7E8, tr("Load this library"),
                             tr("A rule that is off stays saved but is not applied"),
                             m_enabled);
  m_library = new QLineEdit(m_rows);
  m_library->setPlaceholderText(tr("A DLL, relative to the game folder"));
  m_browseLibrary = new QPushButton(m_rows);
  m_process = new QLineEdit(m_rows);
  m_process->setPlaceholderText(tr("For example %1").arg(QStringLiteral("game.exe")));
  m_browseProcess = new QPushButton(m_rows);
  for (QPushButton* browse : {m_browseLibrary, m_browseProcess}) {
    browse->setIcon(FluentIcons::glyph(0xE8DA));
    browse->setFixedWidth(40);
    browse->setAutoDefault(false);
    browse->setProperty("fluentKeepIcon", true);
  }
  m_browseLibrary->setToolTip(tr("Browse for the library"));
  m_browseLibrary->setAccessibleName(m_browseLibrary->toolTip());
  m_browseProcess->setToolTip(tr("Browse for the program"));
  m_browseProcess->setAccessibleName(m_browseProcess->toolTip());
  FluentPanels::addEditRow(m_rows, 0xE8F1, tr("Library"),
                           tr("The DLL Mod Organizer loads into the program"),
                           {m_library, m_browseLibrary});
  FluentPanels::addEditRow(m_rows, 0xE756, tr("Program"),
                           tr("The process it is loaded into, by file name"),
                           {m_process, m_browseProcess});
  auto found = FluentPanels::addDetailRow(m_rows, 0xE946, tr("File"),
                                          tr("Show the library in its folder"), 0xE838);
  m_found = found.value;
  connect(found.action, &QToolButton::clicked, this, [this] {
    const QString file = libraryFile(selectedRule());
    if (!file.isEmpty() && QFileInfo::exists(file)) {
      shell::Explore(file);
    }
  });
  m_forcedNote = new QLabel(
      tr("The game plugin needs this library, so it cannot be changed or removed. "
         "You can still turn it off."),
      m_rows);
  m_forcedNote->setObjectName(QStringLiteral("fluentSettingDescription"));
  m_forcedNote->setWordWrap(true);
  m_forcedNote->setContentsMargins(18, 12, 18, 0);
  rows->addWidget(m_forcedNote);
  rows->addStretch(1);

  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  m_duplicate = new QPushButton(FluentIcons::glyph(0xE8C8), tr("Duplicate"), actions);
  m_duplicate->setAutoDefault(false);
  m_remove = new QPushButton(FluentIcons::glyph(0xE74D, FluentIcons::Tone::Critical),
                             tr("Remove"), actions);
  m_remove->setAutoDefault(false);
  m_remove->setToolTip(tr("Remove the rule (Delete)"));
  actionsLayout->addWidget(m_duplicate);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_remove);
  cardLayout->addWidget(actions);
  body->addWidget(card, 1);

  root->insertLayout(0, body, 1);

  // the fields write into the selected rule widget
  connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
    if (auto* rule = selectedRule(); rule != nullptr && !m_fluentSyncing) {
      rule->setEnabled(on);
      refreshFluent();
    }
  });
  connect(m_library, &QLineEdit::textEdited, this, [this](const QString& text) {
    if (auto* rule = selectedRule()) {
      rule->setLibraryPath(text);
      refreshFluent();
    }
  });
  connect(m_process, &QLineEdit::textEdited, this, [this](const QString& text) {
    if (auto* rule = selectedRule()) {
      rule->setProcess(text);
      refreshFluent();
    }
  });
  const auto browse = [this](const char* button) {
    if (auto* rule = selectedRule()) {
      if (auto* b = rule->findChild<QPushButton*>(QString::fromLatin1(button))) {
        b->click();
        refreshFluent();
      }
    }
  };
  connect(m_browseLibrary, &QPushButton::clicked, this, [browse] {
    browse("libraryPathBrowseButton");
  });
  connect(m_browseProcess, &QPushButton::clicked, this, [browse] {
    browse("processBrowseButton");
  });
  connect(m_duplicate, &QPushButton::clicked, this, [this] {
    if (auto* rule = selectedRule()) {
      const QString process = rule->getProcess();
      const QString library = rule->getLibraryPath();
      const bool enabled    = rule->getEnabled();
      auto* copy            = addRule();
      copy->setProcess(process);
      copy->setLibraryPath(library);
      copy->setEnabled(enabled);
      refreshFluent();
    }
  });
  connect(m_remove, &QPushButton::clicked, this, &ForcedLoadDialog::on_deleteRowButton_clicked);

  auto* newShortcut = new QShortcut(QKeySequence::New, this);
  connect(newShortcut, &QShortcut::activated, this, [this] { addRule(); });
  auto* del = new QShortcut(QKeySequence(Qt::Key_Delete), m_list);
  del->setContext(Qt::WidgetShortcut);
  connect(del, &QShortcut::activated, this, &ForcedLoadDialog::on_deleteRowButton_clicked);

  // help on the left of OK and Cancel
  if (auto* buttons = qobject_cast<QHBoxLayout*>(ui->horizontalLayout)) {
    auto* help = new QToolButton(this);
    help->setObjectName(QStringLiteral("instanceHelpButton"));
    help->setText(tr("When do I need this?"));
    help->setIcon(FluentIcons::glyph(0xE897, FluentIcons::Tone::Muted));
    help->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    help->setAutoRaise(true);
    help->setCursor(Qt::PointingHandCursor);
    buttons->insertWidget(0, help);
    connect(help, &QToolButton::clicked, this, [this] {
      FluentContentDialog dlg(
          this, QStringLiteral("ForcedLoadHelpDialog"), tr("When do I need this?"),
          tr("Some mods come as a DLL that the game does not load by itself, and some "
             "tools need a library loaded before they start. A rule here makes Mod "
             "Organizer load that DLL into the named program each time it starts from "
             "this entry.\n\nMost mods do not need it: script extenders and DLL "
             "loaders already load their plugins."));
      auto* ok = dlg.addButton(tr("OK"), true);
      connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
      dlg.resize(520, dlg.sizeHint().height());
      dlg.exec();
    });
  }

  setMinimumSize(860, 520);
  resize(960, 600);
  m_fluent = true;
  refreshFluent();
}

ForcedLoadDialogWidget* ForcedLoadDialog::selectedRule() const
{
  if (m_list == nullptr || m_list->currentRow() < 0) {
    return nullptr;
  }
  return qobject_cast<ForcedLoadDialogWidget*>(
      ui->tableWidget->cellWidget(m_list->currentRow(), 0));
}

QString ForcedLoadDialog::libraryFile(ForcedLoadDialogWidget* rule) const
{
  if (rule == nullptr || rule->getLibraryPath().trimmed().isEmpty()) {
    return {};
  }
  const QFileInfo info(rule->getLibraryPath().trimmed());
  return info.isAbsolute()
             ? info.absoluteFilePath()
             : QDir(m_GamePlugin->gameDirectory()).absoluteFilePath(info.filePath());
}

void ForcedLoadDialog::refreshFluent()
{
  if (!m_fluent) {
    return;
  }

  // one list row per table row, in the same order
  m_fluentSyncing = true;
  const int current = m_list->currentRow();
  while (m_list->count() > ui->tableWidget->rowCount()) {
    delete m_list->takeItem(m_list->count() - 1);
  }
  while (m_list->count() < ui->tableWidget->rowCount()) {
    m_list->addItem(new QListWidgetItem);
  }
  for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
    auto* rule = qobject_cast<ForcedLoadDialogWidget*>(ui->tableWidget->cellWidget(row, 0));
    if (rule == nullptr) {
      continue;
    }
    const QString library = QFileInfo(rule->getLibraryPath()).fileName();
    QStringList caption;
    caption << (rule->getProcess().isEmpty() ? tr("No program") : rule->getProcess());
    if (!rule->getEnabled()) {
      caption << tr("Off");
    }
    if (rule->getForced()) {
      caption << tr("Game plugin");
    }
    QListWidgetItem* item = m_list->item(row);
    item->setText(library.isEmpty() ? tr("New library") : library);
    item->setData(FluentPanels::CaptionRole, caption.join(QStringLiteral(" · ")));
    item->setIcon(FluentIcons::glyph(0xE8F1, rule->getEnabled() ? FluentIcons::Tone::Ink
                                                                 : FluentIcons::Tone::Muted));
  }
  m_list->setCurrentRow(std::min(current, m_list->count() - 1));
  m_count->setText(QString::number(m_list->count()));

  ForcedLoadDialogWidget* rule = selectedRule();
  const bool has               = rule != nullptr;
  m_rows->parentWidget()->parentWidget()->setVisible(has);
  m_empty->setVisible(!has);
  m_duplicate->setEnabled(has);
  m_remove->setEnabled(has && !rule->getForced());
  if (!has) {
    m_name->setText(tr("No library selected"));
    m_caption->setText(tr("Add one to load it into a program"));
    m_fluentSyncing = false;
    return;
  }

  const QString library = QFileInfo(rule->getLibraryPath()).fileName();
  m_name->setText(library.isEmpty() ? tr("New library") : library);
  m_caption->setText(rule->getProcess().isEmpty()
                         ? tr("Choose the program it goes into")
                         : tr("Loaded into %1").arg(rule->getProcess()));
  m_enabled->setChecked(rule->getEnabled());
  if (m_library->text() != rule->getLibraryPath()) {
    m_library->setText(rule->getLibraryPath());
  }
  if (m_process->text() != rule->getProcess()) {
    m_process->setText(rule->getProcess());
  }
  for (QWidget* w : std::initializer_list<QWidget*>{m_library, m_process, m_browseLibrary,
                                                    m_browseProcess}) {
    w->setEnabled(!rule->getForced());
  }
  m_forcedNote->setVisible(rule->getForced());

  const QString file = libraryFile(rule);
  m_found->setFullText(file.isEmpty()           ? tr("Choose the library first")
                   : QFileInfo::exists(file) ? QDir::toNativeSeparators(file)
                                             : tr("Not found: %1").arg(
                                                   QDir::toNativeSeparators(file)));
  m_fluentSyncing = false;
}

void ForcedLoadDialog::showFluentListMenu(const QPoint& pos)
{
  QListWidgetItem* item = m_list->itemAt(pos);
  QMenu menu(this);
  menu.addAction(FluentIcons::glyph(0xE710),
                 tr("Add library") + QLatin1Char('\t') +
                     QKeySequence(QKeySequence::New).toString(QKeySequence::NativeText),
                 this, [this] {
                   addRule();
                 });
  if (item != nullptr) {
    m_list->setCurrentItem(item);
    ForcedLoadDialogWidget* rule = selectedRule();
    if (rule == nullptr) {
      return;
    }
    menu.addAction(FluentIcons::glyph(0xE8C8), tr("Duplicate"), m_duplicate,
                   &QPushButton::click);
    menu.addAction(rule->getEnabled() ? tr("Turn off") : tr("Turn on"), this, [this, rule] {
      rule->setEnabled(!rule->getEnabled());
      refreshFluent();
    });
    menu.addSeparator();
    const QString file = libraryFile(rule);
    auto* show = menu.addAction(menuGlyph(MenuGlyphs::Folder), tr("Show in folder"), this,
                                [file] {
                                  shell::Explore(file);
                                });
    show->setEnabled(!file.isEmpty() && QFileInfo::exists(file));
    addCopyMenu(&menu, {{tr("Library path"), QDir::toNativeSeparators(file)},
                        {tr("Program"), rule->getProcess()}});
    menu.addSeparator();
    auto* remove = menu.addAction(
        FluentIcons::icon(":/MO/gui/contextmenu/remove.svg"),
        tr("Remove") + QLatin1Char('\t') +
            QKeySequence(Qt::Key_Delete).toString(QKeySequence::NativeText),
        this, &ForcedLoadDialog::on_deleteRowButton_clicked);
    remove->setEnabled(!rule->getForced());
  }
  menu.exec(m_list->viewport()->mapToGlobal(pos));
}
