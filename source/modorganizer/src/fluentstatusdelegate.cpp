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
  Caution,
  Critical
};

struct Status
{
  QString text;
  Tone tone;
};

std::optional<Status> statusFor(const QModelIndex& index)
{
  bool ok           = false;
  const int modIndex = index.data(ModList::IndexRole).toInt(&ok);
  if (!ok || modIndex < 0 || static_cast<unsigned int>(modIndex) >= ModInfo::getNumMods()) {
    return {};
  }

  ModInfo::Ptr mod = ModInfo::getByIndex(modIndex);
  if (mod->isSeparator() || mod->isOverwrite() || mod->isForeign()) {
    return {};
  }

  if (mod->updateAvailable()) {
    return Status{QObject::tr("New version"), Tone::Accent};
  }

  const auto flags = mod->getConflictFlags();
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_CONFLICT_REDUNDANT) !=
      flags.end()) {
    return Status{QObject::tr("Fully overwritten"), Tone::Critical};
  }

  // counted once per conflict check, not on every paint
  const std::size_t others = mod->getConflictingModCount();
  if (others == 0) {
    return {};
  }

  return Status{others == 1 ? QObject::tr("1 conflict")
                            : QObject::tr("%1 conflicts").arg(others),
                Tone::Caution};
}

// label colors per theme, matching the stylesheet's status tokens
std::pair<QColor, QColor> colorsFor(Tone tone)
{
  using namespace MOBase;
  const bool dark  = RevampedTheme::isDark();
  const bool black = RevampedTheme::isBlack();

  switch (tone) {
  case Tone::Accent:
    if (black) {
      return {QColor("#333333"), QColor("#E6E6E6")};
    }
    return dark ? std::pair{QColor("#1F3443"), QColor("#60CDFF")}
                : std::pair{QColor("#DCE8F5"), QColor("#005FB8")};
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

  const auto status = statusFor(index);
  if (!status) {
    return;
  }

  QFont font = option.font;
  font.setPointSizeF(std::max(7.0, font.pointSizeF() - 0.5));
  const QFontMetrics metrics(font);
  const int height = std::min(option.rect.height() - 6, metrics.height() + 6);
  const int width  = std::min(option.rect.width() - 12,
                              metrics.horizontalAdvance(status->text) + 16);
  const QRect pill(option.rect.left() + 6,
                   option.rect.top() + (option.rect.height() - height) / 2, width, height);

  const auto [background, foreground] = colorsFor(status->tone);
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setPen(Qt::NoPen);
  painter->setBrush(background);
  painter->drawRoundedRect(pill, 4, 4);
  painter->setFont(font);
  painter->setPen(foreground);
  painter->drawText(pill.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    metrics.elidedText(status->text, Qt::ElideRight, pill.width() - 16));
  painter->restore();
}

QSize FluentStatusDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
  QSize size = QStyledItemDelegate::sizeHint(option, index);
  size.setWidth(std::max(size.width(), 140));
  return size;
}
