#include "fluentstatusdelegate.h"

#include "modinfo.h"
#include "modlist.h"
#include "revampedtheme.h"

#include <QApplication>
#include <QPainter>

namespace
{
enum class Tone
{
  Accent,
  Success,
  Caution,
  Critical
};

// which side of its conflicts a mod is on, in sort order
enum class Side
{
  None,
  Wins,
  Mixed,
  Loses,
  Redundant
};

struct Status
{
  QString text;
  QString compactText;  // used when two pills share the cell
  Tone tone;
};

ModInfo::Ptr modFor(const QModelIndex& index)
{
  bool ok            = false;
  const int modIndex = index.data(ModList::IndexRole).toInt(&ok);
  if (!ok || modIndex < 0 || static_cast<unsigned int>(modIndex) >= ModInfo::getNumMods()) {
    return {};
  }

  ModInfo::Ptr mod = ModInfo::getByIndex(modIndex);
  if (mod->isSeparator() || mod->isOverwrite() || mod->isForeign()) {
    return {};
  }
  return mod;
}

Side sideOf(const ModInfo::Ptr& mod)
{
  bool wins  = false;
  bool loses = false;
  for (const auto flag : mod->getConflictFlags()) {
    switch (flag) {
    case ModInfo::FLAG_CONFLICT_REDUNDANT:
      return Side::Redundant;
    case ModInfo::FLAG_CONFLICT_OVERWRITE:
    case ModInfo::FLAG_ARCHIVE_LOOSE_CONFLICT_OVERWRITE:
    case ModInfo::FLAG_ARCHIVE_CONFLICT_OVERWRITE:
      wins = true;
      break;
    case ModInfo::FLAG_CONFLICT_OVERWRITTEN:
    case ModInfo::FLAG_ARCHIVE_LOOSE_CONFLICT_OVERWRITTEN:
    case ModInfo::FLAG_ARCHIVE_CONFLICT_OVERWRITTEN:
      loses = true;
      break;
    case ModInfo::FLAG_CONFLICT_MIXED:
    case ModInfo::FLAG_ARCHIVE_CONFLICT_MIXED:
      wins  = true;
      loses = true;
      break;
    default:
      break;
    }
  }
  if (wins && loses) {
    return Side::Mixed;
  }
  return wins ? Side::Wins : (loses ? Side::Loses : Side::None);
}

std::vector<Status> statusesFor(const QModelIndex& index)
{
  std::vector<Status> result;
  ModInfo::Ptr mod = modFor(index);
  if (!mod) {
    return result;
  }

  if (mod->updateAvailable()) {
    result.push_back(
        {QObject::tr("New version"), QObject::tr("New version"), Tone::Accent});
  }

  const Side side = sideOf(mod);
  if (side == Side::Redundant) {
    result.push_back({QObject::tr("Fully overwritten"), QObject::tr("Fully overwritten"),
                      Tone::Critical});
    return result;
  }

  // counted once per conflict check, not on every paint
  const std::size_t others = mod->getConflictingModCount();
  if (others == 0 && side == Side::None) {
    return result;
  }

  // arrows: up = this mod wins, down = it is overwritten, up-down = both
  QString glyph;
  Tone tone = Tone::Caution;
  switch (side) {
  case Side::Wins:
    glyph = QString(QChar(0x2191));
    tone  = Tone::Success;
    break;
  case Side::Loses:
    glyph = QString(QChar(0x2193));
    break;
  case Side::Mixed:
    glyph = QString(QChar(0x2195));
    break;
  default:
    break;
  }

  QString text = others == 1 ? QObject::tr("1 conflict")
                             : QObject::tr("%1 conflicts").arg(others);
  QString compact = QString::number(others);
  if (!glyph.isEmpty()) {
    text    = glyph + QChar(' ') + text;
    compact = glyph + QChar(' ') + compact;
  }
  result.push_back({text, compact, tone});
  return result;
}

// label colors per theme, matching the stylesheet's status tokens
std::pair<QColor, QColor> colorsFor(Tone tone)
{
  using namespace MOBase;
  const bool dark = RevampedTheme::isDark();

  switch (tone) {
  case Tone::Accent:
    return dark ? std::pair{QColor("#1F3443"), QColor("#60CDFF")}
                : std::pair{QColor("#DCE8F5"), QColor("#005FB8")};
  case Tone::Success:
    return dark ? std::pair{QColor("#2A3B22"), QColor("#9FD89F")}
                : std::pair{QColor("#DFF6DD"), QColor("#0E700E")};
  case Tone::Caution:
    return dark ? std::pair{QColor("#433519"), QColor("#F5D58C")}
                : std::pair{QColor("#FFF4CE"), QColor("#6B4A00")};
  case Tone::Critical:
  default:
    return dark ? std::pair{QColor("#442726"), QColor("#FF99A4")}
                : std::pair{QColor("#FDE7E9"), QColor("#C42B1C")};
  }
}
}  // namespace

void FluentStatusDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
  // the row background, hover and selection come from the style as usual
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  opt.text.clear();
  opt.icon = QIcon();
  const QWidget* widget = option.widget;
  QStyle* style         = widget ? widget->style() : QApplication::style();
  style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

  const auto statuses = statusesFor(index);
  if (statuses.empty()) {
    return;
  }

  QFont font = option.font;
  font.setPointSizeF(std::max(7.0, font.pointSizeF() - 0.5));
  const QFontMetrics metrics(font);
  const int height = std::min(option.rect.height() - 6, metrics.height() + 6);
  const int top    = option.rect.top() + (option.rect.height() - height) / 2;
  const int right  = option.rect.right() - 6;

  // full texts when every pill fits, the short conflict text otherwise
  auto widthOf = [&](const QString& text) {
    return metrics.horizontalAdvance(text) + 16;
  };
  int fullWidth = 0;
  for (const auto& status : statuses) {
    fullWidth += widthOf(status.text) + 4;
  }
  const bool compact = statuses.size() > 1 && option.rect.left() + 6 + fullWidth > right;

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setFont(font);
  int left = option.rect.left() + 6;
  for (const auto& status : statuses) {
    const QString& text = compact ? status.compactText : status.text;
    const int width     = std::min(right - left, widthOf(text));
    if (width < 24) {
      break;
    }
    const QRect pill(left, top, width, height);
    const auto [background, foreground] = colorsFor(status.tone);
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRoundedRect(pill, 4, 4);
    painter->setPen(foreground);
    painter->drawText(pill.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(text, Qt::ElideRight, pill.width() - 16));
    left += width + 4;
  }
  painter->restore();
}

quint64 FluentStatusDelegate::sortKey(const ModInfo::Ptr& mod)
{
  if (!mod || mod->isSeparator() || mod->isOverwrite() || mod->isForeign()) {
    return 0;
  }
  const quint64 update = mod->updateAvailable() ? 1 : 0;
  const Side side      = sideOf(mod);
  const quint64 count =
      side == Side::Redundant
          ? 0
          : std::min<quint64>(mod->getConflictingModCount(), 0xFFFFFFFFull);
  return (update << 40) | (static_cast<quint64>(side) << 32) | count;
}

QSize FluentStatusDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
  QSize size = QStyledItemDelegate::sizeHint(option, index);
  size.setWidth(std::max(size.width(), 140));
  return size;
}
