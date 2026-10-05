#include "problemsdialog.h"
#include "organizercore.h"
#include "ui_problemsdialog.h"
#include <QPushButton>
#include <Shellapi.h>
#include <iplugin.h>
#include <iplugindiagnose.h>
#include <utility.h>

#include "plugincontainer.h"

using namespace MOBase;

ProblemsDialog::ProblemsDialog(const PluginContainer& pluginContainer, QWidget* parent)
    : QDialog(parent), ui(new Ui::ProblemsDialog), m_PluginContainer(pluginContainer),
      m_hasProblems(false), m_problemCount(0)
{
  ui->setupUi(this);
  ui->notificationSplitter->setStretchFactor(0, 4);
  ui->notificationSplitter->setStretchFactor(1, 5);
  ui->fixButton->hide();
  ui->manageOverwriteButton->hide();

  connect(ui->problemsWidget, SIGNAL(itemSelectionChanged()), this,
          SLOT(selectionChanged()));
  connect(ui->descriptionText, SIGNAL(anchorClicked(QUrl)), this,
          SLOT(urlClicked(QUrl)));
  connect(ui->fixButton, &QPushButton::clicked, this, &ProblemsDialog::startFix);
  connect(ui->manageOverwriteButton, &QPushButton::clicked, this,
          [this]() { emit manageOverwriteRequested(); });

  runDiagnosis();
}

ProblemsDialog::~ProblemsDialog()
{
  delete ui;
}

int ProblemsDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);
  return QDialog::exec();
}

void ProblemsDialog::runDiagnosis()
{
  m_hasProblems = false;
  m_problemCount = 0;
  ui->problemsWidget->clear();

  for (IPluginDiagnose* diagnose : m_PluginContainer.plugins<IPluginDiagnose>()) {
    if (!m_PluginContainer.isEnabled(diagnose)) {
      continue;
    }

    std::vector<unsigned int> activeProblems = diagnose->activeProblems();
    foreach (unsigned int key, activeProblems) {
      QTreeWidgetItem* newItem = new QTreeWidgetItem();
      const QString shortDescription = diagnose->shortDescription(key);
      const QString fullDescription  = diagnose->fullDescription(key);
      const bool isOverwrite =
          (shortDescription + fullDescription).contains("overwrite", Qt::CaseInsensitive);
      const bool hasGuidedFix = diagnose->hasGuidedFix(key);

      newItem->setText(0, shortDescription);
      newItem->setData(0, Qt::UserRole, fullDescription);
      newItem->setData(0, Qt::UserRole + 1,
                       QVariant::fromValue(reinterpret_cast<void*>(diagnose)));
      newItem->setData(0, Qt::UserRole + 2, key);
      newItem->setData(0, Qt::UserRole + 3, hasGuidedFix);
      newItem->setData(0, Qt::UserRole + 4, isOverwrite);

      ui->problemsWidget->addTopLevelItem(newItem);
      m_hasProblems = true;
      ++m_problemCount;
    }
  }

  ui->notificationCountLabel->setText(
      tr("%n active notification(s)", nullptr, static_cast<int>(m_problemCount)));

  if (!m_hasProblems) {
    auto* item = new QTreeWidgetItem;

    item->setText(0, tr("No active notifications"));
    item->setData(0, Qt::UserRole, QString());
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

    QFont font = item->font(0);
    font.setItalic(true);
    item->setFont(0, font);

    ui->problemsWidget->addTopLevelItem(item);
    selectionChanged();
  } else {
    ui->problemsWidget->setCurrentItem(ui->problemsWidget->topLevelItem(0));
    ui->problemsWidget->scrollToTop();
  }
}

bool ProblemsDialog::hasProblems() const
{
  return m_hasProblems;
}

void ProblemsDialog::selectionChanged()
{
  QTreeWidgetItem* item = ui->problemsWidget->currentItem();
  if (item == nullptr || !m_hasProblems) {
    ui->selectedTitleLabel->setText(tr("You're all caught up"));
    ui->notificationContextLabel->setText(
        tr("There are no active notifications to review."));
    ui->descriptionText->clear();
    ui->fixButton->hide();
    ui->manageOverwriteButton->hide();
    return;
  }

  const QString text = item->data(0, Qt::UserRole).toString();
  const bool isOverwrite = item->data(0, Qt::UserRole + 4).toBool();

  ui->selectedTitleLabel->setText(item->text(0));
  ui->notificationContextLabel->setText(
      isOverwrite
          ? tr("Files here are not assigned to a regular mod yet. Review them and move "
               "anything you want to keep into a mod. Deleting files here removes "
               "them from this instance.")
          : tr("Review the details below to understand this notification and choose an "
               "available action."));

  if (text.isEmpty()) {
    ui->descriptionText->setPlainText(
        tr("The plugin that reported this notification did not provide additional "
           "details."));
  } else {
    ui->descriptionText->setText(text);
  }
  ui->descriptionText->setLineWrapMode(QTextEdit::WidgetWidth);
  ui->fixButton->setVisible(item->data(0, Qt::UserRole + 3).toBool());
  ui->manageOverwriteButton->setVisible(isOverwrite);
}

void ProblemsDialog::startFix()
{
  QTreeWidgetItem* item = ui->problemsWidget->currentItem();
  if (item == nullptr || !item->data(0, Qt::UserRole + 3).toBool()) {
    return;
  }

  IPluginDiagnose* plugin = reinterpret_cast<IPluginDiagnose*>(
      item->data(0, Qt::UserRole + 1).value<void*>());
  if (plugin == nullptr) {
    log::warn("notification fix has no diagnosis plugin");
    return;
  }

  plugin->startGuidedFix(item->data(0, Qt::UserRole + 2).toUInt());
  runDiagnosis();
}

void ProblemsDialog::urlClicked(const QUrl& url)
{
  shell::Open(url);
}
