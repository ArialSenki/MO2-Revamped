#include "quickui.h"

#include "fluentmotion.h"
#include "settings.h"
#include "usertheme.h"

#include <log.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontDatabase>
#include <QHash>
#include <QJSEngine>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <memory>
#include <QToolTip>
#include <QUrl>

using namespace MOBase;

namespace QuickUi
{

namespace
{

constexpr char SettingKey[] = "use_quick_interface";

// the icons the views show, by QIcon::cacheKey()
QHash<QString, QIcon>& icons()
{
  static QHash<QString, QIcon> map;
  return map;
}

class IconProvider : public QQuickImageProvider
{
public:
  IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

  QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested) override
  {
    const QIcon icon = icons().value(id.section(QLatin1Char('?'), 0, 0));
    const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(32, 32);
    QPixmap pixmap   = icon.isNull() ? QPixmap() : icon.pixmap(want);
    if (pixmap.isNull()) {
      pixmap = QPixmap(1, 1);
      pixmap.fill(Qt::transparent);
    }
    if (size != nullptr) {
      *size = pixmap.size();
    }
    return pixmap;
  }
};

// one engine for the views that are open: the QML types and the compiled files
// load once for them. It goes when the last view does, so the memory of the
// dialogs comes back while MO2 runs (the startup Instances window, then the
// game), and is made again for the next view. The main window's summary row
// keeps it for as long as that window is open
QPointer<QQmlEngine>& engineHolder()
{
  static QPointer<QQmlEngine> engine;
  return engine;
}

int& openViews()
{
  static int count = 0;
  return count;
}

// the event loop has ended: an engine still in use goes with its last view,
// at once, as nothing runs a deleteLater() any more
bool& quitting()
{
  static bool done = false;
  return done;
}

QQmlEngine* sharedEngine()
{
  auto& engine = engineHolder();
  if (!engine) {
    if (QCoreApplication::closingDown()) {
      return nullptr;
    }
    engine = new QQmlEngine;
    engine->addImageProvider(QStringLiteral("mo2"), new IconProvider);
    // gone before the application is: the engine first, then what it read
    static const bool once = [] {
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
        quitting() = true;
        // the main window (and its summary row) is closed after the loop
        // ends: its items must not outlive the engine they run on
        if (openViews() == 0) {
          delete engineHolder().data();
          icons().clear();
        }
      });
      return true;
    }();
    Q_UNUSED(once);
  }
  return engine;
}

void viewGone()
{
  if (--openViews() == 0) {
    // out of the holder first: a view made before the deletion runs (the
    // theme question right after the startup Instances window) gets a new one
    if (QQmlEngine* engine = engineHolder().data()) {
      engineHolder().clear();
      if (quitting()) {
        delete engine;
      } else {
        engine->deleteLater();
      }
    }
    icons().clear();
  }
}

// follows the theme: Settings applies a new style sheet to the running windows
class ThemeFollower : public QObject
{
public:
  using QObject::QObject;

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
      Fluent::instance()->refresh();
      // a view that shows the window behind it (such as the main window's
      // summary row) keeps its clear color transparent
      if (auto* view = qobject_cast<QQuickWidget*>(watched);
          view != nullptr && !view->property("quickSeeThrough").toBool()) {
        view->setClearColor(Fluent::instance()->window());
      }
    }
    return false;
  }
};

// what Qt Quick draws with in this run (see initialize())
QSGRendererInterface::GraphicsApi viewApi()
{
  static const QSGRendererInterface::GraphicsApi api = [] {
    const QByteArray wanted = qgetenv("MO2R_QUICK_BACKEND").toLower();
    if (wanted == "d3d11") {
      return QSGRendererInterface::Direct3D11;
    } else if (wanted == "opengl") {
      return QSGRendererInterface::OpenGL;
    } else if (wanted == "default") {
      return QSGRendererInterface::Unknown;
    }
    return QSGRendererInterface::Software;
  }();
  return api;
}

QString apiName(QSGRendererInterface::GraphicsApi api)
{
  switch (api) {
  case QSGRendererInterface::Software:
    return QStringLiteral("software");
  case QSGRendererInterface::Direct3D11:
    return QStringLiteral("d3d11");
  case QSGRendererInterface::Direct3D12:
    return QStringLiteral("d3d12");
  case QSGRendererInterface::OpenGL:
    return QStringLiteral("opengl");
  case QSGRendererInterface::Vulkan:
    return QStringLiteral("vulkan");
  case QSGRendererInterface::Unknown:
    return QStringLiteral("default");
  default:
    return QString::number(static_cast<int>(api));
  }
}

QColor faint(QColor color, int alpha)
{
  color.setAlpha(alpha);
  return color;
}

}  // namespace

void initialize()
{
  if (!enabled()) {
    return;
  }
  // Qt keeps the first choice for the whole run, for every Qt Quick user (the
  // web views too), so it is made here, before any of them
  const auto api = viewApi();
  if (api != QSGRendererInterface::Unknown) {
    QQuickWindow::setGraphicsApi(api);
  }
  log::debug("qt quick: on, drawing with {}", apiName(api));
}

bool enabled()
{
  static const bool on = [] {
    const QByteArray forced = qgetenv("MO2R_QUICK");
    if (forced == "1") {
      return true;
    }
    if (forced == "0") {
      return false;
    }
    return stored();
  }();
  return on;
}

bool stored()
{
  return GlobalSettings::value(QString::fromLatin1(SettingKey), false).toBool();
}

void setStored(bool on)
{
  GlobalSettings::setValue(QString::fromLatin1(SettingKey), on);
}

QQuickWidget* createView(QWidget* parent, const QString& name, const QVariantMap& properties)
{
  if (!enabled()) {
    return nullptr;
  }

  QElapsedTimer timer;
  timer.start();

  // the same text as the widgets: Windows' own rendering, not Qt Quick's
  // distance fields, which look soft at 9 pt
  static const bool textRendering = [] {
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    return true;
  }();
  Q_UNUSED(textRendering);

  // the font of the window the view goes in, as its style sheet sets it
  if (parent != nullptr) {
    parent->ensurePolished();
  }
  auto* fluent = Fluent::instance();
  fluent->refresh();

  QQmlEngine* engine = sharedEngine();
  if (engine == nullptr) {
    return nullptr;
  }
  auto* view = new QQuickWidget(engine, parent);
  ++openViews();
  QObject::connect(view, &QObject::destroyed, [] {
    viewGone();
  });
  view->setObjectName(name + QStringLiteral("QuickView"));
  view->setResizeMode(QQuickWidget::SizeRootObjectToView);
  view->setClearColor(fluent->window());
  view->setFocusPolicy(Qt::StrongFocus);
  view->installEventFilter(new ThemeFollower(view));
  view->setInitialProperties(properties);
  view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/MO2Revamped/qml/%1.qml").arg(name)));

  if (view->status() != QQuickWidget::Ready || view->rootObject() == nullptr) {
    for (const QQmlError& error : view->errors()) {
      log::error("qt quick: {}: {}", name, error.toString());
    }
    log::warn("qt quick: {} didn't load, the Widgets version is used", name);
    delete view;
    return nullptr;
  }

  // which one it really draws with, once
  auto* window  = view->quickWindow();
  auto logged   = std::make_shared<QMetaObject::Connection>();
  *logged = QObject::connect(window, &QQuickWindow::afterRendering, view, [window, name, logged] {
    QObject::disconnect(*logged);
    const auto* ri = window->rendererInterface();
    log::debug("qt quick: {} draws with {}", name,
               apiName(ri != nullptr ? ri->graphicsApi() : QSGRendererInterface::Unknown));
  });

  log::debug("qt quick: {} made in {} ms", name, timer.elapsed());
  return view;
}

QString iconUrl(const QIcon& icon, const QString& key)
{
  if (icon.isNull()) {
    return {};
  }
  icons().insert(key, icon);
  // the icon's own key in the URL: a new icon is a new image to the view, the
  // same one stays cached
  return QStringLiteral("image://mo2/%1?%2").arg(key).arg(icon.cacheKey());
}

Fluent::Fluent(QObject* parent) : QObject(parent)
{
  m_iconFamily = QFontDatabase::hasFamily(QStringLiteral("Segoe Fluent Icons"))
                     ? QStringLiteral("Segoe Fluent Icons")
                     : QStringLiteral("Segoe MDL2 Assets");
  refresh();
}

Fluent* Fluent::instance()
{
  static Fluent* fluent = new Fluent(qApp);
  return fluent;
}

Fluent* Fluent::create(QQmlEngine*, QJSEngine*)
{
  auto* fluent = instance();
  // shared by every engine and view: QML must not delete it
  QJSEngine::setObjectOwnership(fluent, QJSEngine::CppOwnership);
  return fluent;
}

void Fluent::refresh()
{
  const bool dark = qApp != nullptr && qApp->property("revampedDark").toBool();
  const bool oled = qApp != nullptr && qApp->property("revampedOled").toBool();

  // the built-in style sheets' colors (MO2 Fluent v4 - Light/Dark.qss), through
  // the user theme or OLED Black like everything code-painted
  auto c = [dark](const char* light, const char* darkColor) {
    return UserTheme::recolor(QColor(QString::fromLatin1(dark ? darkColor : light)));
  };

  m_window              = c("#F5F5F5", "#242424");
  m_surface             = c("#FBFBFB", "#2B2B2B");
  m_surfaceBorder       = c("#E5E5E5", "#363636");
  m_divider             = c("#EBEBEB", "#353535");
  m_text                = c("#1A1A1A", "#FFFFFF");
  m_muted               = c("#5D5D5D", "#C5C5C5");
  m_disabledText        = c("#8A8A8A", "#9A9A9A");
  m_accent              = c("#005FB8", "#60CDFF");
  m_critical            = c("#C42B1C", "#FF99A4");
  m_caution             = c("#9D5D00", "#FCE100");
  m_success             = c("#0F7B0F", "#6CCB5F");
  m_accentFill          = c("#005FB8", "#4CC2FF");
  m_accentFillHover     = c("#1A6FC0", "#48B1E8");
  m_accentFillPressed   = c("#3B82C8", "#42A0D2");
  m_onAccent            = dark ? QColor("#000000") : QColor("#FFFFFF");
  m_control             = c("#FBFBFB", "#2D2D2D");
  m_controlHover        = c("#F6F6F6", "#323232");
  m_controlPressed      = c("#F2F2F2", "#272727");
  m_controlDisabled     = c("#F5F5F5", "#2A2A2A");
  m_controlBorder       = c("#E5E5E5", "#3D3D3D");
  m_controlBorderBottom = c("#CCCCCC", "#454545");
  m_input               = c("#FFFFFF", "#2D2D2D");
  // QLabel#fluentStatBadge, #fluentStatCaption and QPushButton#fluentStatAction
  m_tintAccent    = c("#DCE8F5", "#1F3443");
  m_tintCaution   = c("#FFF4CE", "#433519");
  m_tintCritical  = c("#FDE7E9", "#442726");
  m_tintNeutral   = c("#E5E5E5", "#3D3D3D");
  m_onTintCaution = c("#6B4A00", "#F5D58C");
  m_caption       = c("#6E6E6E", "#9A9A9A");
  m_chip          = c("#FFFFFF", "#2F2F2F");
  m_chipHover     = c("#F9F9F9", "#343434");
  m_subtleHover    = faint(m_text, 15);
  m_subtleSelected = faint(m_text, 26);
  m_subtlePressed  = faint(m_text, 10);

  // as the Fluent v4 layer: QWidget { font-family: "Segoe UI Variable Text", "Segoe UI" }
  static const QString family =
      QFontDatabase::hasFamily(QStringLiteral("Segoe UI Variable Text"))
          ? QStringLiteral("Segoe UI Variable Text")
          : QStringLiteral("Segoe UI");
  m_fontFamily = family;
  m_fontSize   = 9;
  m_dark       = dark;
  m_oled       = oled;
  m_animated   = FluentMotion::enabled();
  emit changed();
}

void Fluent::showToolTip(const QString& text, qreal globalX, qreal globalY)
{
  if (text.isEmpty()) {
    QToolTip::hideText();
    return;
  }
  QToolTip::showText(QPoint(qRound(globalX), qRound(globalY)), text);
}

void Fluent::hideToolTip()
{
  QToolTip::hideText();
}

QString Fluent::glyph(int codePoint) const
{
  return QString(QChar(static_cast<char16_t>(codePoint)));
}

}  // namespace QuickUi
