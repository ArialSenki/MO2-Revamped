#include "fluentstyle.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cmath>

#include <windows.h>
#include <dwmapi.h>

namespace Fluent
{

namespace
{

Theme g_theme;
int g_forceDark = -1;

QColor mix(const QColor& from, const QColor& to, qreal amount)
{
  const qreal t = qBound<qreal>(0.0, amount, 1.0);
  return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                          from.greenF() + (to.greenF() - from.greenF()) * t,
                          from.blueF() + (to.blueF() - from.blueF()) * t);
}

qreal luminance(const QColor& c)
{
  auto channel = [](qreal v) {
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) +
         0.0722 * channel(c.blueF());
}

qreal contrast(const QColor& a, const QColor& b)
{
  const qreal la = luminance(a);
  const qreal lb = luminance(b);
  return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

// Windows' "app mode": Settings > Personalization > Colors
bool windowsUsesDarkApps()
{
  QSettings personalize(
      QStringLiteral(
          "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
      QSettings::NativeFormat);
  return personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
}

// the accent shades Windows derives from the accent colour, lightest first:
// Light3, Light2, Light1, Accent, Dark1, Dark2, Dark3 (read only)
bool accentPalette(QColor (&shades)[7])
{
  BYTE data[32] = {};
  DWORD size    = sizeof(data);
  const LSTATUS status =
      RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                   L"AccentPalette", RRF_RT_REG_BINARY, nullptr, data, &size);
  if (status != ERROR_SUCCESS || size < 28) {
    return false;
  }
  for (int i = 0; i < 7; ++i) {
    shades[i] = QColor(data[i * 4], data[i * 4 + 1], data[i * 4 + 2]);
  }
  return true;
}

Theme makeTheme(bool dark)
{
  // values from MO2 Revamped's Fluent v4 Light and Dark themes
  Theme t;
  t.dark = dark;
  if (dark) {
    t.bg = QColor("#202226"); t.card = QColor("#2B2B2B"); t.cardLine = QColor("#363636");
    t.line = QColor("#353535"); t.hover = QColor("#2F3237"); t.pressed = QColor("#292B2F");
    t.sel = QColor("#343B44");
    t.field = QColor("#2F2F2F"); t.fieldLine = QColor("#3D3D3D"); t.fieldBottom = QColor("#9A9A9A");
    t.btn = QColor("#2D2D2D"); t.btnHover = QColor("#323232"); t.btnPressed = QColor("#272727");
    t.btnLine = QColor("#3D3D3D"); t.btnBottom = QColor("#454545"); t.disabledBg = QColor("#2A2A2A");
    t.text = QColor("#FFFFFF"); t.muted = QColor("#C5C5C5"); t.faint = QColor("#9A9A9A");
    t.caption = QColor("#9A9A9A");
    t.accent = QColor("#4CC2FF"); t.accentHover = QColor("#48B1E8"); t.accentPressed = QColor("#42A0D2");
    t.accentFg = QColor("#000000"); t.accentFgPressed = QColor("#1B3A4C");
    t.accentText = QColor("#60CDFF"); t.accentSubtle = QColor("#1F3443");
    t.warnBg = QColor("#433519"); t.warnFg = QColor("#F5D58C");
    t.critBg = QColor("#442726"); t.critFg = QColor("#FF99A4");
    t.okBg = QColor("#213A22"); t.okFg = QColor("#6CCB5F");
    t.chip = QColor("#353535"); t.chipText = QColor("#E4E4E4");
  } else {
    t.bg = QColor("#EFF1F4"); t.card = QColor("#FBFBFB"); t.cardLine = QColor("#E5E5E5");
    t.line = QColor("#EBEBEB"); t.hover = QColor("#E6E8EB"); t.pressed = QColor("#DFE1E4");
    t.sel = QColor("#E6EBF1");
    t.field = QColor("#FFFFFF"); t.fieldLine = QColor("#E5E5E5"); t.fieldBottom = QColor("#8A8A8A");
    t.btn = QColor("#FBFBFB"); t.btnHover = QColor("#F6F6F6"); t.btnPressed = QColor("#F2F2F2");
    t.btnLine = QColor("#E5E5E5"); t.btnBottom = QColor("#CCCCCC"); t.disabledBg = QColor("#F5F5F5");
    t.text = QColor("#1A1A1A"); t.muted = QColor("#5D5D5D"); t.faint = QColor("#8A8A8A");
    t.caption = QColor("#6E6E6E");
    t.accent = QColor("#005FB8"); t.accentHover = QColor("#1A6FC0"); t.accentPressed = QColor("#3B82C8");
    t.accentFg = QColor("#FFFFFF"); t.accentFgPressed = QColor("#D6E6F5");
    t.accentText = QColor("#005FB8"); t.accentSubtle = QColor("#DCE8F5");
    t.warnBg = QColor("#FFF4CE"); t.warnFg = QColor("#6B4A00");
    t.critBg = QColor("#FDE7E9"); t.critFg = QColor("#C42B1C");
    t.okBg = QColor("#DFF6DD"); t.okFg = QColor("#0F7B0F");
    t.chip = QColor("#EDEFF2"); t.chipText = QColor("#3A3A3A");
  }

  // Windows' own accent, used like WinUI does: Dark1 on light, Light2 on dark,
  // as long as the text on it stays readable (4.5:1)
  QColor shades[7];
  if (accentPalette(shades)) {
    const QColor fill  = dark ? shades[1] : shades[4];
    const QColor fg    = dark ? QColor(Qt::black) : QColor(Qt::white);
    const QColor text  = dark ? shades[0] : shades[5];
    if (contrast(fill, fg) >= 4.5 && contrast(text, t.card) >= 4.5) {
      t.accent        = fill;
      t.accentHover   = mix(fill, t.card, 0.10);
      t.accentPressed = mix(fill, t.card, 0.20);
      t.accentFg      = fg;
      t.accentFgPressed = mix(fg, fill, 0.30);
      t.accentText    = text;
      t.accentSubtle  = mix(t.card, fill, dark ? 0.22 : 0.14);
    }
  }
  t.sel = mix(t.card, t.accent, dark ? 0.16 : 0.10);
  return t;
}

QString css(const QColor& c)
{
  return c.name(QColor::HexRgb);
}

QString styleSheet(const Theme& t)
{
  QString s = QStringLiteral(R"(
QWidget { font-family: "Segoe UI Variable Text", "Segoe UI"; color: @text@; }
QMainWindow, QDialog, QMessageBox { background-color: @bg@; }
QWidget#page, QWidget#dialogContent { background-color: @card@; }
QWidget#page { background-color: @bg@; }
QLabel { background: transparent; }
QLabel#pageTitle { font-family: "Segoe UI Variable Display", "Segoe UI"; font-size: 15pt; font-weight: 600; color: @text@; }
QLabel#pageSubtitle, QLabel#dialogText { color: @muted@; }
QLabel#fieldLabel { font-weight: 600; color: @text@; }
QLabel#fieldProblem { color: @critFg@; }
QLabel#cardTitle { font-size: 10.5pt; font-weight: 600; color: @text@; }
QLabel[caption="true"] { color: @caption@; }
QLabel#registrationValue { font-weight: 600; }
QLabel#statusPill { border-radius: 10px; padding: 2px 10px; font-weight: 600; }
QLabel#statusPill[kind="ok"] { background-color: @okBg@; color: @okFg@; }
QLabel#statusPill[kind="warn"] { background-color: @warnBg@; color: @warnFg@; }
QLabel#statusPill[kind="crit"] { background-color: @critBg@; color: @critFg@; }
QLabel#resultValue { font-weight: 600; font-size: 10.5pt; }

QFrame[fluentCard="true"] { background-color: @card@; border: 1px solid @cardLine@; border-radius: 8px; }
QFrame#infoBar { border-radius: 6px; border: 1px solid @cardLine@; }
QFrame#infoBar[kind="warn"] { background-color: @warnBg@; }
QFrame#infoBar[kind="crit"] { background-color: @critBg@; }
QFrame#infoBar QLabel { color: @text@; }
QFrame#footer { background-color: @bg@; border: none; border-top: 1px solid @line@; }
QFrame#divider { background-color: @line@; border: none; max-height: 1px; min-height: 1px; }

QPushButton {
  color: @text@; background-color: @btn@;
  border: 1px solid @btnLine@; border-bottom-color: @btnBottom@;
  border-radius: 4px; padding: 5px 14px; min-height: 20px;
}
QPushButton:hover { background-color: @btnHover@; }
QPushButton:pressed { color: @muted@; background-color: @btnPressed@; border-bottom-color: @btnLine@; }
QPushButton:disabled { color: @faint@; background-color: @disabledBg@; border-color: @line@; }
QPushButton[primary="true"], QMessageBox QPushButton:default {
  color: @accentFg@; background-color: @accent@; border: 1px solid @accent@;
}
QPushButton[primary="true"]:hover, QMessageBox QPushButton:default:hover {
  background-color: @accentHover@; border-color: @accentHover@;
}
QPushButton[primary="true"]:pressed, QMessageBox QPushButton:default:pressed {
  color: @accentFgPressed@; background-color: @accentPressed@; border-color: @accentPressed@;
}
QPushButton[primary="true"]:disabled { color: @faint@; background-color: @disabledBg@; border-color: @line@; }
QMessageBox QPushButton { min-width: 80px; }

QToolButton {
  color: @text@; background-color: transparent; border: 1px solid transparent;
  border-radius: 4px; padding: 4px 8px;
}
QToolButton:hover { background-color: @hover@; }
QToolButton:pressed { background-color: @pressed@; color: @muted@; }
QToolButton:disabled { color: @faint@; }

QLineEdit, QComboBox {
  color: @text@; background-color: @field@;
  border: 1px solid @fieldLine@; border-bottom: 1px solid @fieldBottom@;
  border-radius: 4px; padding: 4px 8px; min-height: 22px;
  selection-background-color: @accent@; selection-color: @accentFg@;
}
QLineEdit:focus, QComboBox:focus { border-bottom: 2px solid @accent@; }
QLineEdit:disabled, QComboBox:disabled { color: @faint@; background-color: @disabledBg@; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView {
  background-color: @card@; border: 1px solid @cardLine@; outline: none;
  selection-background-color: @sel@; selection-color: @text@; padding: 4px;
}

QListWidget#handlerList { background-color: transparent; border: none; outline: none; }
QListWidget#gameList {
  background-color: @field@; border: 1px solid @fieldLine@; border-radius: 4px;
  outline: none; padding: 4px;
}
QListWidget#gameList::item { padding: 4px 6px; border-radius: 4px; color: @text@; }
QListWidget#gameList::item:hover { background-color: @hover@; }
QListWidget#gameList::item:selected { background-color: @sel@; color: @text@; }

QMenu { background-color: @card@; border: 1px solid @cardLine@; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 4px; color: @text@; background: transparent; }
QMenu::item:selected { background-color: @hover@; }
QMenu::item:disabled { color: @faint@; }
QMenu::separator { height: 1px; background: @line@; margin: 4px 8px; }
QMenu::icon { padding-left: 8px; }

QToolTip { color: @text@; background-color: @card@; border: 1px solid @cardLine@; padding: 4px 6px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @faint@; min-height: 30px; border-radius: 3px; margin: 0 2px; }
QScrollBar::handle:vertical:hover { background: @muted@; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @faint@; min-width: 30px; border-radius: 3px; margin: 2px 0; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");

  const std::pair<const char*, QColor> tokens[] = {
      {"bg", t.bg},           {"card", t.card},
      {"cardLine", t.cardLine}, {"line", t.line},
      {"hover", t.hover},     {"pressed", t.pressed},
      {"sel", t.sel},         {"field", t.field},
      {"fieldLine", t.fieldLine}, {"fieldBottom", t.fieldBottom},
      {"btn", t.btn},         {"btnHover", t.btnHover},
      {"btnPressed", t.btnPressed}, {"btnLine", t.btnLine},
      {"btnBottom", t.btnBottom}, {"disabledBg", t.disabledBg},
      {"text", t.text},       {"muted", t.muted},
      {"faint", t.faint},     {"caption", t.caption},
      {"accent", t.accent},   {"accentHover", t.accentHover},
      {"accentPressed", t.accentPressed}, {"accentFg", t.accentFg},
      {"accentFgPressed", t.accentFgPressed}, {"warnBg", t.warnBg},
      {"warnFg", t.warnFg},   {"critBg", t.critBg},
      {"critFg", t.critFg},   {"okBg", t.okBg},
      {"okFg", t.okFg}};
  for (const auto& [name, color] : tokens) {
    s.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'), css(color));
  }
  return s;
}

QPalette makePalette(const Theme& t)
{
  QPalette p;
  p.setColor(QPalette::Window, t.bg);
  p.setColor(QPalette::WindowText, t.text);
  p.setColor(QPalette::Base, t.field);
  p.setColor(QPalette::AlternateBase, t.card);
  p.setColor(QPalette::ToolTipBase, t.card);
  p.setColor(QPalette::ToolTipText, t.text);
  p.setColor(QPalette::PlaceholderText, t.faint);
  p.setColor(QPalette::Text, t.text);
  p.setColor(QPalette::Button, t.btn);
  p.setColor(QPalette::ButtonText, t.text);
  p.setColor(QPalette::BrightText, t.critFg);
  p.setColor(QPalette::Highlight, t.accent);
  p.setColor(QPalette::HighlightedText, t.accentFg);
  p.setColor(QPalette::Link, t.accentText);
  p.setColor(QPalette::LinkVisited, t.accentText);
  p.setColor(QPalette::Light, t.card);
  p.setColor(QPalette::Midlight, t.cardLine);
  p.setColor(QPalette::Mid, t.btnBottom);
  p.setColor(QPalette::Dark, t.faint);
  p.setColor(QPalette::Shadow, t.faint);
  p.setColor(QPalette::Accent, t.accent);
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    p.setColor(QPalette::Disabled, role, t.faint);
  }
  return p;
}

// the native title bar follows the window's light or dark mode
void matchTitleBar(QWidget* window)
{
  if (window == nullptr || !window->isWindow() || !window->testAttribute(Qt::WA_WState_Created)) {
    return;
  }
  const BOOL dark   = g_theme.dark ? TRUE : FALSE;
  const HWND handle = reinterpret_cast<HWND>(window->winId());
  // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 2004 and later)
  DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
}

class TitleBarFilter : public QObject
{
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject* object, QEvent* event) override
  {
    if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
      if (auto* widget = qobject_cast<QWidget*>(object); widget != nullptr && widget->isWindow()) {
        matchTitleBar(widget);
      }
    }
    return false;
  }
};

void apply(QApplication& app)
{
  g_theme = makeTheme(g_forceDark < 0 ? windowsUsesDarkApps() : g_forceDark == 1);
  app.setPalette(makePalette(g_theme));
  app.setStyleSheet(styleSheet(g_theme));
  for (QWidget* window : QApplication::topLevelWidgets()) {
    matchTitleBar(window);
  }
}

}  // namespace

const Theme& theme()
{
  return g_theme;
}

void install(QApplication& app, int forceDark)
{
  g_forceDark = forceDark;
  // Fusion draws every control from the palette and the style sheet, so the
  // look is the same on Windows 10 and 11
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    app.setStyle(fusion);
  }
  QFont font(QStringLiteral("Segoe UI Variable Text"));
  if (!QFontDatabase::families().contains(font.family())) {
    font.setFamily(QStringLiteral("Segoe UI"));
  }
  font.setPointSizeF(9.0);
  app.setFont(font);

  apply(app);
  app.installEventFilter(new TitleBarFilter(&app));

  // Windows switched between light and dark while the window is open
  QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
    QTimer::singleShot(0, &app, [&app] {
      apply(app);
    });
  });
}

QString iconFontFamily()
{
  static const QString family = [] {
    const QStringList families = QFontDatabase::families();
    if (families.contains(QStringLiteral("Segoe Fluent Icons"))) {
      return QStringLiteral("Segoe Fluent Icons");
    }
    return QStringLiteral("Segoe MDL2 Assets");
  }();
  return family;
}

QIcon glyphIcon(ushort codepoint, const QColor& color, int size)
{
  QIcon icon;
  for (const qreal ratio : {1.0, 1.25, 1.5, 2.0}) {
    const int px = qRound(size * ratio);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font(iconFontFamily());
    font.setPixelSize(qRound(px * 0.85));
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(codepoint)));
    painter.end();
    pixmap.setDevicePixelRatio(ratio);
    icon.addPixmap(pixmap);
  }
  return icon;
}

void makeCard(QWidget* widget)
{
  widget->setProperty("fluentCard", true);
  widget->setAttribute(Qt::WA_StyledBackground, true);
}

}  // namespace Fluent
