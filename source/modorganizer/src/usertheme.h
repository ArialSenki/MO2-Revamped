#ifndef MO2R_USERTHEME_H
#define MO2R_USERTHEME_H

#include <QColor>
#include <QList>
#include <QMap>
#include <QString>
#include <optional>

// MO2 Revamped: themes of your own. A .qss in the stylesheets folder other than
// Light.qss and dark.qss is a user theme: it loads on top of one of them, so its
// rules win wherever the interface uses the style sheet, and a comment at its
// top can give the colors the code-painted parts use (switches, cards, the
// navigation pane, glyphs). See docs/THEMES.md.
//
//   /* mo2r-theme
//      name: Ember
//      base: dark
//      accent: #E0A84F
//      window: #1C1814
//      surface: #26211B
//      border: #4A3F30
//      text: #F1E6D2
//      muted: #B8A88C
//   */
namespace UserTheme
{

// the color keys a theme can set, in the order the docs list them
QStringList colorKeys();

struct Theme
{
  QString fileName;     // as in the stylesheets folder, e.g. "Ember.qss"
  QString path;         // absolute path of the theme
  QString name;         // shown in Settings
  bool dark = true;     // which built-in theme it loads on top of
  QMap<QString, QColor> colors;  // only the keys the theme sets
};

// Light.qss or dark.qss, whatever the case
bool isBuiltIn(const QString& fileName);

// the built-in theme a user theme loads on top of
QString baseFileName(bool dark);

// reads a theme's header; nothing for a built-in theme or a missing file
std::optional<Theme> load(const QString& stylesheetsDir, const QString& fileName);

// the user themes in the folder, sorted by name
QList<Theme> list(const QString& stylesheetsDir);

// the whole style sheet for a user theme: the built-in one in the theme's colors
// (see recolor), then the theme's own rules, which win at the same specificity
// (their relative url()s made absolute, since the sheet no longer loads from
// the theme's file)
QString composeStyleSheet(const Theme& theme, const QString& baseSheet,
                          const QString& themeSheet);

// the theme the code-painted widgets follow: null for a built-in theme
void publishColors(const Theme* theme);

// a color of the built-in themes (Light or Dark, whichever the user theme loads
// on top of) as the user theme shows it: greys move to the theme's window,
// surface, border, muted and text colors, the accent's hue to its accent, and
// everything else (status colors, user colors) stays. The color itself with a
// built-in theme
QColor recolor(const QColor& color);

// a user theme is in use
bool active();

}  // namespace UserTheme

#endif  // MO2R_USERTHEME_H
