#include "revampedguide.h"

#include "fluentnavigation.h"
#include "fluentpanels.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QToolBar>
#include <QVBoxLayout>

#include <vector>

namespace RevampedGuide
{

namespace
{

// the texts' translation context
struct Text
{
  Q_DECLARE_TR_FUNCTIONS(RevampedGuide)
};

struct Row
{
  char16_t glyph;
  QString title;
  QString text;
};

struct Topic
{
  char16_t glyph;
  QString name;
  QString subtitle;
  std::vector<Row> rows;
};

std::vector<Topic> topics()
{
  return {
      {0xE80F,
       Text::tr("Getting around"),
       Text::tr("The parts of the main window and how to move between them."),
       {{0xE7FC, Text::tr("Top bar"),
         Text::tr("Shows the game, the profile and the program to start. <b>Run</b> "
                  "starts it with the active profile; its arrow has the shortcut "
                  "options. The last line says when the game last saved.")},
        {0xE9D2, Text::tr("Summary tiles"),
         Text::tr("<b>Active mods</b>, <b>With conflicts</b>, <b>Overwrite</b> and "
                  "<b>Updates</b> count what may need a look. Their buttons filter the "
                  "mod list or open Overwrite.")},
        {0xE8FD, Text::tr("Mod list"),
         Text::tr("The chips <b>All</b>, <b>Active</b> and <b>Conflicts</b> filter "
                  "it, <b>Search mods</b> finds a mod and <b>Filter options</b> opens "
                  "the category filters. Drag a mod to change its priority: a mod "
                  "lower in the list wins a conflict.")},
        {0xE8A9, Text::tr("Tabs on the right"),
         Text::tr("<b>Plugins</b>, <b>Archives</b>, <b>Data</b>, <b>Saves</b> and "
                  "<b>Downloads</b>. Games without plugins or archives don't show "
                  "those tabs.")},
        {0xE700, Text::tr("Command bar"),
         Text::tr("Instances, <b>Install</b>, <b>Nexus</b>, <b>Profiles</b>, "
                  "<b>Refresh</b>, <b>Executables</b>, <b>Tools</b>, <b>Settings</b> and "
                  "the bell with the notifications. <b>…</b> holds the links, updates "
                  "and help.")},
        {0xE765, Text::tr("Keyboard"),
         Text::tr("Ctrl+M install a mod · Ctrl+P profiles · Ctrl+E executables · "
                  "Ctrl+I tools · Ctrl+S settings · Ctrl+N Nexus · Ctrl+H help · "
                  "F5 refresh · Ctrl+F search the list · Esc clear the search.")},
        {0xE897, Text::tr("Help on the interface"),
         Text::tr("Choose <b>…</b> &gt; <b>Help on the interface</b>, then click any "
                  "part of the window to read what it does.")}}},

      {0xE77B,
       Text::tr("Profiles"),
       Text::tr("Each profile keeps its own mod list and plugin order."),
       {{0xE77B, Text::tr("Manage profiles"),
         Text::tr("<b>Profiles</b> (Ctrl+P) creates, duplicates, renames and deletes "
                  "profiles, and <b>Use this profile</b> switches to one. The top bar "
                  "switches profiles too.")},
        {0xE8F7, Text::tr("Profile options"),
         Text::tr("<b>Profile-specific saves</b> and <b>Profile-specific game INI "
                  "files</b> keep saves and game settings apart for each profile. "
                  "<b>Transfer saves...</b> copies saves between profiles.")},
        {0xE713, Text::tr("Defaults"),
         Text::tr("Settings &gt; General &gt; <b>Profile defaults</b> chooses the "
                  "options a new profile starts with.")},
        {0xE72E, Text::tr("When it can't change"),
         Text::tr("A profile can't be renamed or deleted while it is the current one "
                  "or while the game runs.")}}},

      {0xE896,
       Text::tr("Downloads"),
       Text::tr("The archives you download from Nexus or add yourself."),
       {{0xE896, Text::tr("Downloads tab"),
         Text::tr("Lists this instance's archives and their status. Double-click one "
                  "to install it, or drag it onto the mod list to choose its place.")},
        {0xE8C8, Text::tr("Right-click"),
         Text::tr("<b>Pause</b>, <b>Resume</b> or <b>Cancel</b> a download; "
                  "<b>Query info</b>, <b>Visit on Nexus</b>, <b>Show in folder</b>, "
                  "<b>Hide</b> or <b>Delete</b> a finished one. Right-click an empty "
                  "spot to hide or delete many at once; deleted files go to the "
                  "Recycle Bin.")},
        {0xE71B, Text::tr("Nexus links"),
         Text::tr("The <b>Mod manager download</b> buttons on Nexus send files to MO2. "
                  "Connect your account in Settings &gt; Nexus.")},
        {0xE713, Text::tr("Options"),
         Text::tr("Settings &gt; General &gt; <b>Download list</b> can hide archives "
                  "once they are installed and make the list compact.")}}},

      {0xE8B7,
       Text::tr("Root folder"),
       Text::tr("Files that go next to the game's program instead of its data folder."),
       {{0xE8B7, Text::tr("What it is"),
         Text::tr("Some mods need files beside the game's .exe: script extenders, "
                  "ENB, ReShade or DXVK. Put them in a folder named <b>Root</b> at the "
                  "top of the mod. MO2 shows them in the game folder without copying "
                  "them.")},
        {0xE8C8, Text::tr("When files are copied"),
         Text::tr("A few DLLs only load from the real game folder. MO2 copies them in "
                  "for the run, backs up any game file they replace, and takes them out "
                  "again once the game closes.")},
        {0xE74E, Text::tr("New files"),
         Text::tr("Files a program creates there go to <b>Overwrite/Root</b>.")},
        {0xEA86, Text::tr("Root Builder"),
         Text::tr("If the Root Builder plugin is installed and on, it stays in charge "
                  "of the Root folders.")}}},

      {0xE8F1,
       Text::tr("Nexus collections"),
       Text::tr("Install a whole Nexus collection in one go."),
       {{0xE721, Text::tr("Find a collection"),
         Text::tr("In <b>Tools</b> or <b>Nexus</b>: <b>Browse collections…</b> to "
                  "search, or <b>Download a Nexus collection…</b> for a link. A "
                  "collection link from the browser opens it too.")},
        {0xE8F1, Text::tr("Collection window"),
         Text::tr("Shows <b>What it needs</b>, the author's notes and the mods. "
                  "<b>Install into</b> chooses the current profile, a new profile or a "
                  "new instance. Then three steps: download, install, order and "
                  "finish.")},
        {0xE9D5, Text::tr("Checks before installing"),
         Text::tr("On the first <b>Start</b>, MO2 looks for things that may get in the "
                  "way: another game version, a missing script extender or DLC, mods "
                  "removed from Nexus, mods already active in the profile, or many "
                  "files to click with a free account. You can start anyway and fix "
                  "them later.")},
        {0xE8CB, Text::tr("Free and Premium"),
         Text::tr("Premium downloads on its own, up to three files at a time. A free "
                  "account opens each file's page on Nexus for its slow download "
                  "click.")},
        {0xE769, Text::tr("Pause and come back"),
         Text::tr("<b>Pause</b>, <b>Continue</b> and <b>Retry failed</b> work at any "
                  "time. An unfinished collection is offered again later. Notices wait "
                  "while it installs.")}}},

      {0xE790,
       Text::tr("Themes"),
       Text::tr("Light, Dark, or a theme of your own."),
       {{0xE790, Text::tr("Choose a theme"),
         Text::tr("Settings &gt; <b>Theme</b> &gt; <b>Interface theme</b>. Your own "
                  "themes show with their base, such as \"Ember (on Dark)\".")},
        {0xE70F, Text::tr("Theme editor"),
         Text::tr("<b>New theme...</b> asks for a name, a base (Light or Dark) and six "
                  "colors, and the window shows them as you pick. <b>Save</b> keeps "
                  "them; <b>Cancel</b> goes back. <b>Edit theme...</b> changes one of "
                  "yours.")},
        {0xE838, Text::tr("Theme files"),
         Text::tr("Themes are .qss files in the <b>stylesheets</b> folder (<b>Open "
                  "themes folder</b>), and MO2 reloads one when its file is saved. "
                  "Don't edit Light.qss or dark.qss: updates replace them.")},
        {0xE8A5, Text::tr("Classic MO2 themes"),
         Text::tr("Older MO2 themes load on top of Dark or Light. docs/THEMES.md in "
                  "the MO2 Revamped repository explains how to write one.")},
        {0xE7B3, Text::tr("Highlight colors"),
         Text::tr("<b>Mod and plugin highlight colors</b> sets the conflict colors; "
                  "<b>Restore default colors</b> brings them back.")}}},

      {0xEA8F,
       Text::tr("Notices"),
       Text::tr("Short messages at the bottom of the window, and the notification bell."),
       {{0xE8BD, Text::tr("Notices"),
         Text::tr("Small cards such as \"Installation successful\" rise at the bottom "
                  "and fade on their own. Hover over one to keep it, click it to close "
                  "it. A repeated notice shows a counter instead of a new card.")},
        {0xE767, Text::tr("Sound only"),
         Text::tr("Settings &gt; General: <b>Notices: sound only, without the card</b> "
                  "plays Windows' notification sound instead. The notices are still in "
                  "MO2's log, and questions and errors still show.")},
        {0xEA8F, Text::tr("Notifications"),
         Text::tr("The bell on the command bar lists problems that plugins found, such "
                  "as a missing script extender. A dot on the bell means there is "
                  "something to read; <b>Fix it</b> solves a problem when it can.")}}},

      {0xE713,
       Text::tr("Settings"),
       Text::tr("What each page of Settings (Ctrl+S) holds."),
       {{0xE713, Text::tr("General"),
         Text::tr("Language, interface size, notices, the download list, updates, "
                  "profile defaults and the toolbar.")},
        {0xE790, Text::tr("Theme"),
         Text::tr("The interface theme, your own themes and the highlight colors.")},
        {0xE8FD, Text::tr("Mod list"),
         Text::tr("How the mod list shows mods, separators and conflicts, and how "
                  "installing and dragging behave.")},
        {0xE8B7, Text::tr("Paths"),
         Text::tr("Where this instance keeps its downloads, mods, profiles, caches and "
                  "Overwrite.")},
        {0xE774, Text::tr("Nexus"),
         Text::tr("Connect your Nexus account, see its API use and choose the download "
                  "servers.")},
        {0xEA86, Text::tr("Plugins"),
         Text::tr("Turn MO2's plugins on or off and change their options.")},
        {0xE90F, Text::tr("Workarounds"),
         Text::tr("Steam, network and virtual file system settings for problem "
                  "cases.")},
        {0xE9D9, Text::tr("Diagnostics"),
         Text::tr("How much MO2 logs, crash dumps, and where the logs are.")}}},

      {0xEC7A,
       Text::tr("Developer mode"),
       Text::tr("Features in testing, for those who want to try them."),
       {{0xEC7A, Text::tr("Turn it on"),
         Text::tr("In <b>…</b> &gt; <b>About MO2 Revamped</b> &gt; <b>Components</b>, "
                  "tap the <b>MO2 base</b> row seven times, as with a phone's build "
                  "number.")},
        {0xE713, Text::tr("Settings > Developer"),
         Text::tr("A page at the end of Settings lists the features in testing. None "
                  "of them is needed to play.")},
        {0xE711, Text::tr("Turn it off"),
         Text::tr("Clear <b>Developer mode</b> on that page. The page is gone the next "
                  "time Settings opens.")}}},

      {0xE897,
       Text::tr("Getting help"),
       Text::tr("When something goes wrong."),
       {{0xEBE8, Text::tr("Create bug report"),
         Text::tr("<b>…</b> &gt; <b>Create bug report</b> shows a report with the logs "
                  "and versions, without your user name, PC name, e-mail or Nexus "
                  "keys. Save it as a ZIP or copy its summary.")},
        {0xE7BA, Text::tr("Report a problem"),
         Text::tr("Opens the MO2 Revamped issue tracker on GitHub. Attach the report "
                  "ZIP to your issue.")},
        {0xE9D9, Text::tr("Log panel"),
         Text::tr("<b>…</b> &gt; <b>Log panel</b> shows MO2's log at the bottom of the "
                  "window.")},
        {0xE8A5, Text::tr("Documentation"),
         Text::tr("The Mod Organizer 2 documentation still covers everything MO2 "
                  "Revamped keeps.")}}},
  };
}

// one page: its heading and a card with a Settings row per entry
QWidget* makePage(const Topic& topic, QWidget* parent)
{
  auto* scroll = new QScrollArea(parent);
  scroll->setObjectName(QStringLiteral("instanceDetailsScroll"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("fluentPageContent"));
  auto* layout = new QVBoxLayout(content);
  // the gap to the window's edge, with the scroll bar inside it
  layout->setContentsMargins(0, 0, 16, 0);
  layout->setSpacing(4);

  auto* title = new QLabel(topic.name, content);
  title->setProperty("wizardPageTitle", true);
  auto* subtitle = new QLabel(topic.subtitle, content);
  subtitle->setProperty("wizardPageText", true);
  subtitle->setWordWrap(true);
  layout->addWidget(title);
  layout->addWidget(subtitle);
  layout->addSpacing(8);

  auto* card = new QFrame(content);
  card->setProperty("fluentCard", true);
  auto* rows = new QVBoxLayout(card);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(0);
  for (const Row& entry : topic.rows) {
    auto* row = new QFrame(card);
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    row->setMinimumHeight(62);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(18, 10, 12, 10);
    rowLayout->setSpacing(14);
    rowLayout->addWidget(FluentPanels::glyphLabel(entry.glyph, row), 0,
                         Qt::AlignVCenter);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto* rowTitle = new QLabel(entry.title, row);
    rowTitle->setObjectName(QStringLiteral("fluentSettingTitle"));
    rowTitle->setWordWrap(true);
    rowTitle->setTextFormat(Qt::PlainText);
    auto* text = new QLabel(entry.text, row);
    text->setObjectName(QStringLiteral("fluentSettingDescription"));
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    texts->addWidget(rowTitle);
    texts->addWidget(text);
    rowLayout->addLayout(texts, 1);
    rows->addWidget(row);
  }
  layout->addWidget(card);
  layout->addStretch(1);
  scroll->setWidget(content);
  return scroll;
}

// a topic's words, for the search: its name, subtitle and rows without tags
QString searchText(const Topic& topic)
{
  QStringList parts{topic.name, topic.subtitle};
  for (const Row& row : topic.rows) {
    parts << row.title << QTextDocumentFragment::fromHtml(row.text).toPlainText();
  }
  return parts.join(QLatin1Char('\n'));
}

}  // namespace

void show(QWidget* parent)
{
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("RevampedGuideDialog"));
  dialog.setWindowTitle(Text::tr("MO2 Revamped guide"));
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog.setMinimumSize(820, 540);
  dialog.resize(960, 660);

  // no right margin: the pages scroll at the window's edge
  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(16, 16, 0, 12);
  layout->setSpacing(12);

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("revampedGuideTabs"));
  layout->addWidget(tabs, 1);

  const std::vector<Topic> all = topics();
  QList<char16_t> glyphs;
  QStringList words;
  for (const Topic& topic : all) {
    tabs->addTab(makePage(topic, tabs), topic.name);
    glyphs << topic.glyph;
    words << searchText(topic);
  }
  FluentNavigation::attach(tabs, glyphs);

  // the search above the pages dims the topics that don't mention it
  auto* search = new QLineEdit(&dialog);
  search->setObjectName(QStringLiteral("revampedGuideSearch"));
  search->setPlaceholderText(Text::tr("Search the guide"));
  search->setAccessibleName(Text::tr("Search the guide"));
  search->setClearButtonEnabled(true);
  FluentNavigation::setNavigationHeader(tabs, search);
  QObject::connect(search, &QLineEdit::textChanged, tabs, [tabs, words](const QString& query) {
    const QString needle = query.trimmed();
    int first = -1;
    for (int i = 0; i < tabs->count(); ++i) {
      const bool match = needle.isEmpty() || words[i].contains(needle, Qt::CaseInsensitive);
      tabs->setTabEnabled(i, match);
      if (match && first < 0) {
        first = i;
      }
    }
    if (first >= 0 && !tabs->isTabEnabled(tabs->currentIndex())) {
      tabs->setCurrentIndex(first);
    }
    FluentNavigation::sync(tabs);
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  buttons->setContentsMargins(0, 0, 16, 0);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  search->setFocus();
  dialog.exec();
}

void applyWhatsThis(Ui::MainWindow* ui)
{
  ui->actionChange_Game->setWhatsThis(Text::tr(
      "Switch to another MO2 instance, or create or manage instances. Each instance "
      "has its own game, mods, downloads and profiles."));
  ui->actionInstallMod->setWhatsThis(Text::tr(
      "Install a mod from an archive on your PC. Archives in the Downloads tab "
      "install with a double-click."));
  ui->actionNexus->setWhatsThis(Text::tr(
      "Open the game's page on Nexus Mods. Its menu also finds and installs Nexus "
      "collections."));
  ui->actionAdd_Profile->setWhatsThis(Text::tr(
      "Create, copy, rename or delete profiles. Each profile has its own mod list, "
      "plugin order and, if you want, its own saves and game settings."));
  ui->action_Refresh->setWhatsThis(Text::tr(
      "Read the mods, plugins and downloads again, after changing files outside "
      "MO2."));
  ui->actionModify_Executables->setWhatsThis(Text::tr(
      "Add or change the programs MO2 can start through its virtual file system, "
      "such as the game, a script extender or a tool."));
  ui->actionTool->setWhatsThis(Text::tr(
      "Tools from MO2's plugins, such as the INI editor and Nexus collections."));
  ui->actionSettings->setWhatsThis(Text::tr(
      "MO2's settings: language, theme, mod list, paths, Nexus, plugins and "
      "diagnostics."));
  ui->actionEndorseMO->setWhatsThis(Text::tr(
      "Endorse MO2 Revamped on Nexus Mods, or say you won't."));
  ui->tabWidget->setWhatsThis(Text::tr(
      "Plugins, archives, the game's files as the game sees them (Data), saves and "
      "downloads. Games without plugins or archives don't show those tabs."));
  ui->logDock->setWhatsThis(Text::tr(
      "MO2's log. Errors and warnings here help to find a problem; Create bug "
      "report in the … menu saves them without your private data."));
}

}  // namespace RevampedGuide
