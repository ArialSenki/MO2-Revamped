#ifndef SETTINGSDIALOGGENERAL_H
#define SETTINGSDIALOGGENERAL_H

#include "plugincontainer.h"
#include "settings.h"
#include "settingsdialog.h"

class QCheckBox;

class GeneralSettingsTab : public SettingsTab
{
public:
  GeneralSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update();

private:
  void addLanguages();
  void selectLanguage();

  void resetDialogs();

  void onEditCategories();
  void onResetDialogs();

  QCheckBox* m_betaMe3 = nullptr;
  QCheckBox* m_betaMe2 = nullptr;
};

#endif  // SETTINGSDIALOGGENERAL_H
