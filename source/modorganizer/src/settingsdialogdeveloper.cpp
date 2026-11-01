#include "settingsdialogdeveloper.h"
#include "circularuihelpers.h"
#include "settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

DeveloperSettingsTab::DeveloperSettingsTab(Settings& s, SettingsDialog& d, QWidget* page)
    : SettingsTab(s, d)
{
  auto* layout = new QVBoxLayout(page);
  layout->setSpacing(12);

  auto* heading = new QLabel(QObject::tr("Developer"), page);
  heading->setObjectName(QStringLiteral("developerPageTitle"));
  heading->setProperty("wizardPageTitle", true);
  auto* text = new QLabel(QObject::tr("Features still in testing. They may change or go away, "
                                      "and none of them is needed to play."),
                          page);
  text->setObjectName(QStringLiteral("developerPageSubtitle"));
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
      QObject::tr("Shows this page. Turned off, it is gone the next time Settings "
                  "opens; seven taps on the version in About turn it on again."),
      true);
  // the section titles sit inside the cards, as on the other pages
  MOBase::CircularUi::addGroupHeading(
      developer, ":/MO/gui/mainwindow/settings.svg", QStringLiteral("developerModeIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("D"));
  layout->addWidget(developer);

  layout->addStretch(1);
}

void DeveloperSettingsTab::update()
{
  if (m_developerMode->isChecked() != GlobalSettings::developerMode()) {
    GlobalSettings::setDeveloperMode(m_developerMode->isChecked());
    // only this page reads it, the next time Settings opens: no restart. The
    // About page's seven taps look at the application's property.
    qApp->setProperty("revampedDeveloperMode", m_developerMode->isChecked());
  }
}
