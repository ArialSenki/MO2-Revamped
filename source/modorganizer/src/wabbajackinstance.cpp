#include "wabbajackinstance.h"

#include "fluentdialogs.h"
#include "organizercore.h"
#include "settings.h"

#include <iplugingame.h>
#include <log.h>

#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QVBoxLayout>

using namespace MOBase;

namespace Wabbajack
{

namespace
{

struct Text
{
  Q_DECLARE_TR_FUNCTIONS(WabbajackInstance)
};

constexpr auto ListKey        = "Wabbajack/list";
constexpr auto GameVersionKey = "Wabbajack/game_version";
constexpr auto ReadmeKey      = "Wabbajack/readme";
constexpr auto ManualKey      = "Wabbajack/manual_folders";
constexpr auto NoticeKey      = "Wabbajack/notice_pending";

InstanceInfo readInfo(const QSettings& ini)
{
  InstanceInfo info;
  info.list          = ini.value(ListKey).toString();
  info.gameVersion   = ini.value(GameVersionKey).toString();
  info.readme        = ini.value(ReadmeKey).toString();
  info.manualFolders = ini.value(ManualKey).toStringList();
  return info;
}

// 1.7.0.3 and 1.7.0.30 differ, 1.7 and 1.7.0.0 don't
QList<int> numbers(const QString& version)
{
  QList<int> result;
  for (const QString& part : version.trimmed().split(u'.')) {
    bool ok       = false;
    const int n   = part.trimmed().toInt(&ok);
    if (!ok) {
      return {};
    }
    result.append(n);
  }
  while (!result.isEmpty() && result.back() == 0) {
    result.removeLast();
  }
  return result;
}

bool sameVersion(const QString& a, const QString& b)
{
  const QList<int> x = numbers(a);
  const QList<int> y = numbers(b);
  // a version that can't be read isn't a difference worth a question
  return x.isEmpty() || y.isEmpty() || x == y;
}

bool inside(const QFileInfo& binary, const QDir& folder)
{
  const QString dir  = QDir::cleanPath(binary.absolutePath());
  const QString base = QDir::cleanPath(folder.absolutePath());
  return dir.compare(base, Qt::CaseInsensitive) == 0 ||
         dir.startsWith(base + u'/', Qt::CaseInsensitive);
}

QLabel* richLabel(const QString& html, QWidget* parent)
{
  auto* label = new QLabel(html, parent);
  label->setTextFormat(Qt::RichText);
  label->setWordWrap(true);
  label->setOpenExternalLinks(true);
  label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  return label;
}

void fit(QDialog& dialog, int width)
{
  const int height =
      dialog.layout() != nullptr ? dialog.layout()->totalHeightForWidth(width) : -1;
  dialog.resize(width, height > 0 ? height : dialog.sizeHint().height());
}

}  // namespace

void writeInstanceInfo(const QString& iniPath, const InstanceInfo& info)
{
  QSettings ini(iniPath, QSettings::IniFormat);
  ini.setValue(ListKey, info.list);
  ini.setValue(GameVersionKey, info.gameVersion);
  ini.setValue(ReadmeKey, info.readme);
  ini.setValue(ManualKey, info.manualFolders);
  ini.setValue(NoticeKey, true);
  ini.sync();
}

void showListNotice(QWidget* parent, OrganizerCore& core)
{
  const QString file = core.settings().filename();
  QSettings ini(file, QSettings::IniFormat);
  if (!ini.value(NoticeKey, false).toBool()) {
    return;
  }
  // shown once, whatever is answered
  ini.setValue(NoticeKey, false);
  ini.sync();

  const InstanceInfo info = readInfo(ini);
  const IPluginGame* game = core.managedGame();
  const QString have      = game != nullptr ? game->gameVersion() : QString();
  const QString base      = core.settings().paths().base();

  QStringList items;
  if (!info.manualFolders.isEmpty()) {
    QStringList folders;
    for (const QString& folder : info.manualFolders) {
      folders << QStringLiteral("<b>%1</b>").arg(
          QDir::toNativeSeparators(QDir(base).filePath(folder)).toHtmlEscaped());
    }
    items << Text::tr("The list leaves files for you to install by hand, as its guide "
                         "says (usually into the game's folder): %1. The game may not start, "
                         "or the list may not work, until they are in place.")
                 .arg(folders.join(QStringLiteral(", ")));
  }
  if (!info.gameVersion.isEmpty() && !have.isEmpty()) {
    if (sameVersion(info.gameVersion, have)) {
      items << Text::tr("The list was made for version %1 of the game, the version you "
                           "have.")
                   .arg(info.gameVersion.toHtmlEscaped());
    } else {
      items << Text::tr("The list was made for version <b>%1</b> of the game, and you have "
                           "<b>%2</b>. Its script extender and some mods may refuse to start "
                           "until the game is that version; the list's guide says how to get "
                           "it.")
                   .arg(info.gameVersion.toHtmlEscaped(), have.toHtmlEscaped());
    }
  }
  if (items.isEmpty()) {
    return;
  }
  if (!info.readme.isEmpty()) {
    items << Text::tr("The guide: <a href=\"%1\">%2</a>.")
                 .arg(info.readme.toHtmlEscaped(), info.readme.toHtmlEscaped());
  }

  const QString name = info.list.isEmpty() ? Text::tr("The list") : info.list;
  FluentContentDialog dialog(parent, QStringLiteral("WabbajackListNoticeDialog"),
                             Text::tr("Before you start %1").arg(name),
                             Text::tr("What the list's guide still asks of you:"));
  for (const QString& item : items) {
    dialog.body()->addWidget(richLabel(item, &dialog));
  }
  auto* ok = dialog.addButton(Text::tr("OK"), true);
  QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
  fit(dialog, 560);
  dialog.exec();
}

bool confirmGameVersion(QWidget* parent, OrganizerCore& core, const QFileInfo& binary)
{
  const IPluginGame* game = core.managedGame();
  if (game == nullptr || !inside(binary, game->gameDirectory())) {
    return true;
  }
  const QString file = core.settings().filename();
  const QSettings ini(file, QSettings::IniFormat);
  const InstanceInfo info = readInfo(ini);
  const QString have      = game->gameVersion();
  if (info.gameVersion.isEmpty() || have.isEmpty() || sameVersion(info.gameVersion, have)) {
    return true;
  }

  // asked once per session and version: a game started anyway isn't asked again
  static QSet<QString> asked;
  const QString key = file.toLower() + u'|' + have;
  if (asked.contains(key)) {
    return true;
  }
  log::warn("wabbajack: {} was made for game version {}, the game is {}", info.list,
            info.gameVersion, have);

  const QString name = info.list.isEmpty() ? Text::tr("The list") : info.list;
  FluentContentDialog dialog(
      parent, QStringLiteral("WabbajackGameVersionDialog"),
      Text::tr("The game isn't the version %1 was made for").arg(name),
      Text::tr("%1 was made for version %2 of the game, and the game is now %3 (a game "
                  "update changes it). Its script extender and some of its mods may refuse "
                  "to start, or the game may close at once. The list's guide says how to get "
                  "version %2.")
          .arg(name, info.gameVersion, have));
  if (!info.readme.isEmpty()) {
    dialog.body()->addWidget(richLabel(
        Text::tr("The guide: <a href=\"%1\">%2</a>.")
            .arg(info.readme.toHtmlEscaped(), info.readme.toHtmlEscaped()),
        &dialog));
  }
  auto* start  = dialog.addButton(Text::tr("Start anyway"), true);
  auto* cancel = dialog.addButton(Text::tr("Cancel"));
  QObject::connect(start, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.askFirst(cancel);
  fit(dialog, 520);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  asked.insert(key);
  return true;
}

}  // namespace Wabbajack
