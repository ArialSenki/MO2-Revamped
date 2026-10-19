#include "fluenticons.h"
#include "revampedtheme.h"

#include <QAbstractButton>
#include <algorithm>
#include <optional>
#include <QAction>
#include <QApplication>
#include <QCache>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QToolBar>

using namespace MOBase;

namespace
{
using FluentIcons::Tone;

constexpr int Sizes[] = {16, 20, 24, 32};

// icon names of the glyph engine start with this, so a glyph icon can be told
// apart from others
const QString GlyphPrefix = QStringLiteral("fluent:");

// the original icon of an action or button, kept so a theme change redraws
// from the source rather than from an already recolored copy
const char* const OriginalProperty = "fluentOriginalIcon";
const char* const InkProperty      = "fluentIconInk";
const char* const ShownProperty    = "fluentIconShown";

struct Glyph
{
  char16_t codepoint;
  Tone tone;
};

// every icon of resources.qrc, by path, and the glyph that stands for it
const QHash<QString, Glyph>& glyphTable()
{
  static const QHash<QString, Glyph> table = [] {
    QHash<QString, Glyph> t;

    auto add = [&t](const char* prefix, std::initializer_list<
                                            std::tuple<const char*, char16_t, Tone>>
                                            list) {
      for (const auto& [name, codepoint, tone] : list) {
        t.insert(QString::fromLatin1(prefix) + QString::fromLatin1(name),
                 Glyph{codepoint, tone});
      }
    };

    add(":/MO/gui/", {
                         {"save", 0xE74E, Tone::Ink},
                         {"word-wrap", 0xE8E4, Tone::Ink},
                         {"add", 0xE710, Tone::Ink},
                         {"remove", 0xE711, Tone::Ink},
                         {"installer", 0xE896, Tone::Ink},
                         {"profiles", 0xE77B, Tone::Ink},
                         {"settings", 0xE713, Tone::Ink},
                         {"executable", 0xECAA, Tone::Ink},
                         {"information", 0xE946, Tone::Accent},
                         {"locked", 0xE72E, Tone::Ink},
                         {"next", 0xE72A, Tone::Ink},
                         {"previous", 0xE72B, Tone::Ink},
                         {"refresh", 0xE72C, Tone::Ink},
                         {"update_available", 0xE896, Tone::Accent},
                         {"important", 0xE8C9, Tone::Caution},
                         {"check", 0xE73E, Tone::Success},
                         {"warning", 0xE7BA, Tone::Caution},
                         {"emblem_backup", 0xEA35, Tone::Muted},
                         {"icon_tools", 0xE90F, Tone::Ink},
                         {"problem", 0xE783, Tone::Critical},
                         {"debug", 0xEBE8, Tone::Muted},
                         {"update", 0xE895, Tone::Ink},
                         {"help", 0xE897, Tone::Ink},
                         {"icon_executable", 0xECAA, Tone::Ink},
                         {"plugins", 0xEA86, Tone::Ink},
                         {"edit_clear", 0xE894, Tone::Ink},
                         {"link", 0xE71B, Tone::Ink},
                         {"icon_favorite", 0xE735, Tone::Caution},
                         {"emblem_notendorsed", 0xEB51, Tone::Muted},
                         {"emblem_conflict", 0xE783, Tone::Critical},
                         {"run", 0xE768, Tone::Ink},
                         {"emblem_conflict_mixed", 0xE8AB, Tone::Caution},
                         {"emblem_conflict_overwrite", 0xE74A, Tone::Success},
                         {"emblem_conflict_overwritten", 0xE74B, Tone::Critical},
                         {"emblem_conflict_redundant", 0xE711, Tone::Critical},
                         {"archive_loose_conflict_mixed", 0xE8AB, Tone::Caution},
                         {"archive_loose_conflict_overwrite", 0xE74A, Tone::Success},
                         {"archive_loose_conflict_overwritten", 0xE74B, Tone::Critical},
                         {"emblem_hidden_files", 0xED1A, Tone::Muted},
                         {"emblem_notes", 0xE70B, Tone::Ink},
                         {"version_date", 0xE787, Tone::Muted},
                         {"warning_16", 0xE7BA, Tone::Caution},
                         {"attachment", 0xE723, Tone::Ink},
                         {"backup", 0xEA35, Tone::Ink},
                         {"restore", 0xE777, Tone::Ink},
                         {"sort", 0xE8CB, Tone::Ink},
                         {"active", 0xE73E, Tone::Success},
                         {"awaiting", 0xE823, Tone::Muted},
                         {"inactive", 0xE711, Tone::Muted},
                         {"package", 0xE7B8, Tone::Ink},
                         {"instance_switch", 0xE8AB, Tone::Ink},
                         {"open_folder", 0xE838, Tone::Ink},
                         {"folder", 0xE8B7, Tone::Ink},
                         {"multiply_red", 0xE711, Tone::Critical},
                         {"archive_conflict_loser", 0xF012, Tone::Critical},
                         {"archive_conflict_mixed", 0xF012, Tone::Caution},
                         {"archive_conflict_neutral", 0xF012, Tone::Muted},
                         {"archive_conflict_winner", 0xF012, Tone::Success},
                         {"alternate_game_alt", 0xE7FC, Tone::Caution},
                         {"alternate_game", 0xE7FC, Tone::Caution},
                         {"tracked", 0xE7B3, Tone::Accent},
                     });

    add(":/MO/gui/contextmenu/", {
                                     {"all-mods.svg", 0xE8A9, Tone::Ink},
                                     {"categories.svg", 0xE8EC, Tone::Ink},
                                     {"primary-category.svg", 0xE734, Tone::Ink},
                                     {"updates.svg", 0xE896, Tone::Ink},
                                     {"update-check.svg", 0xE895, Tone::Ink},
                                     {"enable.svg", 0xE73E, Tone::Ink},
                                     {"disable.svg", 0xE711, Tone::Ink},
                                     {"rename.svg", 0xE8AC, Tone::Ink},
                                     {"reinstall.svg", 0xE777, Tone::Ink},
                                     {"remove.svg", 0xE74D, Tone::Ink},
                                     {"backup.svg", 0xEA35, Tone::Ink},
                                     {"endorse.svg", 0xE8E1, Tone::Ink},
                                     {"wont-endorse.svg", 0xE8E0, Tone::Ink},
                                     {"remap-category.svg", 0xE8AB, Tone::Ink},
                                     {"tracking.svg", 0xE7B3, Tone::Ink},
                                     {"visit.svg", 0xE8A7, Tone::Ink},
                                     {"explorer.svg", 0xE838, Tone::Ink},
                                     {"information.svg", 0xE946, Tone::Ink},
                                     {"create-mod.svg", 0xE8F4, Tone::Ink},
                                     {"separator.svg", 0xE76F, Tone::Ink},
                                     {"collapse.svg", 0xF165, Tone::Ink},
                                     {"expand.svg", 0xF164, Tone::Ink},
                                     {"export.svg", 0xEDE1, Tone::Ink},
                                     {"visibility-hide.svg", 0xED1A, Tone::Ink},
                                     {"visibility-show.svg", 0xE890, Tone::Ink},
                                     {"unlock.svg", 0xE785, Tone::Ink},
                                 });

    add(":/MO/gui/mainwindow/", {
                                    {"install.svg", 0xE896, Tone::Ink},
                                    {"profile.svg", 0xE77B, Tone::Ink},
                                    {"executables.svg", 0xE71D, Tone::Ink},
                                    {"tools.svg", 0xE90F, Tone::Ink},
                                    {"github-updates.svg", 0xE895, Tone::Ink},
                                    {"settings.svg", 0xE713, Tone::Ink},
                                    {"mod-page.svg", 0xE774, Tone::Ink},
                                    {"notifications.svg", 0xEA8F, Tone::Ink},
                                    {"help.svg", 0xE897, Tone::Ink},
                                    {"instance.svg", 0xE8AB, Tone::Ink},
                                    {"run.svg", 0xE768, Tone::Ink},
                                    {"explore-virtual-folder.svg", 0xE8DA, Tone::Ink},
                                    {"refresh.svg", 0xE72C, Tone::Ink},
                                    {"restore.svg", 0xE777, Tone::Ink},
                                    {"sort.svg", 0xE8CB, Tone::Ink},
                                    {"clear.svg", 0xE894, Tone::Ink},
                                    {"filter.svg", 0xE71C, Tone::Ink},
                                    {"shortcut-add.svg", 0xE71B, Tone::Ink},
                                    {"shortcut-remove.svg", 0xE74D, Tone::Ink},
                                    {"action-add.svg", 0xE710, Tone::Ink},
                                    {"action-add-light.svg", 0xE710, Tone::Ink},
                                    {"action-down.svg", 0xE74B, Tone::Ink},
                                    {"action-reset.svg", 0xE7A7, Tone::Ink},
                                    {"action-switch.svg", 0xE8AB, Tone::Ink},
                                    {"action-up.svg", 0xE74A, Tone::Ink},
                                });

    add(":/MO/gui/mainwindow/status/",
        {
            {"warning.svg", 0xE7BA, Tone::Caution},
            {"error.svg", 0xE783, Tone::Critical},
            {"info.svg", 0xE946, Tone::Accent},
            {"about-revamped.svg", 0xE946, Tone::Ink},
            {"debug.svg", 0xEBE8, Tone::Muted},
            {"update.svg", 0xE896, Tone::Accent},
            {"version.svg", 0xE81C, Tone::Muted},
            {"backup.svg", 0xEA35, Tone::Muted},
            {"locked.svg", 0xE72E, Tone::Ink},
            {"attachment.svg", 0xE723, Tone::Ink},
            {"notes.svg", 0xE70B, Tone::Ink},
            {"hidden-files.svg", 0xED1A, Tone::Muted},
            {"not-endorsed.svg", 0xEB51, Tone::Muted},
            {"alternate-game.svg", 0xE7FC, Tone::Caution},
            {"tracked.svg", 0xE7B3, Tone::Accent},
            {"awaiting.svg", 0xE823, Tone::Muted},
            {"plugin-flag.svg", 0xEA86, Tone::Ink},
            {"archive-neutral.svg", 0xF012, Tone::Muted},
            {"no-records.svg", 0xE9CE, Tone::Muted},
            {"cleanup.svg", 0xE75C, Tone::Ink},
            {"conflict-mixed.svg", 0xE8AB, Tone::Caution},
            {"conflict-overwrite.svg", 0xE74A, Tone::Success},
            {"conflict-overwritten.svg", 0xE74B, Tone::Critical},
            {"conflict-redundant.svg", 0xE711, Tone::Critical},
            {"loose-overwrite.svg", 0xE74A, Tone::Success},
            {"loose-overwritten.svg", 0xE74B, Tone::Critical},
            {"loose-mixed.svg", 0xE8AB, Tone::Caution},
            {"archive-mixed.svg", 0xF012, Tone::Caution},
            {"archive-winner.svg", 0xF012, Tone::Success},
            {"archive-loser.svg", 0xF012, Tone::Critical},
        });

    add(":/MO/gui/mainwindow/files/", {
                                          {"folder.svg", 0xE8B7, Tone::Ink},
                                          {"file.svg", 0xE7C3, Tone::Ink},
                                          {"executable.svg", 0xECAA, Tone::Ink},
                                          {"library.svg", 0xE943, Tone::Ink},
                                          {"game-data.svg", 0xE7B8, Tone::Ink},
                                          {"archive.svg", 0xF012, Tone::Ink},
                                          {"config.svg", 0xE713, Tone::Ink},
                                          {"script.svg", 0xE943, Tone::Ink},
                                          {"image.svg", 0xEB9F, Tone::Ink},
                                          {"save.svg", 0xE74E, Tone::Ink},
                                          {"text.svg", 0xE8A5, Tone::Ink},
                                      });

    add(":/MO/gui/content/", {
                                 {"plugin", 0xEA86, Tone::Ink},
                                 {"skyproc", 0xE945, Tone::Ink},
                                 {"texture", 0xEB9F, Tone::Ink},
                                 {"music", 0xEC4F, Tone::Ink},
                                 {"sound", 0xE8D6, Tone::Ink},
                                 {"interface", 0xE7F4, Tone::Ink},
                                 {"skse", 0xE943, Tone::Ink},
                                 {"script", 0xE943, Tone::Ink},
                                 {"mesh", 0xF158, Tone::Ink},
                                 {"string", 0xE8BD, Tone::Ink},
                                 {"bsa", 0xF012, Tone::Ink},
                                 {"menu", 0xE8FD, Tone::Ink},
                                 {"inifile", 0xE713, Tone::Ink},
                                 {"modgroup", 0xE8F1, Tone::Ink},
                                 {"facegen", 0xE77B, Tone::Ink},
                                 {"material", 0xE790, Tone::Ink},
                                 {"media", 0xE714, Tone::Ink},
                                 {"geometries", 0xF158, Tone::Ink},
                             });

    return t;
  }();

  return table;
}

// paints one glyph; the color is looked up on every paint so the icon follows
// the theme, and the pixmaps are cached per size, color and scale
class GlyphEngine : public QIconEngine
{
public:
  GlyphEngine(char16_t codepoint, Tone tone) : m_codepoint(codepoint), m_tone(tone) {}

  void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
             QIcon::State state) override
  {
    const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap pm  = scaledPixmap(rect.size(), mode, state, scale);
    const QSize logical =
        (QSizeF(pm.size()) / pm.devicePixelRatio()).toSize();
    QRect target(QPoint(0, 0), logical);
    target.moveCenter(rect.center());
    painter->drawPixmap(target, pm);
  }

  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
  {
    return scaledPixmap(size, mode, state, 1.0);
  }

  QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State,
                       qreal scale) override
  {
    const int extent = qMax(1, qMin(size.width(), size.height()));
    QColor ink       = FluentIcons::color(m_tone);
    if (mode == QIcon::Disabled) {
      ink.setAlphaF(ink.alphaF() * 0.4);
    }

    const int pixels  = qMax(1, qRound(extent * scale));
    const QString key = QStringLiteral("fluentglyph-%1-%2-%3")
                            .arg(int(m_codepoint), 0, 16)
                            .arg(pixels)
                            .arg(ink.rgba(), 0, 16);

    QPixmap pm;
    if (!QPixmapCache::find(key, &pm)) {
      pm = QPixmap(pixels, pixels);
      pm.fill(Qt::transparent);

      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      p.setRenderHint(QPainter::TextAntialiasing);
      p.setFont(FluentIcons::font(pixels));
      p.setPen(ink);
      p.drawText(QRect(0, 0, pixels, pixels), Qt::AlignCenter,
                 QString(QChar(m_codepoint)));
      p.end();

      QPixmapCache::insert(key, pm);
    }

    pm.setDevicePixelRatio(scale);
    return pm;
  }

  QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override
  {
    const int extent = qMin(size.width(), size.height());
    return {extent, extent};
  }

  QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override
  {
    QList<QSize> sizes;
    for (int s : Sizes) {
      sizes.append(QSize(s, s));
    }
    return sizes;
  }

  QString iconName() override
  {
    return GlyphPrefix + QString::number(int(m_codepoint), 16);
  }

  bool isNull() override { return false; }

  QString key() const override { return QStringLiteral("fluentglyph"); }

  QIconEngine* clone() const override { return new GlyphEngine(m_codepoint, m_tone); }

private:
  char16_t m_codepoint;
  Tone m_tone;
};

// fingerprint of an icon's small pixmap, used to recognize icons that were
// created from a resource elsewhere (a .ui file, a plugin)
size_t fingerprint(const QIcon& icon)
{
  const QImage image =
      icon.pixmap(QSize(16, 16)).toImage().convertToFormat(QImage::Format_ARGB32);
  if (image.isNull()) {
    return 0;
  }
  return qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes()),
                   static_cast<size_t>(image.width()));
}

// glyphs by the fingerprint of the resource they replace, computed on first
// use
const QHash<size_t, Glyph>& fingerprintTable()
{
  static const QHash<size_t, Glyph> table = [] {
    QHash<size_t, Glyph> t;
    const auto& glyphs = glyphTable();
    for (auto it = glyphs.begin(); it != glyphs.end(); ++it) {
      const size_t f = fingerprint(QIcon(it.key()));
      if (f != 0) {
        t.insert(f, it.value());
      }
    }
    return t;
  }();
  return table;
}

QPixmap recolor(const QPixmap& source, const QColor& ink)
{
  QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const QRgb px   = line[x];
      const int alpha = qAlpha(px);
      if (alpha == 0) {
        continue;
      }
      // darker source pixels carry more ink; the pale tints the drawings use
      // as fills (almost white) nearly vanish
      const double luminance =
          (0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px)) / 255.0;
      const double strength = std::clamp((1.0 - luminance) / 0.55, 0.0, 1.0);
      line[x] = qRgba(ink.red(), ink.green(), ink.blue(),
                      static_cast<int>(alpha * strength));
    }
  }
  QPixmap result = QPixmap::fromImage(image);
  result.setDevicePixelRatio(source.devicePixelRatio());
  return result;
}

QString cacheKey(const QIcon& icon, const QColor& ink)
{
  return QString::number(icon.cacheKey()) + QLatin1Char('/') + ink.name(QColor::HexArgb);
}
}  // namespace

namespace FluentIcons
{

bool active()
{
  return qApp != nullptr && qApp->property("revampedFluent").toBool();
}

QColor color(Tone tone)
{
  const bool black = RevampedTheme::isBlack();
  const bool dark  = RevampedTheme::isDark();

  switch (tone) {
  case Tone::Ink:
    return black ? QColor(0xED, 0xED, 0xED)
                 : (dark ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x1A, 0x1A, 0x1A));
  case Tone::Muted:
    return black ? QColor(0xBD, 0xBD, 0xBD)
                 : (dark ? QColor(0xC5, 0xC5, 0xC5) : QColor(0x5D, 0x5D, 0x5D));
  case Tone::Accent:
    return black ? QColor(0xE6, 0xE6, 0xE6)
                 : (dark ? QColor(0x60, 0xCD, 0xFF) : QColor(0x00, 0x5F, 0xB8));
  case Tone::Success:
    return black ? RevampedTheme::successText()
                 : (dark ? QColor(0x6C, 0xCB, 0x5F) : QColor(0x0F, 0x7B, 0x0F));
  case Tone::Caution:
    return black ? RevampedTheme::cautionText()
                 : (dark ? QColor(0xFC, 0xE1, 0x00) : QColor(0x9D, 0x5D, 0x00));
  case Tone::Critical:
    return black ? RevampedTheme::criticalText()
                 : (dark ? QColor(0xFF, 0x99, 0xA4) : QColor(0xC4, 0x2B, 0x1C));
  }

  return {};
}

QFont font(int pixelSize)
{
  QFont f;
  f.setFamilies({QStringLiteral("Segoe Fluent Icons"),
                 QStringLiteral("Segoe MDL2 Assets")});
  f.setPixelSize(qMax(1, pixelSize));
  f.setHintingPreference(QFont::PreferNoHinting);
  return f;
}

QIcon glyph(char16_t codepoint, Tone tone)
{
  return QIcon(new GlyphEngine(codepoint, tone));
}

QIcon icon(const QString& resource)
{
  // glyph icons pick their colors when painted, so one per resource serves
  // every theme; item views ask for these on every row
  static QHash<QString, QIcon> glyphs;
  static QHash<QString, QIcon> originals;

  if (active()) {
    auto cached = glyphs.constFind(resource);
    if (cached != glyphs.constEnd()) {
      return *cached;
    }

    const auto& table = glyphTable();
    auto it           = table.find(resource);
    if (it != table.end()) {
      return *glyphs.insert(resource, glyph(it->codepoint, it->tone));
    }
  }

  auto original = originals.constFind(resource);
  if (original != originals.constEnd()) {
    return *original;
  }
  return *originals.insert(resource, QIcon(resource));
}

bool isGlyph(const QIcon& icon)
{
  return !icon.isNull() && icon.name().startsWith(GlyphPrefix);
}

QIcon fluent(const QIcon& icon)
{
  if (!active() || icon.isNull() || isGlyph(icon)) {
    return icon;
  }

  // the same QIcon is often set on many actions; remember the answer per icon
  static QHash<qint64, std::optional<Glyph>> known;
  auto it = known.find(icon.cacheKey());
  if (it == known.end()) {
    const auto& table = fingerprintTable();
    auto match        = table.find(fingerprint(icon));
    it = known.insert(icon.cacheKey(), match == table.end()
                                           ? std::nullopt
                                           : std::optional<Glyph>(*match));
  }

  if (!it->has_value()) {
    return icon;
  }
  return glyph((*it)->codepoint, (*it)->tone);
}

QIcon monochrome(const QIcon& icon, const QColor& ink)
{
  if (icon.isNull()) {
    return icon;
  }

  static QCache<QString, QIcon> cache(400);
  const QString key = cacheKey(icon, ink);
  if (const QIcon* cached = cache.object(key)) {
    return *cached;
  }

  QIcon result;
  for (int size : Sizes) {
    for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
      const QPixmap pixmap = icon.pixmap(QSize(size, size), QIcon::Normal);
      if (pixmap.isNull()) {
        continue;
      }
      QColor color = ink;
      if (mode == QIcon::Disabled) {
        color.setAlphaF(color.alphaF() * 0.4);
      }
      result.addPixmap(recolor(pixmap, color), mode);
    }
  }
  cache.insert(key, new QIcon(result));
  return result;
}

// gives an action's or a button's icon the Fluent look: the glyph of a known
// resource, or a monochrome copy of anything else. When code or the style
// sheet has put a different icon there since, that one becomes the new
// original.
template <class T>
void restyle(T* object, const QColor& ink)
{
  const QIcon current = object->icon();
  if (current.isNull() || isGlyph(current)) {
    return;
  }

  const bool ours = current.cacheKey() == object->property(ShownProperty).toLongLong();
  if (ours && object->property(InkProperty).template value<QColor>() == ink) {
    return;
  }

  const QIcon original =
      ours ? object->property(OriginalProperty).template value<QIcon>() : current;

  const QIcon glyphIcon = fluent(original);
  if (isGlyph(glyphIcon)) {
    object->setIcon(glyphIcon);
    return;
  }

  const QIcon result = monochrome(original, ink);
  object->setProperty(OriginalProperty, QVariant::fromValue(original));
  object->setIcon(result);
  object->setProperty(ShownProperty, result.cacheKey());
  object->setProperty(InkProperty, ink);
}

void apply(QMenu* menu)
{
  if (menu == nullptr) {
    return;
  }

  const QColor ink = menu->palette().color(QPalette::Active, QPalette::Text);
  for (QAction* action : menu->actions()) {
    restyle(action, ink);
  }
}

void apply(QAbstractButton* button)
{
  if (button == nullptr || button->property("fluentKeepIcon").toBool() ||
      qobject_cast<QToolBar*>(button->parentWidget()) != nullptr) {
    return;
  }

  restyle(button, button->palette().color(QPalette::Active, QPalette::ButtonText));
}

}  // namespace FluentIcons
