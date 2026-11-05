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
#include <QTimer>
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
// points, in the order they are written. GitHub's text is Markdown; only what
// release notes use is read: "#" headings, "-", "*", "+" and "1." points (with
// their indented continuation lines), paragraphs, **bold**, `code`,
// [links](url) and bare web addresses. Anything else stays text.
struct NotesBlock
{
  enum Kind
  {
    Paragraph,
    Point,
  };
  Kind kind = Paragraph;
  QString marker;  // a point's bullet or number
  QString text;
};

struct NotesSection
{
  QString title;
  std::vector<NotesBlock> blocks;
};

// inline Markdown to rich text. Code, links and web addresses are set aside
// first, so bold never lands inside them; the rest is escaped, so HTML in the
// notes stays text, and images become their text, so nothing loads a file or
// a network share by itself
QString inlineHtml(const QString& text)
{
  static const QRegularExpression image(QStringLiteral(R"(!\[([^\]]*)\]\([^)]*\))"));
  static const QRegularExpression token(QStringLiteral(
      R"((`[^`]+`)|\[([^\]]+)\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)|<?(https?://[^\s<>()]+[^\s<>().,;:!?'"])>?)"));
  static const QRegularExpression bold(QStringLiteral(R"(\*\*(.+?)\*\*)"));

  QString source = text;
  source.replace(image, QStringLiteral("\\1"));

  // the pieces that keep their own markup, replaced by \x01<n>\x02 meanwhile
  QStringList kept;
  QString rest;
  qsizetype from = 0;
  auto it = token.globalMatch(source);
  while (it.hasNext()) {
    const auto m = it.next();
    rest += source.mid(from, m.capturedStart() - from);
    from = m.capturedEnd();
    QString html;
    if (m.capturedLength(1) > 0) {
      const QString code = m.captured(1).mid(1, m.capturedLength(1) - 2);
      html = QStringLiteral("<code>%1</code>").arg(code.toHtmlEscaped());
    } else {
      const bool named = m.capturedLength(2) > 0;
      const QString url   = named ? m.captured(3) : m.captured(4);
      QString label       = named ? m.captured(2) : url;
      label               = label.toHtmlEscaped();
      label.replace(bold, QStringLiteral("<b>\\1</b>"));
      html = QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), label);
    }
    rest += QChar(0x01) + QString::number(kept.size()) + QChar(0x02);
    kept.append(html);
  }
  rest += source.mid(from);

  QString html = rest.toHtmlEscaped();
  html.replace(bold, QStringLiteral("<b>\\1</b>"));
  for (qsizetype i = 0; i < kept.size(); ++i) {
    html.replace(QChar(0x01) + QString::number(i) + QChar(0x02), kept[i]);
  }
  return html;
}

// a heading or a section title without its Markdown marks
QString plainTitle(QString title)
{
  title.remove(QStringLiteral("**")).remove(QLatin1Char('`'));
  return title.trimmed();
}

std::vector<NotesSection> parseNotes(const QString& notes)
{
  // CommonMark: up to 3 spaces, 1 to 6 #, and closing #s only after a space,
  // so "## Notes for C#" keeps its #
  static const QRegularExpression heading(
      QStringLiteral(R"(^ {0,3}#{1,6}(?:[ \t]+(.*?))?(?:[ \t]+#+)?[ \t]*$)"));
  static const QRegularExpression point(
      QStringLiteral(R"(^ {0,3}([-*+]|\d{1,3}[.)])[ \t]+(.*)$)"));
  static const QRegularExpression indented(QStringLiteral(R"(^(?: {2,}|\t))"));

  std::vector<NotesSection> sections(1);
  QString paragraph;
  bool afterPoint = false;  // a point was the last block, blank lines aside
  bool pointOpen  = false;  // and the line before belonged to it

  const auto flush = [&] {
    if (!paragraph.isEmpty()) {
      sections.back().blocks.push_back({NotesBlock::Paragraph, {}, paragraph});
      paragraph.clear();
    }
  };

  QString body = notes;
  body.remove(QLatin1Char('\r'));
  for (const QString& raw : body.split(QLatin1Char('\n'))) {
    const QString line = raw.trimmed();
    if (line.isEmpty()) {
      flush();
      pointOpen = false;
      continue;
    }
    if (const auto m = heading.match(raw); m.hasMatch()) {
      flush();
      sections.push_back({plainTitle(m.captured(1))});
      afterPoint = pointOpen = false;
      continue;
    }
    if (const auto m = point.match(raw); m.hasMatch()) {
      flush();
      const QString marker = m.captured(1);
      const bool numbered  = marker.front().isDigit();
      sections.back().blocks.push_back(
          {NotesBlock::Point, numbered ? marker : QStringLiteral("•"),
           m.captured(2).trimmed()});
      afterPoint = pointOpen = true;
      continue;
    }
    // an indented line goes on with the point before it, also after a blank
    // line, and so does a line right below it (a lazy continuation); deeper
    // points are flattened into it
    if (paragraph.isEmpty() && ((afterPoint && indented.match(raw).hasMatch()) || pointOpen)) {
      sections.back().blocks.back().text += QLatin1Char(' ') + line;
      pointOpen = true;
      continue;
    }
    afterPoint = pointOpen = false;
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
    std::erase_if(s.blocks, [](const NotesBlock& b) {
      return b.kind == NotesBlock::Paragraph &&
             b.text.startsWith(QLatin1String("MO2 Revamped is an independent"));
    });
    if (!s.title.isEmpty() || !s.blocks.empty()) {
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

// a tool tip that shows text from GitHub as text: Qt reads a tool tip that
// looks like HTML as HTML, which could load an image from anywhere
QString plainTip(const QString& text)
{
  return QStringLiteral("<p style='white-space:pre'>%1</p>").arg(text.toHtmlEscaped());
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
  // the section titles of the notes; set once here, not on each title
  setStyleSheet(QStringLiteral("QLabel#updateNotesSection { font-size: 11pt; "
                               "font-weight: 600; padding-top: 6px; }"));

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
    item->setToolTip(plainTip(release.name));
  }

  // where the user stands, for comparison; it can't be picked
  addHeading(tr("Yours"));
  auto* installed = new QListWidgetItem(currentVersion, m_list);
  installed->setIcon(FluentIcons::glyph(InstalledGlyph, FluentIcons::Tone::Muted));
  installed->setData(FluentPanels::CaptionRole, tr("Installed"));
  installed->setFlags(Qt::NoItemFlags);

  layout->addWidget(m_list, 1);

  // a heading or the installed version can't be the current row: a click on
  // one keeps the release that was shown
  connect(m_list, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current, QListWidgetItem* previous) {
            if (current != nullptr && current->data(ReleaseRole).isValid()) {
              showRelease(current->data(ReleaseRole).toInt());
            } else if (previous != nullptr && previous->data(ReleaseRole).isValid()) {
              QTimer::singleShot(0, m_list, [list = m_list, previous] {
                list->setCurrentItem(previous);
              });
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
  // a long word or address in the notes scrolls instead of being cut
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
  nexus->setToolTip(plainTip(nexusPageUrl()));

  const QString newest = m_releases.front().tag;
  auto* skip =
      new QPushButton(FluentIcons::glyph(SkipGlyph), tr("Skip this version"), actions);
  skip->setObjectName(QStringLiteral("updateSkipButton"));
  skip->setAutoDefault(false);
  skip->setToolTip(plainTip(tr("%1 no longer opens this window; a newer release "
                               "does. The status bar still shows it.")
                                .arg(shownVersion(newest))));
  actionsLayout->addWidget(m_github);
  actionsLayout->addWidget(nexus);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(skip);
  layout->addWidget(actions);

  // the shown release's page, and only a web page
  connect(m_github, &QPushButton::clicked, this, [this] {
    if (m_shown >= 0) {
      openNotesLink(m_releases[static_cast<std::size_t>(m_shown)].url);
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
  m_shown             = index;

  m_name->setText(release.name.isEmpty()
                      ? QStringLiteral("MO2 Revamped %1").arg(shownVersion(release.tag))
                      : release.name);
  QString caption = facts(release, QLocale::LongFormat);
  if (index == 0) {
    caption = tr("%1 · newest").arg(caption);
  }
  m_caption->setText(caption);
  fillNotes(release.notes);
  m_github->setToolTip(plainTip(release.url));
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

  for (std::size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];
    // the text before the first heading is the release's summary
    const bool summary = (i == 0 && section.title.isEmpty());
    if (!section.title.isEmpty()) {
      if (i > 0) {
        layout->addSpacing(10);
      }
      auto* title = new QLabel(section.title, m_notesBox);
      title->setObjectName(QStringLiteral("updateNotesSection"));
      title->setTextFormat(Qt::PlainText);
      title->setWordWrap(true);
      layout->addWidget(title);
    }

    bool afterPoints = false;
    for (const auto& block : section.blocks) {
      if (block.kind == NotesBlock::Point) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        row->setContentsMargins(4, 2, 0, 2);
        auto* marker = new QLabel(block.marker, m_notesBox);
        marker->setTextFormat(Qt::PlainText);
        marker->setAlignment(Qt::AlignTop | Qt::AlignRight);
        marker->setMinimumWidth(10);
        row->addWidget(marker, 0, Qt::AlignTop);
        row->addWidget(text(inlineHtml(block.text)), 1);
        layout->addLayout(row);
        afterPoints = true;
      } else {
        // a summary reads as the release's caption, a remark after the points
        // as a footnote
        layout->addWidget(text(inlineHtml(block.text),
                               summary       ? QStringLiteral("instanceDetailCaption")
                               : afterPoints ? QStringLiteral("fluentSettingDescription")
                                             : QString()));
      }
    }
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
