#ifndef MO2R_THEMEEDITORDIALOG_H
#define MO2R_THEMEEDITORDIALOG_H

#include "fluentdialogs.h"
#include "usertheme.h"

#include <QMap>
#include <QTimer>
#include <functional>

class QComboBox;
class QLineEdit;
class QPushButton;

// MO2 Revamped: makes or edits a theme of your own (see UserTheme): its name,
// the theme it goes on top of and its six colors, shown on MO2 as they change.
// Save writes the theme to the stylesheets folder; Cancel or closing puts the
// theme that was in use back.
class ThemeEditorDialog : public FluentContentDialog
{
  Q_OBJECT

public:
  // applies a theme by its file name in the stylesheets folder, as
  // Settings::styleChanged does
  using ApplyStyle = std::function<void(const QString&)>;

  // a new theme when existing is null
  ThemeEditorDialog(QWidget* parent, const QString& stylesheetsDir,
                    const QString& styleInUse, const UserTheme::Theme* existing,
                    ApplyStyle apply);
  ~ThemeEditorDialog() override;

  // the file the theme was saved to, once saved
  QString savedFileName() const { return m_saved; }

  void done(int result) override;

private:
  struct ColorRow
  {
    QPushButton* swatch = nullptr;
    QLineEdit* hex      = nullptr;
  };

  UserTheme::Theme theme() const;
  void setColor(const QString& key, const QColor& color);
  void pickColor(const QString& key);
  void baseChanged();
  void schedulePreview();
  void preview();
  void removePreviews();
  bool save();

  QString m_dir;
  QString m_styleInUse;
  QString m_existingFile;  // empty for a new theme
  QString m_rules;         // the theme's own rules, kept as they are
  ApplyStyle m_apply;

  QLineEdit* m_name    = nullptr;
  QComboBox* m_base    = nullptr;
  QMap<QString, ColorRow> m_rows;
  QMap<QString, QColor> m_colors;
  bool m_dark = true;

  QTimer m_previewTimer;
  int m_previewTurn  = 0;
  bool m_previewed   = false;
  QString m_saved;
};

#endif  // MO2R_THEMEEDITORDIALOG_H
