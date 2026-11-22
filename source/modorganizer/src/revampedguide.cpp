#include "revampedguide.h"

#include "fluenticons.h"
#include "fluentnavigation.h"
#include "fluentpanels.h"
#include "ui_mainwindow.h"

#include <iplugingame.h>
#include <utility.h>

#include <algorithm>

#include <QAction>
#include <QEvent>
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
#include <QUrl>
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
  // an optional button: its label and what it does ("url:<address>" opens
  // the browser, anything else goes to the window that opened the guide)
  QString button = {};
  QString action = {};
};

struct Topic
{
  QString id;
  char16_t glyph;
  QString name;
  QString subtitle;
  std::vector<Row> rows;
};

// a Settings row's button that opens that page of Settings
Row settingsRow(char16_t glyph, const QString& title, const QString& text,
                const QString& page)
{
  return {glyph, title, text, Text::tr("Open this page"),
          QStringLiteral("settings:") + page};
}

// the start of the rows every game page ends with
Row runFromMo2(const QString& launchers)
{
  return {0xE768, Text::tr("Start the game from MO2"),
          Text::tr("Choose the program in the top bar and press <b>Run</b>. Started "
                   "from %1, the game doesn't see the mods.")
              .arg(launchers)};
}

std::vector<Topic> topics()
{
  return {
      {FirstSteps,
       0xE7C1,
       Text::tr("First steps"),
       Text::tr("From a new instance to the game with mods, in six steps."),
       {{0xE8B7, Text::tr("1. Create an instance"),
         Text::tr("An instance is one game's setup: its mods, downloads and profiles. "
                  "<b>Manage instances…</b> on the command bar creates one: choose "
                  "<b>Global (recommended)</b>, the game, its edition and a name."),
         Text::tr("Manage instances"), QStringLiteral("instances")},
        {0xE774, Text::tr("2. Connect your Nexus account"),
         Text::tr("Settings &gt; Nexus &gt; <b>Connect to Nexus</b> links your account "
                  "in the browser; <b>Enter API key manually</b> works too. Then the "
                  "<b>Mod manager download</b> buttons on Nexus send files to MO2."),
         Text::tr("Open Settings > Nexus"), QStringLiteral("settings:nexusTab")},
        {0xE896, Text::tr("3. Install mods"),
         Text::tr("Double-click an archive in <b>Downloads</b> to install it, or use "
                  "<b>Install</b> (Ctrl+M) for one on your PC. Tick a mod in the list "
                  "to turn it on; a mod lower in the list wins a conflict."),
         Text::tr("Install a mod"), QStringLiteral("install")},
        {0xE8F1, Text::tr("4. Or a whole collection or list"),
         Text::tr("Tools &gt; <b>Nexus collections</b> installs a Nexus collection, and "
                  "Tools &gt; <b>Wabbajack lists</b> a Wabbajack list in an instance of "
                  "its own. Both download, install and order everything for you."),
         Text::tr("Browse collections"), QStringLiteral("collections")},
        {0xE768, Text::tr("5. Start the game"),
         Text::tr("Choose the game in the top bar and press <b>Run</b>. Always start it "
                  "from MO2: started from Steam or its launcher, it doesn't see the "
                  "mods. When a script extender is installed, MO2 offers to use it."),
         Text::tr("Executables"), QStringLiteral("executables")},
        {0xEBE8, Text::tr("6. If something goes wrong"),
         Text::tr("The bell on the command bar lists the problems MO2 found. Create a "
                  "bug report to share the logs without your private data."),
         Text::tr("Create bug report"), QStringLiteral("bugreport")}}},

      {QStringLiteral("getting-around"),
       0xE80F,
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

      {QStringLiteral("profiles"),
       0xE77B,
       Text::tr("Profiles"),
       Text::tr("Each profile keeps its own mod list and plugin order."),
       {{0xE77B, Text::tr("Manage profiles"),
         Text::tr("<b>Profiles</b> (Ctrl+P) creates, duplicates, renames and deletes "
                  "profiles, and <b>Use this profile</b> switches to one. The top bar "
                  "switches profiles too."),
         Text::tr("Profiles"), QStringLiteral("profiles")},
        {0xE8F7, Text::tr("Profile options"),
         Text::tr("<b>Profile-specific saves</b> and <b>Profile-specific game INI "
                  "files</b> keep saves and game settings apart for each profile. "
                  "<b>Transfer saves…</b> copies saves between profiles.")},
        {0xE713, Text::tr("Defaults"),
         Text::tr("Settings &gt; General &gt; <b>Profile defaults</b> chooses the "
                  "options a new profile starts with.")},
        {0xE72E, Text::tr("When it can't change"),
         Text::tr("A profile can't be renamed or deleted while it is the current one "
                  "or while the game runs.")}}},

      {QStringLiteral("downloads"),
       0xE896,
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

      {QStringLiteral("root"),
       0xE8B7,
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

      {QStringLiteral("collections"),
       0xE8F1,
       Text::tr("Nexus collections"),
       Text::tr("Install a whole Nexus collection in one go."),
       {{0xE721, Text::tr("Find a collection"),
         Text::tr("In <b>Tools</b> or <b>Nexus</b>: <b>Browse collections…</b> to "
                  "search, or <b>Download a Nexus collection…</b> for a link. A "
                  "collection link from the browser opens it too."),
         Text::tr("Browse collections"), QStringLiteral("collections")},
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

      {QStringLiteral("wabbajack"),
       0xE7B8,
       Text::tr("Wabbajack lists"),
       Text::tr("Install a Wabbajack list in an instance of its own."),
       {{0xE721, Text::tr("Find a list"),
         Text::tr("Tools &gt; <b>Wabbajack lists</b> &gt; <b>Browse Wabbajack "
                  "lists…</b> shows Wabbajack's gallery by game. <b>Open a Wabbajack "
                  "list…</b> takes a .wabbajack file you already have."),
         Text::tr("Browse Wabbajack lists"), QStringLiteral("wabbajack")},
        {0xE896, Text::tr("Download the archives"),
         Text::tr("The list window shows <b>What it needs</b> and each archive with its "
                  "status. <b>Download</b> fetches them and <b>Check by hash</b> "
                  "verifies those already in the downloads folder. Without Premium, "
                  "each Nexus file is one click on its page.")},
        {0xE8B7, Text::tr("Install the list"),
         Text::tr("<b>Install</b> puts the list in a new instance: give it a name and "
                  "an empty install folder that isn't the game's folder, the downloads "
                  "folder or a drive's root.")},
        {0xE9D5, Text::tr("Manual steps"),
         Text::tr("Some lists leave files for the game folder. <b>Manual steps of this "
                  "list…</b>, in the same menu, copies them, backs up what they replace "
                  "and runs their patchers when you ask.")},
        {0xE7BA, Text::tr("Common problems"),
         Text::tr("A list needs its game installed and a lot of free space; MO2 says "
                  "how much before it starts. When Nexus's request limit is reached, "
                  "continue later: nothing is lost. A file gone from its server has "
                  "to be found by hand.")}}},

      {QStringLiteral("appearance"),
       0xE790,
       Text::tr("Appearance"),
       Text::tr("The theme, the size of the interface, animations and the toolbar."),
       {settingsRow(0xE790, Text::tr("Choose a theme"),
                    Text::tr("Settings &gt; <b>Appearance</b> &gt; <b>Interface "
                             "theme</b>: Light, Dark or <b>OLED Black</b>, a Dark with "
                             "pure black for OLED screens. Your own themes show with "
                             "their base, such as \"Ember (on Dark)\"."),
                    QStringLiteral("themeTab")),
        {0xE916, Text::tr("Animations"),
         Text::tr("<b>Off</b> shows every change at once. <b>Saver (30 Hz)</b> and "
                  "<b>Standard (60 Hz)</b> animate tabs, switches, menus, the log panel "
                  "and the Run button; <b>Monitor</b> follows the screen's refresh "
                  "rate, up to 240 Hz, using a little more CPU while something moves. "
                  "It applies when you press OK.")},
        {0xE8D3, Text::tr("Size and text"),
         Text::tr("<b>Interface size</b> scales every window. <b>Smooth text in grey "
                  "(OLED screens)</b> avoids colored edges on letters. Both apply when "
                  "MO2 starts again.")},
        {0xE700, Text::tr("Toolbar and panels"),
         Text::tr("The toolbar's icon size and button style (icons, text or both), "
                  "and whether the status bar and the log panel show.")},
        {0xE70F, Text::tr("Theme editor"),
         Text::tr("<b>New theme…</b> asks for a name, a base (Light or Dark) and six "
                  "colors, and the window shows them as you pick. <b>Save</b> keeps "
                  "them; <b>Cancel</b> goes back. <b>Edit theme…</b> changes one of "
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

      {QStringLiteral("notices"),
       0xEA8F,
       Text::tr("Notices"),
       Text::tr("Short messages at the bottom of the window, and the notification bell."),
       {{0xE8BD, Text::tr("Notices"),
         Text::tr("Small cards such as \"Installation successful\" rise at the bottom "
                  "and fade on their own. Hover over one to keep it, click it to close "
                  "it. A repeated notice shows a counter instead of a new card.")},
        settingsRow(0xE767, Text::tr("Sound only"),
                    Text::tr("Settings &gt; General: <b>Notices: sound only, without the "
                             "card</b> plays Windows' notification sound instead. The "
                             "notices are still in MO2's log, and questions and errors "
                             "still show."),
                    QStringLiteral("generalTab")),
        {0xEA8F, Text::tr("Notifications"),
         Text::tr("The bell on the command bar lists problems that plugins found, such "
                  "as a missing script extender. A dot on the bell means there is "
                  "something to read; <b>Fix it</b> solves a problem when it can.")}}},

      {SettingsTopic,
       0xE713,
       Text::tr("Settings"),
       Text::tr("What each page of Settings (Ctrl+S) holds."),
       {settingsRow(0xE713, Text::tr("General"),
                    Text::tr("Language, notices, the download list, updates, profile "
                             "defaults, and windows and dialogs."),
                    QStringLiteral("generalTab")),
        settingsRow(0xE790, Text::tr("Appearance"),
                    Text::tr("The theme (Light, Dark, OLED Black or your own), the "
                             "interface size, animations, the toolbar and the "
                             "highlight colors."),
                    QStringLiteral("themeTab")),
        settingsRow(0xE8FD, Text::tr("Mod list"),
                    Text::tr("How the mod list shows mods, separators and conflicts, "
                             "and how installing and dragging behave."),
                    QStringLiteral("uiTab")),
        settingsRow(0xE8B7, Text::tr("Paths"),
                    Text::tr("Where this instance keeps its downloads, mods, profiles, "
                             "caches and Overwrite."),
                    QStringLiteral("pathsTab")),
        settingsRow(0xE774, Text::tr("Nexus"),
                    Text::tr("Connect your Nexus account, see its API use and choose "
                             "the download servers."),
                    QStringLiteral("nexusTab")),
        settingsRow(0xEA86, Text::tr("Plugins"),
                    Text::tr("Turn MO2's plugins on or off and change their options."),
                    QStringLiteral("pluginsTab")),
        settingsRow(0xE90F, Text::tr("Workarounds"),
                    Text::tr("Steam, network and virtual file system settings for "
                             "problem cases."),
                    QStringLiteral("workaroundTab")),
        settingsRow(0xE9D9, Text::tr("Diagnostics"),
                    Text::tr("How much MO2 logs, crash dumps, and where the logs are."),
                    QStringLiteral("diagnosticsTab"))}},

      {QStringLiteral("plugins"),
       0xEA86,
       Text::tr("Plugins"),
       Text::tr("The parts of MO2 that support games, install mods and add tools."),
       {settingsRow(0xEA86, Text::tr("Installed plugins"),
                    Text::tr("Settings &gt; <b>Plugins</b> lists MO2's plugins by "
                             "kind: games, installers, tools, previews and checks. "
                             "Choose one to see its author, version and description."),
                    QStringLiteral("pluginsTab")),
        {0xE711, Text::tr("Turn a plugin off"),
         Text::tr("Clear <b>Enable plugin</b> to turn one off; MO2 asks first when "
                  "others need it. The <b>Plugin blacklist</b> skips a plugin's file "
                  "when MO2 starts.")},
        {0xE70F, Text::tr("Plugin options"),
         Text::tr("<b>Plugin-specific options</b> lists a plugin's settings; "
                  "double-click a value to change it.")},
        {0xE74C, Text::tr("Other games' plugins"),
         Text::tr("A plugin made for one game, such as the FNIS and OMOD tools or the "
                  "Elden Ring tools, isn't loaded in other games' instances, so menus "
                  "stay short and MO2 starts faster. Developer mode loads them all.")},
        {0xE8A5, Text::tr("Game support wiki"),
         Text::tr("When a game's plugin names a modding wiki, <b>…</b> &gt; <b>Game "
                  "support wiki</b> opens it.")}}},

      {QStringLiteral("developer"),
       0xEC7A,
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

      {QStringLiteral("help"),
       0xE897,
       Text::tr("Getting help"),
       Text::tr("When something goes wrong."),
       {{0xEBE8, Text::tr("Create bug report"),
         Text::tr("<b>…</b> &gt; <b>Create bug report</b> shows a report with the logs "
                  "and versions, without your user name, PC name, e-mail or Nexus "
                  "keys. Save it as a ZIP or copy its summary."),
         Text::tr("Create bug report"), QStringLiteral("bugreport")},
        {0xE7BA, Text::tr("Report a problem"),
         Text::tr("Opens the MO2 Revamped issue tracker on GitHub. Attach the report "
                  "ZIP to your issue."),
         Text::tr("Open the issue tracker"),
         QStringLiteral("url:https://github.com/ArialSenki/MO2-Revamped/issues")},
        {0xE9D9, Text::tr("Log panel"),
         Text::tr("<b>…</b> &gt; <b>Log panel</b> shows MO2's log at the bottom of the "
                  "window.")},
        {0xE8A5, Text::tr("Documentation"),
         Text::tr("The Mod Organizer 2 documentation still covers everything MO2 "
                  "Revamped keeps.")}}},

      {QStringLiteral("game-cyberpunk"),
       0xE7FC,
       QStringLiteral("Cyberpunk 2077"),
       Text::tr("What MO2 Revamped does for this game, and its usual problems."),
       {{0xE8FD, Text::tr("Archive order"),
         Text::tr("The game loads .archive files by name. To use the mod order "
                  "instead, turn on the Cyberpunk 2077 plugin's "
                  "<b>enforce_archive_load_order</b> and <b>reverse_archive_load_order</b> "
                  "in Settings &gt; Plugins: then the mod lower in the list wins. Tools "
                  "&gt; <b>Archive conflicts</b> shows which .archive wins where two "
                  "mods change the same files.")},
        {0xE895, Text::tr("REDmod"),
         Text::tr("Changed REDmods are deployed before the game starts. <b>Cyberpunk "
                  "2077 - skip REDmod deploy</b> starts without it and <b>Manually "
                  "deploy REDmod</b> runs it alone. REDmods need the free REDmod DLC.")},
        {0xE74C, Text::tr("Frameworks"),
         Text::tr("Most mods need RED4ext, Cyber Engine Tweaks, redscript, ArchiveXL, "
                  "TweakXL or Codeware. A notification names a missing one.")},
        {0xE7BA, Text::tr("Scripts that don't compile"),
         Text::tr("When redscript can't compile the mods' scripts, the notification "
                  "names the mods. The full log is r6/logs/redscript_rCURRENT.log in "
                  "Overwrite. Update or turn off the mods it names.")},
        runFromMo2(QStringLiteral("Steam, GOG")),
        {0xE8A5, Text::tr("Modding wiki"),
         Text::tr("The Cyberpunk 2077 modding community's wiki."), Text::tr("Open"),
         QStringLiteral("url:https://wiki.redmodding.org/cyberpunk-2077-modding")}}},

      {QStringLiteral("game-bg3"),
       0xE7FC,
       QStringLiteral("Baldur's Gate 3"),
       Text::tr("What MO2 Revamped does for this game, and its usual problems."),
       {{0xE8FD, Text::tr("Load order"),
         Text::tr("Tools &gt; <b>BG3 Tools</b> &gt; <b>Load Order</b> lists the "
                  "modules with their problems. <b>Move needed mods up</b> puts every "
                  "mod after the ones it needs.")},
        {0xE9D5, Text::tr("Checks before starting"),
         Text::tr("MO2 warns when mods need mods that aren't active, load before what "
                  "they need, need the Script Extender, are installed twice or "
                  "conflict, and when the game turned mods off on its last start.")},
        {0xE896, Text::tr("LSLib and .NET"),
         Text::tr("Reading .pak files needs LSLib, downloaded from GitHub when you "
                  "agree, and the .NET 8 Runtime (x64). <b>Check For LsLib Updates</b> "
                  "in BG3 Tools downloads it again.")},
        {0xE77B, Text::tr("Profiles"),
         Text::tr("Each profile keeps its own modsettings.lsx; MO2 shows it to the "
                  "game in PlayerProfiles/Public.")},
        runFromMo2(QStringLiteral("Steam, GOG, Larian Launcher")),
        {0xE8A5, Text::tr("Modding wiki"),
         Text::tr("The Baldur's Gate 3 modding community's wiki."), Text::tr("Open"),
         QStringLiteral("url:https://wiki.bg3.community")}}},

      {QStringLiteral("game-bethesda"),
       0xE7FC,
       Text::tr("Bethesda games"),
       Text::tr("Skyrim, Fallout, Oblivion, Morrowind and Starfield: what MO2 does for "
                "them, and their usual problems."),
       {{0xE8FD, Text::tr("Plugin order"),
         Text::tr("The <b>Plugins</b> tab sets the order of the .esp, .esm and .esl "
                  "files. <b>Sort</b> uses LOOT; for New Vegas and TTW it's off unless "
                  "you allow it, as most of their guides advise against it.")},
        {0xE756, Text::tr("Script extender"),
         Text::tr("Install SKSE, F4SE or xNVSE from its archive: MO2 offers <b>Install "
                  "it in the game folder</b> (recommended), <b>I'll copy the files "
                  "myself</b> or <b>Install it as a regular mod</b>, for Root Builder. "
                  "Then the game starts through it. For Skyrim SE and Fallout 4, MO2 "
                  "checks that it matches the game version and that Address Library is "
                  "installed.")},
        {0xE7BA, Text::tr("Script extender plugins"),
         Text::tr("When some don't load, a notification says which and why, such as "
                  "an Address Library update or a missing DLL.")},
        {0xE8B9, Text::tr("Loose files and archives"),
         Text::tr("<b>Automatic archive invalidation</b>, in each profile, makes the "
                  "game load loose files over the ones in its archives.")},
        {0xE7B8, Text::tr("DLC and Creation Club"),
         Text::tr("They show in the mod list as \"Creation Club: name\"; order their "
                  "plugins in the Plugins tab. Starfield also warns about active ESP "
                  "plugins and sTestFile lines in StarfieldCustom.ini.")},
        runFromMo2(QStringLiteral("Steam")),
        {0xE90F, Text::tr("Programs"),
         Text::tr("Add the script extender, LOOT, xEdit or other tools as programs, so "
                  "they also see the mods."),
         Text::tr("Executables"), QStringLiteral("executables")}}},

      {QStringLiteral("game-witcher3"),
       0xE7FC,
       QStringLiteral("The Witcher 3"),
       Text::tr("What MO2 Revamped does for this game, and its usual problems."),
       {{0xE8B7, Text::tr("Mod folders"),
         Text::tr("Mods go to the game's Mods folder. When installing, MO2 adds a "
                  "missing <b>mod</b> prefix, removes an extra Mods folder around the "
                  "mod and moves its <b>dlc</b> and <b>bin</b> folders to the mod's root "
                  "folder.")},
        {0xE8B7, Text::tr("Root folder"),
         Text::tr("With the plugin's <b>root_folder_to_game_folder</b>, on by default, "
                  "each mod's root folder shows in the game folder. With Root Builder "
                  "on, Root Builder handles it.")},
        {0xE7BA, Text::tr("Scripts changed by several mods"),
         Text::tr("A notification says when mods change the same scripts: run "
                  "<b>Script Merger</b> to build mod0000_MergedFiles. MO2 adds Script "
                  "Merger to the programs when it finds it.")},
        {0xE7BA, Text::tr("Menus the next-gen game won't load"),
         Text::tr("A notification names a mod's menu XML files that are missing from "
                  "dx11filelist.txt or dx12filelist.txt; without them, the game "
                  "doesn't load those menus.")},
        runFromMo2(QStringLiteral("Steam, GOG")),
        {0xE90F, Text::tr("DX11 and DX12"),
         Text::tr("The programs include the DX12 version of the game when it is "
                  "installed."),
         Text::tr("Executables"), QStringLiteral("executables")}}},

      {QStringLiteral("game-stardew"),
       0xE7FC,
       QStringLiteral("Stardew Valley"),
       Text::tr("What MO2 Revamped does for this game, and its usual problems."),
       {{0xE756, Text::tr("SMAPI"),
         Text::tr("Mods need SMAPI. Install it with its own installer into the game "
                  "folder, not as a mod, then start the game with the <b>SMAPI</b> "
                  "program. A notification says when it is missing.")},
        {0xE8B7, Text::tr("Mod folders"),
         Text::tr("A mod is a folder with a manifest.json. MO2 finds them up to four "
                  "levels deep, so packs of several mods install as they come.")},
        {0xE9D5, Text::tr("Checks before starting"),
         Text::tr("MO2 warns when mods need mods that aren't there, newer versions of "
                  "other mods or a newer SMAPI, and when the same mod is installed "
                  "twice.")},
        {0xE77B, Text::tr("Saves"),
         Text::tr("The <b>Saves</b> tab shows the farms in "
                  "%APPDATA%\\StardewValley\\Saves.")},
        runFromMo2(QStringLiteral("Steam, GOG")),
        {0xE8A5, Text::tr("Modding guide"),
         Text::tr("The Stardew Valley wiki's guide to playing with mods."),
         Text::tr("Open"),
         QStringLiteral(
             "url:https://stardewvalleywiki.com/Modding:Player_Guide/Getting_Started")}}},

      {QStringLiteral("game-valheim"),
       0xE7FC,
       QStringLiteral("Valheim"),
       Text::tr("What MO2 Revamped does for this game, and its usual problems."),
       {{0xE756, Text::tr("BepInEx"),
         Text::tr("Most mods need BepInExPack_Valheim: install it as a mod. Its loader "
                  "loads from the mod. A notification says when it is missing.")},
        {0xE8B7, Text::tr("Mod files"),
         Text::tr("BepInEx plugins, InSlimVML mods, Advanced Builder .vbuild files and "
                  ".assets files are recognized when installing.")},
        {0xE713, Text::tr("Config files"),
         Text::tr("With the plugin's <b>sync_overwrite</b>, on by default, BepInEx "
                  ".cfg files that land in Overwrite move into the mod they belong to, "
                  "when MO2 starts and after the game closes.")},
        {0xE9D5, Text::tr("Checks before starting"),
         Text::tr("MO2 warns when plugins need other plugins that aren't there, such as "
                  "Jotunn, and when InSlimVML mods lack their loader.")},
        runFromMo2(QStringLiteral("Steam"))}},

      {QStringLiteral("game-eldenring"),
       0xE7FC,
       QStringLiteral("Elden Ring · Nightreign"),
       Text::tr("What MO2 Revamped does for this game, and its usual problems."),
       {{0xEA86, Text::tr("Native DLLs"),
         Text::tr("Tools &gt; <b>Native DLL profile</b> lists the DLLs of the enabled "
                  "mods and their load order, for each profile.")},
        {0xE77B, Text::tr("Saves"),
         Text::tr("Tools &gt; <b>Save isolation</b> keeps saves per profile, shares "
                  "them in the instance, or uses the global Steam save.")},
        {0xE768, Text::tr("Startup options"),
         Text::tr("Tools &gt; <b>Startup options</b> skips the intro logos, speeds up "
                  "the start (BootBoost) or starts the game minimized.")},
        {0xE72E, Text::tr("Online play"),
         Text::tr("MO2 doesn't change Easy Anti-Cheat or the online mode. Keep modded "
                  "files and saves for offline play.")},
        {0xE7BA, Text::tr("When the game doesn't start"),
         Text::tr("If it closes right after starting, or runs without MO2's bridge, "
                  "check that <b>Force load libraries</b> is on for its program, and "
                  "read <b>Open last bridge log</b> in Startup options.")},
        {0xE82D, Text::tr("The full guide"),
         Text::tr("Profiles, archive layouts, native DLLs and saves, in detail."),
         Text::tr("Open the guide"), QStringLiteral("game-guide")}}},
  };
}

// gives buttons the width of the widest, measured each time their page shows
// (the style sheet and the scale set their sizes only then)
class SameWidth : public QObject
{
public:
  SameWidth(QWidget* page, QList<QPushButton*> buttons)
      : QObject(page), m_buttons(std::move(buttons))
  {
    page->installEventFilter(this);
  }

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::Show || event->type() == QEvent::StyleChange) {
      int width = 0;
      for (QPushButton* button : m_buttons) {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        width = std::max(width, button->sizeHint().width());
      }
      for (QPushButton* button : m_buttons) {
        button->setFixedWidth(width);
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QList<QPushButton*> m_buttons;
};

// one page: its heading and a card with a Settings row per entry; a row's
// button records its action and closes the guide (links open at once)
QWidget* makePage(const Topic& topic, QWidget* parent, bool withActions,
                  const std::function<void(const QString&)>& onAction)
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
  QList<QPushButton*> buttons;
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

    const bool link = entry.action.startsWith(QLatin1String("url:"));
    if (!entry.button.isEmpty() && (link || withActions)) {
      auto* button = new QPushButton(entry.button, row);
      button->setObjectName(QStringLiteral("revampedGuideRowButton"));
      button->setAutoDefault(false);
      button->setCursor(Qt::PointingHandCursor);
      button->setIcon(FluentIcons::glyph(link ? 0xE8A7 : 0xE76C));
      // screen readers hear what the button does and for which row
      button->setAccessibleName(entry.button + QStringLiteral(": ") + entry.title);
      const QString action = entry.action;
      QObject::connect(button, &QPushButton::clicked, button, [action, onAction] {
        onAction(action);
      });
      rowLayout->addWidget(button, 0, Qt::AlignVCenter);
      buttons << button;
    }
    rows->addWidget(row);
  }
  // a page's buttons line up in one column of the same width, measured when
  // the page shows, once the theme has sized them
  if (buttons.size() > 1) {
    new SameWidth(content, buttons);
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

// the Settings page a topic is about, for the Help button of Settings
QString topicForSettingsPage(const QString& page)
{
  if (page == QLatin1String("themeTab")) {
    return QStringLiteral("appearance");
  }
  if (page == QLatin1String("pluginsTab")) {
    return QStringLiteral("plugins");
  }
  if (page == QLatin1String("developerTab")) {
    return QStringLiteral("developer");
  }
  return SettingsTopic;
}

}  // namespace

QString gameTopic(const MOBase::IPluginGame* game)
{
  if (game == nullptr) {
    return {};
  }
  const QString name = game->gameShortName().toLower();
  if (name == QLatin1String("cyberpunk2077")) {
    return QStringLiteral("game-cyberpunk");
  }
  if (name == QLatin1String("baldursgate3")) {
    return QStringLiteral("game-bg3");
  }
  if (name == QLatin1String("witcher3")) {
    return QStringLiteral("game-witcher3");
  }
  if (name == QLatin1String("stardewvalley")) {
    return QStringLiteral("game-stardew");
  }
  if (name == QLatin1String("valheim")) {
    return QStringLiteral("game-valheim");
  }
  if (name == QLatin1String("eldenring") || name == QLatin1String("nightreign")) {
    return QStringLiteral("game-eldenring");
  }
  static const QStringList bethesda{
      QStringLiteral("skyrimse"),   QStringLiteral("skyrim"),
      QStringLiteral("skyrimvr"),   QStringLiteral("fallout4"),
      QStringLiteral("fallout4vr"), QStringLiteral("fallout3"),
      QStringLiteral("falloutnv"),  QStringLiteral("ttw"),
      QStringLiteral("oblivion"),   QStringLiteral("morrowind"),
      QStringLiteral("starfield"),  QStringLiteral("enderal"),
      QStringLiteral("enderalse"),  QStringLiteral("nehrim")};
  if (bethesda.contains(name)) {
    return QStringLiteral("game-bethesda");
  }
  return {};
}

void show(QWidget* parent, const QString& topic, const Action& action,
          const QString& game)
{
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("RevampedGuideDialog"));
  dialog.setWindowTitle(Text::tr("MO2 Revamped guide"));
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog.setMinimumSize(860, 560);
  dialog.resize(1000, 720);

  // no right margin: the pages scroll at the window's edge
  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(16, 16, 0, 12);
  layout->setSpacing(12);

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("revampedGuideTabs"));
  layout->addWidget(tabs, 1);

  // a button's action runs once the guide is closed, so the window it opens
  // isn't stacked over the guide; links open at once
  QString chosen;
  const auto onAction = [&dialog, &chosen](const QString& id) {
    if (id.startsWith(QLatin1String("url:"))) {
      MOBase::shell::Open(QUrl(id.mid(4)));
      return;
    }
    chosen = id;
    dialog.accept();
  };

  // a topic named as a Settings page ("settings:<page>") is that page's topic
  QString start = topic;
  if (start.startsWith(QLatin1String("settings:"))) {
    start = topicForSettingsPage(start.mid(9));
  }

  const std::vector<Topic> all = topics();
  QList<char16_t> glyphs;
  QStringList words;
  int startIndex = 0;
  for (const Topic& entry : all) {
    // of the game pages, only the instance's own game's
    if (entry.id.startsWith(QLatin1String("game-")) && entry.id != game) {
      continue;
    }
    if (entry.id == start) {
      startIndex = tabs->count();
    }
    tabs->addTab(makePage(entry, tabs, static_cast<bool>(action), onAction), entry.name);
    glyphs << entry.glyph;
    words << searchText(entry);
  }
  tabs->setCurrentIndex(startIndex);
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

  if (!chosen.isEmpty() && action) {
    action(chosen);
  }
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
