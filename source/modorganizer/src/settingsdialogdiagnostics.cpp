#include "settingsdialogdiagnostics.h"
#include "circularuihelpers.h"
#include "organizercore.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <log.h>

#include <QFormLayout>
#include <QGridLayout>
#include <QSizePolicy>
#include <QVBoxLayout>

using namespace MOBase;

DiagnosticsSettingsTab::DiagnosticsSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  MOBase::CircularUi::addIconToHeading(
      ui->diagnosticsPageTitle, ":/MO/gui/mainwindow/files/text.svg",
      QStringLiteral("diagnosticsPageIcon"), 36, QStringLiteral("D"));
  MOBase::CircularUi::addGroupHeading(
      ui->diagnosticsLogGroup, ":/MO/gui/mainwindow/files/text.svg",
      QStringLiteral("diagnosticsLogIcon"), 26,
      QStringLiteral("diagnosticsSectionTitle"), QStringLiteral("L"));
  MOBase::CircularUi::addGroupHeading(
      ui->diagnosticsLootGroup, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("diagnosticsLootIcon"), 26,
      QStringLiteral("diagnosticsSectionTitle"), QStringLiteral("L"));
  MOBase::CircularUi::addGroupHeading(
      ui->diagnosticsFilesGroup, ":/MO/gui/mainwindow/files/folder.svg",
      QStringLiteral("diagnosticsFilesIcon"), 26,
      QStringLiteral("diagnosticsSectionTitle"), QStringLiteral("F"));

  ui->diagnosticsCardsLayout->setContentsMargins(0, 0, 0, 0);
  ui->diagnosticsCardsLayout->setHorizontalSpacing(12);
  ui->diagnosticsCardsLayout->setVerticalSpacing(12);
  ui->diagnosticsCardsLayout->setAlignment(Qt::AlignTop);

  // Keep the three cards in two compact columns. The log controls need a full
  // column, while the file locations and LOOT settings fit naturally beside it.
  const auto cards = {ui->diagnosticsLogGroup, ui->diagnosticsLootGroup,
                      ui->diagnosticsFilesGroup};
  for (QGroupBox* card : cards) {
    ui->diagnosticsCardsLayout->removeWidget(card);
    QSizePolicy policy = card->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Maximum);
    card->setSizePolicy(policy);
  }

  auto* mo2Column = new QVBoxLayout;
  mo2Column->setContentsMargins(0, 0, 0, 0);
  mo2Column->setSpacing(12);
  mo2Column->addWidget(ui->diagnosticsLogGroup, 0, Qt::AlignTop);
  mo2Column->addStretch(1);

  auto* supportColumn = new QVBoxLayout;
  supportColumn->setContentsMargins(0, 0, 0, 0);
  supportColumn->setSpacing(12);
  supportColumn->addWidget(ui->diagnosticsFilesGroup, 0, Qt::AlignTop);
  supportColumn->addWidget(ui->diagnosticsLootGroup, 0, Qt::AlignTop);
  supportColumn->addStretch(1);

  ui->diagnosticsCardsLayout->addLayout(mo2Column, 0, 0);
  ui->diagnosticsCardsLayout->addLayout(supportColumn, 0, 1);
  ui->diagnosticsCardsLayout->setColumnStretch(0, 1);
  ui->diagnosticsCardsLayout->setColumnStretch(1, 1);

  ui->diagnosticsLogLayout->setContentsMargins(9, 8, 9, 9);
  ui->diagnosticsLogLayout->setSpacing(5);
  ui->diagnosticsLootLayout->setContentsMargins(9, 8, 9, 9);
  ui->diagnosticsLootLayout->setSpacing(5);
  ui->diagnosticsFilesLayout->setContentsMargins(9, 8, 9, 9);
  ui->diagnosticsFilesLayout->setSpacing(5);

  for (QFormLayout* form : {ui->formLayout_4, ui->formLayout_2}) {
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(7);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  }

  ui->label_12->setObjectName(QStringLiteral("diagnosticsFieldLabel"));
  ui->label_27->setObjectName(QStringLiteral("diagnosticsFieldLabel"));
  ui->label_28->setObjectName(QStringLiteral("diagnosticsFieldLabel"));
  ui->label_32->setObjectName(QStringLiteral("diagnosticsFieldLabel"));

  setLogLevel();
  setLootLogLevel();
  setCrashDumpTypesBox();

  // Give the value and both step buttons enough room at compact UI scales.
  ui->dumpsMaxEdit->setMinimumSize(92, 34);
  ui->dumpsMaxEdit->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  ui->dumpsMaxEdit->setAlignment(Qt::AlignCenter);
  ui->dumpsMaxEdit->setValue(settings().diagnostics().maxCoreDumps());

  QString logsPath = QUrl::fromLocalFile(qApp->property("dataPath").toString() + "/" +
                                         QString::fromStdWString(AppConfig::logPath()))
                         .toString();

  ui->diagnosticsExplainedLabel->setText(
      ui->diagnosticsExplainedLabel->text()
          .replace("LOGS_FULL_PATH", logsPath)
          .replace("LOGS_DIR", QString::fromStdWString(AppConfig::logPath()))
          .replace("DUMPS_FULL_PATH",
                   QUrl::fromLocalFile(
                       QString::fromStdWString(OrganizerCore::getGlobalCoreDumpPath()))
                       .toString())
          .replace("DUMPS_DIR", QString::fromStdWString(AppConfig::dumpsDir())));
}

void DiagnosticsSettingsTab::setLogLevel()
{
  ui->logLevelBox->clear();

  ui->logLevelBox->addItem(QObject::tr("Debug"), log::Debug);
  ui->logLevelBox->addItem(QObject::tr("Info (recommended)"), log::Info);
  ui->logLevelBox->addItem(QObject::tr("Warning"), log::Warning);
  ui->logLevelBox->addItem(QObject::tr("Error"), log::Error);

  const auto sel = settings().diagnostics().logLevel();

  for (int i = 0; i < ui->logLevelBox->count(); ++i) {
    if (ui->logLevelBox->itemData(i) == sel) {
      ui->logLevelBox->setCurrentIndex(i);
      break;
    }
  }
}

void DiagnosticsSettingsTab::setLootLogLevel()
{
  using L = lootcli::LogLevels;

  auto v = [](L level) {
    return QVariant(static_cast<int>(level));
  };

  ui->lootLogLevel->clear();

  ui->lootLogLevel->addItem(QObject::tr("Trace"), v(L::Trace));
  ui->lootLogLevel->addItem(QObject::tr("Debug"), v(L::Debug));
  ui->lootLogLevel->addItem(QObject::tr("Info (recommended)"), v(L::Info));
  ui->lootLogLevel->addItem(QObject::tr("Warning"), v(L::Warning));
  ui->lootLogLevel->addItem(QObject::tr("Error"), v(L::Error));

  const auto sel = settings().diagnostics().lootLogLevel();

  for (int i = 0; i < ui->lootLogLevel->count(); ++i) {
    if (ui->lootLogLevel->itemData(i) == v(sel)) {
      ui->lootLogLevel->setCurrentIndex(i);
      break;
    }
  }
}

void DiagnosticsSettingsTab::setCrashDumpTypesBox()
{
  ui->dumpsTypeBox->clear();

  auto add = [&](auto&& text, auto&& type) {
    ui->dumpsTypeBox->addItem(text, static_cast<int>(type));
  };

  add(QObject::tr("None"), env::CoreDumpTypes::None);
  add(QObject::tr("Mini (recommended)"), env::CoreDumpTypes::Mini);
  add(QObject::tr("Data"), env::CoreDumpTypes::Data);
  add(QObject::tr("Full"), env::CoreDumpTypes::Full);

  const auto current = static_cast<int>(settings().diagnostics().coreDumpType());

  for (int i = 0; i < ui->dumpsTypeBox->count(); ++i) {
    if (ui->dumpsTypeBox->itemData(i) == current) {
      ui->dumpsTypeBox->setCurrentIndex(i);
      break;
    }
  }
}

void DiagnosticsSettingsTab::update()
{
  settings().diagnostics().setLogLevel(
      static_cast<log::Levels>(ui->logLevelBox->currentData().toInt()));

  settings().diagnostics().setCoreDumpType(
      static_cast<env::CoreDumpTypes>(ui->dumpsTypeBox->currentData().toInt()));

  settings().diagnostics().setMaxCoreDumps(ui->dumpsMaxEdit->value());

  settings().diagnostics().setLootLogLevel(
      static_cast<lootcli::LogLevels>(ui->lootLogLevel->currentData().toInt()));
}
