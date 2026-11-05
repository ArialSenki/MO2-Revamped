#include "revampedtheme.h"
#include "modinfodialognexus.h"
#include "fluentdialogs.h"
#include "fluenticons.h"
#include "bbcode.h"
#include "iplugingame.h"
#include "mainwindow.h"
#include "organizercore.h"
#include "settings.h"
#include "ui_modinfodialog.h"
#include <log.h>
#include <utility.h>
#include <versioninfo.h>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtWebEngineWidgets/QWebEngineView>

using namespace MOBase;

bool isValidModID(int id)
{
  return (id > 0);
}

namespace
{
// an empty field is how an id gets removed; QIntValidator wouldn't let the line
// edit report it
class ModIDValidator : public QIntValidator
{
public:
  using QIntValidator::QIntValidator;

  State validate(QString& input, int& pos) const override
  {
    return input.isEmpty() ? Acceptable : QIntValidator::validate(input, pos);
  }
};
}  // namespace

NexusTab::NexusTab(ModInfoDialogTabContext cx)
    : ModInfoDialogTab(std::move(cx)), m_requestStarted(false), m_loading(false),
      m_browserPageReady(false)
{
  ui->modID->setValidator(
      new ModIDValidator(0, std::numeric_limits<int>::max(), ui->modID));
  ui->endorse->setVisible(core().settings().nexus().endorsementIntegration());
  ui->track->setVisible(core().settings().nexus().trackedIntegration());

  connect(ui->modID, &QLineEdit::editingFinished, [&] {
    onModIDChanged();
  });
  connect(ui->sourceGame,
          static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [&] {
            onSourceGameChanged();
          });
  connect(ui->version, &QLineEdit::editingFinished, [&] {
    onVersionChanged();
  });
  connect(ui->category, &QLineEdit::editingFinished, [&] {
    onCategoryChanged();
  });

  connect(ui->showDescription, &QPushButton::clicked, [&] {
    showDescription();
  });
  connect(ui->refresh, &QPushButton::clicked, [&] {
    onRefreshBrowser();
  });
  connect(ui->visitNexus, &QPushButton::clicked, [&] {
    onVisitNexus();
  });
  connect(ui->endorse, &QPushButton::clicked, [&] {
    onEndorse();
  });
  connect(ui->track, &QPushButton::clicked, [&] {
    onTrack();
  });

  connect(ui->hasCustomURL, &QCheckBox::toggled, [&] {
    onCustomURLToggled();
  });
  connect(ui->customURL, &QLineEdit::editingFinished, [&] {
    onCustomURLChanged();
  });
  connect(ui->visitCustomURL, &QPushButton::clicked, [&] {
    onVisitCustomURL();
  });
}

NexusTab::~NexusTab()
{
  cleanup();
}

void NexusTab::cleanup()
{
  if (m_modConnection) {
    disconnect(m_modConnection);
    m_modConnection = {};
  }
}

// the description has a window of its own: a window holding a web view is drawn
// through the GPU until it closes, which shows in how smoothly it responds, and
// Mod information stays clear of that. Everything web starts here, on request.
void NexusTab::showDescription()
{
  if (m_descriptionWindow.isNull()) {
    MainWindow::prepareWebEngine();

    auto* dialog = new FluentContentDialog(parentWidget(),
                                           QStringLiteral("nexusDescriptionDialog"),
                                           mod().name());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowMaximizeButtonHint);

    m_browser = new QWebEngineView(dialog);
    dialog->body()->addWidget(m_browser, 1);

    connect(dialog->addButton(tr("Refresh")), &QPushButton::clicked, this, [this] {
      onRefreshBrowser();
    });
    connect(dialog->addButton(tr("Open in Browser")), &QPushButton::clicked, this,
            [this] {
              onVisitNexus();
            });
    connect(dialog->addButton(tr("Close")), &QPushButton::clicked, dialog,
            &QDialog::close);

    dialog->resize(980, 760);
    m_descriptionWindow = dialog;
    m_browserPageReady  = false;
    loadDescription();
  }

  m_descriptionWindow->show();
  m_descriptionWindow->raise();
  m_descriptionWindow->activateWindow();
}

// each mod gets a fresh page, so the last mod's description is never left behind
void NexusTab::ensureBrowserPage()
{
  if (m_browserPageReady) {
    return;
  }

  auto* page = new NexusTabWebpage(m_browser);
  m_browser->setPage(page);

  connect(page, &NexusTabWebpage::linkClicked, [&](const QUrl& url) {
    shell::Open(url);
  });

  m_browserPageReady = true;
}

void NexusTab::clear()
{
  // the new mod is already set: nothing here may be taken for an edit of it
  QScopedValueRollback loading(m_loading, true);

  ui->modID->clear();
  ui->sourceGame->clear();
  ui->version->clear();
  ui->category->clear();
  m_browserPageReady = false;
  ui->hasCustomURL->setChecked(false);
  ui->customURL->clear();
  setHasData(false);
}

void NexusTab::update()
{
  QScopedValueRollback loading(m_loading, true);

  clear();

  if (isValidModID(mod().nexusId())) {
    ui->modID->setText(QString("%1").arg(mod().nexusId()));
  }

  QString gameName = mod().gameName();
  ui->sourceGame->addItem(core().managedGame()->gameName(),
                          core().managedGame()->gameShortName());

  if (core().managedGame()->validShortNames().size() == 0) {
    ui->sourceGame->setDisabled(true);
  } else {
    for (auto game : plugin().plugins<MOBase::IPluginGame>()) {
      for (QString gameName : core().managedGame()->validShortNames()) {
        if (game->gameShortName().compare(gameName, Qt::CaseInsensitive) == 0) {
          ui->sourceGame->addItem(game->gameName(), game->gameShortName());
          break;
        }
      }
    }
  }

  ui->sourceGame->setCurrentIndex(ui->sourceGame->findData(gameName));

  ui->category->setText(QString("%1").arg(mod().getNexusCategory()));

  if (!m_descriptionWindow.isNull()) {
    loadDescription();
  }

  updateActions();

  setHasData(isValidModID(mod().nexusId()) || mod().hasCustomURL());
}

void NexusTab::firstActivation()
{
  updateWebpage();
}

void NexusTab::setMod(ModInfoPtr mod, MOShared::FilesOrigin* origin)
{
  cleanup();

  // whatever was still on its way was for the last mod
  m_requestStarted = false;

  ModInfoDialogTab::setMod(mod, origin);

  m_modConnection = connect(mod.data(), &ModInfo::modDetailsUpdated, [&] {
    onModChanged();
  });
}

bool NexusTab::usesOriginFiles() const
{
  return false;
}

void NexusTab::updateVersionColor()
{
  // no newest version yet says nothing about this one; the same checks as
  // the mod list's status column, so the two never disagree
  const bool known  = mod().newestVersion().isValid();
  const bool update = known && mod().updateAvailable();
  const bool older  = known && !update && mod().downgradeAvailable();
  if (update) {
    ui->version->setToolTip(
        tr("Current Version: %1").arg(mod().newestVersion().canonicalString()));
  } else if (older) {
    ui->version->setToolTip(tr("Older on Nexus: %1")
                                .arg(mod().newestVersion().canonicalString()));
  } else {
    ui->version->setToolTip(known ? tr("No update available") : QString());
  }

  // the theme colors it (versionState): an update in the accent, as the mod
  // list shows it, an older Nexus version in the caution tone
  const QString state = !known  ? QString()
                        : update ? QStringLiteral("update")
                        : older  ? QStringLiteral("older")
                                 : QStringLiteral("current");
  if (ui->version->property("versionState").toString() != state) {
    ui->version->setProperty("versionState", state);
    ui->version->style()->unpolish(ui->version);
    ui->version->style()->polish(ui->version);
  }

}

void NexusTab::updateWebpage()
{
  const int modID = mod().nexusId();

  if (isValidModID(modID)) {
    refreshData(modID);
  } else {
    onModChanged();
  }

  ui->version->setText(mod().version().displayString());
  ui->hasCustomURL->setChecked(mod().hasCustomURL());
  ui->customURL->setText(mod().url());
  ui->customURL->setEnabled(mod().hasCustomURL());
  ui->visitCustomURL->setEnabled(mod().hasCustomURL());
  ui->visitCustomURL->setToolTip(mod().parseCustomURL().toString());

  updateTracking();
}

void NexusTab::updateActions()
{
  const int modID  = mod().nexusId();
  const bool valid = isValidModID(modID);

  ui->showDescription->setEnabled(valid);
  ui->showDescription->setToolTip(
      valid ? tr("The mod's description from Nexus, with its images, in its own window")
            : tr("This mod has no Nexus ID."));
  ui->visitNexus->setEnabled(valid);
  ui->visitNexus->setToolTip(
      valid ? NexusInterface::instance().getModURL(modID, mod().gameName())
            : tr("This mod has no Nexus ID."));
  ui->refresh->setEnabled(valid);
  ui->track->setEnabled(valid);
  ui->endorse->setEnabled(valid &&
                          (mod().endorsedState() == EndorsedState::ENDORSED_FALSE ||
                           mod().endorsedState() == EndorsedState::ENDORSED_NEVER));
}

void NexusTab::updateTracking()
{
  if (mod().trackedState() == TrackedState::TRACKED_TRUE) {
    ui->track->setChecked(true);
    ui->track->setText(tr("Tracked"));
  } else {
    ui->track->setChecked(false);
    ui->track->setText(tr("Untracked"));
  }
}

void NexusTab::refreshData(int modID)
{
  if (tryRefreshData(modID)) {
    m_requestStarted = true;
  } else {
    onModChanged();
  }
}

bool NexusTab::tryRefreshData(int modID)
{
  if (isValidModID(modID) && !m_requestStarted) {
    if (mod().updateNXMInfo()) {
      if (!m_descriptionWindow.isNull()) {
        ensureBrowserPage();
        m_browser->setHtml("");
      }
      return true;
    }
  }

  return false;
}

// the description page in the colors of the Fluent theme's cards, with its
// text font, instead of Nexus' own grey (which stayed dark in Light too)
static QString fluentDescriptionStyle(QString html)
{
  const bool dark = MOBase::RevampedTheme::isDark();
  const QString card    = dark ? "#2B2B2B" : "#FBFBFB";
  const QString text    = dark ? "#FFFFFF" : "#1A1A1A";
  const QString muted   = dark ? "#C5C5C5" : "#5D5D5D";
  const QString link    = dark ? "#60CDFF" : "#005FB8";
  const QString quote   = dark ? "#2F2F2F" : "#F3F3F3";
  const QString inset   = dark ? "#262626" : "#F2F2F2";
  const QString line    = dark ? "#3D3D3D" : "#E5E5E5";
  const QString control = dark ? "#323232" : "#F6F6F6";

  html.replace("font-family: sans-serif;",
               "font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif;");
  html.replace("font-size: 14px;", "font-size: 13px;");
  html.replace("background: #404040;", "background: " + card + ";");
  html.replace("color: #f1f1f1;", "color: " + text + ";");
  html.replace("color: #e1e1e1;", "color: " + muted + ";");
  html.replace("color: #8197ec;", "color: " + link + ";");
  html.replace("border-left-color: rgb(87, 165, 204);", "border-left-color: " + link + ";");
  html.replace("background: #383838 url(", "background: " + quote + " url(");
  html.replace("background: #262626;", "background: " + inset + ";");
  html.replace("border: 1px dashed #3b3b3b;", "border: 1px dashed " + line + ";");
  html.replace("border: 1px solid black;", "border: 1px solid " + line + ";");
  html.replace("background-color: #454545;", "background-color: " + control + ";");
  html.replace("color: #E6E6E6;", "color: " + text + ";");
  // the page's scroll bar follows the theme too
  html.replace("</style>", QStringLiteral("    :root { color-scheme: %1; }\n"
                                          "    div.empty { text-align: center; color: %2; "
                                          "padding-top: 24px; }\n    </style>")
                               .arg(dark ? "dark" : "light", muted));
  return html;
}

void NexusTab::onModChanged()
{
  m_requestStarted = false;

  if (!m_descriptionWindow.isNull()) {
    loadDescription();
  }

  updateVersionColor();
  updateTracking();
  updateActions();
}

void NexusTab::loadDescription()
{
  const QString nexusDescription = mod().getNexusDescription();

  QString descriptionAsHTML = R"(
<html>
  <head>
    <style class="nexus-description">
    body
    {
      font-family: sans-serif;
      font-size: 14px;
      background: #404040;
      color: #f1f1f1;
      max-width: 1060px;
      margin-left: auto;
      margin-right: auto;
      padding-right: 7px;
      padding-left: 7px;
      padding-top: 20px;
      padding-bottom: 20px;
    }

    img {
      max-width: 100%;
    }

    figure.quote {
      position: relative;
      padding: 24px;
      margin: 10px 20px 10px 10px;
      color: #e1e1e1;
      line-height: 1.5;
      font-style: italic;
      border-left: 6px solid #57a5cc;
      border-left-color: rgb(87, 165, 204);
      background: #383838 url(data:image/svg+xml;base64,PHN2ZyBjbGFzcz0iaWNvbi1xdW90ZSIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHN0eWxlPSJmaWxsOnJnYig2OSwgNjksIDcwKTtoZWlnaHQ6MjlweDtsZWZ0OjE1cHg7cG9zaXRpb246YWJzb2x1dGU7dG9wOjE1cHg7d2lkdGg6MzhweDsiPjxwYXRoIGNsYXNzPSJwYXRoMSIgZD0iTTAgMjAuNjc0YzAgNy4yMjUgNC42NjggMTEuMzM3IDkuODkyIDExLjMzNyA0LjgyNC0wLjA2MiA4LjcxOS0zLjk1NiA4Ljc4MS04Ljc3NSAwLTQuNzg1LTMuMzM0LTguMDA5LTcuNTU4LTguMDA5LTAuMDc4LTAuMDA0LTAuMTctMC4wMDYtMC4yNjItMC4wMDYtMC43MDMgMC0xLjM3NyAwLjEyNC0yLjAwMSAwLjM1MiAxLjA0MS00LjAxNCA1LjE1My04LjY4MyA4LjcxLTEwLjU3MmwtNi4xMTMtNS4wMDJjLTYuODkxIDQuODkxLTExLjQ0OCAxMi4zMzgtMTEuNDQ4IDIwLjY3NHpNMjIuNjc1IDIwLjY3NGMwIDcuMjI1IDQuNjY4IDExLjMzNyA5Ljg5MiAxMS4zMzcgNC44LTAuMDU2IDguNjctMy45NjEgOC42Ny04Ljc2OSAwLTAuMDA0IDAtMC4wMDggMC0wLjAxMiAwLTQuNzc5LTMuMjIzLTguMDAyLTcuNDQ3LTguMDAyLTAuMDk1LTAuMDA2LTAuMjA2LTAuMDA5LTAuMzE4LTAuMDA5LTAuNjg0IDAtMS4zMzkgMC4xMjYtMS45NDMgMC4zNTUgMC45MjctNC4wMTQgNS4xNS04LjY4MiA4LjcwNy0xMC41NzJsLTYuMTI0LTUuMDAyYy02Ljg5MSA0Ljg5MS0xMS40MzcgMTIuMzM4LTExLjQzNyAyMC42NzR6IiBzdHlsZT0iZmlsbDpyZ2IoNjksIDY5LCA3MCk7aGVpZ2h0OmF1dG87d2lkdGg6YXV0bzsiLz48L3N2Zz4=) no-repeat;
    }

    figure.quote blockquote {
      position: relative;
      margin: 0;
      padding: 0;
    }

    div.spoiler_content {
      background: #262626;
      border: 1px dashed #3b3b3b;
      padding: 5px;
      margin: 5px;
    }

    div.bbc_spoiler_show{
      border: 1px solid black;
      background-color: #454545;
      font-size: 11px;
      padding: 3px;
      color: #E6E6E6;
      border-radius: 3px;
      display: inline-block;
      cursor: pointer;
    }

    details summary::marker {
      display:none;
    }

    summary:focus {
      outline: 0;
    }

    a
    {
      /*should avoid overflow with long links forcing wordwrap regardless of spaces*/
      overflow-wrap: break-word;
      word-wrap: break-word;

      color: #8197ec;
      text-decoration: none;
    }
    </style>
  </head>
  <body>%1</body>
</html>)";

  const bool validID = isValidModID(mod().nexusId());
  if (!validID || nexusDescription.isEmpty()) {
    descriptionAsHTML = descriptionAsHTML.arg(
        QStringLiteral(R"(<div class="empty"><p>%1</p></div>)")
            .arg(validID
                     ? tr("Nexus has no description for this mod, or it could not be "
                          "read. Refresh tries again.")
                     : tr("This mod has no valid Nexus ID. To open another site for it, "
                          "turn on Custom web page above and enter its address.")));
  } else {
    descriptionAsHTML = descriptionAsHTML.arg(BBCode::convertToHTML(nexusDescription));
  }

  ensureBrowserPage();

  descriptionAsHTML = fluentDescriptionStyle(descriptionAsHTML);
  // no white frame while the page loads
  m_browser->page()->setBackgroundColor(
      QColor(MOBase::RevampedTheme::isDark() ? 0x2B2B2B : 0xFBFBFB));

  m_browser->page()->setHtml(descriptionAsHTML);

  // the window names the mod it shows
  m_descriptionWindow->setWindowTitle(mod().name());
  if (auto* title = m_descriptionWindow->findChild<QLabel*>(
          QStringLiteral("fluentDialogTitle"))) {
    title->setText(mod().name());
  }
}

void NexusTab::onModIDChanged()
{
  if (m_loading) {
    return;
  }

  const int oldID = mod().nexusId();
  const int newID = ui->modID->text().toInt();

  // -1 and 0 both mean no id
  if (oldID != newID && (isValidModID(oldID) || isValidModID(newID))) {
    mod().setNexusID(newID);
    mod().setLastNexusQuery(QDateTime::fromSecsSinceEpoch(0));

    m_requestStarted = false;
    updateActions();
    setHasData(isValidModID(newID) || mod().hasCustomURL());
    refreshData(newID);
  }
}

void NexusTab::onSourceGameChanged()
{
  if (m_loading) {
    return;
  }

  for (auto game : plugin().plugins<MOBase::IPluginGame>()) {
    if (game->gameName() == ui->sourceGame->currentText()) {
      mod().setGameName(game->gameShortName());
      mod().setLastNexusQuery(QDateTime::fromSecsSinceEpoch(0));
      refreshData(mod().nexusId());
      return;
    }
  }
}

void NexusTab::onVersionChanged()
{
  if (m_loading) {
    return;
  }

  // leaving the field untouched mustn't parse the version again
  if (ui->version->text() == mod().version().displayString()) {
    return;
  }

  MOBase::VersionInfo version(ui->version->text());
  mod().setVersion(version);
  updateVersionColor();
}

void NexusTab::onCategoryChanged()
{
  if (m_loading) {
    return;
  }

  int category = ui->category->text().toInt();
  mod().setNexusCategory(category);
}

void NexusTab::onRefreshBrowser()
{
  const auto modID = mod().nexusId();

  if (isValidModID(modID)) {
    mod().setLastNexusQuery(QDateTime::fromSecsSinceEpoch(0));
    updateWebpage();
  } else {
    log::info("Mod has no valid Nexus ID, info can't be updated.");
  }
}

void NexusTab::onVisitNexus()
{
  const int modID = mod().nexusId();

  if (isValidModID(modID)) {
    const QString nexusLink =
        NexusInterface::instance().getModURL(modID, mod().gameName());

    shell::Open(QUrl(nexusLink));
  }
}

void NexusTab::onEndorse()
{
  // use modPtr() instead of mod() or this because the callback may be
  // executed after the dialog is closed
  core().loggedInAction(parentWidget(), [m = modPtr()] {
    m->endorse(true);
  });
}

void NexusTab::onTrack()
{
  // use modPtr() instead of mod() or this because the callback may be
  // executed after the dialog is closed
  core().loggedInAction(parentWidget(), [m = modPtr()] {
    if (m->trackedState() == TrackedState::TRACKED_TRUE) {
      m->track(false);
    } else {
      m->track(true);
    }
  });
}

void NexusTab::onCustomURLToggled()
{
  if (m_loading) {
    return;
  }

  mod().setHasCustomURL(ui->hasCustomURL->isChecked());
  ui->customURL->setEnabled(mod().hasCustomURL());
  ui->visitCustomURL->setEnabled(mod().hasCustomURL());
  setHasData(isValidModID(mod().nexusId()) || mod().hasCustomURL());
}

void NexusTab::onCustomURLChanged()
{
  if (m_loading) {
    return;
  }

  mod().setCustomURL(ui->customURL->text());
  ui->visitCustomURL->setToolTip(mod().parseCustomURL().toString());
}

void NexusTab::onVisitCustomURL()
{
  const QUrl url = mod().parseCustomURL();
  if (url.isValid()) {
    shell::Open(url);
  }
}
