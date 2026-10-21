#include "createinstancedialogui.h"
#include "fluenticons.h"
#include "fluentswitch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCommandLinkButton>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
// Segoe Fluent Icons code points
constexpr char16_t GameGlyph    = 0xE7FC;
constexpr char16_t FolderGlyph  = 0xE8B7;
constexpr char16_t PeopleGlyph  = 0xE716;
constexpr char16_t SwitchGlyph  = 0xE8AB;
constexpr char16_t BrowseGlyph  = 0xED25;  // OpenFolderHorizontal
constexpr char16_t DriveGlyph   = 0xEDA2;  // HardDrive
constexpr char16_t PlayGlyph    = 0xE768;
constexpr char16_t StackGlyph   = 0xF156;  // Stack
constexpr char16_t HelpGlyph    = 0xE897;
constexpr char16_t ListGlyph    = 0xEA37;
constexpr char16_t SettingsGlyph = 0xE713;
constexpr char16_t SaveGlyph    = 0xE74E;
constexpr char16_t FoldersGlyph = 0xE8F4;  // NewFolder

// a stack whose size is its current page's: QStackedWidget answers with the
// tallest of all its pages, so the simple folder page would keep the height
// of the advanced one
class CurrentPageStack : public QStackedWidget
{
public:
  using QStackedWidget::QStackedWidget;

  bool hasHeightForWidth() const override
  {
    return currentWidget() != nullptr && currentWidget()->hasHeightForWidth();
  }

  int heightForWidth(int width) const override
  {
    return currentWidget() != nullptr ? currentWidget()->heightForWidth(width) : -1;
  }

  QSize sizeHint() const override
  {
    return currentWidget() != nullptr ? currentWidget()->sizeHint() : QSize();
  }

  QSize minimumSizeHint() const override
  {
    return currentWidget() != nullptr ? currentWidget()->minimumSizeHint() : QSize();
  }
};

// the strings keep the Designer file's context, so their translations stay
QString tr(const char* text)
{
  return QCoreApplication::translate("CreateInstanceDialog", text);
}

template <class W>
W* named(W* widget, const char* name)
{
  widget->setObjectName(QString::fromLatin1(name));
  return widget;
}

QLabel* label(QWidget* parent, const char* name, const QString& text = {},
              bool wrap = true)
{
  auto* l = named(new QLabel(text, parent), name);
  l->setWordWrap(wrap);
  return l;
}

QLabel* glyph(QWidget* parent, char16_t code)
{
  auto* l = label(parent, "instanceRowGlyph", QString(QChar(code)), false);
  l->setFont(FluentIcons::font(16));
  l->setFixedWidth(24);
  l->setAlignment(Qt::AlignCenter);
  return l;
}

// a check box the Fluent themes show as an animated switch
QCheckBox* toggle(QWidget* parent, const char* name, const QString& text)
{
  auto* box = named(new QCheckBox(text, parent), name);
  box->setProperty("fluentSwitch", true);
  box->setCursor(Qt::PointingHandCursor);
  if (FluentIcons::active()) {
    FluentSwitch::animate(box);
  }
  return box;
}

QToolButton* browseButton(QWidget* parent, const char* name)
{
  auto* b = named(new QToolButton(parent), name);
  b->setIcon(FluentIcons::glyph(BrowseGlyph));
  b->setIconSize(QSize(16, 16));
  b->setToolTip(tr("Browse"));
  b->setAccessibleName(tr("Browse"));
  b->setProperty("wizardBrowse", true);
  b->setProperty("fluentKeepStyle", true);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

// one of the page's rows: hairline above, the card's padding
QFrame* row(QWidget* parent, QVBoxLayout* rows, bool first = false)
{
  auto* frame = named(new QFrame(parent), "fluentSettingRow");
  if (first) {
    frame->setProperty("fluentGridRow", true);
  }
  rows->addWidget(frame);
  return frame;
}

// a row with a title and a description on the left and a control on the
// right, like a Windows 11 Settings row; returns the description label
QLabel* fieldRow(QWidget* parent, QVBoxLayout* rows, const QString& title,
                 const QString& description, QWidget* control, int controlWidth = 340,
                 QLabel* descriptionLabel = nullptr, QWidget* underText = nullptr)
{
  auto* frame  = row(parent, rows);
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(20, 12, 16, 12);
  layout->setSpacing(16);

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  text->addWidget(label(frame, "fluentSettingTitle", title, false));
  if (descriptionLabel == nullptr) {
    descriptionLabel = label(frame, "fluentSettingDescription", description);
  } else {
    descriptionLabel->setParent(frame);
    descriptionLabel->setProperty("wizardRowText", true);
  }
  text->addWidget(descriptionLabel);
  if (underText != nullptr) {
    underText->setParent(frame);
    text->addWidget(underText, 0, Qt::AlignLeft);
  }
  layout->addLayout(text, 1);

  control->setParent(frame);
  control->setFixedWidth(controlWidth);
  layout->addWidget(control, 0, Qt::AlignVCenter);
  return descriptionLabel;
}

// an information row: glyph, title and description; returns the description
QLabel* infoRow(QWidget* parent, QVBoxLayout* rows, char16_t code, const QString& title,
                const QString& description, bool first = false)
{
  auto* frame  = row(parent, rows, first);
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(20, 12, 16, 12);
  layout->setSpacing(14);
  layout->addWidget(glyph(frame, code), 0, Qt::AlignTop);
  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  text->addWidget(label(frame, "fluentSettingTitle", title, false));
  auto* descriptionLabel = label(frame, "fluentSettingDescription", description);
  text->addWidget(descriptionLabel);
  layout->addLayout(text, 1);
  return descriptionLabel;
}

// a wizard page: its title and text at the top of the card, then its rows
// edge to edge; returns the layout for the rows
//
// the window keeps one size on every step, so a page scrolls if it ever gets
// taller than the card (a long creation log, a translated text); with `fill`,
// the rows get the page's spare height and decide where it goes
QVBoxLayout* addPage(QStackedWidget* stack, QWidget*& page, const char* name,
                     QLabel*& heading, const char* headingName,
                     const QString& headingText, QLabel*& text, const char* textName,
                     const QString& textValue, bool fill = false)
{
  auto* scroll = named(new QScrollArea(stack), "wizardPageScroll");
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  page = named(new QWidget(scroll), name);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* top = named(new QWidget(page), "wizardPageTop");
  auto* topLayout = new QVBoxLayout(top);
  topLayout->setContentsMargins(20, 18, 20, 14);
  topLayout->setSpacing(3);
  // new names: the old Designer ones still have rules in the classic themes
  Q_UNUSED(headingName);
  Q_UNUSED(textName);
  heading = label(top, "wizardHeading", cid::plainHeading(headingText));
  heading->setProperty("wizardPageTitle", true);
  text = label(top, "wizardText", textValue);
  text->setProperty("wizardPageText", true);
  topLayout->addWidget(heading);
  topLayout->addWidget(text);
  layout->addWidget(top);

  auto* rows = new QVBoxLayout;
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(0);
  if (fill) {
    layout->addLayout(rows, 1);
  } else {
    layout->addLayout(rows);
    layout->addStretch(1);
  }

  scroll->setWidget(page);
  stack->addWidget(scroll);
  return rows;
}

// a selectable row: storage types, editions and games
QCommandLinkButton* choice(QWidget* parent, const char* name, const QString& text,
                           const QString& description)
{
  auto* b = named(new QCommandLinkButton(text, description, parent), name);
  b->setProperty("wizardChoice", true);
  b->setCheckable(true);
  b->setAutoDefault(false);
  b->setIconSize(QSize(24, 24));
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

// a warning or an error, in a padded box under the rows
QLabel* infoBar(QWidget* parent, const char* name, const QString& text,
                const char* severity)
{
  auto* l = label(parent, name, text);
  l->setProperty("wizardInfoBar", QString::fromLatin1(severity));
  l->hide();
  return l;
}

QWidget* paddedBox(QWidget* parent, QVBoxLayout* rows, QVBoxLayout*& inner)
{
  auto* box = named(new QWidget(parent), "wizardPadded");
  inner     = new QVBoxLayout(box);
  inner->setContentsMargins(20, 6, 20, 10);
  inner->setSpacing(6);
  rows->addWidget(box);
  return box;
}
}  // namespace

namespace cid
{
QString plainHeading(const QString& text)
{
  static const QRegularExpression tags(QStringLiteral("</?h\\d[^>]*>"),
                                       QRegularExpression::CaseInsensitiveOption);
  return QString(text).remove(tags).trimmed();
}
}  // namespace cid

namespace Ui
{

void CreateInstanceDialog::setupUi(QDialog* dialog)
{
  dialog->setObjectName(QStringLiteral("CreateInstanceDialog"));
  dialog->setWindowTitle(tr("New instance"));

  auto* root = new QVBoxLayout(dialog);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  // the card: the progress line on its top edge, then the current page
  auto* card = named(new QFrame(dialog), "wizardCard");
  card->setProperty("fluentCard", true);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(1, 1, 1, 1);
  cardLayout->setSpacing(0);

  progress = named(new QProgressBar(card), "wizardProgress");
  progress->setTextVisible(false);
  progress->setFixedHeight(3);
  progress->setAccessibleName(tr("Progress"));
  cardLayout->addWidget(progress);

  pages = named(new QStackedWidget(card), "pages");
  cardLayout->addWidget(pages, 1);
  root->addWidget(card, 1);

  // intro: its translated text starts with the heading
  {
    const QString full = tr(
        "<h3>A separate workspace for every game setup</h3>\n<p>An instance keeps one "
        "game's profiles, mods, downloads and settings together. Choose Global or "
        "Portable to store a separate data set managed by this MO2 installation.</p>\n"
        "<p>Choose Isolated to copy the MO2 application into its own folder for one "
        "game. Elden Ring is the first supported game for isolated copies.</p>");
    static const QRegularExpression h3(QStringLiteral("<h3>(.*)</h3>"),
                                       QRegularExpression::CaseInsensitiveOption |
                                           QRegularExpression::DotMatchesEverythingOption);
    const auto match = h3.match(full);
    auto* rows = addPage(pages, page_1, "page_1", introHeading, "introHeading",
                         match.hasMatch() ? match.captured(1) : full, label_15, "label_15",
                         tr("Each instance is a setup of its own. These are the steps."),
                         true);
    infoRow(page_1, rows, FolderGlyph, tr("Where it's stored"),
            tr("Global in your user folder, portable next to Mod Organizer, or an "
               "isolated copy of Mod Organizer for one game."));
    infoRow(page_1, rows, GameGlyph, tr("The game it manages"),
            tr("Installed games are found for you; any other folder can be chosen."));
    infoRow(page_1, rows, PeopleGlyph, tr("Its name, folders and profiles"),
            tr("All of it can be checked on the last step before anything is created."));
    infoRow(page_1, rows, SwitchGlyph, tr("Switching later"),
            tr("Manage Instances opens any instance at any time."));
    infoRow(page_1, rows, PlayGlyph, tr("Before you start"),
            tr("Run the game once from its own launcher, so its INI files and its save "
               "folder exist."));
    introInstances = infoRow(page_1, rows, StackGlyph, tr("Your instances"), {});
    rows->addStretch(1);
    auto* last = row(page_1, rows);
    auto* lastLayout = new QHBoxLayout(last);
    lastLayout->setContentsMargins(20, 12, 16, 12);
    hideIntro = toggle(last, "hideIntro", tr("Never show this page again"));
    lastLayout->addWidget(hideIntro);
  }

  // storage type
  {
    auto* rows = addPage(
        pages, page_2, "page_2", label_2, "label_2",
        tr("<h3>Choose where this setup will live</h3>"), typeSelectionHint,
        "typeSelectionHint",
        tr("Select a storage model. MO2 will show the folders before it creates "
           "anything."));
    widget_15 = named(new QWidget(page_2), "widget_15");
    auto* choices = new QVBoxLayout(widget_15);
    choices->setContentsMargins(0, 0, 0, 0);
    choices->setSpacing(0);
    createGlobal = choice(widget_15, "createGlobal", tr("Global (recommended)"),
                          tr("Kept in your user folder and managed by this Mod Organizer. "
                             "Its data folders can be moved later."));
    createPortable = choice(widget_15, "createPortable", tr("Portable"),
                            tr("Kept next to this Mod Organizer, so the program and the "
                               "instance travel together."));
    createIsolated = choice(widget_15, "createIsolated", tr("Isolated copy"),
                            tr("A separate copy of Mod Organizer with its own settings and "
                               "data, for one game. Elden Ring and Nightreign are supported for now."));
    choices->addWidget(createGlobal);
    choices->addWidget(createPortable);
    choices->addWidget(createIsolated);
    rows->addWidget(widget_15);
    portableExistsLabel = label(page_2, "portableExistsLabel",
                                tr("Each portable instance is stored in a separate "
                                   "folder under the MO2 installation."));
    portableExistsLabel->hide();
    rows->addWidget(portableExistsLabel);

    // the differences that matter when choosing, one column per kind
    auto* frame = row(page_2, rows);
    typeComparison = frame;
    auto* grid = new QGridLayout(frame);
    grid->setContentsMargins(20, 12, 20, 12);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(0, 3);
    for (int c = 1; c <= 3; ++c) {
      grid->setColumnStretch(c, 2);
    }
    auto cell = [&](int r, int c, const QString& text, const char* kind) {
      auto* l = label(frame, "wizardCompareCell", text);
      l->setProperty("wizardCompare", QString::fromLatin1(kind));
      grid->addWidget(l, r, c);
    };
    cell(0, 0, tr("What changes"), "key");
    cell(0, 1, tr("Global"), "head");
    cell(0, 2, tr("Portable"), "head");
    cell(0, 3, tr("Isolated copy"), "head");
    cell(1, 0, tr("Mod Organizer settings"), "key");
    cell(1, 1, tr("Shared"), "cell");
    cell(1, 2, tr("Shared"), "cell");
    cell(1, 3, tr("Its own"), "cell");
    cell(2, 0, tr("Moves with the program folder"), "key");
    cell(2, 1, tr("No"), "cell");
    cell(2, 2, tr("Yes"), "cell");
    cell(2, 3, tr("Yes"), "cell");
    cell(3, 0, tr("Plugins and version"), "key");
    cell(3, 1, tr("This Mod Organizer's"), "cell");
    cell(3, 2, tr("This Mod Organizer's"), "cell");
    cell(3, 3, tr("Its own copy"), "cell");
  }

  // game
  {
    auto* rows = addPage(
        pages, page_3, "page_3", label_3, "label_3",
        tr("<h3>Choose a game for this instance</h3>"), gameSelectionHint,
        "gameSelectionHint",
        tr("MO2 detects installed games automatically. Browse to the game folder if it "
           "is stored elsewhere."),
        true);
    QVBoxLayout* inner = nullptr;
    paddedBox(page_3, rows, inner);
    auto* tools = new QHBoxLayout;
    tools->setSpacing(8);
    gamesFilter = named(new QLineEdit(page_3), "gamesFilter");
    gamesFilter->setPlaceholderText(tr("Search games"));
    gamesFilter->setClearButtonEnabled(true);
    browseGameFolderButton = named(new QPushButton(tr("Browse…"), page_3),
                                   "browseGameFolderButton");
    browseGameFolderButton->setIcon(FluentIcons::glyph(BrowseGlyph));
    browseGameFolderButton->setToolTip(tr("Choose a game folder that wasn't found"));
    browseGameFolderButton->setAutoDefault(false);
    tools->addWidget(gamesFilter, 1);
    tools->addWidget(browseGameFolderButton);
    inner->addLayout(tools);
    showAllGames = toggle(page_3, "showAllGames", tr("Show all supported games"));
    inner->addWidget(showAllGames);

    scrollArea_3 = named(new QScrollArea(page_3), "scrollArea_3");
    scrollArea_3->setWidgetResizable(true);
    scrollArea_3->setFrameShape(QFrame::NoFrame);
    scrollArea_3->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    games = named(new QWidget(scrollArea_3), "games");
    auto* gamesLayout = new QVBoxLayout(games);
    gamesLayout->setContentsMargins(0, 0, 0, 0);
    gamesLayout->setSpacing(0);
    gamesLayout->addStretch(1);
    scrollArea_3->setWidget(games);
    rows->addWidget(scrollArea_3, 1);
    QVBoxLayout* warningBox = nullptr;
    paddedBox(page_3, rows, warningBox);
    gameProtectedWarning = infoBar(
        page_3, "gameProtectedWarning",
        tr("%1 is in Program Files. Windows protects that folder, and some mods and "
           "tools fail when they write to it. Moving the game to another library avoids "
           "it (in Steam: Properties, Installed Files, Move install folder)."),
        "caution");
    warningBox->addWidget(gameProtectedWarning);
    warningBox->parentWidget()->hide();
    gameMissingHint =
        infoRow(page_3, rows, HelpGlyph, tr("Is your game missing?"),
                tr("Choose Browse… and select its folder, or turn on Show all "
                   "supported games."));
  }

  // edition
  {
    auto* rows = addPage(
        pages, page_4, "page_4", label_4, "label_4",
        tr("<h3>Confirm the game edition.</h3>"), label_14, "label_14",
        tr("This game has multiple editions. Select the one installed on your computer "
           "so MO2 can configure the correct game plugin."));
    editions = named(new QDialogButtonBox(page_4), "editions");
    editions->setOrientation(Qt::Vertical);
    editions->setCenterButtons(false);
    if (editions->layout() != nullptr) {
      editions->layout()->setSpacing(0);
      editions->layout()->setContentsMargins(0, 0, 0, 0);
    }
    rows->addWidget(editions);
  }

  // name, group and notes
  {
    auto* rows = addPage(
        pages, page_5, "page_5", instanceNameLabel, "instanceNameLabel",
        tr("<h3>Choose a name for this instance.</h3>"), instanceNameHint,
        "instanceNameHint", tr("It also names the instance's folder."));
    widget_9 = named(new QWidget(page_5), "widget_9");
    auto* fields = new QVBoxLayout(widget_9);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(0);

    instanceName = named(new QLineEdit, "instanceName");
    instanceName->setPlaceholderText(tr("Instance name"));
    instanceName->setAccessibleName(tr("Instance name"));
    fieldRow(widget_9, fields, tr("Name"), tr("How it's listed in Manage Instances."),
             instanceName, 340);

    // the folder it makes, at full width so a long path reads whole, then
    // any warning
    QVBoxLayout* bars = nullptr;
    paddedBox(widget_9, fields, bars);
    instanceNamePath = label(widget_9, "wizardNamePath", tr("Storage folder preview"));
    instanceNamePath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bars->addWidget(instanceNamePath);
    instanceNameContext = label(widget_9, "instanceNameContext", {});
    instanceNameContext->hide();
    instanceNameExists = infoBar(widget_9, "instanceNameExists",
                                 tr("There is already an instance with this name."),
                                 "caution");
    instanceNameInvalid = infoBar(
        widget_9, "instanceNameInvalid",
        tr("The name contains invalid characters. It must be a valid folder name."),
        "critical");
    bars->addWidget(instanceNameContext);
    bars->addWidget(instanceNameExists);
    bars->addWidget(instanceNameInvalid);
    // the folder preview keeps the box shown; the warnings come and go in it

    instanceGroup = named(new QComboBox, "instanceGroup");
    instanceGroup->setEditable(true);
    instanceGroup->setAccessibleName(tr("Group"));
    fieldRow(widget_9, fields, tr("Group"),
             tr("Optional. Instances in a group are listed together in Manage "
                "Instances."),
             instanceGroup);

    instanceNotes = named(new QLineEdit, "instanceNotes");
    instanceNotes->setPlaceholderText(tr("Stable setup with Seamless Co-op, for example"));
    instanceNotes->setAccessibleName(tr("Notes"));
    fieldRow(widget_9, fields, tr("Notes"),
             tr("Optional. What this instance is for; the search finds it."),
             instanceNotes);

    instanceSiblings = infoRow(widget_9, fields, ListGlyph, tr("Already for this game"), {});

    auto* shortcutRow    = row(widget_9, fields);
    auto* shortcutLayout = new QHBoxLayout(shortcutRow);
    shortcutLayout->setContentsMargins(20, 12, 16, 12);
    desktopShortcut = toggle(shortcutRow, "desktopShortcut",
                             tr("Add a desktop shortcut that opens it"));
    shortcutLayout->addWidget(desktopShortcut);

    detectedPrograms = named(new QWidget(widget_9), "detectedPrograms");
    auto* programs   = new QVBoxLayout(detectedPrograms);
    programs->setContentsMargins(0, 0, 0, 0);
    programs->setSpacing(0);
    detectedPrograms->hide();
    fields->addWidget(detectedPrograms);
    rows->addWidget(widget_9);
  }

  // profile settings; the Elden Ring plugin puts its save card into
  // widget_25's layout, next to profileSavesCheckbox
  {
    auto* rows = addPage(
        pages, page, "page", profileSettingsLabel, "profileSettingsLabel",
        tr("<h3>Configure your profile settings.</h3>"), profileSettingsHint,
        "profileSettingsHint",
        tr("New profiles of this instance start with these settings. Each profile can "
           "change them later."));
    widget_25 = named(new QWidget(page), "widget_25");
    auto* options = new QVBoxLayout(widget_25);
    options->setContentsMargins(0, 0, 0, 0);
    options->setSpacing(0);

    auto addOption = [&](QCheckBox*& box, const char* name, const QString& text,
                         QLabel*& description, const char* descriptionName,
                         const QString& descriptionText) {
      box = toggle(widget_25, name, text);
      box->setProperty("wizardOption", true);
      description = label(widget_25, descriptionName, descriptionText);
      description->setProperty("wizardOptionText", true);
      options->addWidget(box);
      options->addWidget(description);
    };
    addOption(profileInisCheckbox, "profileInisCheckbox",
              tr("Use profile-specific game INI files"), profileInisDescription,
              "profileInisDescription",
              tr("Each profile keeps its own copy of the game's INI files."));
    addOption(profileSavesCheckbox, "profileSavesCheckbox",
              tr("Use profile-specific save games"), profileSavesDescription,
              "profileSavesDescription",
              tr("Each profile keeps its own saves, separate from the game's."));
    addOption(archiveInvalidationCheckbox, "archiveInvalidationCheckbox",
              tr("Automatic archive invalidation"), archiveInvalidationDescription,
              "archiveInvalidationDescription",
              tr("Lets loose files override the game's archives in games that need it."));
    rows->addWidget(widget_25);

    // what the switches move: the game's own folders
    gameInisPath  = infoRow(page, rows, SettingsGlyph, tr("The game's INI files now"), {});
    gameSavesPath = infoRow(page, rows, SaveGlyph, tr("The game's saves now"), {});

    QVBoxLayout* warningBox = nullptr;
    paddedBox(page, rows, warningBox);
    oneDriveWarning = infoBar(
        page, "oneDriveWarning",
        tr("OneDrive syncs these folders. It can lock the INI files and saves, or bring "
           "back older copies, while the game runs. With profile-specific INI files and "
           "saves they stay inside the instance, away from OneDrive. "
           "<a href=\"both\">Turn both on</a>"),
        "caution");
    oneDriveWarning->setTextFormat(Qt::RichText);
    warningBox->addWidget(oneDriveWarning);
    warningBox->parentWidget()->hide();
  }

  // folders
  {
    auto* rows = addPage(
        pages, page_6, "page_6", label_5, "label_5",
        tr("<h3>Select a folder where the data should be stored.</h3>"), pathsLabel,
        "pathsLabel",
        tr("This includes downloads, mods, profiles and overwrite for your <b>%1</b> "
           "instance. If there is enough space on this drive, you should use the "
           "default folder."),
        true);
    pathsLabel->setTextFormat(Qt::RichText);

    widget_13 = named(new QWidget(page_6), "widget_13");
    auto* folders = new QVBoxLayout(widget_13);
    folders->setContentsMargins(0, 0, 0, 0);
    folders->setSpacing(0);
    pathPages = named(new CurrentPageStack(widget_13), "pathPages");

    // one folder
    auto* simple = named(new QWidget(pathPages), "page_6_simple");
    auto* simpleRows = new QVBoxLayout(simple);
    simpleRows->setContentsMargins(0, 0, 0, 0);
    simpleRows->setSpacing(0);
    auto* locationBox = new QWidget;
    auto* locationLayout = new QHBoxLayout(locationBox);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    locationLayout->setSpacing(6);
    location = named(new QLineEdit(locationBox), "location");
    location->setPlaceholderText(tr("Folder"));
    location->setAccessibleName(tr("Location"));
    browseLocation = browseButton(locationBox, "browseLocation");
    locationLayout->addWidget(location, 1);
    locationLayout->addWidget(browseLocation);
    diskSpace = named(new QLabel, "diskSpace");
    diskBar   = named(new QProgressBar, "wizardDiskBar");
    diskBar->setTextVisible(false);
    diskBar->setFixedSize(150, 4);
    diskBar->setRange(0, 1000);
    diskBar->setAccessibleName(tr("Disk usage"));
    label_6 = new QLabel(tr("Location"), simple);
    label_6->hide();
    fieldRow(simple, simpleRows, tr("Location"), {}, locationBox, 420, diskSpace, diskBar);
    QVBoxLayout* simpleBars = nullptr;
    paddedBox(simple, simpleRows, simpleBars);
    locationExists = infoBar(simple, "locationExists",
                             tr("Warning: This folder already exists."), "caution");
    locationInvalid = infoBar(simple, "locationInvalid",
                              tr("Warning: The folder contains invalid characters."),
                              "critical");
    simpleBars->addWidget(locationExists);
    simpleBars->addWidget(locationInvalid);
    simpleBars->parentWidget()->hide();

    sharedDownloads = named(new QComboBox, "sharedDownloads");
    sharedDownloads->setAccessibleName(tr("Downloads folder"));
    fieldRow(simple, simpleRows, tr("Downloads"),
             tr("Another instance's downloads can be shared, so the same archives "
                "aren't kept twice."),
             sharedDownloads, 420);
    folderPreview = infoRow(simple, simpleRows, FoldersGlyph, tr("Folders it creates"), {});
    folderPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    simpleRows->addStretch(1);
    pathPages->addWidget(simple);

    // every folder
    auto* advanced = named(new QWidget(pathPages), "page_6_advanced");
    auto* grid = new QGridLayout(advanced);
    grid->setContentsMargins(20, 12, 16, 12);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    int r = 0;
    auto addFolder = [&](QLabel*& caption, const char* captionName, const QString& text,
                         QLineEdit*& edit, const char* editName, QToolButton*& browse,
                         const char* browseName) {
      caption = label(advanced, captionName, text, false);
      caption->setProperty("wizardFieldCaption", true);
      edit = named(new QLineEdit(advanced), editName);
      edit->setPlaceholderText(tr("Folder"));
      edit->setAccessibleName(text);
      browse = browseButton(advanced, browseName);
      grid->addWidget(caption, r, 0);
      grid->addWidget(edit, r, 1);
      grid->addWidget(browse, r, 2);
      ++r;
    };
    addFolder(label_8, "label_8", tr("Base directory"), base, "base", browseBase,
              "browseBase");
    addFolder(label_9, "label_9", tr("Downloads"), downloads, "downloads",
              browseDownloads, "browseDownloads");
    addFolder(label_10, "label_10", tr("Mods"), mods, "mods", browseMods, "browseMods");
    addFolder(label_12, "label_12", tr("Profiles"), profiles, "profiles", browseProfiles,
              "browseProfiles");
    addFolder(label_13, "label_13", tr("Overwrite"), overwrite, "overwrite",
              browseOverwrite, "browseOverwrite");
    label_11 = label(advanced, "label_11",
                     tr("Use %BASE_DIR% to refer to the Base Directory."));
    grid->addWidget(label_11, r++, 1, 1, 2);
    advancedDirExists = infoBar(advanced, "advancedDirExists",
                                tr("Warning: The folder %1 already exists."), "caution");
    advancedDirInvalid = infoBar(advanced, "advancedDirInvalid",
                                 tr("Warning: The folder %1 contains invalid characters."),
                                 "critical");
    grid->addWidget(advancedDirExists, r++, 0, 1, 3);
    grid->addWidget(advancedDirInvalid, r++, 0, 1, 3);
    pathPages->addWidget(advanced);
    folders->addWidget(pathPages);
    folders->addStretch(1);

    auto* toggleRow = row(widget_13, folders);
    auto* toggleLayout = new QHBoxLayout(toggleRow);
    toggleLayout->setContentsMargins(20, 12, 16, 12);
    advancedPathOptions = toggle(toggleRow, "advancedPathOptions",
                                 tr("Choose each folder separately"));
    toggleLayout->addWidget(advancedPathOptions);
    rows->addWidget(widget_13, 1);
  }

  // Nexus
  {
    auto* rows = addPage(
        pages, page_7, "page_7", label_7, "label_7",
        tr("<h3>Link Mod Organizer with your Nexus account</h3>"), label_18, "label_18",
        tr("Linking with Nexus allows you to download mods directly from Mod Organizer "
           "and automatically check for updates. This is optional."));
    auto* buttons = new QWidget;
    auto* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(8);
    nexusConnect = named(new QPushButton(tr("Connect to Nexus"), buttons), "nexusConnect");
    nexusConnect->setAutoDefault(false);
    nexusManual =
        named(new QPushButton(tr("Enter API Key Manually"), buttons), "nexusManual");
    nexusManual->setAutoDefault(false);
    buttonsLayout->addWidget(nexusConnect);
    buttonsLayout->addWidget(nexusManual);
    fieldRow(page_7, rows, tr("Nexus account"),
             tr("You can also do this later in Settings."), buttons, 340);

    QVBoxLayout* inner = nullptr;
    paddedBox(page_7, rows, inner);
    nexusLog = named(new QListWidget(page_7), "nexusLog");
    nexusLog->setAccessibleName(tr("Nexus connection log"));
    nexusLog->setMaximumHeight(120);
    inner->addWidget(nexusLog);
    // shown once the connection writes something
    inner->parentWidget()->hide();
  }

  // review
  {
    auto* rows = addPage(
        pages, page_8, "page_8", label_16, "label_16", tr("<h3>Confirmation</h3>"),
        label_17, "label_17",
        tr("The instance is about to be created. Review the information below and "
           "press 'Finish'."));
    widget_19 = named(new QWidget(page_8), "widget_19");
    auto* reviewLayout = new QVBoxLayout(widget_19);
    reviewLayout->setContentsMargins(0, 0, 0, 0);
    reviewLayout->setSpacing(0);
    review = named(new QWidget(widget_19), "review");
    auto* reviewRows = new QVBoxLayout(review);
    reviewRows->setContentsMargins(0, 0, 0, 0);
    reviewRows->setSpacing(0);
    reviewLayout->addWidget(review);

    {
      auto* launchRow    = row(widget_19, reviewLayout);
      auto* launchLayout = new QHBoxLayout(launchRow);
      launchLayout->setContentsMargins(20, 10, 16, 10);
      launch = toggle(launchRow, "launch", tr("Launch the new instance"));
      launchLayout->addWidget(launch);
    }

    QVBoxLayout* inner = nullptr;
    paddedBox(widget_19, reviewLayout, inner);
    creationLog = named(new QTextEdit(widget_19), "creationLog");
    creationLog->setReadOnly(true);
    creationLog->setPlaceholderText(tr("Instance creation log"));
    creationLog->setMaximumHeight(110);
    creationLog->hide();
    inner->addWidget(creationLog);
    inner->parentWidget()->hide();
    rows->addWidget(widget_19);
  }

  // under the card
  auto* footer = new QHBoxLayout;
  footer->setContentsMargins(4, 0, 0, 0);
  footer->setSpacing(8);
  title = label(dialog, "wizardStepText", {}, false);
  back = named(new QPushButton(tr("Back"), dialog), "back");
  back->setAutoDefault(false);
  next = named(new QPushButton(tr("Next"), dialog), "next");
  next->setDefault(true);
  next->setMinimumWidth(130);
  cancel = named(new QPushButton(tr("Cancel"), dialog), "cancel");
  cancel->setAutoDefault(false);
  footer->addWidget(title, 1);
  footer->addWidget(back);
  footer->addWidget(next);
  footer->addWidget(cancel);
  root->addLayout(footer);
}

}  // namespace Ui
