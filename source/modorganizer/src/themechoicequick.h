#ifndef MO2R_THEMECHOICEQUICK_H
#define MO2R_THEMECHOICEQUICK_H

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>

class QDialog;
class QWidget;

// MO2 Revamped: the first-start theme question in Qt Quick (qml/ThemeChoice.qml),
// used by FluentChoices::chooseTheme() when QuickUi is on. Same texts, same
// choices, same result as the Widgets tiles
class ThemeChoiceBridge : public QObject
{
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("made by chooseTheme()")

  Q_PROPERTY(QString heading MEMBER m_heading CONSTANT)
  Q_PROPERTY(QString description MEMBER m_description CONSTANT)
  Q_PROPERTY(QString acceptLabel MEMBER m_acceptLabel CONSTANT)
  // each theme: name and the colors of its little window
  Q_PROPERTY(QVariantList themes MEMBER m_themes CONSTANT)
  Q_PROPERTY(int chosen READ chosen WRITE choose NOTIFY chosenChanged)

public:
  ThemeChoiceBridge(QDialog* dialog, QString heading, QString description,
                    QString acceptLabel, QVariantList themes, int chosen);

  int chosen() const { return m_chosen; }
  void choose(int index);

  // Use this theme, or a double click on a tile
  Q_INVOKABLE void accept();
  Q_INVOKABLE void reject();

signals:
  void chosenChanged();

private:
  QDialog* m_dialog;
  QString m_heading, m_description, m_acceptLabel;
  QVariantList m_themes;
  int m_chosen;
};

namespace QuickUi
{
// asks the theme question in Qt Quick, with a tile for each of the themes (a
// map with "name" and the colors of its little window: background, card, line,
// text, muted, accent, onAccent); false when the view can't be made (then the
// Widgets tiles ask it). The index chosen is in result, nullopt when closed
bool chooseTheme(QWidget* parent, const QString& windowTitle, const QString& heading,
                 const QString& description, const QString& acceptLabel,
                 const QVariantList& themes, int current, std::optional<int>& result);
}  // namespace QuickUi

#endif  // MO2R_THEMECHOICEQUICK_H
