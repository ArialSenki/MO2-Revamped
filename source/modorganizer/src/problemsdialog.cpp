#include "problemsdialog.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "organizercore.h"
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <Shellapi.h>
#include <iplugin.h>
#include <iplugindiagnose.h>
#include <utility.h>

#include "eldenringsavesettings.h"
#include "plugincontainer.h"

using namespace MOBase;

namespace
{
// Segoe Fluent Icons code points
constexpr char16_t WarningGlyph  = 0xE7BA;
constexpr char16_t FolderGlyph   = 0xE8B7;
constexpr char16_t CheckGlyph    = 0xE73E;
constexpr char16_t RepairGlyph   = 0xE90F;
constexpr char16_t CopyGlyph     = 0xE8C8;
constexpr char16_t RefreshGlyph  = 0xE72C;

// item data of a notification row
constexpr int TextRole      = Qt::UserRole + 10;  // the full description
constexpr int PluginRole    = Qt::UserRole + 11;  // the IPluginDiagnose*
constexpr int KeyRole       = Qt::UserRole + 12;  // the problem's key
constexpr int FixRole       = Qt::UserRole + 13;  // has a guided fix
constexpr int OverwriteRole = Qt::UserRole + 14;  // about Overwrite

// diagnose_basic's "files in your Overwrite mod directory" problem
constexpr unsigned int BasicOverwriteProblem = 2;
}  // namespace

ProblemsDialog::ProblemsDialog(const PluginContainer& pluginContainer, QWidget* parent)
    : QDialog(parent), m_PluginContainer(pluginContainer), m_hasProblems(false),
      m_problemCount(0)
{
  setupUi();
  runDiagnosis();
}

ProblemsDialog::~ProblemsDialog() = default;

void ProblemsDialog::setupUi()
{
  setObjectName(QStringLiteral("ProblemsDialog"));
  setWindowTitle(tr("Notifications"));
  setMinimumSize(820, 500);
  resize(940, 600);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  auto* body = new QHBoxLayout;
  body->setSpacing(8);
  body->addWidget(createListCard());
  body->addWidget(createDetails(), 1);
  layout->addLayout(body, 1);

  // check again on the left, Close on the right
  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  auto* again = new QToolButton(this);
  again->setObjectName(QStringLiteral("instanceHelpButton"));
  again->setText(tr("Check again"));
  again->setToolTip(tr("Ask every plugin again whether something needs attention"));
  again->setIcon(FluentIcons::glyph(RefreshGlyph, FluentIcons::Tone::Muted));
  again->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  again->setAutoRaise(true);
  again->setCursor(Qt::PointingHandCursor);
  footer->addWidget(again);
  footer->addStretch(1);
  auto* close = new QPushButton(tr("Close"), this);
  close->setObjectName(QStringLiteral("close"));
  close->setAutoDefault(false);
  footer->addWidget(close);
  layout->addLayout(footer);

  connect(again, &QToolButton::clicked, this, &ProblemsDialog::runDiagnosis);
  connect(close, &QPushButton::clicked, this, &QDialog::close);
}

QWidget* ProblemsDialog::createListCard()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("notificationListCard"));
  card->setProperty("fluentCard", true);
  card->setFixedWidth(300);

  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Notifications"), card);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_count = new QLabel(card);
  m_count->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_count);
  layout->addLayout(title);

  m_list = new QListWidget(card);
  m_list->setObjectName(QStringLiteral("problemsWidget"));
  m_list->setItemDelegate(new FluentPanels::RowDelegate(m_list, 56, 20));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setMouseTracking(true);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setAccessibleName(tr("Notifications"));
  layout->addWidget(m_list, 1);

  connect(m_list, &QListWidget::itemSelectionChanged, this,
          &ProblemsDialog::selectionChanged);
  // Enter or a double-click runs the fix when the notification has one
  connect(m_list, &QListWidget::itemActivated, this, &ProblemsDialog::startFix);

  return card;
}

QWidget* ProblemsDialog::createDetails()
{
  m_details = new QStackedWidget(this);
  m_details->setObjectName(QStringLiteral("notificationDetailsStack"));

  // nothing to review: a check mark and one line, like Windows Update
  auto* empty = new QFrame(m_details);
  empty->setProperty("fluentCard", true);
  auto* emptyLayout = new QVBoxLayout(empty);
  emptyLayout->addStretch(1);
  auto* check = new QLabel(QString(QChar(CheckGlyph)), empty);
  check->setFont(FluentIcons::font(40));
  check->setAlignment(Qt::AlignCenter);
  QPalette palette = check->palette();
  palette.setColor(QPalette::WindowText, FluentIcons::color(FluentIcons::Tone::Success));
  check->setPalette(palette);
  auto* caughtUp = new QLabel(tr("You're all caught up"), empty);
  caughtUp->setObjectName(QStringLiteral("instanceDetailName"));
  caughtUp->setAlignment(Qt::AlignCenter);
  auto* caughtUpText =
      new QLabel(tr("No plugin has anything for you to review right now."), empty);
  caughtUpText->setObjectName(QStringLiteral("instanceDetailCaption"));
  caughtUpText->setAlignment(Qt::AlignCenter);
  caughtUpText->setWordWrap(true);
  emptyLayout->addWidget(check);
  emptyLayout->addSpacing(8);
  emptyLayout->addWidget(caughtUp);
  emptyLayout->addWidget(caughtUpText);
  emptyLayout->addStretch(1);
  m_details->addWidget(empty);

  auto* card = new QFrame(m_details);
  card->setObjectName(QStringLiteral("notificationDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // the notification and the plugin that raised it
  auto* head = new QVBoxLayout;
  head->setContentsMargins(18, 14, 18, 14);
  head->setSpacing(2);
  m_title = new QLabel(card);
  m_title->setObjectName(QStringLiteral("instanceDetailName"));
  m_title->setWordWrap(true);
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  head->addWidget(m_title);
  head->addWidget(m_caption);
  layout->addLayout(head);

  // what it means for the user, then the plugin's own text
  auto* text = new QFrame(card);
  text->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* textLayout = new QVBoxLayout(text);
  textLayout->setContentsMargins(18, 12, 18, 12);
  textLayout->setSpacing(10);
  m_context = new QLabel(text);
  m_context->setObjectName(QStringLiteral("fluentSettingDescription"));
  m_context->setWordWrap(true);
  m_description = new QTextBrowser(text);
  m_description->setObjectName(QStringLiteral("descriptionText"));
  m_description->setFrameShape(QFrame::NoFrame);
  m_description->setOpenLinks(false);
  m_description->setLineWrapMode(QTextEdit::WidgetWidth);
  // reading text, not a field: no box around it
  m_description->viewport()->setAutoFillBackground(false);
  m_description->setStyleSheet(
      QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
  textLayout->addWidget(m_context);
  textLayout->addWidget(m_description, 1);
  layout->addWidget(text, 1);

  // what can be done about it
  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  m_fix = new QPushButton(FluentIcons::glyph(RepairGlyph), tr("Fix it"), actions);
  m_fix->setObjectName(QStringLiteral("fixButton"));
  m_fix->setProperty("fluentPrimary", true);
  m_fix->setAutoDefault(false);
  m_manageOverwrite =
      new QPushButton(FluentIcons::glyph(FolderGlyph), tr("Manage Overwrite"), actions);
  m_manageOverwrite->setObjectName(QStringLiteral("manageOverwriteButton"));
  m_manageOverwrite->setAutoDefault(false);
  m_copy = new QPushButton(FluentIcons::glyph(CopyGlyph), tr("Copy text"), actions);
  m_copy->setAutoDefault(false);
  m_copy->setToolTip(tr("Copy the notification, to paste it in a bug report or a post"));
  actionsLayout->addWidget(m_fix);
  actionsLayout->addWidget(m_manageOverwrite);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_copy);
  layout->addWidget(actions);

  connect(m_description, &QTextBrowser::anchorClicked, this, &ProblemsDialog::urlClicked);
  connect(m_fix, &QPushButton::clicked, this, &ProblemsDialog::startFix);
  connect(m_manageOverwrite, &QPushButton::clicked, this, [this]() {
    emit manageOverwriteRequested();
  });
  connect(m_copy, &QPushButton::clicked, this, &ProblemsDialog::copyText);

  m_details->addWidget(card);
  return m_details;
}

int ProblemsDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);
  return QDialog::exec();
}

void ProblemsDialog::runDiagnosis()
{
  m_hasProblems  = false;
  m_problemCount = 0;
  m_list->clear();

  for (IPluginDiagnose* diagnose : m_PluginContainer.plugins<IPluginDiagnose>()) {
    if (!m_PluginContainer.isEnabled(diagnose)) {
      continue;
    }

    const auto* plugin = dynamic_cast<const IPlugin*>(diagnose);
    // the basic diagnosis plugin has no translated name of its own
    const bool basic =
        plugin != nullptr && plugin->name() == QLatin1String("Basic diagnosis plugin");
    const QString source = plugin == nullptr ? QStringLiteral("Mod Organizer")
                           : basic           ? tr("Basic diagnosis")
                                             : plugin->localizedName();

    std::vector<unsigned int> activeProblems = diagnose->activeProblems();
    for (unsigned int key : activeProblems) {
      // the basic diagnosis plugin's Overwrite notice, recognized by its key
      // so translated texts are recognized too; other notices that merely
      // mention Overwrite keep their own text
      const bool isOverwrite = basic && key == BasicOverwriteProblem;
      // its title names Overwrite as the rest of MO2 does (the translations
      // called it "directorio mod Sobrescribir")
      const QString shortDescription = isOverwrite ? tr("There are files in Overwrite")
                                                   : diagnose->shortDescription(key);
      const QString fullDescription  = diagnose->fullDescription(key);

      auto* item = new QListWidgetItem(shortDescription, m_list);
      item->setData(FluentPanels::CaptionRole, source);
      item->setIcon(isOverwrite
                        ? FluentIcons::glyph(FolderGlyph, FluentIcons::Tone::Accent)
                        : FluentIcons::glyph(WarningGlyph, FluentIcons::Tone::Caution));
      item->setToolTip(shortDescription);
      item->setData(TextRole, fullDescription);
      item->setData(PluginRole, QVariant::fromValue(reinterpret_cast<void*>(diagnose)));
      item->setData(KeyRole, key);
      item->setData(FixRole, diagnose->hasGuidedFix(key));
      item->setData(OverwriteRole, isOverwrite);

      m_hasProblems = true;
      ++m_problemCount;
    }
  }

  m_count->setText(QString::number(m_problemCount));
  if (m_hasProblems) {
    m_list->setCurrentRow(0);
  }
  selectionChanged();
}

bool ProblemsDialog::hasProblems() const
{
  return m_hasProblems;
}

void ProblemsDialog::selectionChanged()
{
  QListWidgetItem* item = m_list->currentItem();
  if (item == nullptr || !m_hasProblems) {
    m_details->setCurrentIndex(0);
    return;
  }
  m_details->setCurrentIndex(1);

  const QString text     = item->data(TextRole).toString();
  const bool isOverwrite = item->data(OverwriteRole).toBool();

  m_title->setText(item->text());
  m_caption->setText(tr("From %1").arg(item->data(FluentPanels::CaptionRole).toString()));
  m_context->setText(
      isOverwrite
          ? tr("Files here are not assigned to a regular mod yet. Review them and move "
               "anything you want to keep into a mod. Deleting files here removes "
               "them from this instance.")
          : tr("Review the details below to understand this notification and choose an "
               "available action."));

  if (isOverwrite && EldenRingSaveSettings::supports(m_PluginContainer.managedGame())) {
    // the basic diagnosis text is written for the Bethesda games (Wrye Bash,
    // xEdit, ESL files); Elden Ring gets its own
    m_description->setHtml(
        tr("<p>Overwrite holds files that the game and tools created while running "
           "through MO2: logs and settings written by DLL mods, files saved by mod "
           "tools, and similar output. They do not belong to any mod yet.</p>"
           "<p>Files in <b>Overwrite</b> take priority over every mod in the profile. "
           "Move the files you want to keep into a mod: drag them from the Manage "
           "Overwrite window onto a mod, or right-click Overwrite in the mod list and "
           "create a mod from its contents. Delete what you no longer need, such as "
           "old logs.</p>"
           "<p>Startup options can clear old mod logs from Overwrite before each "
           "launch.</p>"));
  } else if (text.isEmpty()) {
    m_description->setPlainText(
        tr("The plugin that reported this notification did not provide additional "
           "details."));
  } else {
    m_description->setText(text);
  }
  m_fix->setVisible(item->data(FixRole).toBool());
  m_manageOverwrite->setVisible(isOverwrite);
}

void ProblemsDialog::startFix()
{
  QListWidgetItem* item = m_list->currentItem();
  if (item == nullptr || !item->data(FixRole).toBool()) {
    return;
  }

  IPluginDiagnose* plugin =
      reinterpret_cast<IPluginDiagnose*>(item->data(PluginRole).value<void*>());
  if (plugin == nullptr) {
    log::warn("notification fix has no diagnosis plugin");
    return;
  }

  const unsigned int key = item->data(KeyRole).toUInt();
  plugin->startGuidedFix(key);
  runDiagnosis();

  // stay on the same notification when it is still there
  for (int i = 0; i < m_list->count(); ++i) {
    QListWidgetItem* row = m_list->item(i);
    if (row->data(PluginRole).value<void*>() == reinterpret_cast<void*>(plugin) &&
        row->data(KeyRole).toUInt() == key) {
      m_list->setCurrentItem(row);
      break;
    }
  }
}

void ProblemsDialog::copyText()
{
  if (const QListWidgetItem* item = m_list->currentItem()) {
    QGuiApplication::clipboard()->setText(item->text() + QStringLiteral("\n\n") +
                                          m_description->toPlainText());
  }
}

void ProblemsDialog::urlClicked(const QUrl& url)
{
  shell::Open(url);
}
