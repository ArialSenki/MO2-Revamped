#include "collectiondialog.h"

#include "fluentdialogs.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "organizercore.h"
#include "profile.h"
#include "settings.h"

#include "revampedtheme.h"
#include <utility.h>

#include <QResizeEvent>
#include <QMoveEvent>
#include <QApplication>
#include <QButtonGroup>
#include <QGuiApplication>
#include <QRadioButton>
#include <QScreen>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace MOBase;
using namespace Collections;
using Status = CollectionSession::Status;
using Stage  = CollectionSession::Stage;

namespace
{

constexpr char16_t ProfileGlyph   = 0xE77B;  // Contact
constexpr char16_t SeparatorGlyph = 0xE8FD;  // BulletedList
constexpr char16_t NextGlyph      = 0xE8A7;  // OpenInNewWindow
constexpr char16_t InstanceGlyph  = 0xE8B7;  // Folder
constexpr char16_t CurrentGlyph   = 0xE73E;  // CheckMark

enum Column
{
  ColName = 0,
  ColVersion,
  ColSize,
  ColSource,
  ColRequired,
  ColStatus,
  ColCount
};

constexpr int RowRole = Qt::UserRole + 1;

}  // namespace

CollectionDialog::CollectionDialog(OrganizerCore& core, const CollectionLink& link,
                                   QWidget* anchor)
    // a window of its own, with its own taskbar button: no parent, so Windows
    // doesn't keep it over the main window, which stays usable
    : QDialog(nullptr, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                           Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint),
      m_core(core), m_session(new CollectionSession(core, link, this)), m_anchor(anchor)
{
  setAttribute(Qt::WA_DeleteOnClose);
  // DialogPlacement leaves it alone: no owner, its own place and size
  setProperty("mo2rOwnWindow", true);
  setProperty("mo2rOwnPosition", true);
  setupUi();
  placeWindow();

  // MO2 may still be starting (a collection link on its command line): the main
  // window is then shown and maximized after this; the window follows it for a
  // few seconds, unless the user has moved or resized it by then
  if (m_anchor != nullptr) {
    m_anchor->installEventFilter(this);
    m_placedAt.start();
  }

  connect(m_session, &CollectionSession::changed, this, &CollectionDialog::onChanged);
  connect(m_session, &CollectionSession::entryChanged, this,
          &CollectionDialog::onEntryChanged);
  connect(m_session, &CollectionSession::finished, this, [this] {
    onChanged();
    raise();
    activateWindow();
  });

  onChanged();
  QTimer::singleShot(0, m_session, &CollectionSession::load);
}

CollectionDialog::~CollectionDialog() = default;

void CollectionDialog::placeWindow()
{
  QWidget* anchor = m_anchor;
  QScreen* screen = anchor != nullptr ? anchor->screen() : QGuiApplication::primaryScreen();
  if (screen == nullptr) {
    return;
  }
  const QRect available = screen->availableGeometry();

  // never smaller than this, never larger than the work area
  const QSize least = QSize(900, 600).boundedTo(available.size());
  setMinimumSize(least);

  // where the user left it, when that is on the main window's monitor
  if (Settings::instance().geometry().restoreGeometry(this) &&
      QGuiApplication::screenAt(geometry().center()) == screen &&
      available.contains(geometry().center())) {
    resize(size().boundedTo(available.size()));
    return;
  }

  // else large from the start, centred over the main window; over the work area
  // when the main window fills it or isn't shown yet (its size is then not its
  // final one)
  const bool overWindow = anchor != nullptr && anchor->isVisible() &&
                          !anchor->isMaximized() && !anchor->isFullScreen();
  const QRect base      = overWindow ? anchor->frameGeometry() : available;
  // wide enough for the table, tall enough for its rows under the options
  QSize wanted(base.width() * 7 / 10, base.height() * 85 / 100);
  wanted       = wanted.expandedTo(least).boundedTo(available.size() - QSize(16, 40));
  resize(wanted);
  const QPoint middle = base.center();
  QPoint pos(middle.x() - wanted.width() / 2, middle.y() - wanted.height() / 2);
  pos.setX(qBound(available.left(), pos.x(),
                  qMax(available.left(), available.right() - wanted.width() - 16)));
  pos.setY(qBound(available.top(), pos.y(),
                  qMax(available.top(), available.bottom() - wanted.height() - 40)));
  move(pos);
}

bool CollectionDialog::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_anchor.data() && m_placedAt.isValid() &&
      (event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange ||
       event->type() == QEvent::Resize)) {
    if (m_placedAt.elapsed() > 5000 || m_userPlaced) {
      m_anchor->removeEventFilter(this);
      m_placedAt.invalidate();
    } else {
      // after the main window has applied its new state
      QTimer::singleShot(0, this, [this] {
        if (!m_userPlaced) {
          m_placing = true;
          placeWindow();
          m_placing = false;
        }
        // a window of its own: the main window, shown after it, would cover it
        if (isVisible() && !isMinimized()) {
          raise();
          activateWindow();
        }
      });
    }
  }
  return QDialog::eventFilter(watched, event);
}

void CollectionDialog::moveEvent(QMoveEvent* e)
{
  QDialog::moveEvent(e);
  // a move the window manager made because the user dragged it
  if (!m_placing && isVisible() && e->spontaneous()) {
    m_userPlaced = true;
  }
}

void CollectionDialog::resizeEvent(QResizeEvent* e)
{
  QDialog::resizeEvent(e);
  if (!m_placing && isVisible() && e->spontaneous()) {
    m_userPlaced = true;
  }
}

bool CollectionDialog::claims(const QString& nxmUrl) const
{
  return m_session->claims(nxmUrl);
}

void CollectionDialog::setAutoStart(bool b)
{
  m_session->setAutoStart(b);
}

void CollectionDialog::setSwitchProfile(std::function<bool(const QString&)> f)
{
  m_session->setSwitchProfile(std::move(f));
}

void CollectionDialog::setRunLoot(std::function<void()> f)
{
  m_runLoot = std::move(f);
}

bool CollectionDialog::isRunning() const
{
  return m_session->stage() == Stage::Running;
}

std::optional<CollectionLink> CollectionDialog::askLink(QWidget* parent)
{
  FluentContentDialog dlg(
      parent, QStringLiteral("CollectionLinkDialog"), tr("Download a Nexus collection"),
      tr("Paste the address of the collection's page on Nexus Mods. Its latest "
         "revision is installed."));
  auto* caption = new QLabel(tr("Collection address"), &dlg);
  caption->setObjectName(QStringLiteral("instanceNameLabel"));
  auto* edit = new QLineEdit(&dlg);
  edit->setPlaceholderText(
      QStringLiteral("https://www.nexusmods.com/games/…/collections/…"));
  caption->setBuddy(edit);
  auto* error = new QLabel(&dlg);
  error->setObjectName(QStringLiteral("instanceNameValidation"));
  error->setWordWrap(true);
  error->hide();
  dlg.body()->addWidget(caption);
  dlg.body()->addWidget(edit);
  dlg.body()->addWidget(error);

  const QString clip = QApplication::clipboard()->text().trimmed();
  if (clip.contains(QStringLiteral("/collections/")) && parseCollectionLink(clip)) {
    edit->setText(clip);
    edit->selectAll();
  }

  auto* ok     = dlg.addButton(tr("Open"), true);
  auto* cancel = dlg.addButton(tr("Cancel"));
  connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

  auto check = [&] {
    const QString t   = edit->text().trimmed();
    const bool valid  = parseCollectionLink(t).has_value();
    const bool show   = !t.isEmpty() && !valid;
    error->setText(show ? tr("This isn't the address of a collection.") : QString());
    error->setVisible(show);
    ok->setEnabled(valid);
  };
  connect(edit, &QLineEdit::textChanged, &dlg, check);
  check();

  dlg.resize(520, dlg.sizeHint().height());
  if (dlg.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return parseCollectionLink(edit->text());
}

// ---------------------------------------------------------------------------
// building the window

void CollectionDialog::setupUi()
{
  setObjectName(QStringLiteral("CollectionDialog"));
  setWindowTitle(tr("Nexus collection"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 12);
  layout->setSpacing(8);
  // the content changes inside the window: text, rows, notices and the
  // summary never resize or move it
  layout->setSizeConstraint(QLayout::SetNoConstraint);

  layout->addWidget(createHeader());
  layout->addWidget(createInstructions());
  layout->addWidget(createMods(), 1);
  layout->addWidget(createOptions());
  layout->addWidget(createSummary());

  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_status->setWordWrap(true);
  m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  footer->addWidget(m_status, 1);

  m_next = new QPushButton(FluentIcons::glyph(NextGlyph), tr("Open next on Nexus"), this);
  m_next->setObjectName(QStringLiteral("collectionNext"));
  m_next->setAutoDefault(false);
  m_next->setToolTip(tr("Opens the next file's page on Nexus. Press \"Slow download\" "
                        "there and the file comes to Mod Organizer."));
  m_start = new QPushButton(tr("Start"), this);
  m_start->setObjectName(QStringLiteral("collectionStart"));
  m_start->setProperty("fluentPrimary", true);
  m_start->setAutoDefault(false);
  m_start->setMinimumWidth(102);
  m_cancel = new QPushButton(tr("Cancel"), this);
  m_cancel->setObjectName(QStringLiteral("collectionCancel"));
  m_cancel->setAutoDefault(false);
  m_cancel->setMinimumWidth(102);
  footer->addWidget(m_next);
  footer->addWidget(m_start);
  footer->addWidget(m_cancel);
  layout->addLayout(footer);

  connect(m_next, &QPushButton::clicked, this, [this] {
    if (!m_session->openNext()) {
      m_status->setText(tr("All files from Nexus have arrived."));
    }
  });
  connect(m_start, &QPushButton::clicked, this, [this] {
    if (m_session->stage() == Stage::Running) {
      m_session->pause();
    } else {
      m_session->start();
    }
  });
  connect(m_cancel, &QPushButton::clicked, this, &QDialog::close);
}

QFrame* CollectionDialog::createHeader()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("collectionHeaderCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QHBoxLayout(card);
  layout->setContentsMargins(18, 14, 18, 14);
  layout->setSpacing(10);

  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  m_name = new QLabel(card);
  m_name->setObjectName(QStringLiteral("instanceDetailName"));
  m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_caption->setWordWrap(true);
  names->addWidget(m_name);
  names->addWidget(m_caption);
  layout->addLayout(names, 1);

  m_loading = new QLabel(card);
  m_loading->setObjectName(QStringLiteral("fluentStatCaption"));
  m_loading->setWordWrap(true);
  layout->addWidget(m_loading, 0, Qt::AlignVCenter | Qt::AlignRight);
  return card;
}

QFrame* CollectionDialog::createInstructions()
{
  m_instructionsCard = new QFrame(this);
  m_instructionsCard->setObjectName(QStringLiteral("collectionInstructionsCard"));
  m_instructionsCard->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(m_instructionsCard);
  layout->setContentsMargins(14, 10, 8, 8);
  layout->setSpacing(4);

  auto* title = new QLabel(tr("Instructions"), m_instructionsCard);
  title->setObjectName(QStringLiteral("fluentCardTitle"));
  layout->addWidget(title);

  m_instructions = new QTextBrowser(m_instructionsCard);
  m_instructions->setObjectName(QStringLiteral("collectionInstructions"));
  m_instructions->setFrameShape(QFrame::NoFrame);
  // the curator's links open in the browser, web addresses only
  m_instructions->setOpenLinks(false);
  m_instructions->setOpenExternalLinks(false);
  connect(m_instructions, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    if (url.isValid() && (scheme == "http" || scheme == "https")) {
      shell::Open(url);
    }
  });
  m_instructions->setMaximumHeight(100);
  layout->addWidget(m_instructions);

  m_instructionsCard->hide();
  return m_instructionsCard;
}

QFrame* CollectionDialog::createMods()
{
  m_modsCard = new QFrame(this);
  m_modsCard->setObjectName(QStringLiteral("collectionModsCard"));
  m_modsCard->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(m_modsCard);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Mods"), m_modsCard);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_count = new QLabel(m_modsCard);
  m_count->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_count);
  layout->addLayout(title);

  m_tree = new QTreeWidget(m_modsCard);
  m_tree->setObjectName(QStringLiteral("collectionModList"));
  m_tree->setColumnCount(ColCount);
  m_tree->setHeaderLabels({tr("Name"), tr("Version"), tr("Size"), tr("Source"),
                           tr("Required"), tr("Status")});
  m_tree->setRootIsDecorated(false);
  m_tree->setUniformRowHeights(true);
  m_tree->setAlternatingRowColors(false);
  m_tree->setFrameShape(QFrame::NoFrame);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  m_tree->setAccessibleName(tr("Mods"));
  m_tree->header()->setStretchLastSection(true);
  m_tree->header()->setSectionResizeMode(ColName, QHeaderView::Interactive);
  m_tree->setColumnWidth(ColName, 360);
  m_tree->setColumnWidth(ColVersion, 90);
  m_tree->setColumnWidth(ColSize, 90);
  m_tree->setColumnWidth(ColSource, 90);
  m_tree->setColumnWidth(ColRequired, 90);
  layout->addWidget(m_tree, 1);

  // a mod from outside Nexus: what to do, and the buttons for it
  m_external = new QFrame(m_modsCard);
  m_external->setObjectName(QStringLiteral("collectionExternal"));
  auto* ext = new QHBoxLayout(m_external);
  ext->setContentsMargins(6, 4, 6, 2);
  ext->setSpacing(8);
  m_externalText = new QLabel(m_external);
  m_externalText->setWordWrap(true);
  m_externalText->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_externalText->setOpenExternalLinks(false);
  ext->addWidget(m_externalText, 1);
  m_externalOpen   = new QPushButton(tr("Open"), m_external);
  m_externalChoose = new QPushButton(tr("Choose file…"), m_external);
  m_externalDone   = new QPushButton(tr("Mark as done"), m_external);
  for (auto* b : {m_externalOpen, m_externalChoose, m_externalDone}) {
    b->setAutoDefault(false);
    ext->addWidget(b, 0, Qt::AlignVCenter);
  }
  layout->addWidget(m_external);
  m_external->hide();

  m_notes = new QLabel(m_modsCard);
  m_notes->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_notes->setWordWrap(true);
  m_notes->setContentsMargins(6, 0, 6, 0);
  m_notes->hide();
  layout->addWidget(m_notes);

  connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
    if (col == ColName && (item->flags() & Qt::ItemIsUserCheckable)) {
      m_session->setSelected(item->data(ColName, RowRole).toInt(),
                             item->checkState(ColName) == Qt::Checked);
    }
  });
  connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] {
    updateExternal();
  });
  connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
          &CollectionDialog::showMenu);
  connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
    m_session->openPage(item->data(ColName, RowRole).toInt());
  });
  connect(m_externalOpen, &QPushButton::clicked, this, [this] {
    m_session->openPage(currentRow());
  });
  connect(m_externalChoose, &QPushButton::clicked, this, [this] {
    chooseFile(currentRow());
  });
  connect(m_externalDone, &QPushButton::clicked, this, [this] {
    m_session->markDone(currentRow());
  });

  return m_modsCard;
}

QFrame* CollectionDialog::createOptions()
{
  m_optionsCard = new QFrame(this);
  m_optionsCard->setObjectName(QStringLiteral("collectionOptionsCard"));
  m_optionsCard->setProperty("fluentCard", true);
  auto* rows = new QVBoxLayout(m_optionsCard);
  rows->setContentsMargins(0, 4, 0, 4);
  rows->setSpacing(0);

  m_targets = new QButtonGroup(m_optionsCard);
  auto addTarget = [this](CollectionSession::Target target, char16_t glyph,
                          const QString& title, const QString& description,
                          QLineEdit* field) {
    auto* radio = new QRadioButton(m_optionsCard);
    radio->setAccessibleName(title);
    radio->setAccessibleDescription(description);
    m_targets->addButton(radio, static_cast<int>(target));
    QList<QWidget*> widgets;
    if (field != nullptr) {
      field->setMinimumWidth(260);
      field->setAccessibleName(title);
      widgets.append(field);
    }
    widgets.append(radio);
    return FluentPanels::addEditRow(m_optionsCard, glyph, title, description, widgets);
  };

  m_instanceName = new QLineEdit(m_optionsCard);
  m_instanceName->setObjectName(QStringLiteral("collectionInstanceName"));
  m_instanceRow = addTarget(CollectionSession::Target::NewInstance, InstanceGlyph,
                            tr("Install into a new instance"),
                            tr("Its own instance, apart from your other mods. Mod "
                               "Organizer restarts in it."),
                            m_instanceName);
  m_instanceProblem = new QLabel(m_optionsCard);
  m_instanceProblem->setObjectName(QStringLiteral("instanceNameValidation"));
  m_instanceProblem->setContentsMargins(58, 0, 18, 6);
  m_instanceProblem->hide();
  rows->addWidget(m_instanceProblem);

  m_profileName = new QLineEdit(m_optionsCard);
  m_profileName->setObjectName(QStringLiteral("collectionProfileName"));
  addTarget(CollectionSession::Target::NewProfile, ProfileGlyph,
            tr("Install into a new profile"),
            tr("A new profile in this instance. Its mods are shared with your other "
               "profiles."),
            m_profileName);
  addTarget(CollectionSession::Target::CurrentProfile, CurrentGlyph,
            tr("Install into the current profile"),
            tr("The mods are added to the profile you are using now."), nullptr);

  m_separator = new QLineEdit(m_optionsCard);
  m_separator->setObjectName(QStringLiteral("collectionSeparator"));
  FluentPanels::addEditRow(m_optionsCard, SeparatorGlyph, tr("Separator"),
                           tr("The collection's mods go under a separator with this "
                              "name."),
                           {m_separator});

  m_autoOpen = new QCheckBox(m_optionsCard);
  m_autoOpen->setObjectName(QStringLiteral("collectionAutoOpen"));
  m_autoOpenRow = FluentPanels::addSwitchRow(
      m_optionsCard, NextGlyph, tr("Open the next page automatically"),
      tr("When a file from Nexus arrives, the next file's page opens."), m_autoOpen);

  connect(m_targets, &QButtonGroup::idClicked, this, [this](int id) {
    m_session->setTarget(static_cast<CollectionSession::Target>(id));
    updateButtons();
  });
  connect(m_instanceName, &QLineEdit::textChanged, this, [this](const QString& text) {
    m_session->setInstanceName(text.trimmed());
    updateButtons();
  });
  connect(m_profileName, &QLineEdit::editingFinished, this, [this] {
    m_session->setProfileName(m_profileName->text().trimmed());
  });
  connect(m_separator, &QLineEdit::editingFinished, this, [this] {
    m_session->setSeparatorName(m_separator->text().trimmed());
  });
  connect(m_autoOpen, &QCheckBox::toggled, this, [this](bool on) {
    m_session->setAutoOpenNext(on);
  });

  return m_optionsCard;
}

QFrame* CollectionDialog::createSummary()
{
  m_summaryCard = new QFrame(this);
  m_summaryCard->setObjectName(QStringLiteral("collectionSummaryCard"));
  m_summaryCard->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(m_summaryCard);
  layout->setContentsMargins(14, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  auto* heading = new QLabel(tr("Summary"), m_summaryCard);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  title->addWidget(heading);
  title->addStretch(1);
  m_loot = new QPushButton(tr("Sort plugins with LOOT"), m_summaryCard);
  m_loot->setAutoDefault(false);
  m_loot->setToolTip(tr("The collection doesn't set a plugin order."));
  title->addWidget(m_loot);
  layout->addLayout(title);

  m_summary = new QPlainTextEdit(m_summaryCard);
  m_summary->setReadOnly(true);
  m_summary->setFrameShape(QFrame::NoFrame);
  m_summary->setMaximumHeight(170);
  layout->addWidget(m_summary);

  connect(m_loot, &QPushButton::clicked, this, [this] {
    if (m_runLoot) {
      m_runLoot();
    }
  });

  m_summaryCard->hide();
  return m_summaryCard;
}

// ---------------------------------------------------------------------------
// showing the session

void CollectionDialog::onChanged()
{
  const Stage stage = m_session->stage();

  m_name->setText(m_session->displayName());
  QStringList caption;
  if (!m_session->author().isEmpty()) {
    caption.append(m_session->author());
  }
  if (m_session->revision() > 0) {
    caption.append(tr("revision %1").arg(m_session->revision()));
  }
  if (!m_session->gameName().isEmpty()) {
    caption.append(m_session->gameName());
  }
  m_caption->setText(caption.join(QStringLiteral(" · ")));
  setWindowTitle(tr("Nexus collection: %1").arg(m_session->displayName()));

  if (stage == Stage::Error) {
    m_loading->setText(QString());
    m_status->setText(m_session->errorText());
    QPalette p = m_status->palette();
    p.setColor(QPalette::WindowText, RevampedTheme::criticalText());
    m_status->setPalette(p);
  } else {
    m_loading->setText(m_session->loadingText());
  }

  const bool haveRows = !m_session->entries().empty();
  if (haveRows && !m_built) {
    buildRows();
  }

  if (haveRows) {
    const QString instructions = m_session->manifest().installInstructions.trimmed();
    if (!instructions.isEmpty() && m_instructions->toPlainText().isEmpty()) {
      m_instructions->setMarkdown(instructions);
    }
    m_instructionsCard->setVisible(!instructions.isEmpty() && stage != Stage::Done);

    QString count = tr("%n mod(s)", "", static_cast<int>(m_session->entries().size()));
    count += QStringLiteral(" · ") +
             tr("%1 in total").arg(localizedByteSize(m_session->totalSize()));
    if (m_session->started()) {
      count += QStringLiteral(" · ") +
               tr("%1 installed").arg(m_session->count(Status::Installed));
      if (m_session->remainingSize() > 0) {
        count += QStringLiteral(" · ") +
                 tr("%1 left").arg(localizedByteSize(m_session->remainingSize()));
      }
    }
    m_count->setText(count);

    // the profile and separator fields as the session has them
    QSignalBlocker b1(m_targets), b2(m_autoOpen), b3(m_instanceName);
    if (auto* b = m_targets->button(static_cast<int>(m_session->target()))) {
      b->setChecked(true);
    }
    if (!m_instanceName->hasFocus()) {
      m_instanceName->setText(m_session->instanceName());
    }
    if (!m_profileName->hasFocus()) {
      m_profileName->setText(m_session->profileName());
    }
    if (!m_separator->hasFocus()) {
      m_separator->setText(m_session->separatorName());
    }
    m_autoOpen->setChecked(m_session->autoOpenNext());
  }

  // only before it starts: they can't change once it runs, and the mod list
  // needs the room while it downloads
  m_optionsCard->setVisible(stage == Stage::Loading || stage == Stage::Ready);
  m_optionsCard->setEnabled(haveRows);
  m_summaryCard->setVisible(stage == Stage::Done);
  if (stage == Stage::Done) {
    m_summary->setPlainText(m_session->summary());
    m_loot->setVisible(m_session->shouldOfferLoot());
  }

  updateExternal();
  updateButtons();
}

void CollectionDialog::buildRows()
{
  m_built = true;
  QSignalBlocker block(m_tree);
  m_tree->clear();
  const auto& entries = m_session->entries();
  for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
    auto* item = new QTreeWidgetItem(m_tree);
    item->setData(ColName, RowRole, row);
    updateRow(item, row);
  }
}

void CollectionDialog::onEntryChanged(int row)
{
  if (!m_built || row < 0 || row >= m_tree->topLevelItemCount()) {
    return;
  }
  QSignalBlocker block(m_tree);
  updateRow(m_tree->topLevelItem(row), row);
  if (row == currentRow()) {
    updateExternal();
  }
}

void CollectionDialog::updateRow(QTreeWidgetItem* item, int row)
{
  const auto& e   = m_session->entries()[row];
  const auto& mod = m_session->modOf(e);

  item->setText(ColName, mod.name);
  QStringList tip;
  if (e.modName != mod.name) {
    tip.append(tr("Installed as \"%1\"").arg(e.modName));
  }
  if (!mod.instructions.isEmpty()) {
    tip.append(mod.instructions);
  }
  item->setToolTip(ColName, tip.join(QStringLiteral("\n")));
  item->setText(ColVersion, e.fileVersion);
  item->setText(ColSize, e.size > 0 ? localizedByteSize(e.size) : QString());
  item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
  item->setText(ColSource, sourceText(mod.type));
  item->setText(ColRequired, mod.optional ? tr("Optional") : tr("Required"));

  const bool locked = m_session->stage() == Stage::Running || e.status == Status::Installed;
  // an optional mod has a box, which can't change while the collection runs
  if (mod.optional) {
    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (!locked) {
      flags |= Qt::ItemIsUserCheckable;
    }
    item->setFlags(flags);
    item->setData(ColName, Qt::CheckStateRole, e.selected ? Qt::Checked : Qt::Unchecked);
  }

  item->setText(ColStatus, statusText(e));
  item->setToolTip(ColStatus, e.reason);

  QColor color;
  switch (e.status) {
  case Status::Installed:
    color = RevampedTheme::successText();
    break;
  case Status::Failed:
    color = RevampedTheme::criticalText();
    break;
  case Status::WaitingForClick:
    color = RevampedTheme::cautionText();
    break;
  case Status::Downloading:
    color = RevampedTheme::infoText();
    break;
  case Status::Skipped:
    color = RevampedTheme::dimmedText();
    break;
  default:
    break;
  }
  item->setForeground(ColStatus, color.isValid() ? QBrush(color) : QBrush());
}

QString CollectionDialog::statusText(const CollectionSession::Entry& e) const
{
  switch (e.status) {
  case Status::Pending:
    return e.byHand ? tr("Install by hand: another game's file") : tr("Pending");
  case Status::WaitingForClick:
    return tr("Waiting for your click");
  case Status::Downloading:
    return e.progress >= 0 ? tr("Downloading %1%").arg(e.progress) : tr("Downloading");
  case Status::Downloaded:
    return tr("Downloaded");
  case Status::Installed:
    return e.reason.isEmpty() ? tr("Installed") : e.reason;
  case Status::Skipped:
    return tr("Skipped");
  case Status::Failed:
    return e.reason.isEmpty() ? tr("Failed")
                              : tr("Failed: %1", "status with the reason").arg(e.reason);
  }
  return {};
}

QString CollectionDialog::sourceText(SourceType t) const
{
  switch (t) {
  case SourceType::Nexus:
    return tr("Nexus");
  case SourceType::Bundle:
    return tr("Bundled");
  case SourceType::Direct:
  case SourceType::Browse:
  case SourceType::Manual:
  case SourceType::Unknown:
    break;
  }
  return tr("External");
}

int CollectionDialog::currentRow() const
{
  const auto* item = m_tree->currentItem();
  return item != nullptr ? item->data(ColName, RowRole).toInt() : -1;
}

void CollectionDialog::updateExternal()
{
  const int row = currentRow();
  if (row < 0 || row >= static_cast<int>(m_session->entries().size())) {
    m_external->hide();
    return;
  }
  const auto& e   = m_session->entries()[row];
  const auto& mod = m_session->modOf(e);
  // a mod from outside Nexus, or a Nexus file of another game
  const bool external =
      e.byHand || (mod.type != SourceType::Nexus && mod.type != SourceType::Bundle);
  if (!external || e.status == Status::Installed || e.status == Status::Skipped) {
    m_external->hide();
    return;
  }

  QStringList text;
  text.append(QStringLiteral("<b>%1</b>").arg(mod.name.toHtmlEscaped()));
  const QString instructions =
      !mod.sourceInstructions.isEmpty() ? mod.sourceInstructions : mod.instructions;
  if (e.byHand) {
    text.append(tr("This file is for another game on Nexus (%1), so Mod Organizer can't "
                   "download it here. Get it from its page, then choose it here, or "
                   "install it yourself and mark it as done.")
                    .arg(mod.domainName)
                    .toHtmlEscaped());
  } else if (!instructions.isEmpty()) {
    text.append(instructions.toHtmlEscaped());
  } else {
    text.append(tr("This mod doesn't come from Nexus. Get its file, then choose it "
                   "here, or install it yourself and mark it as done.")
                    .toHtmlEscaped());
  }
  if (!e.byHand && !mod.url.isEmpty()) {
    text.append(mod.url.toHtmlEscaped());
  }
  m_externalText->setText(text.join(QStringLiteral("<br>")));
  m_externalOpen->setEnabled(e.byHand || !mod.url.isEmpty());
  m_external->show();
}

void CollectionDialog::updateButtons()
{
  const Stage stage    = m_session->stage();
  const bool premium   = m_session->isPremium();
  const bool loaded    = stage != Stage::Loading && stage != Stage::Error;
  const bool locked    = m_session->started();

  m_start->setVisible(loaded);
  switch (stage) {
  case Stage::Ready:
    m_start->setText(tr("Start"));
    break;
  case Stage::Running:
    m_start->setText(tr("Pause"));
    break;
  case Stage::Paused:
  case Stage::Done:
    m_start->setText(tr("Continue"));
    break;
  default:
    break;
  }
  // a finished collection continues only when something was put back
  m_start->setEnabled(stage != Stage::Done);
  m_start->setVisible(loaded && stage != Stage::Done);
  m_cancel->setText(stage == Stage::Done || stage == Stage::Error ? tr("Close")
                                                                  : tr("Cancel"));

  m_next->setVisible(loaded && !premium && stage != Stage::Done);
  m_next->setEnabled(stage == Stage::Running);
  // the row follows its switch
  m_autoOpen->setVisible(!premium);

  // the place is chosen once
  const auto target = m_session->target();
  for (auto* b : m_targets->buttons()) {
    b->setEnabled(!locked);
  }
  m_instanceRow->setVisible(m_session->canMakeInstance());
  m_instanceName->setEnabled(!locked && target == CollectionSession::Target::NewInstance);
  m_profileName->setEnabled(!locked && target == CollectionSession::Target::NewProfile);
  QString problem;
  if (!locked && target == CollectionSession::Target::NewInstance) {
    problem = m_session->instanceNameProblem(m_instanceName->text());
  }
  m_instanceProblem->setText(problem);
  m_instanceProblem->setVisible(!problem.isEmpty());
  if (!problem.isEmpty()) {
    m_start->setEnabled(false);
  }

  if (stage != Stage::Error) {
    m_status->setPalette(QPalette());
    if (stage == Stage::Running) {
      m_status->setText(
          premium ? tr("Premium: up to three files download at a time.")
                  : tr("Free account: press \"Open next on Nexus\", then \"Slow download\" "
                       "on the page. Each file is installed when it arrives."));
    } else if (stage == Stage::Paused) {
      m_status->setText(tr("Paused. Downloads already started go on in the Downloads "
                           "tab."));
    } else if (stage == Stage::Ready) {
      m_status->setText(premium ? tr("The files are downloaded and installed in order.")
                                : tr("Nexus asks free accounts to click once for each "
                                     "file on its website. Mod Organizer opens each "
                                     "page for you."));
    } else if (stage == Stage::Done) {
      m_status->setText(tr("The collection is installed."));
    } else {
      m_status->clear();
    }
  }

  // binary patches are for later
  if (!m_session->entries().empty()) {
    int patched = 0;
    for (const auto& e : m_session->entries()) {
      patched += m_session->modOf(e).hasPatches;
    }
    QStringList notes;
    if (m_session->isReduced()) {
      notes.append(tr("The collection's archive couldn't be read, so this is a reduced "
                      "install: the mod list from Nexus, without installer choices, mod "
                      "order or bundled mods."));
    }
    if (patched > 0) {
      notes.append(tr("%n mod(s) of this collection come with patches, which Mod "
                      "Organizer doesn't apply yet. They are listed in the summary.",
                      "", patched));
    }
    m_notes->setText(notes.join(QStringLiteral("\n")));
    m_notes->setVisible(!notes.isEmpty() && (stage != Stage::Done || m_session->isReduced()));
    QPalette p = m_notes->palette();
    if (m_session->isReduced()) {
      p.setColor(QPalette::WindowText, RevampedTheme::cautionText());
      m_notes->setPalette(p);
    } else {
      m_notes->setPalette(QPalette());
    }
  }

  // the rows' boxes lock while it runs
  if (m_built) {
    QSignalBlocker block(m_tree);
    for (int row = 0; row < m_tree->topLevelItemCount(); ++row) {
      updateRow(m_tree->topLevelItem(row), row);
    }
  }
}

void CollectionDialog::showMenu(const QPoint& pos)
{
  auto* item = m_tree->itemAt(pos);
  if (item == nullptr) {
    return;
  }
  const int row   = item->data(ColName, RowRole).toInt();
  const auto& e   = m_session->entries()[row];
  const auto& mod = m_session->modOf(e);

  QMenu menu(this);
  if (mod.type == SourceType::Nexus) {
    menu.addAction(tr("Open on Nexus"), this, [this, row] {
      m_session->openPage(row);
    });
  } else if (!mod.url.isEmpty()) {
    menu.addAction(tr("Open"), this, [this, row] {
      m_session->openPage(row);
    });
  }
  if (mod.type != SourceType::Bundle && e.status != Status::Installed) {
    menu.addAction(tr("Choose file…"), this, [this, row] {
      chooseFile(row);
    });
  }
  if (e.status != Status::Installed) {
    menu.addAction(tr("Mark as done"), this, [this, row] {
      m_session->markDone(row);
    });
  }
  if (e.status == Status::Failed || e.status == Status::Skipped) {
    menu.addAction(tr("Try again"), this, [this, row] {
      m_session->retry(row);
    });
  } else if (e.status != Status::Installed) {
    menu.addAction(tr("Skip"), this, [this, row] {
      m_session->skip(row);
    });
  }
  menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void CollectionDialog::chooseFile(int row)
{
  if (row < 0) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Choose the mod's file"), QString(),
      tr("Archives (*.7z *.zip *.rar *.fomod *.001);;All files (*)"));
  if (!path.isEmpty()) {
    m_session->chooseFile(row, path);
  }
}

void CollectionDialog::closeEvent(QCloseEvent* e)
{
  Settings::instance().geometry().saveGeometry(this);
  if (m_session->stage() == Stage::Running) {
    // nothing is lost: it continues from the same place next time
    m_session->pause();
  }
  QDialog::closeEvent(e);
}
