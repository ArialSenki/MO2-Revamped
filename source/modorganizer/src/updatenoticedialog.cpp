#include "updatenoticedialog.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "revampedversion.h"
#include "settings.h"

#include <log.h>
#include <utility.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{
constexpr char16_t StableGlyph    = 0xE930;  // Completed
constexpr char16_t BetaGlyph      = 0xE7C1;  // Flag
constexpr char16_t DevGlyph       = 0xEC7A;  // DeveloperTools
constexpr char16_t InstalledGlyph = 0xE80F;  // Home
constexpr char16_t DownloadGlyph  = 0xE896;
constexpr char16_t RingerGlyph    = 0xEA8F;
constexpr char16_t OpenGlyph      = 0xE8A7;  // OpenInNewWindow
constexpr char16_t SkipGlyph      = 0xE893;  // Next

// the index into m_releases of a list row; headings and the installed version
// have none
constexpr int ReleaseRole = Qt::UserRole + 10;

char16_t channelGlyph(UpdateChannel channel)
{
  switch (channel) {
  case UpdateChannel::Dev:
    return DevGlyph;
  case UpdateChannel::Beta:
    return BetaGlyph;
  default:
    return StableGlyph;
  }
}

// a release's notes as a changelog: the summary, then each section with its
// points. GitHub's text is Markdown; only what release notes use is read:
// "#" headings, "-" or "*" points (with their indented continuation lines),
// paragraphs, **bold**, `code` and [links](url).
struct NotesSection
{
  QString title;
  QStringList paragraphs;  // before the points
  QStringList points;
  QStringList after;       // paragraphs after the points
};

// inline Markdown to rich text. The text is escaped first, so HTML in the
// notes stays text; images become their text, so nothing loads a file or a
// network share by itself
QString inlineHtml(const QString& text)
{
  static const QRegularExpression image(QStringLiteral(R"(!\[([^\]]*)\]\([^)]*\))"));
  static const QRegularExpression link(QStringLiteral(R"(\[([^\]]+)\]\(([^)\s]+)\))"));
  static const QRegularExpression bold(QStringLiteral(R"(\*\*(.+?)\*\*)"));
  static const QRegularExpression code(QStringLiteral("`([^`]+)`"));

  QString html = text;
  html.replace(image, QStringLiteral("\\1"));
  html = html.toHtmlEscaped();
  html.replace(link, QStringLiteral("<a href=\"\\2\">\\1</a>"));
  html.replace(bold, QStringLiteral("<b>\\1</b>"));
  html.replace(code, QStringLiteral("<code>\\1</code>"));
  return html;
}

std::vector<NotesSection> parseNotes(const QString& notes)
{
  static const QRegularExpression heading(QStringLiteral(R"(^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$)"));
  static const QRegularExpression point(QStringLiteral(R"(^\s{0,3}[-*+]\s+(.*)$)"));

  std::vector<NotesSection> sections(1);
  QString paragraph;
  bool inPoint = false;

  const auto flush = [&] {
    if (!paragraph.isEmpty()) {
      auto& s = sections.back();
      (s.points.isEmpty() ? s.paragraphs : s.after).append(paragraph);
      paragraph.clear();
    }
    inPoint = false;
  };

  for (const QString& raw : notes.split(QLatin1Char('\n'))) {
    const QString line = raw.trimmed();
    if (line.isEmpty()) {
      flush();
      continue;
    }
    if (const auto m = heading.match(raw); m.hasMatch()) {
      flush();
      sections.push_back({m.captured(1)});
      continue;
    }
    if (const auto m = point.match(raw); m.hasMatch()) {
      flush();
      sections.back().points.append(m.captured(1).trimmed());
      inPoint = true;
      continue;
    }
    if (inPoint && raw.startsWith(QLatin1Char(' '))) {
      sections.back().points.last() += QLatin1Char(' ') + line;
      continue;
    }
    if (inPoint) {
      inPoint = false;
    }
    paragraph += (paragraph.isEmpty() ? QString() : QStringLiteral(" ")) + line;
  }
  flush();

  // what the window already says another way: the download section (How to
  // update says it) and the closing line about the project
  static const QRegularExpression download(QStringLiteral("^download"),
                                           QRegularExpression::CaseInsensitiveOption);
  std::vector<NotesSection> kept;
  for (auto& s : sections) {
    if (download.match(s.title).hasMatch()) {
      continue;
    }
    s.after.removeIf([](const QString& p) {
      return p.startsWith(QLatin1String("MO2 Revamped is an independent"));
    });
    s.paragraphs.removeIf([](const QString& p) {
      return p.startsWith(QLatin1String("MO2 Revamped is an independent"));
    });
    if (!s.title.isEmpty() || !s.paragraphs.isEmpty() || !s.points.isEmpty() ||
        !s.after.isEmpty()) {
      kept.push_back(std::move(s));
    }
  }
  return kept;
}

// links from the notes open in the browser, and only web links
void openNotesLink(const QString& link)
{
  const QUrl url(link);
  if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) {
    shell::Open(url);
  } else {
    log::warn("update notice: not opening '{}', only web links open", link);
  }
}

// "v1.1.0-beta.4" is shown as "1.1.0-beta.4"
QString shownVersion(const QString& tag)
{
  if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
    return tag.mid(1);
  }
  return tag;
}
}  // namespace

QString UpdateNoticeDialog::nexusPageUrl()
{
  return QStringLiteral("https://www.nexusmods.com/eldenring/mods/%1")
      .arg(MO2R_NEXUS_MOD_ID);
}

UpdateNoticeDialog::UpdateNoticeDialog(
    QWidget* parent, Settings& settings, const QString& currentVersion,
    const std::vector<SelfUpdater::ReleaseInfo>& releases)
    : QDialog(parent), m_locale(settings.interface().language()), m_releases(releases)
{
  setObjectName(QStringLiteral("UpdateNoticeDialog"));
  setWindowTitle(tr("MO2 Revamped %1 is out").arg(shownVersion(m_releases.front().tag)));
  setMinimumSize(820, 520);
  resize(980, 660);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  auto* body = new QHBoxLayout;
  body->setSpacing(8);
  body->addWidget(createListCard(currentVersion));
  body->addWidget(createDetailsCard(settings), 1);
  layout->addLayout(body, 1);

  // Remind me later closes; Esc and the title bar do the same
  auto* footer = new QHBoxLayout;
  footer->setSpacing(8);
  footer->addStretch(1);
  auto* later = new QPushButton(tr("Remind me later"), this);
  later->setObjectName(QStringLiteral("close"));
  later->setAutoDefault(false);
  later->setToolTip(tr("The window opens again the next time MO2 starts."));
  footer->addWidget(later);
  layout->addLayout(footer);
  connect(later, &QPushButton::clicked, this, &QDialog::close);

  // the newest release, the first row after the heading
  m_list->setCurrentRow(1);
  m_list->setFocus();
}

QWidget* UpdateNoticeDialog::createListCard(const QString& currentVersion)
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("updateListCard"));
  card->setProperty("fluentCard", true);
  card->setFixedWidth(280);

  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 10, 8, 8);
  layout->setSpacing(6);

  auto* title = new QHBoxLayout;
  title->setContentsMargins(6, 0, 6, 2);
  auto* heading = new QLabel(tr("Versions"), card);
  heading->setObjectName(QStringLiteral("fluentCardTitle"));
  auto* count = new QLabel(QString::number(m_releases.size()), card);
  count->setObjectName(QStringLiteral("fluentStatCaption"));
  count->setToolTip(tr("Releases newer than yours"));
  title->addWidget(heading);
  title->addStretch(1);
  title->addWidget(count);
  layout->addLayout(title);

  // the list style of the instance manager: transparent, rows with an icon, a
  // name and a caption
  m_list = new QListWidget(card);
  m_list->setObjectName(QStringLiteral("instanceList"));
  m_list->setItemDelegate(new FluentPanels::RowDelegate(m_list, 56, 24));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setMouseTracking(true);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setAccessibleName(tr("Versions"));

  const auto addHeading = [this](const QString& text) {
    auto* item = new QListWidgetItem(text, m_list);
    item->setData(FluentPanels::HeaderRole, true);
    item->setFlags(Qt::ItemIsEnabled);
  };

  addHeading(tr("Newer than yours"));
  for (std::size_t i = 0; i < m_releases.size(); ++i) {
    const auto& release = m_releases[i];
    auto* item = new QListWidgetItem(shownVersion(release.tag), m_list);
    item->setIcon(FluentIcons::glyph(channelGlyph(release.channel),
                                     i == 0 ? FluentIcons::Tone::Accent
                                            : FluentIcons::Tone::Muted));
    item->setData(FluentPanels::CaptionRole, facts(release, QLocale::ShortFormat));
    item->setData(ReleaseRole, static_cast<int>(i));
    item->setToolTip(release.name);
  }

  // where the user stands, for comparison; it can't be picked
  addHeading(tr("Yours"));
  auto* installed = new QListWidgetItem(currentVersion, m_list);
  installed->setIcon(FluentIcons::glyph(InstalledGlyph, FluentIcons::Tone::Muted));
  installed->setData(FluentPanels::CaptionRole, tr("Installed"));
  installed->setFlags(Qt::ItemIsEnabled);

  layout->addWidget(m_list, 1);

  connect(m_list, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current) {
            if (current != nullptr && current->data(ReleaseRole).isValid()) {
              showRelease(current->data(ReleaseRole).toInt());
            }
          });

  return card;
}

QWidget* UpdateNoticeDialog::createDetailsCard(Settings& settings)
{
  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("updateDetailsCard"));
  card->setProperty("fluentCard", true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // the release's name, its channel and date
  auto* head = new QVBoxLayout;
  head->setContentsMargins(18, 14, 12, 14);
  head->setSpacing(2);
  m_name = new QLabel(card);
  m_name->setObjectName(QStringLiteral("instanceDetailName"));
  m_name->setTextFormat(Qt::PlainText);
  m_name->setWordWrap(true);
  m_caption = new QLabel(card);
  m_caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  m_caption->setTextFormat(Qt::PlainText);
  head->addWidget(m_name);
  head->addWidget(m_caption);
  layout->addLayout(head);

  // the rows and the notes scroll together inside the card
  m_scroll = new QScrollArea(card);
  m_scroll->setObjectName(QStringLiteral("instanceDetailsScroll"));
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setWidgetResizable(true);
  // wide tables or code in the notes scroll instead of being cut
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  auto* rows = new QWidget(m_scroll);
  rows->setObjectName(QStringLiteral("instanceDetailRows"));
  auto* rowsLayout = new QVBoxLayout(rows);
  rowsLayout->setContentsMargins(0, 0, 0, 0);
  rowsLayout->setSpacing(0);
  m_scroll->setWidget(rows);
  layout->addWidget(m_scroll, 1);

  // the selected release's changelog, rebuilt by showRelease(), so only one
  // release's text is laid out at a time; it takes the rest of the card, so
  // short notes leave no band below
  m_notesBox = new QWidget(rows);
  m_notesBox->setObjectName(QStringLiteral("updateNotes"));
  auto* notesLayout = new QVBoxLayout(m_notesBox);
  notesLayout->setContentsMargins(18, 4, 18, 18);
  notesLayout->setSpacing(4);
  rowsLayout->addWidget(m_notesBox, 1);

  FluentPanels::addEditRow(
      rows, DownloadGlyph, tr("How to update"),
      tr("Download the setup from GitHub or Nexus, run it and choose Update MO2 "
         "Revamped. Your instances, mods and settings stay, and nothing is "
         "downloaded by itself."),
      {});
  FluentPanels::addEditRow(rows, RingerGlyph, tr("Notices"),
                           tr("Turn them off, or pick which versions you hear "
                              "about, in Settings, General, Updates."),
                           {});

  // the release's pages on the left, skipping on the right
  auto* actions = new QFrame(card);
  actions->setObjectName(QStringLiteral("fluentSettingRow"));
  auto* actionsLayout = new QHBoxLayout(actions);
  actionsLayout->setContentsMargins(12, 12, 12, 12);
  actionsLayout->setSpacing(8);
  m_github = new QPushButton(FluentIcons::glyph(OpenGlyph), tr("Open on GitHub"), actions);
  m_github->setObjectName(QStringLiteral("updateGitHubButton"));
  m_github->setProperty("fluentPrimary", true);
  m_github->setAutoDefault(false);
  auto* nexus =
      new QPushButton(FluentIcons::glyph(OpenGlyph), tr("Open on Nexus"), actions);
  nexus->setObjectName(QStringLiteral("updateNexusButton"));
  nexus->setAutoDefault(false);
  nexus->setToolTip(nexusPageUrl());

  const QString newest = m_releases.front().tag;
  auto* skip =
      new QPushButton(FluentIcons::glyph(SkipGlyph), tr("Skip this version"), actions);
  skip->setObjectName(QStringLiteral("updateSkipButton"));
  skip->setAutoDefault(false);
  skip->setToolTip(tr("%1 no longer opens this window; a newer release does. The "
                      "status bar still shows it.")
                       .arg(shownVersion(newest)));
  actionsLayout->addWidget(m_github);
  actionsLayout->addWidget(nexus);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(skip);
  layout->addWidget(actions);

  connect(m_github, &QPushButton::clicked, this, [this] {
    const auto* item = m_list->currentItem();
    if (item != nullptr && item->data(ReleaseRole).isValid()) {
      shell::Open(QUrl(m_releases[item->data(ReleaseRole).toInt()].url));
    }
  });
  connect(nexus, &QPushButton::clicked, this, [] {
    shell::Open(QUrl(nexusPageUrl()));
  });
  connect(skip, &QPushButton::clicked, this, [this, &settings, newest] {
    log::info("update notice: {} skipped", newest);
    settings.setSkippedUpdate(newest);
    close();
  });

  return card;
}

void UpdateNoticeDialog::showRelease(int index)
{
  const auto& release = m_releases[static_cast<std::size_t>(index)];

  m_name->setText(release.name.isEmpty()
                      ? QStringLiteral("MO2 Revamped %1").arg(shownVersion(release.tag))
                      : release.name);
  QString caption = facts(release, QLocale::LongFormat);
  if (index == 0) {
    caption = tr("%1 · newest").arg(caption);
  }
  m_caption->setText(caption);
  fillNotes(release.notes);
  m_github->setToolTip(release.url);
  m_scroll->verticalScrollBar()->setValue(0);
}

// the changelog of one release, Nexus-style: the summary, then each section's
// title and its points, one row per point with the bullet hanging
void UpdateNoticeDialog::fillNotes(const QString& notes)
{
  auto* layout = static_cast<QVBoxLayout*>(m_notesBox->layout());
  while (QLayoutItem* item = layout->takeAt(0)) {
    // hidden at once, deleted later: the old text never shows over the new
    if (QWidget* w = item->widget()) {
      w->hide();
      w->deleteLater();
    } else if (QLayout* l = item->layout()) {
      // a point's row: its labels belong to m_notesBox
      while (QLayoutItem* inner = l->takeAt(0)) {
        if (inner->widget() != nullptr) {
          inner->widget()->hide();
          inner->widget()->deleteLater();
        }
        delete inner;
      }
    }
    delete item;
  }

  const auto text = [this](const QString& html, const QString& name = {}) {
    auto* label = new QLabel(html, m_notesBox);
    if (!name.isEmpty()) {
      label->setObjectName(name);
    }
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setOpenExternalLinks(false);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connect(label, &QLabel::linkActivated, this, &openNotesLink);
    return label;
  };

  const auto sections = parseNotes(notes);
  if (sections.empty()) {
    layout->addWidget(text(tr("This release has no notes on GitHub.").toHtmlEscaped(),
                           QStringLiteral("fluentSettingDescription")));
    layout->addStretch(1);
    return;
  }

  bool first = true;
  for (const auto& section : sections) {
    if (!section.title.isEmpty()) {
      if (!first) {
        layout->addSpacing(10);
      }
      auto* title = new QLabel(section.title, m_notesBox);
      title->setObjectName(QStringLiteral("updateNotesSection"));
      title->setTextFormat(Qt::PlainText);
      title->setWordWrap(true);
      title->setStyleSheet(QStringLiteral(
          "QLabel#updateNotesSection { font-size: 11pt; font-weight: 600; "
          "padding-top: 6px; }"));
      layout->addWidget(title);
    }
    for (const QString& p : section.paragraphs) {
      // the summary before the first section reads as the release's caption
      layout->addWidget(text(inlineHtml(p), section.title.isEmpty()
                                                ? QStringLiteral("instanceDetailCaption")
                                                : QString()));
    }
    for (const QString& p : section.points) {
      auto* row = new QHBoxLayout;
      row->setSpacing(8);
      row->setContentsMargins(4, 2, 0, 2);
      auto* bullet = new QLabel(QStringLiteral("•"), m_notesBox);
      bullet->setAlignment(Qt::AlignTop);
      bullet->setFixedWidth(10);
      row->addWidget(bullet, 0, Qt::AlignTop);
      row->addWidget(text(inlineHtml(p)), 1);
      layout->addLayout(row);
    }
    for (const QString& p : section.after) {
      layout->addWidget(text(inlineHtml(p), QStringLiteral("fluentSettingDescription")));
    }
    first = false;
  }
  layout->addStretch(1);
}

QString UpdateNoticeDialog::facts(const SelfUpdater::ReleaseInfo& release,
                                  QLocale::FormatType format) const
{
  QString channel;
  switch (release.channel) {
  case UpdateChannel::Dev:
    channel = tr("Development build");
    break;
  case UpdateChannel::Beta:
    channel = tr("Beta");
    break;
  default:
    channel = tr("Stable release");
    break;
  }
  if (!release.published.isValid()) {
    return channel;
  }
  const QDate date = release.published.toLocalTime().date();
  if (format == QLocale::ShortFormat) {
    return QStringLiteral("%1 · %2").arg(channel,
                                         m_locale.toString(date, QLocale::ShortFormat));
  }
  return tr("%1 · published %2").arg(channel, m_locale.toString(date, QLocale::LongFormat));
}
