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

#include "moapplication.h"
#include "commandline.h"
#include "instancemanager.h"
#include "loglist.h"
#include "mainwindow.h"
#include "messagedialog.h"
#include "multiprocess.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "organizercore.h"
#include "sanitychecks.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include "thread_utils.h"
#include "tutorialmanager.h"
#include <QAbstractItemView>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QVariant>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QSSLSocket>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTimer>
#include <QWidget>
#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <scopeguard.h>
#include <utility.h>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

// see addDllsToPath() below
#pragma comment(linker, "/manifestDependency:\""                                       \
                        "name='dlls' "                                                 \
                        "processorArchitecture='x86' "                                 \
                        "version='1.0.0.0' "                                           \
                        "type='win32' \"")

using namespace MOBase;
using namespace MOShared;

namespace
{

void applyNativeTitleBarTheme(QWidget* window, const QString& styleName);

class ItemViewSelectionHighlightFilter final : public QObject
{
public:
  explicit ItemViewSelectionHighlightFilter(QObject* parent) : QObject(parent) {}

  void installForWindow(QWidget* window)
  {
    if (qApp == nullptr || m_installed) {
      return;
    }

    // One application-level filter also covers item views created later in
    // dialogs. Tracking every child widget separately made selection painting
    // depend on widget lifetime and event-filter installation order.
    qApp->installEventFilter(this);
    m_installed = true;
    updateAllItemViews(window);
  }

  void clearActiveView()
  {
    m_dragView = nullptr;
    m_dragSawItem       = false;
    m_dragMoved         = false;
    setActiveView(nullptr);
  }

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    const QEvent::Type eventType = event->type();
    if (eventType != QEvent::MouseButtonPress &&
        !(eventType == QEvent::MouseMove &&
          (static_cast<QMouseEvent*>(event)->buttons() & Qt::LeftButton)) &&
        !(eventType == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) &&
        eventType != QEvent::Leave && eventType != QEvent::HoverLeave &&
        eventType != QEvent::KeyPress) {
      return QObject::eventFilter(watched, event);
    }

    auto* widget = qobject_cast<QWidget*>(watched);

    if (eventType == QEvent::MouseButtonPress) {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        if (auto* view = itemViewAtViewport(widget)) {
          const bool onItem =
              view->indexAt(eventPositionInViewport(view, widget, mouseEvent))
                  .isValid();
          m_dragView   = view;
          m_dragSawItem = onItem;
          m_dragMoved  = false;
          // Apply the translucent palette before the view processes selection
          // so dragging never flashes the theme's opaque selection color.
          setActiveView(view);
        } else {
          setActiveView(nullptr);
          m_dragView = nullptr;
          m_dragSawItem = false;
          m_dragMoved = false;
        }
      } else {
        m_dragView = nullptr;
        m_dragSawItem       = false;
        m_dragMoved         = false;
      }
    } else if (eventType == QEvent::MouseMove &&
               (static_cast<QMouseEvent*>(event)->buttons() & Qt::LeftButton)) {
      // Track the actual event coordinates. The global cursor can be ahead of
      // the queued mouse event during a drag and can point into another view.
      if (!m_dragView.isNull()) {
        m_dragMoved = true;
        const QPoint viewportPosition =
            eventPositionInViewport(m_dragView, widget,
                                    static_cast<QMouseEvent*>(event));
        if (m_dragView->viewport()->rect().contains(viewportPosition) &&
            m_dragView->indexAt(viewportPosition).isValid()) {
          m_dragSawItem = true;
        }
      }
    } else if (eventType == QEvent::MouseButtonRelease &&
               static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
      QPointer<QAbstractItemView> dragView = m_dragView;
      const bool completedSelectionDrag = m_dragMoved && m_dragSawItem;
      m_dragView = nullptr;
      m_dragSawItem       = false;
      m_dragMoved         = false;
      auto* view = itemViewAtViewport(widget);
      if (view != nullptr) {
        const QPoint viewportPosition =
            eventPositionInViewport(view, widget, static_cast<QMouseEvent*>(event));
        const bool releasedOnItem = view->indexAt(viewportPosition).isValid();
        const bool finishedRubberBandInsideView =
            view == dragView && completedSelectionDrag &&
            view->viewport()->rect().contains(viewportPosition);
        if (!releasedOnItem && !finishedRubberBandInsideView) {
          view = nullptr;
        }
      }
      // A normal click on blank space clears the visual highlight. A completed
      // row-selection drag remains highlighted when released inside its view.
      setActiveView(view);
    } else if (eventType == QEvent::Leave ||
               eventType == QEvent::HoverLeave) {
      // Repaint after Qt clears transient hover state to avoid stale decoration colors.
      if (auto* view = itemViewAtViewport(widget)) {
        QPointer<QAbstractItemView> repaintView(view);
        QTimer::singleShot(0, view, [repaintView]() {
          if (!repaintView.isNull()) {
            repaintView->viewport()->update();
            repaintView->update();
          }
        });
      }
    } else if (eventType == QEvent::KeyPress) {
      setActiveView(itemViewAtViewport(widget));
    } else {
      return QObject::eventFilter(watched, event);
    }

    return QObject::eventFilter(watched, event);
  }

private:
  static QAbstractItemView* itemViewAtViewport(QWidget* widget)
  {
    for (QWidget* parent = widget; parent != nullptr; parent = parent->parentWidget()) {
      auto* view = qobject_cast<QAbstractItemView*>(parent);
      if (view != nullptr &&
          (widget == view || widget == view->viewport() ||
           view->viewport()->isAncestorOf(widget))) {
        return isPopupView(view) ? nullptr : view;
      }
    }

    return nullptr;
  }

  static bool isPopupView(const QAbstractItemView* view)
  {
    // Combo-box and completer popups use item views too, but their highlighted
    // option is transient feedback and must retain the active theme's styling.
    return view->windowFlags().testFlag(Qt::Popup);
  }

  static QPoint eventPositionInViewport(QAbstractItemView* view,
                                        QWidget* eventWidget,
                                        const QMouseEvent* event)
  {
    if (eventWidget == nullptr) {
      return QPoint(-1, -1);
    }

    return view->viewport()->mapFromGlobal(
        eventWidget->mapToGlobal(event->position().toPoint()));
  }

  void setActiveView(QAbstractItemView* activeView)
  {
    if (m_activeView == activeView) {
      return;
    }

    QPointer<QAbstractItemView> previousView = m_activeView;
    m_activeView = activeView;
    updateView(previousView, false);
    updateView(m_activeView, true);
  }

  static void updateView(QAbstractItemView* view, bool active)
  {
    if (view == nullptr || isPopupView(view)) {
      return;
    }

    // The official themes use this property to distinguish the active view's
    // selected row from selections that remain in the other panes.
    const QVariant selectionState = view->property("selectionHighlightActive");
    if (!selectionState.isValid() || selectionState.toBool() != active) {
      view->setProperty("selectionHighlightActive", active);
      QStyle* style = view->style();
      style->unpolish(view);
      style->polish(view);
    }

    // Keep the existing row brush and let the Fusion style blend this alpha
    // highlight over it. Inactive selections keep their model state but paint
    // no selection fill.
    const QColor highlight = active ? QColor(75, 146, 198, 82)
                                    : QColor(0, 0, 0, 0);
    const QColor highlightedText = active ? QColor(24, 62, 95)
                                          : QColor(41, 56, 71);
    QPalette palette = view->palette();
    for (QPalette::ColorGroup group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
      palette.setColor(group, QPalette::Highlight, highlight);
      palette.setColor(group, QPalette::HighlightedText, highlightedText);
    }
    view->setPalette(palette);
    view->viewport()->setPalette(palette);
    view->viewport()->update();
  }

  static void updateAllItemViews(QWidget* window)
  {
    if (window == nullptr) {
      return;
    }

    QList<QWidget*> widgets = window->findChildren<QWidget*>();
    widgets.prepend(window);
    for (QWidget* widget : widgets) {
      if (auto* view = qobject_cast<QAbstractItemView*>(widget);
          view != nullptr && !isPopupView(view)) {
        updateView(view, false);
      }
    }
  }

  QPointer<QAbstractItemView> m_activeView;
  QPointer<QAbstractItemView> m_dragView;
  bool m_dragSawItem       = false;
  bool m_dragMoved         = false;
  bool m_installed         = false;
};

void installItemViewSelectionHighlightFilter(QWidget* window)
{
  static auto* filter = new ItemViewSelectionHighlightFilter(qApp);
  static bool signalsConnected = false;
  if (qApp != nullptr) {
    filter->installForWindow(window);
    if (!signalsConnected) {
      QObject::connect(qApp, &QGuiApplication::applicationStateChanged, filter,
                       [](Qt::ApplicationState state) {
                         if (state != Qt::ApplicationActive) {
                           filter->clearActiveView();
                         }
                       });
      signalsConnected = true;
    }
  }
}

}  // namespace

// style proxy that changes the appearance of drop indicators
//
class ProxyStyle : public QProxyStyle
{
public:
  ProxyStyle(QStyle* baseStyle = 0) : QProxyStyle(baseStyle) {}

  int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                const QWidget* widget = nullptr,
                QStyleHintReturn* returnData = nullptr) const override
  {
    // Use a regular, control-anchored list popup for every combo box. Some
    // platform styles replace it with a scrolling menu and ignore
    // maxVisibleItems(), which can make the list jump or display only a few
    // rows with extra up/down scroll buttons.
    if (hint == SH_ComboBox_Popup) {
      return 0;
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget) const override
  {
    if (element == QStyle::PE_IndicatorItemViewItemDrop) {

      // 0. Fix a bug that made the drop indicator sometimes appear on top
      // of the mod list when selecting a mod.
      if (option->rect.height() == 0 && option->rect.bottomRight() == QPoint(-1, -1)) {
        return;
      }

      // 1. full-width drop indicator
      QRect rect(option->rect);
      if (auto* view = qobject_cast<const QTreeView*>(widget)) {
        rect.setLeft(view->indentation());
        rect.setRight(widget->width());
      }

      // 2. stylish drop indicator
      painter->setRenderHint(QPainter::Antialiasing, true);

      QColor col(option->palette.windowText().color());
      QPen pen(col);
      pen.setWidth(2);
      col.setAlpha(50);

      painter->setPen(pen);
      painter->setBrush(QBrush(col));
      if (rect.height() == 0) {
        QPoint tri[3] = {rect.topLeft(), rect.topLeft() + QPoint(-5, 5),
                         rect.topLeft() + QPoint(-5, -5)};
        painter->drawPolygon(tri, 3);
        painter->drawLine(rect.topLeft(), rect.topRight());
      } else {
        painter->drawRoundedRect(rect, 5, 5);
      }
    } else {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
  }
};

// This adds the `dlls` directory to the path so the dlls can be found. How
// MO is able to find dlls in there is a bit convoluted:
//
// Dependencies on DLLs can be baked into an executable by passing a
// `manifestdependency` option to the linker. This can be done on the command
// line or with a pragma. Typically, the dependency will not be a hardcoded
// filename, but an assembly name, such as Microsoft.Windows.Common-Controls.
//
// When Windows loads the exe, it will look for this assembly in a variety of
// places, such as in the WinSxS folder, but also in the program's folder. It
// will look for `assemblyname.dll` or `assemblyname/assemblyname.dll` and try
// to load that.
//
// If these files don't exist, then the loader gets creative and looks for
// `assemblyname.manifest` and `assemblyname/assemblyname.manifest`. A manifest
// file is just an XML file that can contain a list of DLLs to load for this
// assembly.
//
// In MO's case, there's a `pragma` at the beginning of this file which adds
// `dlls` as an "assembly" dependency. This is a bit of a hack to just force
// the loader to eventually find `dlls/dlls.manifest`, which contains the list
// of all the DLLs MO requires to load.
//
// This file was handwritten in `modorganizer/src/dlls.manifest.qt5` and
// is copied and renamed in CMakeLists.txt into `bin/dlls/dlls.manifest`. Note
// that the useless and incorrect .qt5 extension is removed.
//
void addDllsToPath()
{
  const auto dllsPath =
      QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/dlls");

  QCoreApplication::setLibraryPaths(QStringList(dllsPath) +
                                    QCoreApplication::libraryPaths());

  env::prependToPath(dllsPath);
}

MOApplication::MOApplication(int& argc, char** argv) : QApplication(argc, argv)
{
  TimeThis tt("MOApplication()");

  qputenv("QML_DISABLE_DISK_CACHE", "true");

  connect(&m_styleWatcher, &QFileSystemWatcher::fileChanged, [&](auto&& file) {
    log::debug("style file '{}' changed, reloading", file);
    updateStyle(file);
  });

  m_defaultStyle = "windowsvista";
  updateStyle(m_defaultStyle);
  addDllsToPath();
}

OrganizerCore& MOApplication::core()
{
  return *m_core;
}

void MOApplication::firstTimeSetup(MOMultiProcess& multiProcess)
{
  connect(
      &multiProcess, &MOMultiProcess::messageSent, this,
      [this](auto&& s) {
        externalMessage(s);
      },
      Qt::QueuedConnection);
}

int MOApplication::setup(MOMultiProcess& multiProcess, bool forceSelect)
{
  TimeThis tt("MOApplication setup()");

  // makes plugin data path available to plugins, see
  // IOrganizer::getPluginDataPath()
  MOBase::details::setPluginDataPath(OrganizerCore::pluginDataPath());

  // figuring out the current instance
  m_instance = getCurrentInstance(forceSelect);
  if (!m_instance) {
    return 1;
  }

  // first time the data path is available, set the global property and log
  // directory, then log a bunch of debug stuff
  const QString dataPath = m_instance->directory();
  setProperty("dataPath", dataPath);

  if (!setLogDirectory(dataPath)) {
    reportError(tr("Failed to create log folder."));
    InstanceManager::singleton().clearCurrentInstance();
    return 1;
  }

  log::debug("command line: '{}'", QString::fromWCharArray(GetCommandLineW()));

  log::info("starting Mod Organizer version {} revision {} in {}, usvfs: {}",
            createVersionInfo().displayString(3), GITID,
            QCoreApplication::applicationDirPath(), MOShared::getUsvfsVersionString());

  if (multiProcess.secondary()) {
    log::debug("another instance of MO is running but --multiple was given");
  }

  log::info("data path: {}", m_instance->directory());
  log::info("working directory: {}", QDir::currentPath());

  tt.start("MOApplication::doOneRun() settings");

  // deleting old files, only for the main instance
  if (!multiProcess.secondary()) {
    purgeOldFiles();
  }

  // loading settings
  m_settings.reset(new Settings(m_instance->iniPath(), true));
  const QString configuredStyle =
      m_settings->interface().styleName().value_or(QString());
  QString selectedStyle;
  if (configuredStyle.compare("Light.qss", Qt::CaseInsensitive) == 0) {
    selectedStyle = "Light.qss";
  } else if (configuredStyle.compare("dark.qss", Qt::CaseInsensitive) == 0) {
    selectedStyle = "dark.qss";
  } else {
    selectedStyle = Settings::defaultInterfaceStyleName();
  }

  const QString stylesheetsPath =
      QDir(applicationDirPath())
          .filePath(ToQString(AppConfig::stylesheetsPath()));
  const auto stylePath = [&stylesheetsPath](const QString& fileName) {
    return QDir(stylesheetsPath).filePath(fileName);
  };
  if (!QFile::exists(stylePath(selectedStyle))) {
    selectedStyle = selectedStyle == "Light.qss" ? "dark.qss" : "Light.qss";
  }

  if (QFile::exists(stylePath(selectedStyle))) {
    if (configuredStyle != selectedStyle) {
      m_settings->interface().setStyleName(selectedStyle);
      log::info("normalized interface style '{}' to Revamped theme '{}'",
                configuredStyle.isEmpty() ? QStringLiteral("<unset>")
                                          : configuredStyle,
                selectedStyle);
    }
  } else {
    log::error("Revamped Light and Dark stylesheets are missing from '{}'",
               stylesheetsPath);
  }
  log::getDefault().setLevel(m_settings->diagnostics().logLevel());
  log::debug("using ini at '{}'", m_settings->filename());

  OrganizerCore::setGlobalCoreDumpType(m_settings->diagnostics().coreDumpType());

  tt.start("MOApplication::doOneRun() log and checks");

  // logging and checking
  env::Environment env;
  env.dump(*m_settings);
  m_settings->dump();
  sanity::checkEnvironment(env);

  m_modules = std::move(env.onModuleLoaded(qApp, [](auto&& m) {
    if (m.interesting()) {
      log::debug("loaded module {}", m.toString());
    }

    sanity::checkIncompatibleModule(m);
  }));

  auto sslBuildVersion = QSslSocket::sslLibraryBuildVersionString();
  auto sslVersion      = QSslSocket::sslLibraryVersionString();
  log::debug("SSL Build Version: {}, SSL Runtime Version {}", sslBuildVersion,
             sslVersion);

  // nexus interface
  tt.start("MOApplication::doOneRun() NexusInterface");
  log::debug("initializing nexus interface");
  m_nexus.reset(new NexusInterface(m_settings.get()));

  // organizer core
  tt.start("MOApplication::doOneRun() OrganizerCore");
  log::debug("initializing core");

  m_core.reset(new OrganizerCore(*m_settings));
  if (!m_core->bootstrap()) {
    reportError(tr("Failed to set up data paths."));
    InstanceManager::singleton().clearCurrentInstance();
    return 1;
  }

  // plugins
  tt.start("MOApplication::doOneRun() plugins");
  log::debug("initializing plugins");

  m_plugins = std::make_unique<PluginContainer>(m_core.get());
  m_plugins->loadPlugins();

  // instance
  if (auto r = setupInstanceLoop(*m_instance, *m_plugins)) {
    return *r;
  }

  if (m_instance->isPortable()) {
    log::debug("this is a portable instance");
  }

  tt.start("MOApplication::doOneRun() OrganizerCore setup");

  sanity::checkPaths(*m_instance->gamePlugin(), *m_settings);

  // setting up organizer core
  m_core->setManagedGame(m_instance->gamePlugin());
  m_core->createDefaultProfile();

  log::info("using game plugin '{}' ('{}', variant {}, steam id '{}') at {}",
            m_instance->gamePlugin()->gameName(),
            m_instance->gamePlugin()->gameShortName(),
            (m_settings->game().edition().value_or("").isEmpty()
                 ? "(none)"
                 : *m_settings->game().edition()),
            m_instance->gamePlugin()->steamAPPId(),
            m_instance->gamePlugin()->gameDirectory().absolutePath());

  CategoryFactory::instance().loadCategories();
  m_core->updateExecutablesList();
  m_core->updateModInfoFromDisc();
  m_core->setCurrentProfile(m_instance->profileName());

  return 0;
}

int MOApplication::run(MOMultiProcess& multiProcess)
{
  // checking command line
  TimeThis tt("MOApplication::run()");

  // show splash
  tt.start("MOApplication::doOneRun() splash");

  MOSplash splash(*m_settings, m_instance->directory(), m_instance->gamePlugin());

  tt.start("MOApplication::doOneRun() finishing");

  // start an api check
  QString apiKey;
  if (GlobalSettings::nexusApiKey(apiKey)) {
    m_nexus->getAccessManager()->apiCheck(apiKey);
  }

  // tutorials
  log::debug("initializing tutorials");
  TutorialManager::init(qApp->applicationDirPath() + "/" +
                            QString::fromStdWString(AppConfig::tutorialsPath()) + "/",
                        m_core.get());

  // styling
  if (!setStyleFile(m_settings->interface().styleName().value_or(""))) {
    // disable invalid stylesheet
    m_settings->interface().setStyleName("");
  }

  int res = 1;

  {
    tt.start("MOApplication::doOneRun() MainWindow setup");
    MainWindow mainWindow(*m_settings, *m_core, *m_plugins);

    // the nexus interface can show dialogs, make sure they're parented to the
    // main window
    m_nexus->getAccessManager()->setTopLevelWidget(&mainWindow);

    connect(
        &mainWindow, &MainWindow::styleChanged, this,
        [this](auto&& file) {
          setStyleFile(file);
        },
        Qt::QueuedConnection);

    auto& interfaceSettings = m_settings->interface();
    if (!interfaceSettings.themeChoicePromptCompleted()) {
      const QString currentTheme = interfaceSettings.styleName().value_or(
          Settings::defaultInterfaceStyleName());

      QMessageBox themeChoice(&mainWindow);
      themeChoice.setIcon(QMessageBox::Information);
      themeChoice.setWindowTitle(tr("MO2 Revamped"));
      themeChoice.setText(tr("Choose a theme for this instance."));
      themeChoice.setInformativeText(
          tr("You can change it later in Settings > Theme."));

      auto* lightButton =
          themeChoice.addButton(tr("Light"), QMessageBox::AcceptRole);
      auto* darkButton =
          themeChoice.addButton(tr("Dark"), QMessageBox::AcceptRole);
      auto* defaultButton =
          currentTheme == "Light.qss" ? lightButton : darkButton;
      themeChoice.setDefaultButton(defaultButton);
      themeChoice.setEscapeButton(defaultButton);
      themeChoice.exec();

      QString selectedTheme = currentTheme;
      if (themeChoice.clickedButton() == lightButton) {
        selectedTheme = "Light.qss";
      } else if (themeChoice.clickedButton() == darkButton) {
        selectedTheme = "dark.qss";
      }

      if (selectedTheme != currentTheme) {
        interfaceSettings.setStyleName(selectedTheme);
        setStyleFile(selectedTheme);
      }

      interfaceSettings.setThemeChoicePromptCompleted(true);
      if (m_settings->sync() != QSettings::NoError) {
        log::warn("failed to save the initial Revamped theme choice");
      }
    }

    log::debug("displaying main window");
    installItemViewSelectionHighlightFilter(&mainWindow);
    mainWindow.show();
    mainWindow.activateWindow();
    applyNativeTitleBarTheme(
        &mainWindow, m_settings->interface().styleName().value_or(QString()));
    splash.close();

    tt.stop();

    res = exec();
    mainWindow.close();

    // main window is about to be destroyed
    m_nexus->getAccessManager()->setTopLevelWidget(nullptr);
  }

  // reset geometry if the flag was set from the settings dialog
  m_settings->geometry().resetIfNeeded();

  return res;
}

void MOApplication::externalMessage(const QString& message)
{
  log::debug("received external message '{}'", message);

  MOShortcut moshortcut(message);

  if (moshortcut.isValid()) {
    if (moshortcut.hasExecutable()) {
      try {
        m_core->processRunner()
            .setFromShortcut(moshortcut)
            .setWaitForCompletion(ProcessRunner::TriggerRefresh)
            .run();
      } catch (std::exception&) {
        // user was already warned
      }
    }
  } else if (isNxmLink(message)) {
    MessageDialog::showMessage(tr("Download started"), qApp->activeWindow(), false);
    m_core->downloadRequestedNXM(message);
  } else {
    cl::CommandLine cl;

    if (auto r = cl.process(message.toStdWString())) {
      log::debug("while processing external message, command line wants to "
                 "exit; ignoring");

      return;
    }

    if (auto i = cl.instance()) {
      const auto ci = InstanceManager::singleton().currentInstance();

      if (*i != ci->displayName()) {
        reportError(
            tr("This shortcut or command line is for instance '%1', but the current "
               "instance is '%2'.")
                .arg(*i)
                .arg(ci->displayName()));

        return;
      }
    }

    if (auto p = cl.profile()) {
      if (*p != m_core->profileName()) {
        reportError(
            tr("This shortcut or command line is for profile '%1', but the current "
               "profile is '%2'.")
                .arg(*p)
                .arg(m_core->profileName()));

        return;
      }
    }

    cl.runPostOrganizer(*m_core);
  }
}

std::unique_ptr<Instance> MOApplication::getCurrentInstance(bool forceSelect)
{
  auto& m              = InstanceManager::singleton();
  auto currentInstance = m.currentInstance();

  if (forceSelect || !currentInstance) {
    // clear any overrides that might have been given on the command line
    m.clearOverrides();
    currentInstance = selectInstance();
  } else {
    if (!QDir(currentInstance->directory()).exists()) {
      // the previously used instance doesn't exist anymore

      // clear any overrides that might have been given on the command line
      m.clearOverrides();

      if (m.hasAnyInstances()) {
        reportError(QObject::tr("Instance at '%1' not found. Select another instance.")
                        .arg(currentInstance->directory()));
      } else {
        reportError(
            QObject::tr("Instance at '%1' not found. You must create a new instance")
                .arg(currentInstance->directory()));
      }

      currentInstance = selectInstance();
    }
  }

  return currentInstance;
}

std::optional<int> MOApplication::setupInstanceLoop(Instance& currentInstance,
                                                    PluginContainer& pc)
{
  for (;;) {
    const auto setupResult = setupInstance(currentInstance, pc);

    if (setupResult == SetupInstanceResults::Okay) {
      return {};
    } else if (setupResult == SetupInstanceResults::TryAgain) {
      continue;
    } else if (setupResult == SetupInstanceResults::SelectAnother) {
      InstanceManager::singleton().clearCurrentInstance();
      return ReselectExitCode;
    } else {
      return 1;
    }
  }
}

void MOApplication::purgeOldFiles()
{
  // remove the temporary backup directory in case we're restarting after an
  // update
  QString backupDirectory = qApp->applicationDirPath() + "/update_backup";
  if (QDir(backupDirectory).exists()) {
    shellDelete(QStringList(backupDirectory));
  }

  // Keep only the newest five usvfs logs. Avoid shellDelete here: its Windows
  // error UI can block startup when an older log is locked or access is denied.
  const QString logDirectory =
      qApp->property("dataPath").toString() + "/" +
      QString::fromStdWString(AppConfig::logPath());
  const QFileInfoList usvfsLogs =
      QDir(logDirectory).entryInfoList(QStringList{"usvfs*.log"}, QDir::Files,
                                       QDir::Name);
  const int excessLogs =
      usvfsLogs.size() > 5 ? static_cast<int>(usvfsLogs.size() - 5) : 0;
  for (int i = 0; i < excessLogs; ++i) {
    QFile oldLog(usvfsLogs.at(i).absoluteFilePath());
    if (!oldLog.remove()) {
      log::warn("failed to remove old usvfs log '{}': {}", oldLog.fileName(),
                oldLog.errorString());
    }
  }
}

void MOApplication::resetForRestart()
{
  LogModel::instance().clear();
  ResetExitFlag();

  // make sure the log file isn't locked in case MO was restarted and
  // the previous instance gets deleted
  log::getDefault().setFile({});

  // clear instance and profile overrides
  InstanceManager::singleton().clearOverrides();

  m_core     = {};
  m_plugins  = {};
  m_nexus    = {};
  m_settings = {};
  m_instance = {};
}

namespace
{

QColor styleWindowBackgroundColor(const QString& styleName)
{
  QFile stylesheet(styleName);
  if (!stylesheet.exists()) {
    const QString styleFile = QFileInfo(styleName).fileName();
    if (!styleFile.isEmpty()) {
      stylesheet.setFileName(
          QApplication::applicationDirPath() + "/" +
          MOBase::ToQString(AppConfig::stylesheetsPath()) + "/" + styleFile);
    }
  }

  if (stylesheet.exists() && stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
    const QString contents = QString::fromUtf8(stylesheet.readAll());
    const QRegularExpression widgetRule(
        QStringLiteral("(?:^|\\n)\\s*(?:QWidget|QMainWindow|QDialog)"
                       "[^\\{]*\\{([^}]*)\\}"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression backgroundColor(
        QStringLiteral("background(?:-color)?\\s*:\\s*[^;]*?#"
                       "([0-9a-f]{6})\\b"),
        QRegularExpression::CaseInsensitiveOption);

    auto rule = widgetRule.globalMatch(contents);
    while (rule.hasNext()) {
      const auto match = rule.next();
      const auto colorMatch = backgroundColor.match(match.captured(1));
      if (colorMatch.hasMatch()) {
        const QColor color(QStringLiteral("#") + colorMatch.captured(1));
        if (color.isValid()) {
          return color;
        }
      }
    }
  }

  return {};
}

bool stylePrefersDarkTitleBar(const QString& styleName)
{
  const QString styleId = QFileInfo(styleName).completeBaseName().toCaseFolded();
  if (styleId.contains("dark") || styleId.contains("dracula") ||
      styleId.contains("night") || styleId.contains("midnight") ||
      styleId.contains("nord") || styleId.contains("obsidian") ||
      styleId.contains("black")) {
    return true;
  }

  if (styleId.contains("light") || styleId.contains("paper") ||
      styleId.contains("parchment") || styleId.contains("white")) {
    return false;
  }

  const QColor background = styleWindowBackgroundColor(styleName);
  return background.isValid() && background.lightness() < 128;
}

void applyNativeTitleBarTheme(QWidget* window, const QString& styleName)
{
#ifdef Q_OS_WIN
  if (window == nullptr || !qobject_cast<QMainWindow*>(window)) {
    return;
  }

  const HWND handle = reinterpret_cast<HWND>(window->winId());
  const BOOL useDarkMode = stylePrefersDarkTitleBar(styleName) ? TRUE : FALSE;

  // Attribute 20 is supported on current Windows 11 builds; 19 is the
  // compatibility value used by earlier Windows 10 releases.
  const HRESULT result = DwmSetWindowAttribute(
      handle, static_cast<DWMWINDOWATTRIBUTE>(20), &useDarkMode,
      sizeof(useDarkMode));
  if (FAILED(result)) {
    DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(19),
                          &useDarkMode, sizeof(useDarkMode));
  }

  // Match the native caption to the background in the active Revamped QSS.
  // Windows 11 supports custom caption/text colors; older builds retain the
  // standard dark/light caption supplied above.
  const QColor background = styleWindowBackgroundColor(styleName);
  const COLORREF defaultColor = static_cast<COLORREF>(0xFFFFFFFFu);
  if (background.isValid()) {
    const QColor text = background.lightness() < 128 ? QColor("#D3D3D3")
                                                     : QColor("#293847");
    const auto toColorRef = [](const QColor& color) {
      return static_cast<COLORREF>(color.red() | (color.green() << 8) |
                                   (color.blue() << 16));
    };
    const COLORREF captionColor = toColorRef(background);
    const COLORREF textColor    = toColorRef(text);
    DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(35),
                          &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(36),
                          &textColor, sizeof(textColor));
  } else {
    DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(35),
                          &defaultColor, sizeof(defaultColor));
    DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(36),
                          &defaultColor, sizeof(defaultColor));
  }
#else
  Q_UNUSED(window);
  Q_UNUSED(styleName);
#endif
}

void applyNativeTitleBarThemeToVisibleMainWindows(const QString& styleName)
{
  for (QWidget* window : QApplication::topLevelWidgets()) {
    if (window->isVisible()) {
      applyNativeTitleBarTheme(window, styleName);
    }
  }
}

}  // namespace

bool MOApplication::setStyleFile(const QString& styleName)
{
  // remove all files from watch
  QStringList currentWatch = m_styleWatcher.files();
  if (currentWatch.count() != 0) {
    m_styleWatcher.removePaths(currentWatch);
  }
  // set new stylesheet or clear it
  if (styleName.length() != 0) {
    QString styleSheetName = applicationDirPath() + "/" +
                             MOBase::ToQString(AppConfig::stylesheetsPath()) + "/" +
                             styleName;
    if (QFile::exists(styleSheetName)) {
      m_styleWatcher.addPath(styleSheetName);
      updateStyle(styleSheetName);
    } else {
      updateStyle(styleName);
    }
  } else {
    setStyle(new ProxyStyle(QStyleFactory::create(m_defaultStyle)));
    setStyleSheet("");
  }
  applyNativeTitleBarThemeToVisibleMainWindows(styleName);
  return true;
}

bool MOApplication::notify(QObject* receiver, QEvent* event)
{
  try {
    return QApplication::notify(receiver, event);
  } catch (const std::exception& e) {
    log::error("uncaught exception in handler (object {}, eventtype {}): {}",
               receiver->objectName(), event->type(), e.what());
    reportError(tr("an error occurred: %1").arg(e.what()));
    return false;
  } catch (...) {
    log::error("uncaught non-std exception in handler (object {}, eventtype {})",
               receiver->objectName(), event->type());
    reportError(tr("an error occurred"));
    return false;
  }
}

namespace
{
QStringList extractTopStyleSheetComments(QFile& stylesheet)
{
  if (!stylesheet.open(QFile::ReadOnly)) {
    log::error("failed to open stylesheet file {}", stylesheet.fileName());
    return {};
  }
  ON_BLOCK_EXIT([&stylesheet]() {
    stylesheet.close();
  });

  QStringList topComments;

  while (true) {
    const auto byteLine = stylesheet.readLine();
    if (byteLine.isNull()) {
      break;
    }

    const auto line = QString(byteLine).trimmed();

    // skip empty lines
    if (line.isEmpty()) {
      continue;
    }

    // only handle single line comments
    if (!line.startsWith("/*")) {
      break;
    }

    topComments.push_back(line.mid(2, line.size() - 4).trimmed());
  }

  return topComments;
}

QString extractBaseStyleFromStyleSheet(QFile& stylesheet, const QString& defaultStyle)
{
  // read the first line of the files that are either empty or comments
  //
  const auto topLines = extractTopStyleSheetComments(stylesheet);

  const auto factoryStyles = QStyleFactory::keys();

  QString style = defaultStyle;

  for (const auto& line : topLines) {
    if (!line.startsWith("mo2-base-style")) {
      continue;
    }

    const auto parts = line.split(":");
    if (parts.size() != 2) {
      log::warn("found invalid top-comment for mo2 in {}: {}", stylesheet.fileName(),
                line);
      continue;
    }

    const auto tmpStyle = parts[1].trimmed();
    const auto index    = factoryStyles.indexOf(tmpStyle, 0, Qt::CaseInsensitive);
    if (index == -1) {
      log::warn("base style '{}' from style '{}' not found", tmpStyle,
                stylesheet.fileName(), line);
      continue;
    }

    style = factoryStyles[index];
    log::info("found base style '{}' for style '{}'", style, stylesheet.fileName());
    break;
  }

  return style;
}

}  // namespace

void MOApplication::updateStyle(const QString& fileName)
{
  if (QStyleFactory::keys().contains(fileName)) {
    setStyleSheet("");
    setStyle(new ProxyStyle(QStyleFactory::create(fileName)));
  } else {
    QFile stylesheet(fileName);
    if (stylesheet.exists()) {
      setStyle(new ProxyStyle(QStyleFactory::create(
          extractBaseStyleFromStyleSheet(stylesheet, m_defaultStyle))));
      setStyleSheet(QString("file:///%1").arg(fileName));
    } else {
      log::warn("invalid stylesheet: {}", fileName);
    }
  }
  applyNativeTitleBarThemeToVisibleMainWindows(fileName);
}

MOSplash::MOSplash(const Settings& settings, const QString& dataPath,
                   const MOBase::IPluginGame* game)
{
  const auto splashPath = getSplashPath(settings, dataPath, game);
  if (splashPath.isEmpty()) {
    return;
  }

  QPixmap image(splashPath);
  if (image.isNull()) {
    log::error("failed to load splash from {}", splashPath);
    return;
  }

  ss_.reset(new QSplashScreen(image));
  ss_->setAttribute(Qt::WA_TranslucentBackground);
  settings.geometry().centerOnMainWindowMonitor(ss_.get());

  ss_->show();
  ss_->activateWindow();
}

void MOSplash::close()
{
  if (ss_) {
    // don't pass mainwindow as it just waits half a second for it
    // instead of proceding
    ss_->finish(nullptr);
  }
}

QString MOSplash::getSplashPath(const Settings& settings, const QString& dataPath,
                                const MOBase::IPluginGame* game) const
{
  if (!settings.useSplash()) {
    return {};
  }

  // try splash from instance directory
  const QString splashPath = dataPath + "/splash.png";
  if (QFile::exists(dataPath + "/splash.png")) {
    QImage image(splashPath);
    if (!image.isNull()) {
      return splashPath;
    }
  }

  // try splash from plugin
  QString pluginSplash = QString(":/%1/splash").arg(game->gameShortName());
  if (QFile::exists(pluginSplash)) {
    QImage image(pluginSplash);
    if (!image.isNull()) {
      image.save(splashPath);
      return pluginSplash;
    }
  }

  // try default splash from resource
  QString defaultSplash = ":/MO/gui/splash";
  if (QFile::exists(defaultSplash)) {
    QImage image(defaultSplash);
    if (!image.isNull()) {
      return defaultSplash;
    }
  }

  return splashPath;
}
