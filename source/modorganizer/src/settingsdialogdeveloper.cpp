#include "settingsdialogdeveloper.h"
#include "circularuihelpers.h"
#include "fluentdialogs.h"
#include "settings.h"
#include "shared/util.h"

#include <QCheckBox>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using RevampedModes::Mode;

DeveloperSettingsTab::DeveloperSettingsTab(Settings& s, SettingsDialog& d, QWidget* page)
    : SettingsTab(s, d)
{
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(12);

  auto* heading = new QLabel(QObject::tr("Developer"), page);
  heading->setProperty("wizardPageTitle", true);
  auto* text = new QLabel(QObject::tr("Features still in testing. They may change or go away, "
                                      "and none of them is needed to play."),
                          page);
  text->setProperty("wizardPageText", true);
  text->setWordWrap(true);
  layout->addWidget(heading);
  layout->addWidget(text);

  auto addBox = [](QGroupBox* group, const QString& name, const QString& title,
                   const QString& description, bool checked) {
    auto* box = new QCheckBox(title, group);
    box->setObjectName(name);
    box->setToolTip(description);
    box->setWhatsThis(description);
    box->setChecked(checked);
    group->layout()->addWidget(box);
    return box;
  };

  auto* developer = new QGroupBox(QObject::tr("Developer mode"), page);
  developer->setObjectName(QStringLiteral("developerModeGroup"));
  new QVBoxLayout(developer);
  m_developerMode = addBox(
      developer, QStringLiteral("developerModeBox"), QObject::tr("Developer mode"),
      QObject::tr("Shows this page. Turning it off switches every beta feature off and "
                  "restarts MO2; seven taps on the version in About turn it on again."),
      true);
  // the section titles sit inside the cards, as on the other pages
  MOBase::CircularUi::addGroupHeading(
      developer, ":/MO/gui/mainwindow/settings.svg", QStringLiteral("developerModeIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("D"));
  layout->addWidget(developer);

  auto* managers = new QGroupBox(QObject::tr("Mod managers (beta)"), page);
  managers->setObjectName(QStringLiteral("modManagersGroup"));
  new QVBoxLayout(managers);
  m_me3 = addBox(
      managers, QStringLiteral("betaMe3Box"), QObject::tr("me3 integration"),
      QObject::tr("me3 starts Elden Ring with MO2's mods, native DLLs and Seamless Co-op, "
                  "and shows up among the executables when it is installed. Takes "
                  "effect after a restart."),
      settings().betaMe3());
  m_me2 = addBox(
      managers, QStringLiteral("betaMe2Box"), QObject::tr("Mod Engine 2 integration"),
      QObject::tr("Mod Engine 2 gets a config MO2 writes from your active mods, with its "
                  "extensions such as Seamless Co-op, shown before the game starts. Takes "
                  "effect after a restart."),
      settings().betaMe2());

  // taking a manager out: its switch, its entry and its mode's instance
  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(8);
  auto* removeMe3 = new QPushButton(QObject::tr("Remove me3…"), managers);
  auto* removeMe2 = new QPushButton(QObject::tr("Remove Mod Engine 2…"), managers);
  for (auto* button : {removeMe3, removeMe2}) {
    button->setToolTip(QObject::tr("Turns the integration off, takes its entry out of the "
                                   "executables and, if you want, its mode's instance "
                                   "too, then restarts MO2."));
    buttons->addWidget(button);
  }
  buttons->addStretch(1);
  static_cast<QVBoxLayout*>(managers->layout())->addLayout(buttons);
  QObject::connect(removeMe3, &QPushButton::clicked, page, [this] {
    removeManager(Mode::Me3, m_me3);
  });
  QObject::connect(removeMe2, &QPushButton::clicked, page, [this] {
    removeManager(Mode::Me2, m_me2);
  });
  MOBase::CircularUi::addGroupHeading(
      managers, ":/MO/gui/mainwindow/tools.svg", QStringLiteral("modManagersIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("M"));
  layout->addWidget(managers);
  layout->addStretch(1);
}

void DeveloperSettingsTab::removeManager(Mode mode, QCheckBox* box)
{
  const QString folder = RevampedModes::instanceFolder(settings(), mode);
  FluentContentDialog prompt(
      parentWidget(), QStringLiteral("RemoveModManagerDialog"),
      QObject::tr("Remove %1?").arg(RevampedModes::title(mode)),
      QObject::tr("The %1 integration turns off and its entry leaves the executables. "
                  "%1 itself stays installed. MO2 restarts.")
          .arg(RevampedModes::title(mode)));
  QCheckBox* deleteInstance = nullptr;
  if (!folder.isEmpty()) {
    // a check box doesn't wrap: its text stays short, the detail goes below
    deleteInstance = new QCheckBox(
        QObject::tr("Also move \"%1\" to the Recycle Bin").arg(QDir(folder).dirName()), &prompt);
    auto* detail = new QLabel(
        QObject::tr("The instance of this mode, with its mods, profiles and saves."), &prompt);
    detail->setObjectName(QStringLiteral("fluentSettingDescription"));
    detail->setWordWrap(true);
    detail->setContentsMargins(26, 0, 0, 0);
    prompt.body()->addWidget(deleteInstance);
    prompt.body()->addWidget(detail);
  }
  auto* remove = prompt.addButton(QObject::tr("Remove and restart"), true);
  auto* cancel = prompt.addButton(QObject::tr("Cancel"));
  QObject::connect(remove, &QPushButton::clicked, &prompt, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &prompt, &QDialog::reject);
  prompt.resize(560, prompt.sizeHint().height());
  if (prompt.exec() != QDialog::Accepted) {
    return;
  }

  if (deleteInstance != nullptr && deleteInstance->isChecked() &&
      !RevampedModes::removeInstance(parentWidget(), settings(), mode)) {
    return;
  }

  // Settings closes as with OK, so this and every other page are saved
  box->setChecked(false);
  dialog().setExitNeeded(dialog().exitNeeded() | Exit::Restart);
  dialog().accept();
}

void DeveloperSettingsTab::update()
{
  if (!m_developerMode->isChecked()) {
    // off: every beta feature goes with it
    m_me3->setChecked(false);
    m_me2->setChecked(false);
  }

  if (m_me3->isChecked() != settings().betaMe3() ||
      m_me2->isChecked() != settings().betaMe2() ||
      m_developerMode->isChecked() != GlobalSettings::developerMode()) {
    settings().setBetaMe3(m_me3->isChecked());
    settings().setBetaMe2(m_me2->isChecked());
    GlobalSettings::setDeveloperMode(m_developerMode->isChecked());
    // plugins add their executables and hooks as MO2 starts
    dialog().setExitNeeded(dialog().exitNeeded() | Exit::Restart);
  }
}
