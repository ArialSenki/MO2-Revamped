#include "instancemanagerdialog.h"
#include "createinstancedialog.h"
#include "envshortcut.h"
#include "filesystemutilities.h"
#include "fluentdialogs.h"
#include "fluenticons.h"
#include "fluentmotion.h"
#include "gameiconutils.h"
#include "instancemanager.h"
#include "plugincontainer.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include <iplugingame.h>
#include <report.h>
#include <utility.h>

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <numeric>
#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QMenu>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{
// Segoe Fluent Icons code points
constexpr char16_t AddGlyph      = 0xE710;
constexpr char16_t RenameGlyph   = 0xE8AC;
constexpr char16_t OpenGlyph     = 0xE838;  // FolderOpen
constexpr char16_t DocumentGlyph = 0xE8A5;
constexpr char16_t DeleteGlyph   = 0xE74D;
constexpr char16_t HelpGlyph     = 0xE897;
constexpr char16_t GameGlyph     = 0xE7FC;
constexpr char16_t DriveGlyph    = 0xE88E;  // USB, a portable folder
constexpr char16_t FolderGlyph   = 0xE8B7;
constexpr char16_t LibraryGlyph  = 0xE8F1;
constexpr char16_t HomeGlyph     = 0xE80F;
constexpr char16_t PeopleGlyph   = 0xE716;
constexpr char16_t SizeGlyph     = 0xEDA2;  // HardDrive
constexpr char16_t CopyGlyph     = 0xE8C8;
constexpr char16_t PinGlyph      = 0xE718;
constexpr char16_t PlayGlyph     = 0xE768;
constexpr char16_t DocumentOpenGlyph = 0xE8E5;  // OpenFile
constexpr char16_t TagGlyph      = 0xE8EC;
constexpr char16_t NoteGlyph     = 0xE70B;  // QuickNote

constexpr int RowHeight   = 60;
constexpr int CaptionRole = Qt::UserRole + 1;
constexpr int IndexRole   = Qt::UserRole + 2;  // the instance's index, -1 on a heading
constexpr int HeaderRole  = Qt::UserRole + 3;  // a group heading
constexpr int GroupRole   = Qt::UserRole + 4;  // a heading's group name
constexpr int NotesRole   = Qt::UserRole + 5;  // searched along with the name
constexpr int HeaderHeight = 34;

QLabel* glyphLabel(char16_t glyph, QWidget* parent)
{
  auto* label = new QLabel(QString(QChar(glyph)), parent);
  label->setObjectName(QStringLiteral("instanceRowGlyph"));
  label->setFont(FluentIcons::font(16));
  label->setFixedWidth(24);
  label->setAlignment(Qt::AlignCenter);
  return label;
}

// a label that elides its text in the middle, so a long path keeps its drive
// and its last folder; the whole text is in the tool tip
class ElidedLabel : public QLabel
{
public:
  using QLabel::QLabel;

  void setFullText(const QString& text)
  {
    m_full = text;
    setToolTip(text);
    updateGeometry();
    update();
  }

  QSize minimumSizeHint() const override { return QSize(40, QLabel::minimumSizeHint().height()); }

  QSize sizeHint() const override
  {
    return QSize(fontMetrics().horizontalAdvance(m_full), fontMetrics().height());
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter p(this);
    p.setPen(palette().color(QPalette::WindowText));
    p.setFont(font());
    p.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
               fontMetrics().elidedText(m_full, Qt::ElideMiddle, contentsRect().width()));
  }

private:
  QString m_full;
};

// the instance rows of the list card: the game's badge, the name and a
// caption below it, a hover fill and the accent pill of the selected row like
// the Settings navigation
class InstanceRowDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    return QSize(QStyledItemDelegate::sizeHint(option, index).width(),
                 index.data(HeaderRole).toBool() ? HeaderHeight : RowHeight);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered  = option.state & QStyle::State_MouseOver;
    const QColor ink    = option.palette.color(QPalette::Active, QPalette::WindowText);

    if (index.data(HeaderRole).toBool()) {
      // a group heading: semibold caption text, no hover
      painter->save();
      QFont heading = option.font;
      heading.setWeight(QFont::DemiBold);
      heading.setPointSizeF(option.font.pointSizeF() * 0.92);
      painter->setFont(heading);
      painter->setPen(FluentIcons::active() ? FluentIcons::color(FluentIcons::Tone::Muted)
                                            : option.palette.color(QPalette::Disabled,
                                                                   QPalette::WindowText));
      painter->drawText(QRectF(option.rect).adjusted(10, 0, -8, -5),
                        Qt::AlignLeft | Qt::AlignBottom,
                        QFontMetrics(heading).elidedText(index.data().toString(),
                                                         Qt::ElideRight,
                                                         option.rect.width() - 18));
      painter->restore();
      return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
    if (selected || hovered) {
      QColor fill = ink;
      fill.setAlpha(selected ? 26 : 15);
      painter->setPen(Qt::NoPen);
      painter->setBrush(fill);
      painter->drawRoundedRect(row, 4, 4);
    }
    if (selected) {
      painter->setBrush(FluentIcons::active() ? FluentIcons::color(FluentIcons::Tone::Accent)
                                              : option.palette.color(QPalette::Highlight));
      painter->drawRoundedRect(QRectF(row.left(), row.center().y() - 8, 3, 16), 1.5, 1.5);
    }

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    const QRect iconRect(static_cast<int>(row.left()) + 12,
                         static_cast<int>(row.center().y()) - 16, 32, 32);
    icon.paint(painter, iconRect);

    const QRectF text = row.adjusted(56, 8, -10, -8);
    QFont name        = option.font;
    name.setWeight(selected ? QFont::DemiBold : QFont::Normal);
    painter->setFont(name);
    painter->setPen(ink);
    const QFontMetrics nameMetrics(name);
    painter->drawText(QRectF(text.left(), text.top(), text.width(), text.height() / 2),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.elidedText(index.data().toString(), Qt::ElideRight,
                                             static_cast<int>(text.width())));

    QFont caption = option.font;
    caption.setPointSizeF(option.font.pointSizeF() * 0.9);
    painter->setFont(caption);
    painter->setPen(FluentIcons::active() ? FluentIcons::color(FluentIcons::Tone::Muted)
                                          : option.palette.color(QPalette::Disabled,
                                                                 QPalette::WindowText));
    painter->drawText(QRectF(text.left(), text.center().y(), text.width(), text.height() / 2),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(caption).elidedText(index.data(CaptionRole).toString(),
                                                       Qt::ElideRight,
                                                       static_cast<int>(text.width())));
    painter->restore();
  }
};

// when the instance was last used: its settings and its log are written
// while it runs; "today at 13:20", "yesterday", "3 days ago" or a date like
// the main window's "Last played"
QDateTime lastUsedTime(const Instance& i)
{
  QDateTime newest;
  for (const QString& file :
       {i.iniPath(), QDir(i.directory()).filePath(QStringLiteral("logs/mo_interface.log"))}) {
    const QDateTime modified = QFileInfo(file).lastModified();
    if (modified.isValid() && (!newest.isValid() || modified > newest)) {
      newest = modified;
    }
  }
  return newest;
}

QString lastUsed(const Instance& i)
{
  const QDateTime newest = lastUsedTime(i);
  if (!newest.isValid()) {
    return {};
  }

  auto tr = [](const char* text) {
    return QCoreApplication::translate("InstanceManagerDialog", text);
  };
  const qint64 days = newest.date().daysTo(QDate::currentDate());
  if (days == 0) {
    return tr("today at %1").arg(QLocale().toString(newest.time(), QLocale::ShortFormat));
  } else if (days == 1) {
    return tr("yesterday");
  } else if (days < 7) {
    return tr("%1 days ago").arg(days);
  }
  return QLocale().toString(newest.date(), QLocale::ShortFormat);
}

// what the user keeps about an instance (its group, its notes), in the
// global settings so it is there whether the instance is open or not
QString instanceValue(const QString& section, const QString& identifier)
{
  return GlobalSettings::instanceValue(section, identifier);
}

void setInstanceValue(const QString& section, const QString& identifier,
                      const QString& value)
{
  GlobalSettings::setInstanceValue(section, identifier, value);
}

const QString GroupsSection = QStringLiteral("InstanceGroups");
const QString NotesSection  = QStringLiteral("InstanceNotes");

QString instanceGroup(const QString& identifier)
{
  return instanceValue(GroupsSection, identifier);
}

void setInstanceGroup(const QString& identifier, const QString& group)
{
  setInstanceValue(GroupsSection, identifier, group);
}

// a group and notes follow an instance that was renamed or copied, and go
// away with it when it is deleted
void moveInstanceValues(const QString& from, const QString& to, bool keepSource)
{
  for (const QString& section : {GroupsSection, NotesSection}) {
    const QString value = instanceValue(section, from);
    if (!keepSource) {
      setInstanceValue(section, from, {});
    }
    if (!to.isEmpty() || !value.isEmpty()) {
      setInstanceValue(section, to, value);
    }
  }
}

}  // namespace

// returns the icon for the given instance or an empty 32x32 icon if the game
// plugin couldn't be found
//
QIcon instanceIcon(PluginContainer& pc, const Instance& i,
                   const QWidget* themeWidget)
{
  auto* game = InstanceManager::singleton().gamePluginForDirectory(i.directory(), pc);

  if (!game) {
    QPixmap empty(32, 32);
    empty.fill(QColor(0, 0, 0, 0));
    return QIcon(empty);
  }

  // it's possible to have the game installed in a way that the game plugin
  // couldn't auto detect; in this case, the instance would have a valid game
  // directory, but the plugin wouldn't know about it
  //
  // it's also possible, but unlikely, to have multiple installations of the
  // same game that have different icons for the same exe
  //
  // so the game directory specified for the instance needs to be given to the
  // game plugin to get the appropriate icon, but since these game plugin
  // objects are created on startup and are global, they should retain their
  // auto detected path
  //
  // if not, creating a new instance for a specific plugin would use the game
  // directory of the instance for which the icon was most recently shown, which
  // would be really inconsistent
  //
  //
  // this game plugin could also be the currently active plugin for the
  // current instance, which should _definitely_ keep pointing to the same
  // directory as before

  // remember old game directory
  //
  // note that gameDirectory() returns a QDir, which doesn't support empty
  // strings (they get converted to "." automatically!), but the plugin _will_
  // try to return an empty string when the game has not been auto-detected
  //
  // so gameDirectory() _cannot_ reliably be used if `isInstalled()` is false
  const QString old = game->isInstalled() ? game->gameDirectory().path() : "";

  // revert
  Guard g([&] {
    game->setGamePath(old);
  });

  // set directory for this instance
  game->setGamePath(i.gameDirectory());

  return MOBase::makeRunStyleGameIcon(game->gameIcon(), themeWidget,
                                      QSize(36, 36));
}

// pops up a dialog to ask for an instance name when renaming
//
QString getInstanceName(QWidget* parent, const QString& title, const QString& moreText,
                        const QString& label, const QString& parentDirectory,
                        const QString& oldName = {}, const QString& okText = {},
                        bool renaming = true)
{
  FluentContentDialog dlg(parent, QStringLiteral("InstanceNameDialog"), title, moreText);

  auto* caption = new QLabel(label, &dlg);
  caption->setObjectName(QStringLiteral("instanceNameLabel"));
  auto* text = new QLineEdit(oldName, &dlg);
  text->setObjectName(QStringLiteral("instanceNameEdit"));
  text->setPlaceholderText(QObject::tr("Enter an instance name"));
  text->selectAll();
  auto* error = new QLabel(&dlg);
  error->setObjectName(QStringLiteral("instanceNameValidation"));
  error->setWordWrap(true);

  dlg.body()->addWidget(caption);
  dlg.body()->addWidget(text);
  dlg.body()->addWidget(error);

  auto* ok     = dlg.addButton(okText.isEmpty() ? QObject::tr("Rename") : okText, true);
  auto* cancel = dlg.addButton(QObject::tr("Cancel"));

  auto check = [&] {
    bool okay = false;

    if (text->text().isEmpty()) {
      error->setText("");
    } else if (!MOBase::validFileName(text->text())) {
      error->setText(QObject::tr("Use a name that's a valid folder name."));
    } else {
      const auto name = MOBase::sanitizeFileName(text->text());

      if ((!renaming || name != oldName) && QDir(parentDirectory).exists(name)) {
        error->setText(QObject::tr("An instance with this name already exists."));
      } else {
        okay = true;
      }
    }

    error->setVisible(!error->text().isEmpty());
    ok->setEnabled(okay);
  };

  QObject::connect(text, &QLineEdit::textChanged, &dlg, check);
  QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

  check();

  dlg.resize(480, dlg.sizeHint().height());
  if (dlg.exec() != QDialog::Accepted) {
    return {};
  }

  return MOBase::sanitizeFileName(text->text());
}

void showInstanceExample(QWidget* parent)
{
  FluentContentDialog dlg(
      parent, QStringLiteral("InstanceHelpDialog"), QObject::tr("What is an instance?"),
      QObject::tr("An instance is its own Mod Organizer workspace. It keeps one game's "
                  "profiles, mods, downloads and settings together, so separate setups "
                  "never mix."));

  auto addExample = [&](char16_t glyph, const QString& game, const QString& detail) {
    auto* row = new QFrame(&dlg);
    row->setObjectName(QStringLiteral("instanceHelpExample"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(14, 10, 14, 10);
    rowLayout->setSpacing(14);
    rowLayout->addWidget(glyphLabel(glyph, row));

    auto* text = new QVBoxLayout;
    text->setSpacing(1);
    auto* gameLabel = new QLabel(game, row);
    gameLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
    auto* detailLabel = new QLabel(detail, row);
    detailLabel->setObjectName(QStringLiteral("fluentSettingDescription"));
    detailLabel->setWordWrap(true);
    text->addWidget(gameLabel);
    text->addWidget(detailLabel);
    rowLayout->addLayout(text, 1);
    dlg.body()->addWidget(row);
  };

  addExample(GameGlyph, QObject::tr("Elden Ring"),
             QObject::tr("Its mods, profiles and saves stay in this instance."));
  addExample(GameGlyph, QObject::tr("Skyrim Special Edition"),
             QObject::tr("A second instance manages Skyrim without touching Elden Ring."));
  addExample(DriveGlyph, QObject::tr("Global or portable"),
             QObject::tr("Global instances are stored in your user folder; portable ones "
                         "live next to Mod Organizer and move with it."));
  dlg.body()->addStretch(1);

  auto* close = dlg.addButton(QObject::tr("Close"), true);
  QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
  dlg.resize(520, dlg.sizeHint().height());
  dlg.exec();
}

InstanceManagerDialog::~InstanceManagerDialog()
{
  if (m_sizeScan) {
    *m_sizeScan = true;
  }
}

InstanceManagerDialog::InstanceManagerDialog(PluginContainer& pc, QWidget* parent)
    : QDialog(parent), m_pc(pc), m_model(new QStandardItemModel(this)),
      m_restartOnSelect(true),
      m_groupBy(GlobalSettings::value(QStringLiteral("InstanceManager/groupBy"),
                                      QStringLiteral("game"))
                    .toString()),
      m_sortBy(GlobalSettings::value(QStringLiteral("InstanceManager/sortBy"),
                                     QStringLiteral("name"))
                   .toString())
{
  setupUi();

  updateInstances();
  updateList();
  m_list->clearSelection();
  m_list->setCurrentIndex(QModelIndex());
  clearData();

  selectActiveInstance();
}

void InstanceManagerDialog::setupUi()
{
  setObjectName(QStringLiteral("InstanceManagerDialog"));
  setWindowTitle(tr("Instances"));
  setMinimumSize(880, 560);
  resize(1000, 680);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  auto* body = new QHBoxLayout;
  body->setSpacing(8);
  body->addWidget(createListCard());
  body->addWidget(createDetails(), 1);
  layout->addLayout(body, 1);

  // help on the left, Switch and Close on the right
  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  auto* help = new QToolButton(this);
  help->setObjectName(QStringLiteral("instanceHelpButton"));
  help->setText(tr("What is an instance?"));
  help->setIcon(FluentIcons::glyph(HelpGlyph, FluentIcons::Tone::Muted));
  help->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  help->setAutoRaise(true);
  help->setCursor(Qt::PointingHandCursor);
  footer->addWidget(help);
  footer->addStretch(1);

  m_switch = new QPushButton(this);
  m_switch->setObjectName(QStringLiteral("switchToInstance"));
  m_switch->setProperty("fluentPrimary", true);
  m_switch->setAutoDefault(false);
  m_switch->setToolTip(tr("Restart Mod Organizer with this instance"));
  auto* close = new QPushButton(tr("Close"), this);
  close->setObjectName(QStringLiteral("close"));
  close->setAutoDefault(false);
  footer->addWidget(m_switch);
  footer->addWidget(close);
  layout->addLayout(footer);

  connect(help, &QToolButton::clicked, this, [this] {
    showInstanceExample(this);
  });

  // keyboard: Enter opens (the list does that), F2 renames, Delete deletes,
  // Ctrl+D duplicates, Ctrl+N creates and Ctrl+F searches; each only does
  // what its button could do right now
  auto shortcut = [this](const QKeySequence& keys, auto action) {
    auto* sc = new QShortcut(keys, this);
    sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc, &QShortcut::activated, this, action);
  };
  auto when = [](QWidget* button, void (InstanceManagerDialog::*slot)(),
                 InstanceManagerDialog* self) {
    return [button, slot, self] {
      if (button->isEnabled() && !button->isHidden()) {
        (self->*slot)();
      }
    };
  };
  shortcut(QKeySequence(Qt::Key_F2), when(m_rename, &InstanceManagerDialog::rename, this));
  shortcut(QKeySequence(Qt::Key_Delete),
           when(m_delete, &InstanceManagerDialog::deleteInstance, this));
  shortcut(QKeySequence(Qt::CTRL | Qt::Key_D),
           when(m_duplicate, &InstanceManagerDialog::duplicateInstance, this));
  shortcut(QKeySequence(Qt::CTRL | Qt::Key_N),
           when(m_new, &InstanceManagerDialog::createNew, this));
  shortcut(QKeySequence::Find, [this] {
    m_filter->setVisible(true);
    m_filter->setFocus();
    m_filter->selectAll();
  });
  connect(m_switch, &QPushButton::clicked, this,
          &InstanceManagerDialog::openSelectedInstance);
  connect(close, &QPushButton::clicked, this, &QDialog::close);
}

QWidget* InstanceManagerDialog::createListCard()
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("instanceListCard"));
  card->setProperty("fluentCard", true);
  card->setFixedWidth(260);

  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Instances"), card);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  m_listCount = new QLabel(card);
  m_listCount->setObjectName(QStringLiteral("fluentStatCaption"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(m_listCount);

  // says how the list is grouped; its menu changes the grouping and order
  m_view = new QToolButton(card);
  m_view->setObjectName(QStringLiteral("instanceViewButton"));
  m_view->setAutoRaise(true);
  m_view->setToolTip(tr("Group and sort the instances"));
  m_view->setCursor(Qt::PointingHandCursor);
  m_view->setPopupMode(QToolButton::InstantPopup);
  m_view->setProperty("fluentKeepStyle", true);
  auto* viewMenu = new QMenu(m_view);
  m_view->setMenu(viewMenu);
  FluentMotion::animateMenuChevron(m_view);
  title->addWidget(m_view);
  layout->addLayout(title);

  // the menu is built when it opens, so its checks follow the current choice
  connect(viewMenu, &QMenu::aboutToShow, this, [this, viewMenu] {
    viewMenu->clear();
    auto addChoices = [&](const QString& section, QString& current, const char* key,
                          const std::vector<std::pair<QString, QString>>& choices) {
      // the theme hides section titles, so the title is a dimmed entry
      if (!viewMenu->isEmpty()) {
        viewMenu->addSeparator();
      }
      viewMenu->addAction(section)->setEnabled(false);
      auto* group = new QActionGroup(viewMenu);
      for (const auto& [value, text] : choices) {
        auto* action = viewMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(current == value);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, &current, key, value = value] {
          current = value;
          GlobalSettings::setValue(QString::fromLatin1(key), value);
          updateList();
        });
      }
    };
    addChoices(tr("Group by"), m_groupBy, "InstanceManager/groupBy",
               {{QStringLiteral("game"), tr("Game")},
                {QStringLiteral("custom"), tr("My groups")},
                {QStringLiteral("storage"), tr("Storage")},
                {QStringLiteral("none"), tr("No groups")}});
    addChoices(tr("Sort by"), m_sortBy, "InstanceManager/sortBy",
               {{QStringLiteral("name"), tr("Name")},
                {QStringLiteral("used"), tr("Last used")}});
    FluentIcons::apply(viewMenu);
  });

  // only shown when there are enough instances to need it
  m_filter = new QLineEdit(card);
  m_filter->setObjectName(QStringLiteral("instanceFilter"));
  m_filter->setPlaceholderText(tr("Search instances"));
  m_filter->setClearButtonEnabled(true);
  layout->addWidget(m_filter);
  connect(m_filter, &QLineEdit::textChanged, this, &InstanceManagerDialog::applyFilter);

  m_list = new QListView(card);
  m_list->setObjectName(QStringLiteral("instanceList"));
  m_list->setModel(m_model);
  m_list->setItemDelegate(new InstanceRowDelegate(m_list));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setMouseTracking(true);
  m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setAccessibleName(tr("Instances"));
  m_list->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(m_list, 1);
  connect(m_list, &QListView::customContextMenuRequested, this,
          &InstanceManagerDialog::showListMenu);

  m_new = new QPushButton(FluentIcons::glyph(AddGlyph), tr("New instance"), card);
  m_new->setObjectName(QStringLiteral("createNew"));
  m_new->setAutoDefault(false);
  m_new->setToolTip(tr("Create an instance for another game or setup (Ctrl+N)"));
  m_new->setVisible(!InstanceManager::singleton().isEldenRingOnlyPortableMode());
  layout->addWidget(m_new);

  connect(m_new, &QPushButton::clicked, this, &InstanceManagerDialog::createNew);
  connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &InstanceManagerDialog::onSelection);
  connect(m_list, &QListView::activated, this, &InstanceManagerDialog::openSelectedInstance);

  return card;
}

QWidget* InstanceManagerDialog::createDetails()
{
  m_detailsStack = new QStackedWidget(this);
  m_detailsStack->setObjectName(QStringLiteral("instanceDetailsStack"));

  // nothing selected
  auto* empty = new QFrame(m_detailsStack);
  empty->setProperty("fluentCard", true);
  auto* emptyLayout = new QVBoxLayout(empty);
  m_emptyText = new QLabel(empty);
  m_emptyText->setObjectName(QStringLiteral("instanceEmptyText"));
  m_emptyText->setAlignment(Qt::AlignCenter);
  m_emptyText->setWordWrap(true);
  emptyLayout->addWidget(m_emptyText);
  m_detailsStack->addWidget(empty);

  auto* card = new QFrame(m_detailsStack);
  card->setObjectName(QStringLiteral("instanceDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // the name, its game, storage and last use below it, and the pencil to
  // rename it
  auto* head = new QHBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(10);
  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  m_name = new QLabel(card);
  m_name->setObjectName(QStringLiteral("instanceDetailName"));
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  names->addWidget(m_name);
  names->addWidget(m_caption);
  head->addLayout(names, 1);
  m_rename = new QToolButton(card);
  m_rename->setObjectName(QStringLiteral("instanceIconButton"));
  m_rename->setIcon(FluentIcons::glyph(RenameGlyph));
  m_rename->setAutoRaise(true);
  m_rename->setAccessibleName(tr("Rename"));
  m_rename->setCursor(Qt::PointingHandCursor);
  head->addWidget(m_rename, 0, Qt::AlignVCenter);
  layout->addLayout(head);
  connect(m_rename, &QToolButton::clicked, this, &InstanceManagerDialog::rename);

  // the rows scroll when the window is too short for all of them
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

  m_locationRow = addDetailRow(rows, FolderGlyph, tr("Instance folder"),
                               tr("Open the instance folder"));
  m_baseRow = addDetailRow(rows, LibraryGlyph, tr("Mod data"), tr("Open the mod data folder"));
  m_gameRow = addDetailRow(rows, GameGlyph, tr("Game folder"), tr("Open the game folder"));
  m_profilesRow = addDetailRow(rows, PeopleGlyph, tr("Profiles"), tr("Open the profiles folder"));
  m_sizeRow     = addDetailRow(rows, SizeGlyph, tr("Size on disk"), {});
  m_iniRow      = addDetailRow(rows, DocumentGlyph, tr("Configuration file"),
                               tr("Open ModOrganizer.ini in its editor"), DocumentOpenGlyph);
  m_storageRow  = addDetailRow(rows, HomeGlyph, tr("Storage"), {});
  m_groupRow    = addDetailRow(rows, TagGlyph, tr("Group"), tr("Move to another group"),
                               RenameGlyph);
  m_notesRow    = addDetailRow(rows, NoteGlyph, tr("Notes"), tr("Edit the notes"), RenameGlyph);
  rowsLayout->addStretch(1);

  // the rows only scroll when the screen is too short for all of them
  const QScreen* screen = QGuiApplication::primaryScreen();
  const int available   = screen != nullptr ? screen->availableGeometry().height() : 1000;
  scroll->setMinimumHeight(qMin(rows->sizeHint().height(), available - 330));

  connect(m_locationRow.action, &QToolButton::clicked, this,
          &InstanceManagerDialog::exploreLocation);
  connect(m_baseRow.action, &QToolButton::clicked, this,
          &InstanceManagerDialog::exploreBaseDirectory);
  connect(m_gameRow.action, &QToolButton::clicked, this, &InstanceManagerDialog::exploreGame);
  connect(m_profilesRow.action, &QToolButton::clicked, this, [this] {
    if (const auto* i = singleSelection()) {
      Settings s(i->iniPath());
      shell::Explore(s.paths().profiles());
    }
  });
  connect(m_iniRow.action, &QToolButton::clicked, this, &InstanceManagerDialog::openINI);
  connect(m_groupRow.action, &QToolButton::clicked, this,
          &InstanceManagerDialog::changeGroup);
  connect(m_notesRow.action, &QToolButton::clicked, this, &InstanceManagerDialog::editNotes);

  // copy and shortcut on the left, delete on the right
  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  const bool isolated = InstanceManager::singleton().isEldenRingOnlyPortableMode();
  m_duplicate = new QPushButton(FluentIcons::glyph(CopyGlyph), tr("Duplicate"), actions);
  m_duplicate->setObjectName(QStringLiteral("duplicateInstance"));
  m_duplicate->setAutoDefault(false);
  m_duplicate->setVisible(!isolated);
  m_shortcut = new QPushButton(FluentIcons::glyph(PinGlyph), tr("Desktop shortcut"), actions);
  m_shortcut->setObjectName(QStringLiteral("instanceShortcut"));
  m_shortcut->setAutoDefault(false);
  m_delete = new QPushButton(FluentIcons::glyph(DeleteGlyph, FluentIcons::Tone::Critical),
                             tr("Delete instance"), actions);
  m_delete->setObjectName(QStringLiteral("deleteInstance"));
  m_delete->setAutoDefault(false);
  m_delete->setVisible(!isolated);
  actionsLayout->addWidget(m_duplicate);
  actionsLayout->addWidget(m_shortcut);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_delete);
  layout->addWidget(actions);
  connect(m_duplicate, &QPushButton::clicked, this, &InstanceManagerDialog::duplicateInstance);
  connect(m_shortcut, &QPushButton::clicked, this, &InstanceManagerDialog::toggleShortcut);
  connect(m_delete, &QPushButton::clicked, this, &InstanceManagerDialog::deleteInstance);

  m_detailsStack->addWidget(card);
  return m_detailsStack;
}

InstanceManagerDialog::DetailRow
InstanceManagerDialog::addDetailRow(QWidget* parent, char16_t glyph, const QString& title,
                                    const QString& tip, char16_t actionGlyph)
{
  // a Settings row: glyph, title with the value below it, and a button that
  // opens it when there is something to open
  DetailRow row;
  auto* frame = new QFrame(parent);
  frame->setObjectName(QStringLiteral("fluentSettingRow"));
  frame->setMinimumHeight(62);

  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(18, 8, 12, 8);
  layout->setSpacing(14);
  layout->addWidget(glyphLabel(glyph, frame));

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  auto* titleLabel = new QLabel(title, frame);
  titleLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
  row.value = new ElidedLabel(frame);
  row.value->setObjectName(QStringLiteral("instanceDetailValue"));
  text->addWidget(titleLabel);
  text->addWidget(row.value);
  layout->addLayout(text, 1);

  if (!tip.isEmpty()) {
    row.action = new QToolButton(frame);
    row.action->setObjectName(QStringLiteral("instanceIconButton"));
    row.action->setIcon(FluentIcons::glyph(actionGlyph));
    row.action->setAutoRaise(true);
    row.action->setToolTip(tip);
    row.action->setAccessibleName(tip);
    row.action->setCursor(Qt::PointingHandCursor);
    layout->addWidget(row.action);
  }

  parent->layout()->addWidget(frame);
  return row;
}

void InstanceManagerDialog::showListMenu(const QPoint& pos)
{
  const QModelIndex index = m_list->indexAt(pos);
  if (!index.isValid()) {
    return;
  }
  if (index.data(HeaderRole).toBool()) {
    showGroupMenu(index.data(GroupRole).toString(), m_list->viewport()->mapToGlobal(pos));
    return;
  }
  select(static_cast<std::size_t>(index.data(IndexRole).toInt()));
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  // the same actions as the buttons, in the same states
  QMenu menu(this);
  auto add = [&](char16_t glyph, const QString& text, const QWidget* twin, auto slot,
                 const QKeySequence& keys = {}) {
    auto* action = menu.addAction(FluentIcons::glyph(glyph), text, this, slot);
    action->setEnabled(twin == nullptr || twin->isEnabled());
    action->setVisible(twin == nullptr || !twin->isHidden());
    if (!keys.isEmpty()) {
      action->setShortcut(keys);
      action->setShortcutVisibleInContextMenu(true);
    }
    return action;
  };
  add(PlayGlyph, m_switch->text(), m_switch, &InstanceManagerDialog::openSelectedInstance,
      QKeySequence(Qt::Key_Enter));
  menu.addSeparator();
  add(RenameGlyph, tr("Rename"), m_rename, &InstanceManagerDialog::rename,
      QKeySequence(Qt::Key_F2));
  add(CopyGlyph, tr("Duplicate"), m_duplicate, &InstanceManagerDialog::duplicateInstance,
      QKeySequence(Qt::CTRL | Qt::Key_D));
  add(PinGlyph, m_shortcut->text(), m_shortcut, &InstanceManagerDialog::toggleShortcut);
  add(TagGlyph, tr("Move to group\u2026"), nullptr, &InstanceManagerDialog::changeGroup);
  add(NoteGlyph, tr("Edit notes\u2026"), nullptr, &InstanceManagerDialog::editNotes);
  menu.addSeparator();
  add(FolderGlyph, tr("Open instance folder"), nullptr, &InstanceManagerDialog::exploreLocation);
  add(GameGlyph, tr("Open game folder"), m_gameRow.action, &InstanceManagerDialog::exploreGame);
  add(DocumentGlyph, tr("Open ModOrganizer.ini"), nullptr, &InstanceManagerDialog::openINI);
  menu.addSeparator();
  add(DeleteGlyph, tr("Delete instance"), m_delete, &InstanceManagerDialog::deleteInstance,
      QKeySequence(Qt::Key_Delete));

  FluentIcons::apply(&menu);
  menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void InstanceManagerDialog::showEvent(QShowEvent* e)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  const auto* s = Settings::maybeInstance();

  if (s) {
    s->geometry().restoreGeometry(this);
  }

  // never shorter than the details need, so their rows don't scroll; a size
  // saved before the rows were added grows to fit
  const int needed = qMax(560, layout()->minimumSize().height());
  setMinimumHeight(needed);
  if (height() < needed) {
    resize(width(), needed);
  }

  // a window that grew stays on its screen; checked once it is shown, when
  // its frame and its place are final
  QTimer::singleShot(0, this, [this] {
    const QScreen* sc = QGuiApplication::screenAt(frameGeometry().center());
    if (sc == nullptr) {
      return;
    }
    const QRect area  = sc->availableGeometry();
    const QRect frame = frameGeometry();
    if (frame.bottom() > area.bottom()) {
      move(pos().x(), qMax(area.top(), pos().y() - (frame.bottom() - area.bottom())));
    }
  });

  QDialog::showEvent(e);
}

void InstanceManagerDialog::done(int r)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  auto* s = Settings::maybeInstance();

  if (s) {
    s->geometry().saveGeometry(this);
  }

  QDialog::done(r);
}

void InstanceManagerDialog::updateInstances()
{
  auto& m = InstanceManager::singleton();

  m_instances.clear();

  for (auto&& d : m.portableInstancePaths()) {
    m_instances.push_back(std::make_unique<Instance>(d, true));
  }

  for (auto&& d : m.globalInstancePaths()) {
    m_instances.push_back(std::make_unique<Instance>(d, false));
  }

  // Keep portable storage together, with the legacy root entry first.
  std::sort(m_instances.begin(), m_instances.end(), [](auto&& a, auto&& b) {
    if (a->isPortable() != b->isPortable()) {
      return a->isPortable();
    }

    const auto root = InstanceManager::singleton().portablePath();
    const auto normalizedRoot =
        QDir::cleanPath(QDir::fromNativeSeparators(root)).toCaseFolded();
    const auto normalizedA = QDir::cleanPath(
                                 QDir::fromNativeSeparators(a->directory()))
                                 .toCaseFolded();
    const auto normalizedB = QDir::cleanPath(
                                 QDir::fromNativeSeparators(b->directory()))
                                 .toCaseFolded();
    const bool aIsRoot = a->isPortable() && normalizedA == normalizedRoot;
    const bool bIsRoot = b->isPortable() && normalizedB == normalizedRoot;
    if (aIsRoot != bIsRoot) {
      return aIsRoot;
    }

    return (MOBase::naturalCompare(a->displayName(), b->displayName()) < 0);
  });

  // read all inis, ignore errors
  for (auto&& i : m_instances) {
    i->readFromIni();
  }
}

void InstanceManagerDialog::updateList()
{
  const auto prevSelIndex = singleSelectionIndex();
  const auto* prevSel     = singleSelection();

  m_model->clear();

  // the order inside each group
  std::vector<std::size_t> order(m_instances.size());
  std::iota(order.begin(), order.end(), 0);
  if (m_sortBy == QLatin1String("used")) {
    std::vector<QDateTime> used;
    for (const auto& ii : m_instances) {
      used.push_back(lastUsedTime(*ii));
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return used[a] > used[b];
    });
  } else {
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return MOBase::naturalCompare(m_instances[a]->displayName(),
                                    m_instances[b]->displayName()) < 0;
    });
  }

  // the group of each instance; an empty name is "no group", listed last
  auto groupOf = [&](const Instance& ii) -> QString {
    if (m_groupBy == QLatin1String("game")) {
      return ii.gameName().isEmpty() ? tr("Unknown game") : ii.gameName();
    } else if (m_groupBy == QLatin1String("storage")) {
      return ii.isPortable() ? tr("Portable") : tr("Global");
    } else if (m_groupBy == QLatin1String("custom")) {
      return instanceGroup(ii.instanceIdentifier());
    }
    return {};
  };

  QStringList groups;
  std::vector<QString> groupOfIndex(m_instances.size());
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    groupOfIndex[i] = groupOf(*m_instances[i]);
    if (!groups.contains(groupOfIndex[i])) {
      groups.push_back(groupOfIndex[i]);
    }
  }
  std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
    if (a.isEmpty() != b.isEmpty()) {
      return b.isEmpty();
    }
    return MOBase::naturalCompare(a, b) < 0;
  });

  // headings only help when there is more than one group, or when the user
  // has made groups of their own
  const bool headings = m_groupBy != QLatin1String("none") &&
                        (groups.size() > 1 || (m_groupBy == QLatin1String("custom") &&
                                               !groups.isEmpty() && !groups[0].isEmpty()));

  std::size_t sel = NoSelection;

  for (const QString& group : groups) {
    std::vector<std::size_t> members;
    for (const auto i : order) {
      if (groupOfIndex[i] == group) {
        members.push_back(i);
      }
    }

    if (headings) {
      auto* heading = new QStandardItem(
          QStringLiteral("%1 (%2)")
              .arg(group.isEmpty() ? tr("No group") : group)
              .arg(members.size()));
      heading->setData(true, HeaderRole);
      heading->setData(-1, IndexRole);
      heading->setData(group, GroupRole);
      heading->setFlags(Qt::ItemIsEnabled);
      m_model->appendRow(heading);
    }

    for (const auto i : members) {
      const auto& ii = *m_instances[i];

      QString caption = ii.isPortable() ? tr("Portable") : tr("Global");
      if (ii.isActive()) {
        caption += QStringLiteral(" · ") + tr("current");
      } else if (!ii.gameName().isEmpty() && m_groupBy != QLatin1String("game")) {
        caption += QStringLiteral(" · ") + ii.gameName();
      } else if (const QString used = lastUsed(ii); !used.isEmpty()) {
        caption += QStringLiteral(" · ") + used;
      }

      auto* item = new QStandardItem(ii.displayName());
      item->setIcon(instanceIcon(m_pc, ii, this));
      item->setData(caption, CaptionRole);
      item->setData(static_cast<int>(i), IndexRole);
      item->setData(instanceValue(NotesSection, ii.instanceIdentifier()), NotesRole);
      item->setToolTip(QDir::toNativeSeparators(ii.directory()));
      m_model->appendRow(item);

      if (&ii == prevSel) {
        sel = i;
      }
    }
  }

  m_listCount->setText(QString::number(m_instances.size()));

  const QString grouping = m_groupBy == QLatin1String("custom")    ? tr("My groups")
                           : m_groupBy == QLatin1String("storage") ? tr("By storage")
                           : m_groupBy == QLatin1String("none")    ? tr("All instances")
                                                                   : tr("By game");
  m_view->setText(grouping);
  m_view->setAccessibleName(tr("Grouping: %1").arg(grouping));

  m_filter->setVisible(m_instances.size() > 6);
  if (!m_filter->isVisible()) {
    m_filter->clear();
  }

  applyFilter();

  // keep current selection or select the next one if there was a selection;
  // there's no selection when opening the dialog, that's handled in the ctor
  if (prevSel) {
    if (m_instances.empty()) {
      select(-1);
    } else {
      if (sel == NoSelection) {
        if (prevSelIndex >= m_instances.size()) {
          sel = m_instances.size() - 1;
        } else {
          sel = prevSelIndex;
        }
      }

      select(sel);
    }
  }
}

int InstanceManagerDialog::rowOf(std::size_t index) const
{
  for (int row = 0; row < m_model->rowCount(); ++row) {
    if (m_model->item(row)->data(IndexRole).toInt() == static_cast<int>(index)) {
      return row;
    }
  }
  return -1;
}

void InstanceManagerDialog::applyFilter()
{
  // a heading hides when none of its instances match
  const QString text = m_filter->text().trimmed();
  int heading        = -1;
  bool anyShown      = false;
  auto closeHeading  = [&] {
    if (heading >= 0) {
      m_list->setRowHidden(heading, !anyShown);
    }
  };

  for (int row = 0; row < m_model->rowCount(); ++row) {
    const QStandardItem* item = m_model->item(row);
    if (item->data(HeaderRole).toBool()) {
      closeHeading();
      heading  = row;
      anyShown = false;
      continue;
    }
    const bool textMatch =
        text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive) ||
        item->data(CaptionRole).toString().contains(text, Qt::CaseInsensitive) ||
        item->data(NotesRole).toString().contains(text, Qt::CaseInsensitive);
    m_list->setRowHidden(row, !textMatch);
    anyShown |= textMatch;
  }
  closeHeading();
}

void InstanceManagerDialog::select(std::size_t i)
{
  if (i < m_instances.size()) {
    const auto& ii = m_instances[i];
    fillData(*ii);

    m_list->selectionModel()->select(m_model->index(rowOf(i), 0),
                                     QItemSelectionModel::ClearAndSelect);
  } else {
    clearData();
  }
}

void InstanceManagerDialog::select(const QString& name)
{
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    if (m_instances[i]->displayName() == name) {
      select(i);
      return;
    }
  }

  log::error("can't select instance {}, not in list", name);
}

void InstanceManagerDialog::selectActiveInstance()
{
  const auto active = InstanceManager::singleton().currentInstance();

  if (active) {
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
      if (m_instances[i]->instanceIdentifier() ==
          active->instanceIdentifier()) {
        select(i);

        m_list->scrollTo(m_model->index(rowOf(i), 0));

        return;
      }
    }
  }

  if (!m_instances.empty()) {
    select(0);
    m_list->scrollTo(m_model->index(rowOf(0), 0));
    return;
  }

  clearData();
}

void InstanceManagerDialog::openSelectedInstance()
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    return;
  }

  const auto& to = *m_instances[i];

  if (to.isActive()) {
    accept();
    return;
  }

  if (!confirmSwitch(to)) {
    return;
  }

  InstanceManager::singleton().setCurrentInstance(to);

  if (m_restartOnSelect) {
    ExitModOrganizer(Exit::Restart);
  }

  accept();
}

bool InstanceManagerDialog::confirmSwitch(const Instance& to)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  auto* s = Settings::maybeInstance();

  // if there is are no settings, no instances are loaded and the confirmation
  // wouldn't make sense
  if (!s) {
    return true;
  }

  if (!s->interface().showChangeGameConfirmation()) {
    // user disabled confirmation
    return true;
  }

  FluentContentDialog dlg(
      this, QStringLiteral("SwitchInstanceDialog"),
      tr("Restart to open %1?").arg(to.displayName()),
      tr("Mod Organizer closes and opens again with the instance %1. Programs started "
         "from Mod Organizer should be closed first.")
          .arg(to.displayName()));

  auto* dontAsk = new QCheckBox(tr("Don't ask me again"), &dlg);
  dontAsk->setObjectName(QStringLiteral("switchInstanceDontAsk"));
  dlg.body()->addWidget(dontAsk);

  auto* restart = dlg.addButton(tr("Restart"), true);
  auto* cancel  = dlg.addButton(tr("Cancel"));
  connect(restart, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  dlg.resize(480, dlg.sizeHint().height());

  if (dlg.exec() != QDialog::Accepted) {
    return false;
  }

  if (dontAsk->isChecked()) {
    s->interface().setShowChangeGameConfirmation(false);
  }

  return true;
}

void InstanceManagerDialog::rename()
{
  auto* i = singleSelection();
  if (!i) {
    return;
  }

  const auto selIndex = singleSelectionIndex();

  auto& m = InstanceManager::singleton();
  if (i->isActive()) {
    QMessageBox::information(this, tr("Rename instance"),
                             tr("The active instance cannot be renamed."));
    return;
  }

  const bool isRootPortable =
      i->isPortable() &&
      QDir::cleanPath(QDir::fromNativeSeparators(i->directory()))
              .compare(QDir::cleanPath(QDir::fromNativeSeparators(m.portablePath())),
                       Qt::CaseInsensitive) == 0;
  if (isRootPortable) {
    QMessageBox::information(
        this, tr("Rename instance"),
        tr("The portable instance in the MO2 installation folder cannot be renamed."));
    return;
  }

  // getting new name
  const auto parentDirectory = QFileInfo(i->directory()).dir().absolutePath();
  const auto oldName = QFileInfo(i->directory()).fileName();
  const auto newName = getInstanceName(
      this, tr("Rename instance"), tr("The instance folder is renamed too."),
      tr("Instance name"), parentDirectory, oldName);

  if (newName.isEmpty()) {
    return;
  }

  // renaming
  const QString src = i->directory();
  const QString dest =
      QDir::toNativeSeparators(QFileInfo(src).dir().path() + "/" + newName);

  log::info("renaming {} to {}", src, dest);

  const auto r = shell::Rename(QFileInfo(src), QFileInfo(dest), false);

  if (!r) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to rename \"%1\" to \"%2\": %3")
                              .arg(src)
                              .arg(dest)
                              .arg(r.toString()));

    return;
  }

  // updating ui; the group follows the new name
  auto newInstance = std::make_unique<Instance>(dest, i->isPortable());
  newInstance->readFromIni();
  moveInstanceValues(i->instanceIdentifier(), newInstance->instanceIdentifier(), false);

  m_instances[selIndex] = std::move(newInstance);
  updateList();
  select(selIndex);
}

void InstanceManagerDialog::exploreLocation()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->directory());
  }
}

void InstanceManagerDialog::exploreBaseDirectory()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->baseDirectory());
  }
}

void InstanceManagerDialog::exploreGame()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->gameDirectory());
  }
}

void InstanceManagerDialog::openINI()
{
  if (const auto* i = singleSelection()) {
    shell::Open(i->iniPath());
  }
}

void InstanceManagerDialog::deleteInstance()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  if (i->isActive()) {
    QMessageBox::information(this, tr("Delete instance"),
                             tr("The active instance cannot be deleted."));
    return;
  }

  const auto files = i->objectsForDeletion();

  FluentContentDialog dlg(
      this, QStringLiteral("DeleteInstanceDialog"),
      tr("Delete %1?").arg(i->displayName()),
      tr("Choose what to remove. Items the instance can't do without stay checked."));

  // the paths in a card of check box rows
  auto* list = new QFrame(&dlg);
  list->setObjectName(QStringLiteral("deleteInstanceList"));
  auto* listLayout = new QVBoxLayout(list);
  listLayout->setContentsMargins(0, 0, 0, 0);
  listLayout->setSpacing(0);

  QList<std::pair<QCheckBox*, QString>> checks;
  for (const auto& f : files) {
    auto* row = new QFrame(list);
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    if (checks.isEmpty()) {
      row->setProperty("fluentGridRow", true);
    }
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(14, 8, 14, 8);
    auto* check = new QCheckBox(QDir::toNativeSeparators(f.path), row);
    check->setObjectName(QStringLiteral("deleteInstancePathCheck"));
    check->setToolTip(f.path);
    check->setChecked(f.mandatoryDelete);
    check->setEnabled(!f.mandatoryDelete);
    rowLayout->addWidget(check, 1);
    listLayout->addWidget(row);
    checks.append({check, f.path});
  }

  if (checks.isEmpty()) {
    auto* none = new QLabel(tr("No files or folders were found."), list);
    none->setObjectName(QStringLiteral("fluentSettingDescription"));
    none->setContentsMargins(14, 10, 14, 10);
    listLayout->addWidget(none);
  }
  listLayout->addStretch(1);

  auto* scroll = new QScrollArea(&dlg);
  scroll->setObjectName(QStringLiteral("deleteInstanceScroll"));
  scroll->setWidget(list);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setMinimumHeight(qMin(60 + 40 * static_cast<int>(checks.size()), 260));
  dlg.body()->addWidget(scroll, 1);

  enum Choice
  {
    Cancelled = 0,
    Recycle,
    Permanent
  };

  auto* recycle   = dlg.addButton(tr("Move to Recycle Bin"), true);
  auto* permanent = dlg.addButton(tr("Delete permanently"));
  auto* cancel    = dlg.addButton(tr("Cancel"));
  connect(recycle, &QPushButton::clicked, &dlg, [&dlg] {
    dlg.done(Recycle);
  });
  connect(permanent, &QPushButton::clicked, &dlg, [&dlg] {
    dlg.done(Permanent);
  });
  connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  dlg.resize(620, dlg.sizeHint().height());

  const auto r = dlg.exec();

  if (r != Recycle && r != Permanent) {
    return;
  }

  // gathering all the selected items
  QStringList selected;

  for (const auto& [check, path] : checks) {
    if (check->isChecked()) {
      selected.append(path);
    }
  }

  if (selected.isEmpty()) {
    QMessageBox::information(this, tr("Delete instance"), tr("Nothing to delete."));

    return;
  }

  // deleting
  const QString identifier = i->instanceIdentifier();
  if (!doDelete(selected, (r == Recycle))) {
    return;
  }
  setInstanceValue(GroupsSection, identifier, {});
  setInstanceValue(NotesSection, identifier, {});

  // updating ui
  updateInstances();
  updateList();
}

void InstanceManagerDialog::setRestartOnSelect(bool b)
{
  m_restartOnSelect = b;

  if (const auto* i = singleSelection()) {
    fillData(*i);
  }
}

bool InstanceManagerDialog::doDelete(const QStringList& files, bool recycle)
{
  // logging
  for (auto&& f : files) {
    if (recycle) {
      log::info("will recycle {}", f);
    } else {
      log::info("will delete {}", f);
    }
  }

  if (MOBase::shellDelete(files, recycle, this)) {
    return true;
  }

  const auto e = GetLastError();
  if (e == ERROR_CANCELLED) {
    log::debug("deletion cancelled by user");
  } else {
    log::error("failed to delete, {}", formatSystemMessage(e));
  }

  return false;
}

void InstanceManagerDialog::onSelection()
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    clearData();
    return;
  }

  fillData(*m_instances[i]);
}

void InstanceManagerDialog::createNew()
{
  if (InstanceManager::singleton().isEldenRingOnlyPortableMode()) {
    return;
  }

  // there might not be settings available; the dialog can be shown when the
  // last selected instance doesn't exist anymore
  CreateInstanceDialog dlg(m_pc, Settings::maybeInstance(), this);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  if (dlg.switching()) {
    // restarting MO
    accept();
    return;
  }

  updateInstances();
  updateList();

  const auto info = dlg.creationInfo();
  Instance created(info.dataPath, info.type == CreateInstanceDialog::Portable);
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    if (m_instances[i]->instanceIdentifier() ==
        created.instanceIdentifier()) {
      select(i);
      break;
    }
  }
}

std::size_t InstanceManagerDialog::singleSelectionIndex() const
{
  const auto sel = m_list->selectionModel()->selectedIndexes();

  if (sel.size() != 1) {
    return NoSelection;
  }

  const int index = sel[0].data(IndexRole).toInt();
  return index < 0 ? NoSelection : static_cast<std::size_t>(index);
}

const Instance* InstanceManagerDialog::singleSelection() const
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    return nullptr;
  }

  return m_instances[i].get();
}

void InstanceManagerDialog::fillData(const Instance& ii)
{
  m_detailsStack->setCurrentIndex(1);

  const auto& m     = InstanceManager::singleton();
  const bool active = ii.isActive();
  auto setValue     = [](DetailRow& row, const QString& text, const QString& tone = {}) {
    static_cast<ElidedLabel*>(row.value)->setFullText(text);
    if (row.value->property("tone").toString() != tone) {
      row.value->setProperty("tone", tone);
      row.value->style()->unpolish(row.value);
      row.value->style()->polish(row.value);
    }
  };

  // name and caption: game, storage, and when it was last used
  m_name->setText(ii.displayName());
  QStringList caption;
  if (!ii.gameName().isEmpty()) {
    caption << ii.gameName();
  }
  caption << (ii.isPortable() ? tr("Portable") : tr("Global"));
  if (active) {
    caption << tr("Current instance");
  } else if (const QString used = lastUsed(ii); !used.isEmpty()) {
    caption << tr("Last used %1").arg(used);
  }
  m_caption->setText(caption.join(QStringLiteral(" · ")));

  // folders
  Settings settings(ii.iniPath());
  const bool settingsRead = settings.iniStatus() == QSettings::NoError;

  setValue(m_locationRow, QDir::toNativeSeparators(ii.directory()));
  const bool sameBase = QDir::cleanPath(QDir::fromNativeSeparators(ii.baseDirectory()))
                            .compare(QDir::cleanPath(QDir::fromNativeSeparators(ii.directory())),
                                     Qt::CaseInsensitive) == 0;
  setValue(m_baseRow, sameBase ? tr("Same as the instance folder")
                               : QDir::toNativeSeparators(ii.baseDirectory()));

  // the game: a missing folder or plugin is said right away, before a switch
  // would fail
  const bool hasPlugin = m.gamePluginForDirectory(ii.directory(), m_pc) != nullptr;
  const bool gameFound = !ii.gameDirectory().isEmpty() && QDir(ii.gameDirectory()).exists();
  if (!hasPlugin) {
    setValue(m_gameRow,
             tr("No game plugin can manage %1. The instance can't be opened.")
                 .arg(ii.gameName().isEmpty() ? tr("this game") : ii.gameName()),
             QStringLiteral("critical"));
  } else if (!gameFound) {
    setValue(m_gameRow,
             tr("Folder not found: %1").arg(QDir::toNativeSeparators(ii.gameDirectory())),
             QStringLiteral("critical"));
  } else {
    setValue(m_gameRow, QDir::toNativeSeparators(ii.gameDirectory()));
  }

  // profiles, the last used one first
  QStringList profiles;
  if (settingsRead) {
    // only folders that are profiles (plugins keep their own folders there)
    const QDir profilesDir(settings.paths().profiles());
    for (const QString& name : profilesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                     QDir::Name | QDir::IgnoreCase)) {
      if (QFileInfo::exists(profilesDir.filePath(name + QStringLiteral("/modlist.txt")))) {
        profiles << name;
      }
    }
    const QString selected = settings.game().selectedProfileName().value_or(QString());
    if (profiles.removeOne(selected)) {
      profiles.prepend(tr("%1 (last used)").arg(selected));
    }
  }
  setValue(m_profilesRow, profiles.isEmpty() ? tr("No profiles yet")
                                             : profiles.join(QStringLiteral(", ")));

  setValue(m_iniRow, QDir::toNativeSeparators(ii.iniPath()));
  setValue(m_storageRow,
           ii.isPortable()
               ? tr("Portable: kept in its own folder, moves with Mod Organizer")
               : tr("Global: kept in your user folder, %1")
                     .arg(QDir::toNativeSeparators(m.globalInstancesRootPath())));

  const QString group = instanceGroup(ii.instanceIdentifier());
  setValue(m_groupRow, group.isEmpty() ? tr("No group") : group);
  const QString notes = instanceValue(NotesSection, ii.instanceIdentifier());
  setValue(m_notesRow, notes.isEmpty() ? tr("No notes")
                                       : notes.simplified());
  m_notesRow.value->setToolTip(notes);

  startSizeScan(ii);

  setButtonsEnabled(true);

  const auto portableRoot = QDir::cleanPath(QDir::fromNativeSeparators(m.portablePath()));
  const auto instanceRoot = QDir::cleanPath(QDir::fromNativeSeparators(ii.directory()));
  const bool isRootPortable =
      ii.isPortable() && portableRoot.compare(instanceRoot, Qt::CaseInsensitive) == 0;

  m_rename->setEnabled(!active && !isRootPortable);
  m_rename->setToolTip(active ? tr("The open instance can't be renamed")
                       : isRootPortable
                           ? tr("The portable instance in the Mod Organizer folder can't "
                                "be renamed")
                           : tr("Rename this instance and its folder (F2)"));
  m_delete->setEnabled(!active);
  m_delete->setToolTip(active ? tr("The open instance can't be deleted")
                              : tr("Choose which of this instance's folders to remove (Delete)"));
  m_duplicate->setEnabled(!isRootPortable);
  m_duplicate->setToolTip(
      isRootPortable
          ? tr("The portable instance in the Mod Organizer folder can't be duplicated")
          : tr("Copy this instance, with its profiles, mods and settings, to a new one "
               "(Ctrl+D)"));
  m_gameRow.action->setEnabled(gameFound);

  const bool hasShortcut = shortcutExists(ii);
  m_shortcut->setText(hasShortcut ? tr("Remove desktop shortcut") : tr("Desktop shortcut"));
  m_shortcut->setToolTip(hasShortcut
                             ? tr("Remove the desktop shortcut that opens this instance")
                             : tr("Add a desktop shortcut that opens this instance directly"));

  // at startup nothing is open yet, so the current instance opens like any
  // other; once MO runs, it's the one already open
  const bool alreadyOpen = active && m_restartOnSelect;
  m_switch->setText(alreadyOpen ? tr("Current instance")
                                : (m_restartOnSelect ? tr("Switch to this instance")
                                                     : tr("Open")));
  m_switch->setEnabled(!alreadyOpen && hasPlugin);
}

void InstanceManagerDialog::startSizeScan(const Instance& ii)
{
  // the previous scan stops; its result would belong to another instance
  if (m_sizeScan) {
    *m_sizeScan = true;
  }
  m_sizeScan = std::make_shared<std::atomic_bool>(false);
  static_cast<ElidedLabel*>(m_sizeRow.value)->setFullText(tr("Calculating…"));

  Settings settings(ii.iniPath());
  if (settings.iniStatus() != QSettings::NoError) {
    static_cast<ElidedLabel*>(m_sizeRow.value)->setFullText(tr("Unknown"));
    return;
  }

  const QStringList folders = {settings.paths().mods(), settings.paths().downloads(),
                               settings.paths().overwrite()};
  auto stop                 = m_sizeScan;
  QPointer<InstanceManagerDialog> self(this);

  QThreadPool::globalInstance()->start([folders, stop, self] {
    std::vector<qint64> sizes;
    for (const QString& folder : folders) {
      qint64 total = 0;
      QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::System,
                      QDirIterator::Subdirectories);
      while (it.hasNext() && !*stop) {
        it.next();
        total += it.fileInfo().size();
      }
      sizes.push_back(total);
    }
    if (*stop) {
      return;
    }

    QMetaObject::invokeMethod(
        qApp,
        [sizes, stop, self] {
          if (!self || *stop) {
            return;
          }
          const QString text = InstanceManagerDialog::tr("Mods %1 · Downloads %2 · "
                                                         "Overwrite %3")
                                   .arg(localizedByteSize(sizes[0]))
                                   .arg(localizedByteSize(sizes[1]))
                                   .arg(localizedByteSize(sizes[2]));
          static_cast<ElidedLabel*>(self->m_sizeRow.value)->setFullText(text);
        },
        Qt::QueuedConnection);
  });
}

bool InstanceManagerDialog::shortcutExists(const Instance& ii) const
{
  return env::instanceShortcut(ii).exists(env::Shortcut::Desktop);
}

void InstanceManagerDialog::toggleShortcut()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  auto shortcut = env::instanceShortcut(*i);
  if (!shortcut.toggle(env::Shortcut::Desktop)) {
    QMessageBox::critical(this, tr("Desktop shortcut"),
                          tr("The desktop shortcut could not be changed."));
  }
  fillData(*i);
}

void InstanceManagerDialog::duplicateInstance()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  auto& m                    = InstanceManager::singleton();
  const QString source       = QDir::cleanPath(i->directory());
  const auto parentDirectory = QFileInfo(source).dir().absolutePath();

  const bool sameBase = QDir::cleanPath(QDir::fromNativeSeparators(i->baseDirectory()))
                            .compare(source, Qt::CaseInsensitive) == 0;
  const QString text =
      sameBase ? tr("The copy gets its own folder with this instance's profiles, mods, "
                    "downloads and settings.")
               : tr("The copy gets its own folder with this instance's profiles and "
                    "settings. Its mod data folder is elsewhere and stays shared with "
                    "the original.");

  const auto name = getInstanceName(
      this, tr("Duplicate instance"), text, tr("Name of the copy"), parentDirectory,
      m.makeUniqueName(i->displayName() + QStringLiteral(" ") + tr("copy"), parentDirectory),
      tr("Duplicate"), false);
  if (name.isEmpty()) {
    return;
  }

  const QString destination = QDir(parentDirectory).filePath(name);
  log::info("duplicating instance {} to {}", source, destination);

  if (!shellCopy(QStringList{QDir::toNativeSeparators(source)},
                 QStringList{QDir::toNativeSeparators(destination)}, this)) {
    const auto e = GetLastError();
    if (e != ERROR_CANCELLED) {
      QMessageBox::critical(this, tr("Duplicate instance"),
                            tr("The instance could not be copied: %1")
                                .arg(QString::fromStdWString(formatSystemMessage(e))));
    }
    return;
  }

  // paths in the copied settings that pointed into the original now point
  // into the copy
  const QString ini = QDir(destination).filePath(QFileInfo(i->iniPath()).fileName());
  QFile file(ini);
  if (file.open(QIODevice::ReadOnly)) {
    QString contents = QString::fromUtf8(file.readAll());
    file.close();
    const QString forward = source;
    QString escaped       = QDir::toNativeSeparators(source);
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    for (const auto& [from, to] :
         {std::pair{forward, QDir::cleanPath(destination)},
          std::pair{escaped, QDir::toNativeSeparators(QDir::cleanPath(destination))
                                 .replace(QStringLiteral("\\"), QStringLiteral("\\\\"))}}) {
      contents.replace(from, to, Qt::CaseInsensitive);
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      file.write(contents.toUtf8());
      // written to disk before the list reads the copy back
      file.close();
    }
  }

  moveInstanceValues(i->instanceIdentifier(),
                     Instance(destination, i->isPortable()).instanceIdentifier(), true);

  updateInstances();
  updateList();
  select(name);
}

void InstanceManagerDialog::clearData()
{
  if (m_sizeScan) {
    *m_sizeScan = true;
  }
  m_emptyText->setText(m_instances.empty()
                           ? tr("No instances yet. Create one with New instance.")
                           : tr("Select an instance to see its folders."));
  m_detailsStack->setCurrentIndex(0);
  setButtonsEnabled(false);
}

void InstanceManagerDialog::setButtonsEnabled(bool b)
{
  m_rename->setEnabled(b);
  m_locationRow.action->setEnabled(b);
  m_baseRow.action->setEnabled(b);
  m_gameRow.action->setEnabled(b);
  m_profilesRow.action->setEnabled(b);
  m_iniRow.action->setEnabled(b);
  m_groupRow.action->setEnabled(b);
  m_notesRow.action->setEnabled(b);
  m_duplicate->setEnabled(b);
  m_shortcut->setEnabled(b);
  m_delete->setEnabled(b);
  m_switch->setEnabled(b);
}

void InstanceManagerDialog::changeGroup()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  // the groups already in use, to pick from or to type a new one
  QStringList groups;
  for (const auto& other : m_instances) {
    const QString group = instanceGroup(other->instanceIdentifier());
    if (!group.isEmpty() && !groups.contains(group, Qt::CaseInsensitive)) {
      groups.push_back(group);
    }
  }
  groups.sort(Qt::CaseInsensitive);

  FluentContentDialog dlg(
      this, QStringLiteral("InstanceGroupDialog"), tr("Move to group"),
      tr("Instances in the same group are listed together. Type a new name to make a "
         "group, or leave it empty for no group."));

  auto* caption = new QLabel(tr("Group"), &dlg);
  auto* combo   = new QComboBox(&dlg);
  combo->setObjectName(QStringLiteral("instanceGroupCombo"));
  combo->setAccessibleName(tr("Group"));
  combo->setEditable(true);
  combo->addItems(groups);
  combo->setCurrentText(instanceGroup(i->instanceIdentifier()));
  combo->lineEdit()->setPlaceholderText(tr("No group"));
  combo->lineEdit()->selectAll();
  dlg.body()->addWidget(caption);
  dlg.body()->addWidget(combo);

  auto* move   = dlg.addButton(tr("Move"), true);
  auto* cancel = dlg.addButton(tr("Cancel"));
  connect(move, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  dlg.resize(460, dlg.sizeHint().height());

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  setInstanceGroup(i->instanceIdentifier(), combo->currentText());

  // grouping by the user's groups shows where the instance went
  if (!combo->currentText().trimmed().isEmpty() && m_groupBy != QLatin1String("custom")) {
    m_groupBy = QStringLiteral("custom");
    GlobalSettings::setValue(QStringLiteral("InstanceManager/groupBy"), m_groupBy);
  }
  updateList();
}

void InstanceManagerDialog::editNotes()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  FluentContentDialog dlg(
      this, QStringLiteral("InstanceNotesDialog"), tr("Notes for %1").arg(i->displayName()),
      tr("What this instance is for, what's in it, or anything to remember. The notes "
         "are also found by the search."));

  auto* edit = new QPlainTextEdit(instanceValue(NotesSection, i->instanceIdentifier()), &dlg);
  edit->setObjectName(QStringLiteral("instanceNotesEdit"));
  edit->setAccessibleName(tr("Notes"));
  edit->setPlaceholderText(tr("Stable setup with Seamless Co-op, for example"));
  edit->setMinimumHeight(130);
  dlg.body()->addWidget(edit, 1);

  auto* save   = dlg.addButton(tr("Save"), true);
  auto* cancel = dlg.addButton(tr("Cancel"));
  connect(save, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  dlg.resize(520, dlg.sizeHint().height());
  edit->setFocus();

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  setInstanceValue(NotesSection, i->instanceIdentifier(), edit->toPlainText());
  updateList();
}

void InstanceManagerDialog::showGroupMenu(const QString& group, const QPoint& globalPos)
{
  // only the user's own groups can be renamed or removed
  if (m_groupBy != QLatin1String("custom") || group.isEmpty()) {
    return;
  }

  QMenu menu(this);
  menu.addAction(FluentIcons::glyph(RenameGlyph), tr("Rename group…"), this,
                 [this, group] {
                   FluentContentDialog dlg(
                       this, QStringLiteral("InstanceGroupRenameDialog"),
                       tr("Rename group"),
                       tr("Every instance in %1 moves to the new name. A name that "
                          "already exists joins the two groups.")
                           .arg(group));
                   auto* edit = new QLineEdit(group, &dlg);
                   edit->setAccessibleName(tr("Group name"));
                   edit->selectAll();
                   dlg.body()->addWidget(edit);
                   auto* ok     = dlg.addButton(tr("Rename"), true);
                   auto* cancel = dlg.addButton(tr("Cancel"));
                   connect(edit, &QLineEdit::textChanged, ok, [ok](const QString& t) {
                     ok->setEnabled(!t.trimmed().isEmpty());
                   });
                   connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
                   connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
                   dlg.resize(460, dlg.sizeHint().height());
                   if (dlg.exec() != QDialog::Accepted) {
                     return;
                   }
                   for (const auto& ii : m_instances) {
                     if (instanceGroup(ii->instanceIdentifier()) == group) {
                       setInstanceGroup(ii->instanceIdentifier(), edit->text());
                     }
                   }
                   updateList();
                 });
  menu.addAction(FluentIcons::glyph(DeleteGlyph), tr("Remove group"), this, [this, group] {
    // the instances stay; they only leave the group
    for (const auto& ii : m_instances) {
      if (instanceGroup(ii->instanceIdentifier()) == group) {
        setInstanceGroup(ii->instanceIdentifier(), {});
      }
    }
    updateList();
  });
  menu.actions().constLast()->setToolTip(tr("The instances stay; they move to No group"));

  FluentIcons::apply(&menu);
  menu.exec(globalPos);
}
