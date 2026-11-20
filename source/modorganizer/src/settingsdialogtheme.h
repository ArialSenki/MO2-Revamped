#ifndef SETTINGSDIALOGTHEME_H
#define SETTINGSDIALOGTHEME_H

#include <QCheckBox>
#include <QComboBox>

#include "settings.h"
#include "settingsdialog.h"

class ThemeSettingsTab : public SettingsTab
{
public:
  ThemeSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update() override;

private:
  void addStyles();
  void selectStyle();
  void addThemeButtons();
  QString stylesheetsDir() const;
  // the interface size, grey text and animations, then the toolbar card
  void addDisplayCard();
  void loadToolbarCard();
  void updateDisplayAndToolbar();

  QComboBox* m_scaleBox     = nullptr;
  QCheckBox* m_greyText     = nullptr;
  QComboBox* m_animationBox = nullptr;
};

#endif  // SETTINGSDIALOGGENERAL_H
