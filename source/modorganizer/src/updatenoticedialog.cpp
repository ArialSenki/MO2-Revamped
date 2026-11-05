#include "updatenoticedialog.h"
#include "fluentsettings.h"
#include "revampedversion.h"
#include "settings.h"

#include <log.h>
#include <utility.h>

#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

using namespace MOBase;

// "v1.1.0-beta.4" is shown as "1.1.0-beta.4"
static QString shownVersion(const QString& tag)
{
  if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
    return tag.mid(1);
  }
  return tag;
}

QString UpdateNoticeDialog::nexusPageUrl()
{
  return QStringLiteral("https://www.nexusmods.com/eldenring/mods/%1")
      .arg(MO2R_NEXUS_MOD_ID);
}

UpdateNoticeDialog::UpdateNoticeDialog(
    QWidget* parent, Settings& settings, const QString& currentVersion,
    const std::vector<SelfUpdater::ReleaseInfo>& releases)
    : FluentContentDialog(parent, QStringLiteral("updateNoticeDialog"), QString())
{
  const SelfUpdater::ReleaseInfo& newest = releases.front();
  const QString version                  = shownVersion(newest.tag);

  const QString title = tr("MO2 Revamped %1 is out").arg(version);
  setWindowTitle(title);
  if (auto* label = findChild<QLabel*>(QStringLiteral("fluentDialogTitle"))) {
    label->setText(title);
  }

  QString text;
  if (releases.size() > 1) {
    text = tr("You have %1, and %n newer releases are out. Here is what they change, "
              "newest first.",
              "", static_cast<int>(releases.size()))
               .arg(currentVersion);
  } else {
    text = tr("You have %1. Here is what the new one changes.").arg(currentVersion);
  }
  text += QStringLiteral("\n\n") +
          tr("To update, download the setup from GitHub or Nexus, run it and choose "
             "Update MO2 Revamped: your instances, mods and settings stay. Nothing "
             "is downloaded or installed by itself. You can turn these notices off "
             "in Settings, General, Updates.");
  if (auto* label = findChild<QLabel*>(QStringLiteral("fluentDialogText"))) {
    label->setText(text);
    label->setVisible(true);
  }

  // dates in the interface's language, not the system's
  m_locale = QLocale(settings.interface().language());
  body()->addWidget(createNotes(releases), 1);

  const QString releaseUrl = newest.url;
  connect(addButton(tr("Open on GitHub"), true), &QPushButton::clicked, this,
          [releaseUrl] {
            shell::Open(QUrl(releaseUrl));
          });
  connect(addButton(tr("Open on Nexus")), &QPushButton::clicked, this, [] {
    shell::Open(QUrl(nexusPageUrl()));
  });

  const QString tag = newest.tag;
  connect(addButton(tr("Skip this version")), &QPushButton::clicked, this,
          [this, &settings, tag] {
            log::info("update notice: {} skipped", tag);
            settings.setSkippedUpdate(tag);
            close();
          });
  connect(addButton(tr("Remind me later")), &QPushButton::clicked, this,
          &QDialog::close);

  // the notes scroll with their bar at the window's edge, as in Settings; the
  // title and the text above them keep their gap to the edge
  FluentSettings::scrollAtEdge(this, 24);
  for (const char* name : {"fluentDialogTitle", "fluentDialogText"}) {
    if (auto* label = findChild<QLabel*>(QString::fromLatin1(name))) {
      label->setContentsMargins(0, 0, 24, 0);
    }
  }

  resize(760, 680);
}

QWidget*
UpdateNoticeDialog::createNotes(const std::vector<SelfUpdater::ReleaseInfo>& releases)
{
  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("updateNoticeNotes"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // the notes are part of the page, not a panel of their own
  scroll->setStyleSheet(QStringLiteral(
      "QScrollArea#updateNoticeNotes, QWidget#updateNoticeNotesContent "
      "{ background: transparent; }"));
  scroll->viewport()->setAutoFillBackground(false);

  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("updateNoticeNotesContent"));
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 8, 0, 8);
  layout->setSpacing(4);

  const QLocale& locale = m_locale;
  for (const auto& release : releases) {
    auto* heading = new QLabel(
        release.name.isEmpty() ? QStringLiteral("MO2 Revamped %1").arg(shownVersion(release.tag))
                               : release.name,
        content);
    // the theme sets every label's font, so the size goes through a style too
    heading->setObjectName(QStringLiteral("updateNoticeRelease"));
    heading->setStyleSheet(QStringLiteral(
        "QLabel#updateNoticeRelease { font-size: 13pt; font-weight: 600; }"));
    heading->setWordWrap(true);
    layout->addWidget(heading);

    QString facts;
    switch (release.channel) {
    case SelfUpdater::UpdateChannel::Dev:
      facts = tr("Development build");
      break;
    case SelfUpdater::UpdateChannel::Beta:
      facts = tr("Beta");
      break;
    default:
      facts = tr("Stable release");
      break;
    }
    if (release.published.isValid()) {
      facts = tr("%1 · published %2")
                  .arg(facts,
                       locale.toString(release.published.toLocalTime().date(),
                                       QLocale::LongFormat));
    }
    auto* detail = new QLabel(facts, content);
    detail->setObjectName(QStringLiteral("fluentSettingDescription"));
    layout->addWidget(detail);

    auto* notes = new QLabel(content);
    notes->setTextFormat(Qt::MarkdownText);
    notes->setText(release.notes.trimmed().isEmpty()
                       ? tr("This release has no notes on GitHub.")
                       : release.notes);
    notes->setWordWrap(true);
    notes->setOpenExternalLinks(true);
    notes->setTextInteractionFlags(Qt::TextBrowserInteraction);
    notes->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(notes);
    layout->addSpacing(16);
  }
  layout->addStretch(1);

  scroll->setWidget(content);
  return scroll;
}
