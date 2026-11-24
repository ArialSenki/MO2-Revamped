#include "usertheme.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <log.h>

using namespace MOBase;

namespace UserTheme
{

namespace
{

// the colors of the built-in themes the theme's colors stand for: every neutral
// grey of the built-in sheet is placed between these by its lightness, and every
// color in the accent's hue follows the accent
struct BaseColors
{
  QColor window, surface, border, muted, text, accent;
};

const BaseColors& baseColors(bool dark)
{
  static const BaseColors darkBase{QColor("#202226"), QColor("#2B2B2B"),
                                   QColor("#3D3D3D"), QColor("#C5C5C5"),
                                   QColor("#FFFFFF"), QColor("#4CC2FF")};
  static const BaseColors lightBase{QColor("#F3F3F3"), QColor("#FBFBFB"),
                                    QColor("#E5E5E5"), QColor("#5D5D5D"),
                                    QColor("#1A1A1A"), QColor("#005FB8")};
  return dark ? darkBase : lightBase;
}

// the theme in use, for recolor(); null with a built-in theme
std::optional<Theme>& current()
{
  static std::optional<Theme> theme;
  return theme;
}

struct Anchor
{
  double lightness;
  QColor to;
};

QList<Anchor> neutralAnchors(const Theme& theme)
{
  const BaseColors& base = baseColors(theme.dark);
  const auto anchor = [&](const QColor& from, const char* key) {
    return Anchor{from.lightnessF(), theme.colors.value(QString::fromLatin1(key), from)};
  };

  QList<Anchor> anchors{anchor(base.window, "window"), anchor(base.surface, "surface"),
                        anchor(base.border, "border"), anchor(base.muted, "muted"),
                        anchor(base.text, "text")};
  std::sort(anchors.begin(), anchors.end(), [](const Anchor& a, const Anchor& b) {
    return a.lightness < b.lightness;
  });
  return anchors;
}

QColor mix(const QColor& a, const QColor& b, double t)
{
  return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                          a.greenF() + (b.greenF() - a.greenF()) * t,
                          a.blueF() + (b.blueF() - a.blueF()) * t);
}

// how colorful a color is, whatever its lightness (HSL saturation climbs near
// white: Light's faintly blue #EFF1F4 has 18% of it)
double chroma(const QColor& c)
{
  return std::max({c.redF(), c.greenF(), c.blueF()}) -
         std::min({c.redF(), c.greenF(), c.blueF()});
}

// the built-in themes' greys, including their faintly blue ones (Dark's
// selected row #343B44, Light's dialog background #EFF1F4)
bool isNeutral(const QColor& c)
{
  return chroma(c) < 0.07;
}

// where a shade sits against the built-in accent, carried over to the theme's:
// lighter shades keep their share of the way to white, darker ones their share
// of the way to black (a ratio alone sends the light theme's pale tints to white)
double carry(double value, double from, double to)
{
  if (value >= from) {
    return from >= 1 ? to : to + (1 - to) * (value - from) / (1 - from);
  }
  return from <= 0 ? to : to * value / from;
}

bool inAccentHue(const QColor& c, const QColor& accent)
{
  if (chroma(c) < 0.07) {
    return false;
  }
  const int d = std::abs(c.hslHue() - accent.hslHue()) % 360;
  return std::min(d, 360 - d) <= 28;
}

// OLED Black: the status colors (red, green, yellow and their tinted rows)
// toned down, since saturated bright colors bloom on black: a fifth less
// saturation, the bright ones halfway to a ceiling and the dark tints darker
QColor oledStatusColor(const QColor& color)
{
  const double s = double(color.hslSaturationF()) * 0.8;
  double l       = color.lightnessF();
  if (l > 0.62) {
    l = 0.62 + (l - 0.62) * 0.5;
  } else if (l < 0.25) {
    l *= 0.75;
  }
  return QColor::fromHslF(std::max<double>(0.0, color.hslHueF()), std::clamp(s, 0.0, 1.0),
                          std::clamp(l, 0.0, 1.0));
}

QColor recolorWith(const Theme& theme, const QColor& color)
{
  if (!color.isValid() || theme.colors.isEmpty()) {
    return color;
  }

  const BaseColors& base = baseColors(theme.dark);
  QColor out;

  // OLED Black: Dark's faintly blue selection would turn plain grey; on black
  // it stays a low tint of the accent
  if (theme.oled && color.rgb() == QColor(0x34, 0x3B, 0x44).rgb()) {
    out = QColor(0x10, 0x21, 0x2C);
    out.setAlphaF(color.alphaF());
    return out;
  }

  if (isNeutral(color)) {
    const QList<Anchor> anchors = neutralAnchors(theme);
    const double l = color.lightnessF();
    if (l <= anchors.front().lightness) {
      out = anchors.front().to;
    } else if (l >= anchors.back().lightness) {
      out = anchors.back().to;
    } else {
      for (int i = 1; i < anchors.size(); ++i) {
        if (l <= anchors[i].lightness) {
          const double span = anchors[i].lightness - anchors[i - 1].lightness;
          const double t = span > 0 ? (l - anchors[i - 1].lightness) / span : 0.0;
          out = mix(anchors[i - 1].to, anchors[i].to, t);
          break;
        }
      }
    }
  } else if (theme.colors.contains(QStringLiteral("accent")) &&
             inAccentHue(color, base.accent)) {
    const QColor accent = theme.colors.value(QStringLiteral("accent"));
    out = QColor::fromHslF(
        accent.hslHueF() < 0 ? 0 : accent.hslHueF(),
        std::clamp(carry(color.hslSaturationF(), base.accent.hslSaturationF(),
                         accent.hslSaturationF()),
                   0.0, 1.0),
        std::clamp(carry(color.lightnessF(), base.accent.lightnessF(), accent.lightnessF()),
                   0.0,
                   // OLED Black: no shade brighter than the accent (Dark's
                   // focus lines are lighter than its accent)
                   theme.oled ? double(accent.lightnessF()) : 1.0));
  } else if (theme.oled) {
    out = oledStatusColor(color);
  } else {
    return color;
  }

  out.setAlphaF(color.alphaF());
  return out;
}

// a theme's text: UTF-8, or the Windows code page for a sheet written by hand
// in an older editor, whose accents UTF-8 would turn into replacement
// characters (and saving would write them back that way)
QString decodeText(const QByteArray& bytes)
{
  QStringDecoder utf8(QStringDecoder::Utf8);
  QString text = utf8(bytes);
  if (!utf8.hasError()) {
    return text;
  }
  QStringDecoder local(QStringDecoder::System);
  return local(bytes);
}

const QRegularExpression& headerPattern()
{
  static const QRegularExpression pattern(
      QStringLiteral("/\\*\\s*mo2r-theme\\b(.*?)\\*/"),
      QRegularExpression::DotMatchesEverythingOption |
          QRegularExpression::CaseInsensitiveOption);
  return pattern;
}

QString hex(const QColor& c)
{
  return c.name(QColor::HexRgb).toUpper();
}

// MO2 Revamped: an old MO2 theme (skyrim.qss, Paper, vs15...) has no mo2r-theme
// header, so nothing told the Fluent parts drawn on top of it (cards, switches,
// the navigation, the accent) its colors: they stayed Fluent's grey and blue on
// the theme's own. Its colors are read from its rules instead: the window and
// text from the QWidget (or QMainWindow, QDialog) rule, the surface from the
// item views and fields, the border from the borders it draws most, the muted
// text from :disabled, the accent from :hover, :focus and selections
struct SheetRule
{
  QString selectors;
  QMap<QString, QString> values;
};

QList<SheetRule> sheetRules(QString sheet)
{
  static const QRegularExpression comments(QStringLiteral("/\\*.*?\\*/"),
                                           QRegularExpression::DotMatchesEverythingOption);
  sheet.remove(comments);
  static const QRegularExpression block(QStringLiteral("([^{}]+)\\{([^{}]*)\\}"));
  QList<SheetRule> rules;
  auto it = block.globalMatch(sheet);
  while (it.hasNext()) {
    const auto m = it.next();
    SheetRule rule;
    rule.selectors = m.captured(1).simplified();
    for (const QString& declaration : m.captured(2).split(';')) {
      const int colon = declaration.indexOf(':');
      if (colon > 0) {
        rule.values.insert(declaration.left(colon).trimmed().toLower(),
                           declaration.mid(colon + 1).trimmed());
      }
    }
    rules.append(rule);
  }
  return rules;
}

// the first color in a value ("1px solid #5F5F5E", "qlineargradient(...)")
QColor colorIn(const QString& value)
{
  static const QRegularExpression color(
      QStringLiteral("#[0-9A-Fa-f]{3,8}\\b|rgba?\\([^)]*\\)|\\b(black|white|gray|grey)\\b"));
  const auto m = color.match(value);
  if (!m.hasMatch()) {
    return {};
  }
  QString text = m.captured(0);
  // Qt's rgba() takes 0-255 alpha, a sheet often 0-1: only the color counts here
  static const QRegularExpression rgba(QStringLiteral("rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)"));
  if (const auto c = rgba.match(text); c.hasMatch()) {
    return QColor(c.captured(1).toInt(), c.captured(2).toInt(), c.captured(3).toInt());
  }
  const QColor parsed(text);
  return parsed.isValid() ? QColor(parsed.rgb()) : QColor();
}

bool selectorHas(const QString& selectors, const QStringList& names)
{
  for (const QString& one : selectors.split(',')) {
    const QString first = one.trimmed().section(' ', 0, 0);
    for (const QString& name : names) {
      if (first.compare(name, Qt::CaseInsensitive) == 0 || first == QStringLiteral("*")) {
        return true;
      }
    }
  }
  return false;
}

QMap<QString, QColor> guessColors(const QString& sheet)
{
  QMap<QString, QColor> colors;
  const auto rules = sheetRules(sheet);
  const auto background = [](const SheetRule& rule) {
    const QColor c = colorIn(rule.values.value(QStringLiteral("background-color")));
    return c.isValid() ? c : colorIn(rule.values.value(QStringLiteral("background")));
  };

  for (const auto& rule : rules) {
    if (!selectorHas(rule.selectors, {QStringLiteral("QWidget"), QStringLiteral("QMainWindow"),
                                      QStringLiteral("QDialog")})) {
      continue;
    }
    if (!colors.contains(QStringLiteral("window")) && background(rule).isValid()) {
      colors.insert(QStringLiteral("window"), background(rule));
    }
    if (const QColor text = colorIn(rule.values.value(QStringLiteral("color")));
        !colors.contains(QStringLiteral("text")) && text.isValid()) {
      colors.insert(QStringLiteral("text"), text);
    }
  }

  const QStringList surfaces = {QStringLiteral("QAbstractItemView"), QStringLiteral("QTreeView"),
                                QStringLiteral("QListView"), QStringLiteral("QTableView"),
                                QStringLiteral("QLineEdit"), QStringLiteral("QTextEdit"),
                                QStringLiteral("QPlainTextEdit")};
  QMap<QRgb, int> borders, accents, fills, hues;
  for (const auto& rule : rules) {
    if (const QColor fill = background(rule);
        fill.isValid() && fill != colors.value(QStringLiteral("window"))) {
      fills[fill.rgb()] += 1;
    }
    // any color with a hue (a link, a progress bar's gradient), for a theme
    // whose states have none
    for (auto v = rule.values.cbegin(); v != rule.values.cend(); ++v) {
      static const QRegularExpression anyColor(QStringLiteral("#[0-9A-Fa-f]{6}\\b"));
      auto all = anyColor.globalMatch(v.value());
      while (all.hasNext()) {
        const QColor c(all.next().captured(0));
        if (c.hsvSaturationF() > 0.25 && c.valueF() > 0.4) {
          hues[c.rgb()] += 1;
        }
      }
    }
    if (!colors.contains(QStringLiteral("surface")) && !rule.selectors.contains(':') &&
        selectorHas(rule.selectors, surfaces) && background(rule).isValid() &&
        background(rule) != colors.value(QStringLiteral("window"))) {
      colors.insert(QStringLiteral("surface"), background(rule));
    }
    for (auto v = rule.values.cbegin(); v != rule.values.cend(); ++v) {
      if (v.key().startsWith(QStringLiteral("border")) && !v.key().contains("radius") &&
          !v.key().contains("width") && !v.key().contains("style")) {
        if (const QColor c = colorIn(v.value()); c.isValid()) {
          borders[c.rgb()] += rule.selectors.contains(':') ? 0 : 1;
        }
      }
    }
    const bool state = rule.selectors.contains(QStringLiteral(":hover")) ||
                       rule.selectors.contains(QStringLiteral(":focus")) ||
                       rule.selectors.contains(QStringLiteral(":checked")) ||
                       rule.selectors.contains(QStringLiteral(":selected"));
    for (auto v = rule.values.cbegin(); v != rule.values.cend(); ++v) {
      const bool selection = v.key() == QStringLiteral("selection-background-color");
      if ((state || selection) && (v.key().contains("color") || v.key().startsWith("border"))) {
        const QColor c = colorIn(v.value());
        // an accent has a hue; greys are borders and fills
        if (c.isValid() && c.hsvSaturationF() > 0.25 && c.valueF() > 0.3) {
          accents[c.rgb()] += 1;
        }
      }
    }
    if (rule.selectors.contains(QStringLiteral(":disabled")) &&
        !colors.contains(QStringLiteral("muted"))) {
      if (const QColor c = colorIn(rule.values.value(QStringLiteral("color"))); c.isValid()) {
        colors.insert(QStringLiteral("muted"), c);
      }
    }
  }
  const auto most = [](const QMap<QRgb, int>& counts) {
    QRgb best = 0;
    int n     = -1;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
      if (it.value() > n) {
        best = it.key();
        n    = it.value();
      }
    }
    return n < 0 ? QColor() : QColor(best);
  };
  if (const QColor b = most(borders); b.isValid()) {
    colors.insert(QStringLiteral("border"), b);
  }
  if (const QColor a = most(accents); a.isValid()) {
    colors.insert(QStringLiteral("accent"), a);
  } else if (const QColor h = most(hues); h.isValid()) {
    colors.insert(QStringLiteral("accent"), h);
  }
  // the item views' own fill, or the fill the theme uses most (hover, menus)
  if (!colors.contains(QStringLiteral("surface"))) {
    if (const QColor f = most(fills); f.isValid()) {
      colors.insert(QStringLiteral("surface"), f);
    }
  }
  // secondary text must stay readable: a disabled color is often too faint
  // for every caption the Fluent parts draw in muted
  const QColor window = colors.value(QStringLiteral("window"));
  const QColor text   = colors.value(QStringLiteral("text"));
  if (window.isValid() && text.isValid()) {
    const QColor muted = colors.value(QStringLiteral("muted"));
    const auto contrast = [](const QColor& a, const QColor& b) {
      const auto lum = [](const QColor& c) {
        const auto ch = [](double v) {
          return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
      };
      const double la = lum(a), lb = lum(b);
      return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    if (!muted.isValid() || contrast(muted, window) < 4.5) {
      colors.insert(QStringLiteral("muted"), mix(window, text, 0.62));
    }
  }
  return colors;
}

// the name in an old theme's first comment ("Skyrim", above its author)
QString commentName(const QString& head)
{
  static const QRegularExpression first(QStringLiteral("^\\s*/\\*!?(.*?)\\*/"),
                                        QRegularExpression::DotMatchesEverythingOption);
  const auto m = first.match(head);
  if (!m.hasMatch()) {
    return {};
  }
  for (QString line : m.captured(1).split('\n')) {
    static const QRegularExpression decoration(QStringLiteral("^[\\s*=!\\-]+|[\\s*=\\-]+$"));
    line.remove(decoration);
    if (!line.isEmpty()) {
      return line.contains(':') || line.size() > 40 ? QString() : line;
    }
  }
  return {};
}

// recolors every #rgb, #rrggbb, rgb() and rgba() in the sheet
QString recolorSheet(const Theme& theme, const QString& sheet)
{
  if (theme.colors.isEmpty()) {
    return sheet;
  }

  static const QRegularExpression colorPattern(
      QStringLiteral("#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\\b|"
                     "rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*"
                     "(?:,\\s*([0-9.]+%?)\\s*)?\\)"),
      QRegularExpression::CaseInsensitiveOption);

  QString out;
  out.reserve(sheet.size());
  qsizetype last = 0;
  auto it = colorPattern.globalMatch(sheet);
  while (it.hasNext()) {
    const auto m = it.next();
    out += QStringView(sheet).mid(last, m.capturedStart() - last);
    last = m.capturedEnd();

    if (!m.captured(1).isEmpty()) {
      const QColor c(QStringLiteral("#") + m.captured(1));
      out += c.isValid() ? hex(recolorWith(theme, c)) : m.captured(0);
      continue;
    }

    const QColor c(m.captured(2).toInt(), m.captured(3).toInt(), m.captured(4).toInt());
    const QColor r = recolorWith(theme, c);
    if (m.captured(5).isEmpty()) {
      out += QStringLiteral("rgb(%1, %2, %3)").arg(r.red()).arg(r.green()).arg(r.blue());
    } else {
      out += QStringLiteral("rgba(%1, %2, %3, %4)")
                 .arg(r.red())
                 .arg(r.green())
                 .arg(r.blue())
                 .arg(m.captured(5));
    }
  }
  out += QStringView(sheet).mid(last);
  return out;
}

// url(x) relative to the theme's folder, as Qt reads it from a file
QString absoluteUrls(const QString& sheet, const QString& themeDir)
{
  static const QRegularExpression urlPattern(
      QStringLiteral("url\\(\\s*([\"']?)([^\"')]+)\\1\\s*\\)"),
      QRegularExpression::CaseInsensitiveOption);

  QString out;
  qsizetype last = 0;
  auto it = urlPattern.globalMatch(sheet);
  while (it.hasNext()) {
    const auto m = it.next();
    out += QStringView(sheet).mid(last, m.capturedStart() - last);
    last = m.capturedEnd();

    const QString target = m.captured(2).trimmed();
    if (target.startsWith(':') || target.startsWith(QStringLiteral("qrc:")) ||
        target.startsWith(QStringLiteral("file:")) || QDir::isAbsolutePath(target)) {
      out += m.captured(0);
    } else {
      out += QStringLiteral("url(\"%1\")")
                 .arg(QDir::cleanPath(QDir(themeDir).filePath(target)));
    }
  }
  out += QStringView(sheet).mid(last);
  return out;
}

// the SVG images the sheet uses (check boxes, switches, radio buttons,
// chevrons), copied in the theme's colors to a folder of their own: a style
// sheet can't recolor an image. Images that can't be read or written keep the
// original
QString recolorImages(const Theme& theme, const QString& sheet)
{
  if (theme.colors.isEmpty()) {
    return sheet;
  }

  const QByteArray key =
      QCryptographicHash::hash((header(theme) + (theme.oled ? "oled" : "")).toUtf8(),
                               QCryptographicHash::Md5)
          .toHex()
          .left(12);
  const QDir cache(QDir::temp().filePath(QStringLiteral("mo2r-theme-images/") +
                                         QString::fromLatin1(key)));
  if (!cache.exists()) {
    if (!QDir().mkpath(cache.path())) {
      log::warn("can't create '{}' for the theme's images", cache.path());
      return sheet;
    }
    // one set of images at a time: the editor's preview makes a new one for
    // each change of color
    QDir root(QFileInfo(cache.path()).absolutePath());
    for (const QString& other : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (other != QString::fromLatin1(key)) {
        QDir(root.filePath(other)).removeRecursively();
      }
    }
  }

  static const QRegularExpression svgUrl(
      QStringLiteral("url\\(\"([^\"]+\\.svg)\"\\)"),
      QRegularExpression::CaseInsensitiveOption);

  QMap<QString, QString> done;
  QString out;
  qsizetype last = 0;
  auto it = svgUrl.globalMatch(sheet);
  while (it.hasNext()) {
    const auto m = it.next();
    out += QStringView(sheet).mid(last, m.capturedStart() - last);
    last = m.capturedEnd();

    const QString source = m.captured(1);
    if (!done.contains(source)) {
      done.insert(source, source);
      const QFileInfo info(source);
      QFile in(source);
      if (info.isAbsolute() && in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString target =
            cache.filePath(info.dir().dirName() + QStringLiteral("-") + info.fileName());
        const QByteArray bytes = recolorSheet(theme, QString::fromUtf8(in.readAll())).toUtf8();
        QFile outFile(target);
        if (outFile.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
            outFile.write(bytes) == bytes.size()) {
          done[source] = QDir::fromNativeSeparators(target);
        }
      }
    }
    out += QStringLiteral("url(\"%1\")").arg(done.value(source));
  }
  out += QStringView(sheet).mid(last);
  return out;
}

}  // namespace

QStringList colorKeys()
{
  return {QStringLiteral("accent"), QStringLiteral("window"), QStringLiteral("surface"),
          QStringLiteral("border"), QStringLiteral("text"),   QStringLiteral("muted")};
}

bool isBuiltIn(const QString& fileName)
{
  return fileName.compare(QStringLiteral("Light.qss"), Qt::CaseInsensitive) == 0 ||
         fileName.compare(QStringLiteral("dark.qss"), Qt::CaseInsensitive) == 0;
}

QString baseFileName(bool dark)
{
  return dark ? QStringLiteral("dark.qss") : QStringLiteral("Light.qss");
}

std::optional<Theme> load(const QString& stylesheetsDir, const QString& fileName)
{
  if (fileName.isEmpty() || isBuiltIn(fileName) ||
      !fileName.endsWith(QStringLiteral(".qss"), Qt::CaseInsensitive)) {
    return {};
  }

  QFile file(QDir(stylesheetsDir).filePath(fileName));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }

  Theme theme;
  theme.fileName = fileName;
  theme.path     = QFileInfo(file).absoluteFilePath();
  theme.name     = QFileInfo(fileName).completeBaseName();

  // the header: the first comment that says mo2r-theme
  const QString head = decodeText(file.read(16 * 1024));
  const auto header  = headerPattern().match(head);

  std::optional<bool> dark;
  if (header.hasMatch()) {
    static const QRegularExpression entry(
        QStringLiteral("^\\s*([A-Za-z]+)\\s*[:=]\\s*(.+?)\\s*;?\\s*$"),
        QRegularExpression::MultilineOption);
    auto it = entry.globalMatch(header.captured(1));
    while (it.hasNext()) {
      const auto m      = it.next();
      const QString key = m.captured(1).toLower();
      const QString val = m.captured(2).trimmed();

      if (key == QStringLiteral("name")) {
        if (!val.isEmpty()) {
          theme.name = val.left(60);
        }
      } else if (key == QStringLiteral("base")) {
        if (val.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
          dark = true;
        } else if (val.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
          dark = false;
        } else {
          log::warn("theme '{}': base must be light or dark, not '{}'", fileName, val);
        }
      } else if (colorKeys().contains(key)) {
        const QColor color(val);
        if (color.isValid()) {
          theme.colors.insert(key, QColor(color.rgb()));
        } else {
          log::warn("theme '{}': '{}' isn't a color for {}", fileName, val, key);
        }
      } else {
        log::warn("theme '{}': unknown key '{}'", fileName, key);
      }
    }
  }

  if (!header.hasMatch()) {
    // an old MO2 theme: its colors from its rules, its name from its comment
    file.seek(0);
    theme.colors = guessColors(decodeText(file.readAll()));
    if (const QString name = commentName(head); !name.isEmpty()) {
      theme.name = name.left(60);
    }
    QStringList found;
    for (auto it = theme.colors.cbegin(); it != theme.colors.cend(); ++it) {
      found << it.key() + "=" + hex(it.value());
    }
    log::debug("theme '{}' has no mo2r-theme header; colors read from its rules: {}",
               fileName, found.join(QStringLiteral(", ")));
  }

  if (dark) {
    theme.dark = *dark;
  } else if (theme.colors.contains(QStringLiteral("window"))) {
    theme.dark = theme.colors.value(QStringLiteral("window")).lightnessF() < 0.5;
  } else {
    // an old MO2 theme without a header: dark is what most of them are
    const QString id = theme.name.toCaseFolded();
    theme.dark = !(id.contains(QStringLiteral("light")) ||
                   id.contains(QStringLiteral("white")) ||
                   id.contains(QStringLiteral("paper")));
  }

  return theme;
}

QList<Theme> list(const QString& stylesheetsDir)
{
  QList<Theme> themes;
  const QStringList files =
      QDir(stylesheetsDir).entryList({QStringLiteral("*.qss")}, QDir::Files);
  for (const QString& fileName : files) {
    // the built-in themes' source names, which a development build has beside
    // their installed copies
    if (fileName.startsWith(QStringLiteral("MO2 Fluent v4 - "), Qt::CaseInsensitive) ||
        fileName.startsWith('.')) {
      continue;
    }
    if (auto theme = load(stylesheetsDir, fileName)) {
      themes.append(*theme);
    }
  }
  std::sort(themes.begin(), themes.end(), [](const Theme& a, const Theme& b) {
    return a.name.localeAwareCompare(b.name) < 0;
  });
  return themes;
}

QString composeStyleSheet(const Theme& theme, const QString& baseSheet,
                          const QString& themeSheet)
{
  // the built-in sheet sits in the theme's folder; given as text, its
  // ./mo2-fluent-v4-dark/ images would be looked for beside the executable
  const QString dir = QFileInfo(theme.path).absolutePath();
  return recolorImages(theme, absoluteUrls(recolorSheet(theme, baseSheet), dir)) +
         QStringLiteral("\n\n/* ---- %1 ---- */\n")
             .arg(theme.oled ? QStringLiteral("OLED Black") : theme.fileName) +
         // OLED Black's own rules use the built-in images, which follow it too;
         // a user theme's own images stay as the theme made them
         (theme.oled ? recolorImages(theme, absoluteUrls(themeSheet, dir))
                     : absoluteUrls(themeSheet, dir));
}

void publishColors(const Theme* theme)
{
  if (theme) {
    current() = *theme;
  } else {
    current().reset();
  }
}

Applied* Applied::instance()
{
  static Applied* applied = new Applied(QCoreApplication::instance());
  return applied;
}

void notifyApplied()
{
  emit Applied::instance()->applied();
}

QMetaObject::Connection whenApplied(QObject* context, std::function<void()> what)
{
  return QObject::connect(Applied::instance(), &Applied::applied, context, std::move(what));
}

QColor recolor(const QColor& color)
{
  const auto& theme = current();
  return theme ? recolorWith(*theme, color) : color;
}

bool active()
{
  return current().has_value();
}

Theme oledTheme(const QString& stylesheetsDir)
{
  Theme theme;
  theme.fileName = baseFileName(true);
  theme.path     = QDir(stylesheetsDir).absoluteFilePath(theme.fileName);
  theme.name     = QCoreApplication::translate("QObject", "OLED Black");
  theme.dark     = true;
  theme.oled     = true;
  // the window black, cards, lists and menus a step above it so they still
  // read as surfaces, and thin dim borders for the structure. Text off-white
  // (pure white on black glares and smears), the secondary text a grey that
  // still reads, and the accent a calmer, darker blue that doesn't bloom.
  // The status colors are toned down too (oledStatusColor())
  theme.colors = {{QStringLiteral("window"), QColor("#000000")},
                  {QStringLiteral("surface"), QColor("#0B0B0B")},
                  {QStringLiteral("border"), QColor("#262626")},
                  {QStringLiteral("muted"), QColor("#A3A3A3")},
                  {QStringLiteral("text"), QColor("#E6E6E6")},
                  {QStringLiteral("accent"), QColor("#5AA9D6")}};
  return theme;
}

QString oledRules()
{
  // in the final colors: these rules aren't recolored
  return QStringLiteral(R"(
/* ---- OLED Black: what stays on screen for hours is dim ---- */

/* Run: an accent outline on a dark tint, not a bright block */
QToolButton#startButton,
QToolButton#refinedRunButton {
    color: #9CCDE8;
    background-color: #0A1A24;
    border: 1px solid #285470;
}
QToolButton#startButton:hover,
QToolButton#refinedRunButton:hover {
    color: #B4DAEF;
    background-color: #0E2331;
    border-color: #346B8D;
}
QToolButton#startButton:pressed,
QToolButton#refinedRunButton:pressed {
    color: #86BBD9;
    background-color: #08151D;
    border-color: #285470;
}
QToolButton#startButton:disabled,
QToolButton#refinedRunButton:disabled {
    color: #6B6B6B;
    background-color: #0B0B0B;
    border-color: #1E1E1E;
}
QToolButton#startButton::menu-button,
QToolButton#refinedRunButton::menu-button {
    border-left: 1px solid #1F4459;
}
QToolButton#startButton::menu-arrow,
QToolButton#refinedRunButton::menu-arrow {
    image: url(./mo2-fluent-v4-dark/chevron-down.svg);
}
QToolButton#startButton[runState="busy"],
QToolButton#startButton[runState="busy"]:disabled {
    color: #9CCDE8;
    background-color: #0A1A24;
    border-color: #285470;
}
QToolButton#startButton[runState="running"],
QToolButton#startButton[runState="running"]:disabled {
    color: #93C68A;
    background-color: #0B190D;
    border-color: #2C5427;
}

/* a dialog's main button: a dim accent fill, an accent outline and accent
   text, so it stands out from the others without a bright block */
QPushButton:default,
QDialog QPushButton[fluentPrimary="true"],
QMessageBox QPushButton:default,
QDialog QPushButton#fixButton,
QDialog QPushButton#manageOverwriteButton,
QMainWindow#MainWindow QPushButton#displayCategoriesBtn:checked {
    color: #A8D4EC;
    background-color: #123349;
    border: 1px solid #4A8DB5;
}
QPushButton:default:hover,
QDialog QPushButton[fluentPrimary="true"]:hover,
QMessageBox QPushButton:default:hover,
QMainWindow#MainWindow QPushButton#displayCategoriesBtn:checked:hover,
QDialog QPushButton#fixButton:hover,
QDialog QPushButton#manageOverwriteButton:hover {
    color: #C0E0F2;
    background-color: #174058;
    border-color: #5AA9D6;
}
QPushButton:default:pressed,
QDialog QPushButton[fluentPrimary="true"]:pressed,
QMessageBox QPushButton:default:pressed,
QDialog QPushButton#fixButton:pressed,
QDialog QPushButton#manageOverwriteButton:pressed,
QMainWindow#MainWindow QPushButton#displayCategoriesBtn:pressed {
    color: #8EC3E1;
    background-color: #0E2A3C;
    border-color: #3D7DA3;
}
QDialog QPushButton:default:disabled,
QDialog QPushButton[fluentPrimary="true"]:disabled,
QDialog QPushButton#fixButton:disabled,
QDialog QPushButton#manageOverwriteButton:disabled {
    color: #6B6B6B;
    background-color: #0B0B0B;
    border-color: #1E1E1E;
}

/* the selected tab's line: thinner and darker */
QTabBar::tab:selected,
QWidget#centralWidget QTabWidget#tabWidget QTabBar::tab:selected,
QDialog#SettingsDialog QTabWidget#tabWidget QTabBar::tab:selected,
QDialog#ModInfoDialog QTabWidget#tabWidget QTabBar::tab:selected {
    border-bottom: 3px solid #3A7395;
}

/* switches: a dim accent track, a light knob */
QMainWindow#MainWindow ModListView#modList {
    qproperty-switchOnColor: #2F6383;
    qproperty-switchKnobOnColor: #C8E4F3;
}
FluentSwitchPainter {
    qproperty-onColor: #2F6383;
    qproperty-knobOnColor: #C8E4F3;
}

/* progress: the loading bar under the status bar stays up the longest */
QProgressBar::chunk,
QProgressBar#wizardDiskBar::chunk,
QProgressBar#statusLoadingBar::chunk,
QProgressBar#wizardProgress::chunk {
    background-color: #2F6383;
}

/* hover and selection steps that still show on near black */
QPushButton:hover {
    background-color: #1A1A1A;
}
QMenu::item:selected,
QMenu#modListOptionsMenu::item:selected,
QMenu#openFolderMenuPopup::item:selected {
    background-color: #1C1C1C;
}
QSplitter::handle:hover,
QSplitter::handle:pressed,
QWidget#centralWidget QSplitter#splitter::handle:hover,
QWidget#centralWidget QSplitter#splitter::handle:pressed,
QSplitter#categoriesSplitter::handle:hover,
QSplitter#categoriesSplitter::handle:pressed {
    background-color: #262626;
}

/* disabled settings: still readable */
QLabel#fluentSettingTitle:disabled,
QLabel#fluentSettingDescription:disabled {
    color: #6E6E6E;
}
)");
}

QColor highlight(const QColor& color)
{
  const auto& theme = current();
  if (!theme || !theme->oled || !color.isValid()) {
    return color;
  }
  QColor out = oledStatusColor(color);
  out.setAlphaF(color.alphaF());
  return out;
}

QString oledChoice()
{
  return QStringLiteral("dark.qss (OLED)");
}

QMap<QString, QColor> builtInColors(bool dark)
{
  const BaseColors& base = baseColors(dark);
  return {{QStringLiteral("accent"), base.accent},   {QStringLiteral("window"), base.window},
          {QStringLiteral("surface"), base.surface}, {QStringLiteral("border"), base.border},
          {QStringLiteral("text"), base.text},       {QStringLiteral("muted"), base.muted}};
}

QString previewFileName(int turn)
{
  return QStringLiteral(".mo2r-preview-%1.qss").arg(turn % 2 + 1);
}

QString rulesOf(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QString text      = decodeText(file.readAll());
  const auto header = headerPattern().match(text);
  if (header.hasMatch()) {
    text.remove(header.capturedStart(), header.capturedLength());
  }
  while (text.startsWith('\n')) {
    text.remove(0, 1);
  }
  return text;
}

QString header(const Theme& theme)
{
  QString out = QStringLiteral("/* mo2r-theme\n");
  // the name can't end the comment ("**//" left "*/" after one pass) nor
  // start a line of its own in it
  QString name = theme.name;
  name.replace(QChar('\r'), QChar(' ')).replace(QChar('\n'), QChar(' '));
  while (name.contains(QStringLiteral("*/"))) {
    name.remove(QStringLiteral("*/"));
  }
  out += QStringLiteral("   name: %1\n").arg(name);
  out += QStringLiteral("   base: %1\n")
             .arg(theme.dark ? QStringLiteral("dark") : QStringLiteral("light"));
  for (const QString& key : colorKeys()) {
    if (theme.colors.contains(key)) {
      out += QStringLiteral("   %1: %2\n").arg(key, hex(theme.colors.value(key)));
    }
  }
  out += QStringLiteral("*/\n");
  return out;
}

QString newFileName(const QString& stylesheetsDir, const QString& name)
{
  QString stem;
  for (const QChar c : name.trimmed()) {
    stem += (c.isLetterOrNumber() || c == ' ' || c == '-' || c == '_') ? c : QChar('_');
  }
  stem = stem.simplified().left(50);
  if (stem.isEmpty()) {
    stem = QStringLiteral("My theme");
  }

  const QDir dir(stylesheetsDir);
  QString candidate = stem + QStringLiteral(".qss");
  for (int i = 2;
       dir.exists(candidate) || isBuiltIn(candidate) ||
       candidate.startsWith(QStringLiteral("MO2 Fluent v4 - "), Qt::CaseInsensitive);
       ++i) {
    candidate = QStringLiteral("%1 %2.qss").arg(stem).arg(i);
  }
  return candidate;
}

bool write(const QString& path, const Theme& theme, const QString& rules, QString* error)
{
  // written beside it and then put in its place: a full disk or a blocked
  // file leaves the theme as it was, not empty
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  QString text = header(theme);
  if (!rules.trimmed().isEmpty()) {
    text += QStringLiteral("\n") + rules;
  }
  const QByteArray bytes = text.toUtf8();
  if (file.write(bytes) != bytes.size()) {
    if (error) {
      *error = file.errorString();
    }
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  return true;
}

}  // namespace UserTheme
