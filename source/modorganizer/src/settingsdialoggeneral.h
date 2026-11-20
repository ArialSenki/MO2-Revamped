#ifndef SETTINGSDIALOGGENERAL_H
#define SETTINGSDIALOGGENERAL_H

class QComboBox;
class QCheckBox;

#include "plugincontainer.h"
#include "settings.h"
#include "settingsdialog.h"


class GeneralSettingsTab : public SettingsTab
{
public:
  GeneralSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update();

private:
  QCheckBox* m_soundOnly = nullptr;

  void addLanguages();
  void selectLanguage();

  void resetDialogs();

  void onResetDialogs();

};

#endif  // SETTINGSDIALOGGENERAL_H
