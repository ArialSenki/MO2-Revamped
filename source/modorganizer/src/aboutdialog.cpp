/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "aboutdialog.h"
#include "fluenticons.h"
#include "fluentnavigation.h"
#include "fluentpanels.h"
#include "shared/util.h"
#include "ui_aboutdialog.h"
#include <utility.h>

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextBrowser>
#include <QVariant>
#include <Qt>

AboutDialog::AboutDialog(const QString& version, QWidget* parent,
                         bool eldenRingEdition)
    : QDialog(parent), ui(new Ui::AboutDialog)
{
  ui->setupUi(this);

  m_LicenseFiles[LICENSE_LGPL3]   = "LGPL-v3.0.txt";
  m_LicenseFiles[LICENSE_LGPL21]  = "GNU-LGPL-v2.1.txt";
  m_LicenseFiles[LICENSE_GPL3]    = "GPL-v3.0.txt";
  m_LicenseFiles[LICENSE_GPL2]    = "GPL-v2.0.txt";
  m_LicenseFiles[LICENSE_BOOST]   = "boost.txt";
  m_LicenseFiles[LICENSE_7ZIP]    = "7zip.txt";
  m_LicenseFiles[LICENSE_CCBY3]   = "BY-SA-v3.0.txt";
  m_LicenseFiles[LICENSE_ZLIB]    = "zlib.txt";
  m_LicenseFiles[LICENSE_PYTHON]  = "python.txt";
  m_LicenseFiles[LICENSE_SSL]     = "openssl.txt";
  m_LicenseFiles[LICENSE_CPPTOML] = "cpptoml.txt";
  m_LicenseFiles[LICENSE_UDIS]    = "udis86.txt";
  m_LicenseFiles[LICENSE_SPDLOG]  = "spdlog.txt";
  m_LicenseFiles[LICENSE_FMT]     = "fmt.txt";
  m_LicenseFiles[LICENSE_SIP]     = "sip.txt";
  m_LicenseFiles[LICENSE_CASTLE]  = "Castle.txt";
  m_LicenseFiles[LICENSE_ANTLR]   = "AntlrBuildTask.txt";
  m_LicenseFiles[LICENSE_DXTEX]   = "DXTex.txt";
  m_LicenseFiles[LICENSE_VDF]     = "ValveFileVDF.txt";

  addLicense("Qt", LICENSE_LGPL3);
  addLicense("Qt Json", LICENSE_GPL3);
  addLicense("Boost Library", LICENSE_BOOST);
  addLicense("7-zip", LICENSE_7ZIP);
  addLicense("ZLib", LICENSE_NONE);
  addLicense("Tango Icon Theme", LICENSE_NONE);
  addLicense("RRZE Icon Set", LICENSE_CCBY3);
  addLicense("Icons by Lorc, Delapouite and sbed available on http://game-icons.net",
             LICENSE_CCBY3);
  addLicense("Castle Core", LICENSE_CASTLE);
  addLicense("ANTLR", LICENSE_ANTLR);
  addLicense("LOOT", LICENSE_GPL3);
  addLicense("Python", LICENSE_PYTHON);
  addLicense("OpenSSL", LICENSE_SSL);
  addLicense("cpptoml", LICENSE_CPPTOML);
  addLicense("Udis86", LICENSE_UDIS);
  addLicense("spdlog", LICENSE_SPDLOG);
  addLicense("{fmt}", LICENSE_FMT);
  addLicense("SIP", LICENSE_SIP);
  addLicense("DXTex Headers", LICENSE_DXTEX);
  addLicense("Valve File VDF Reader", LICENSE_VDF);

  const QString applicationName =
      eldenRingEdition ? tr("Mod Organizer 2: Revamped — Elden Ring Edition")
                       : tr("Mod Organizer 2: Revamped");
  ui->nameLabel->setText(
      QString("<span style=\"font-size:12pt; font-weight:600;\">%1 %2</span>")
          .arg(applicationName)
          .arg(version));
#if defined(HGID)
  ui->revisionLabel->setText(ui->revisionLabel->text() + " " + HGID);
#elif defined(GITID)
  ui->revisionLabel->setText(ui->revisionLabel->text() + " " + GITID);
#else
  ui->revisionLabel->setText(ui->revisionLabel->text() + " unknown");
#endif

  ui->usvfsLabel->setText(ui->usvfsLabel->text() + " " +
                          MOShared::getUsvfsVersionString());
  ui->licenseText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  if (FluentIcons::active()) {
    buildFluentLayout(applicationName, version);
  }
}

void AboutDialog::buildFluentLayout(const QString& applicationName, const QString& version)
{
  // Windows 11 Settings' About page: the pages on the left under the
  // program's card, and the build facts as rows a bug report can copy
  QListWidget* nav = FluentNavigation::attach(ui->tabWidget, {0xE946, 0xE8F1, 0xE716});
  if (nav == nullptr) {
    return;
  }
  ui->aboutHeaderCard->hide();

  auto* card = new QFrame(this);
  card->setObjectName(QStringLiteral("aboutProgramCard"));
  card->setProperty("fluentCard", true);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(14, 14, 14, 14);
  cardLayout->setSpacing(6);
  auto* icon = new QLabel(card);
  const QPixmap logo = ui->iconLabel->pixmap();
  if (!logo.isNull()) {
    icon->setPixmap(logo.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  cardLayout->addWidget(icon);
  auto* name = new QLabel(applicationName, card);
  name->setObjectName(QStringLiteral("instanceDetailName"));
  name->setWordWrap(true);
  auto* caption = new QLabel(tr("Version %1").arg(version), card);
  caption->setObjectName(QStringLiteral("instanceDetailCaption"));
  cardLayout->addWidget(name);
  cardLayout->addWidget(caption);
  FluentNavigation::setNavigationHeader(ui->tabWidget, card);

#if defined(HGID)
  const QString revision = QStringLiteral(HGID);
#elif defined(GITID)
  const QString revision = QStringLiteral(GITID);
#else
  const QString revision = tr("unknown");
#endif
  const QString usvfs   = MOShared::getUsvfsVersionString();
  const QString windows = QSysInfo::prettyProductName() + QStringLiteral(" (") +
                          QSysInfo::kernelVersion() + QLatin1Char(')');

  // the facts, as the first card of the About page
  auto* page = qobject_cast<QVBoxLayout*>(ui->about->layout());
  if (page != nullptr) {
    ui->aboutSummary->hide();
    auto* facts = new QFrame(ui->about);
    facts->setProperty("fluentCard", true);
    auto* rows = new QVBoxLayout(facts);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    const QList<std::tuple<char16_t, QString, QString>> values{
        {0xE81C, tr("Version"), version},
        {0xE943, tr("Build"), revision},
        {0xE8B7, tr("Virtual file system"), tr("usvfs %1").arg(usvfs)},
        {0xE737, tr("Qt"), QString::fromLatin1(qVersion())},
        {0xE770, tr("Windows"), windows}};
    for (const auto& [glyph, title, value] : values) {
      FluentPanels::addDetailRow(facts, glyph, title).value->setFullText(value);
    }

    const QString report = QStringLiteral("%1 %2\n%3: %4\nusvfs %5\nQt %6\n%7")
                               .arg(applicationName, version, tr("Build"), revision, usvfs,
                                    QString::fromLatin1(qVersion()), windows);
    auto* actions = new QFrame(facts);
    actions->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(12, 12, 12, 12);
    actionsLayout->setSpacing(8);
    auto* copy = new QPushButton(FluentIcons::glyph(0xE8C8), tr("Copy for a bug report"),
                                 actions);
    copy->setAutoDefault(false);
    copy->setToolTip(tr("Copies the version, build and system to paste in an issue"));
    connect(copy, &QPushButton::clicked, this, [report, copy] {
      QGuiApplication::clipboard()->setText(report);
      copy->setText(tr("Copied"));
    });
    auto* github = new QPushButton(FluentIcons::glyph(0xE8A7), tr("MO2 Revamped on GitHub"),
                                   actions);
    github->setAutoDefault(false);
    connect(github, &QPushButton::clicked, this, [] {
      MOBase::shell::Open(QUrl(QStringLiteral("https://github.com/ArialSenki/MO2-Revamped")));
    });
    auto* issue = new QPushButton(FluentIcons::glyph(0xEBE8), tr("Report a problem"),
                                  actions);
    issue->setAutoDefault(false);
    connect(issue, &QPushButton::clicked, this, [] {
      MOBase::shell::Open(
          QUrl(QStringLiteral("https://github.com/ArialSenki/MO2-Revamped/issues/new/choose")));
    });
    actionsLayout->addWidget(copy);
    actionsLayout->addWidget(github);
    actionsLayout->addWidget(issue);
    actionsLayout->addStretch(1);
    rows->addWidget(actions);
    page->insertWidget(page->indexOf(ui->aboutSummary) + 1, facts);
  }

  ui->creditsList->setFrameShape(QFrame::NoFrame);
  setMinimumSize(900, 600);
  resize(980, 660);
}

AboutDialog::~AboutDialog()
{
  delete ui;
}

void AboutDialog::addLicense(const QString& name, Licenses license)
{
  QListWidgetItem* item = new QListWidgetItem(name);
  item->setData(Qt::UserRole, license);
  ui->creditsList->addItem(item);
}

void AboutDialog::on_creditsList_currentItemChanged(QListWidgetItem* current,
                                                    QListWidgetItem*)
{
  auto iter = m_LicenseFiles.find(current->data(Qt::UserRole).toInt());
  if (iter != m_LicenseFiles.end()) {
    QString filePath = qApp->applicationDirPath() + "/licenses/" + iter->second;
    QString text     = MOBase::readFileText(filePath);
    ui->licenseText->setText(text);
  } else {
    ui->licenseText->setText(tr("No license"));
  }
}

void AboutDialog::on_sourceText_linkActivated(const QString& link)
{
  MOBase::shell::Open(QUrl(link));
}
