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
  QComboBox* m_scaleBox = nullptr;
  QCheckBox* m_greyText = nullptr;
  QCheckBox* m_soundOnly = nullptr;
  QComboBox* m_animationBox = nullptr;

  void addLanguages();
  void selectLanguage();

  void resetDialogs();

  void onEditCategories();
  void onResetDialogs();

};

#endif  // SETTINGSDIALOGGENERAL_H
