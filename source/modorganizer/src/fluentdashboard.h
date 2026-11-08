#ifndef FLUENTDASHBOARD_H
#define FLUENTDASHBOARD_H

// Fluent v4 "game first" header of the main window: a bar with the game,
// the profile and program pickers and the Run button, and a
// row of summary tiles (active mods, conflicts, Overwrite, updates) above the
// cards. The pickers and Run are the existing widgets, moved here, so code
// and plugins that look them up by name keep working.

#include <QDir>
#include <QFrame>
#include <QPixmap>
#include <QTimer>

#include <functional>

class OrganizerCore;
class ModListView;
class QBoxLayout;
class QComboBox;
class QLabel;
class QPushButton;

namespace Ui
{
class MainWindow;
}

// the game bar above the summary row
class FluentHeroBanner : public QFrame
{
public:
  explicit FluentHeroBanner(QWidget* parent = nullptr);
};

class FluentStatTile : public QFrame
{
public:
  FluentStatTile(char16_t glyph, const QString& caption, QWidget* parent = nullptr);

  // tone is "accent", "caution", "critical" or "neutral"; the stylesheet
  // colors the glyph badge from it
  void setValue(const QString& value, const QString& tone);
  void setCaption(const QString& caption);
  QPushButton* addAction(const QString& text);

private:
  QLabel* m_badge;
  QLabel* m_caption;
  QLabel* m_value;
  QBoxLayout* m_layout;
};

class FluentDashboard : public QObject
{
  Q_OBJECT

public:
  FluentDashboard(Ui::MainWindow* ui, OrganizerCore& core, QWidget* window);

  // reloads the artwork and title for the managed game
  void refreshGame();

  // recounts the summary tiles soon (calls are coalesced)
  void scheduleRefresh();

  // where the current profile keeps its saves, for "last played"
  void setSavesDirectory(std::function<QDir()> provider);

  // the texts again after a language change, and the main window's own that
  // retranslateUi() put back
  void retranslate();

private:
  Ui::MainWindow* ui;
  OrganizerCore& m_core;
  FluentHeroBanner* m_banner;
  QLabel* m_logo;
  QLabel* m_title;
  QLabel* m_lastPlayed;
  std::function<QDir()> m_savesDirectory;
  FluentStatTile* m_active;
  FluentStatTile* m_conflicts;
  FluentStatTile* m_overwrite;
  FluentStatTile* m_updates;
  QPushButton* m_overwriteAction;
  QPushButton* m_conflictsAction;
  QPushButton* m_updatesAction;
  QList<QPushButton*> m_chips;
  QLabel* m_profileLabel = nullptr;
  QLabel* m_programLabel = nullptr;
  QLabel* m_modsTitle    = nullptr;
  QTimer m_refreshTimer;

  void setupBanner(QWidget* central);
  void setupStats(QWidget* central);
  void setupModsHeader();
  void applyTabTexts();
  void refreshStats();
  void refreshLastPlayed();
  void refreshDisk();
  void onQuickFilterChanged(int specialCategory);
};

#endif  // FLUENTDASHBOARD_H
