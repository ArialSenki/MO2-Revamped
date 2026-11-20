#ifndef MO2R_QUICKUI_H
#define MO2R_QUICKUI_H

// MO2 Revamped: Qt Quick pieces inside the Widgets interface.
//
// Some windows have a second version written in QML (qml/*.qml, compiled into
// the executable by qmlcachegen). They are used when Settings > Appearance >
// "Use the new interface" is on; the setting is read once, when MO2 starts, so
// a window never changes version while it is open. With it off, or when a view
// can't be made (no graphics, a QML error), the Widgets version is used.
//
// A Quick view draws itself; what it shows and what it does stay in the C++ of
// the window (see the bridges, such as InstancesQuickBridge), so both versions
// behave the same.

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;
class QQuickWidget;
class QWidget;

namespace QuickUi
{

// whether the Qt Quick versions are used in this run: the setting as it was when
// MO2 started. MO2R_QUICK=1 or 0 in the environment overrides it (to measure)
bool enabled();

// called once the application exists, before any window: with Qt Quick on,
// chooses what Qt Quick draws with in this run. Software by default: these
// windows are small and mostly still, and a GPU device cost about 110 MB while
// one was open (D3D11), with a game running beside MO2. Qt keeps that choice
// for every Qt Quick user, so the web views draw in software too (measured
// fine, and lighter). MO2R_QUICK_BACKEND=d3d11, opengl or default chooses
// another, to measure
void initialize();

// the setting itself, as Settings shows and changes it (applied on restart)
bool stored();
void setStored(bool on);

// makes the Qt Quick view of qrc:/qt/qml/MO2Revamped/<name>.qml in parent; the
// properties are given to the view's root item before it is completed (its
// `required property`s). Null when Qt Quick is off or the view didn't load, so
// the caller uses its Widgets version. The time it took goes to the log
QQuickWidget* createView(QWidget* parent, const QString& name,
                         const QVariantMap& properties);

// a URL a QML Image shows the icon with ("image://mo2/<key>?<version>"); the
// icon is kept under the key (one per thing shown, such as an instance), so a
// new icon for it replaces the old one
QString iconUrl(const QIcon& icon, const QString& key);

// the theme and motion of the Widgets interface, for the views: the colors of
// Light and Dark as the style sheets draw them, through the user theme or OLED
// Black's colors (UserTheme::recolor) like every code-painted widget, and the
// durations of FluentMotion (0 with Animations off). In QML: `Fluent.text`
class Fluent : public QObject
{
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(bool dark READ dark NOTIFY changed)
  Q_PROPERTY(bool oled READ oled NOTIFY changed)

  // surfaces
  Q_PROPERTY(QColor window READ window NOTIFY changed)
  Q_PROPERTY(QColor surface READ surface NOTIFY changed)
  Q_PROPERTY(QColor surfaceBorder READ surfaceBorder NOTIFY changed)
  Q_PROPERTY(QColor divider READ divider NOTIFY changed)
  // text
  Q_PROPERTY(QColor text READ text NOTIFY changed)
  Q_PROPERTY(QColor muted READ muted NOTIFY changed)
  Q_PROPERTY(QColor disabledText READ disabledText NOTIFY changed)
  Q_PROPERTY(QColor accent READ accent NOTIFY changed)
  Q_PROPERTY(QColor critical READ critical NOTIFY changed)
  Q_PROPERTY(QColor caution READ caution NOTIFY changed)
  Q_PROPERTY(QColor success READ success NOTIFY changed)
  // a primary button
  Q_PROPERTY(QColor accentFill READ accentFill NOTIFY changed)
  Q_PROPERTY(QColor accentFillHover READ accentFillHover NOTIFY changed)
  Q_PROPERTY(QColor accentFillPressed READ accentFillPressed NOTIFY changed)
  Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)
  // a standard button and a text box
  Q_PROPERTY(QColor control READ control NOTIFY changed)
  Q_PROPERTY(QColor controlHover READ controlHover NOTIFY changed)
  Q_PROPERTY(QColor controlPressed READ controlPressed NOTIFY changed)
  Q_PROPERTY(QColor controlDisabled READ controlDisabled NOTIFY changed)
  Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY changed)
  Q_PROPERTY(QColor controlBorderBottom READ controlBorderBottom NOTIFY changed)
  Q_PROPERTY(QColor input READ input NOTIFY changed)
  // the fills of a hovered and a selected row (the text color, faint)
  Q_PROPERTY(QColor subtleHover READ subtleHover NOTIFY changed)
  Q_PROPERTY(QColor subtleSelected READ subtleSelected NOTIFY changed)
  Q_PROPERTY(QColor subtlePressed READ subtlePressed NOTIFY changed)

  // fonts: the interface's, at the size the style sheet gives it, and the
  // Segoe Fluent Icons glyphs
  Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
  Q_PROPERTY(qreal fontSize READ fontSize NOTIFY changed)
  Q_PROPERTY(QString iconFamily READ iconFamily CONSTANT)

  // motion: whether things move, and the Windows 11 durations (0 when off)
  Q_PROPERTY(bool animated READ animated NOTIFY changed)
  Q_PROPERTY(int fast READ fast NOTIFY changed)
  Q_PROPERTY(int normal READ normal NOTIFY changed)
  Q_PROPERTY(int slow READ slow NOTIFY changed)

public:
  explicit Fluent(QObject* parent = nullptr);

  // the one instance, shared by every view
  static Fluent* instance();
  static Fluent* create(QQmlEngine*, QJSEngine*);

  // reads the theme, the font and the animation level again
  void refresh();

  bool dark() const { return m_dark; }
  bool oled() const { return m_oled; }
  QColor window() const { return m_window; }
  QColor surface() const { return m_surface; }
  QColor surfaceBorder() const { return m_surfaceBorder; }
  QColor divider() const { return m_divider; }
  QColor text() const { return m_text; }
  QColor muted() const { return m_muted; }
  QColor disabledText() const { return m_disabledText; }
  QColor accent() const { return m_accent; }
  QColor critical() const { return m_critical; }
  QColor caution() const { return m_caution; }
  QColor success() const { return m_success; }
  QColor accentFill() const { return m_accentFill; }
  QColor accentFillHover() const { return m_accentFillHover; }
  QColor accentFillPressed() const { return m_accentFillPressed; }
  QColor onAccent() const { return m_onAccent; }
  QColor control() const { return m_control; }
  QColor controlHover() const { return m_controlHover; }
  QColor controlPressed() const { return m_controlPressed; }
  QColor controlDisabled() const { return m_controlDisabled; }
  QColor controlBorder() const { return m_controlBorder; }
  QColor controlBorderBottom() const { return m_controlBorderBottom; }
  QColor input() const { return m_input; }
  QColor subtleHover() const { return m_subtleHover; }
  QColor subtleSelected() const { return m_subtleSelected; }
  QColor subtlePressed() const { return m_subtlePressed; }
  QString fontFamily() const { return m_fontFamily; }
  qreal fontSize() const { return m_fontSize; }
  QString iconFamily() const { return m_iconFamily; }
  bool animated() const { return m_animated; }
  int fast() const { return m_animated ? 167 : 0; }
  int normal() const { return m_animated ? 250 : 0; }
  int slow() const { return m_animated ? 333 : 0; }

  // the Widgets tool tip (in the theme's style) below a QML item, or none
  Q_INVOKABLE void showToolTip(const QString& text, qreal globalX, qreal globalY);
  Q_INVOKABLE void hideToolTip();

  // a glyph code point as the one-character string a Text shows
  Q_INVOKABLE QString glyph(int codePoint) const;

signals:
  void changed();

private:
  bool m_dark = false, m_oled = false, m_animated = true;
  QColor m_window, m_surface, m_surfaceBorder, m_divider, m_text, m_muted, m_disabledText,
      m_accent, m_critical, m_caution, m_success, m_accentFill, m_accentFillHover,
      m_accentFillPressed, m_onAccent, m_control, m_controlHover, m_controlPressed,
      m_controlDisabled, m_controlBorder, m_controlBorderBottom, m_input, m_subtleHover,
      m_subtleSelected, m_subtlePressed;
  QString m_fontFamily, m_iconFamily;
  qreal m_fontSize = 9;
};

}  // namespace QuickUi

#endif  // MO2R_QUICKUI_H
