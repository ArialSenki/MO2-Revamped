#ifndef SETTINGSDIALOGDEVELOPER_H
#define SETTINGSDIALOGDEVELOPER_H

#include "revampedmodes.h"
#include "settingsdialog.h"

class QCheckBox;

// Settings > Developer, shown while the developer mode is on (seven taps on
// the version in About): the developer mode itself and the beta features,
// each with a way to take it out again
class DeveloperSettingsTab : public SettingsTab
{
public:
  DeveloperSettingsTab(Settings& settings, SettingsDialog& dialog, QWidget* page);

  void update() override;

private:
  QCheckBox* m_developerMode = nullptr;
  QCheckBox* m_me3           = nullptr;
  QCheckBox* m_me2           = nullptr;

  // turns the manager off, takes its mode's instance away if asked, and
  // closes Settings with a restart
  void removeManager(RevampedModes::Mode mode, QCheckBox* box);
};

#endif  // SETTINGSDIALOGDEVELOPER_H
