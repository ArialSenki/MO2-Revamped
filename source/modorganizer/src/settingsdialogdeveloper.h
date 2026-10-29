#ifndef SETTINGSDIALOGDEVELOPER_H
#define SETTINGSDIALOGDEVELOPER_H

#include "settingsdialog.h"

class QCheckBox;

// Settings > Developer, shown while the developer mode is on (seven taps on
// the version in About): the home of features still in testing
class DeveloperSettingsTab : public SettingsTab
{
public:
  DeveloperSettingsTab(Settings& settings, SettingsDialog& dialog, QWidget* page);

  void update() override;

private:
  QCheckBox* m_developerMode = nullptr;
};

#endif  // SETTINGSDIALOGDEVELOPER_H
