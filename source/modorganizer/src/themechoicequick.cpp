#include "themechoicequick.h"

#include "quickui.h"

#include <QDialog>
#include <QQuickWidget>
#include <QVBoxLayout>

ThemeChoiceBridge::ThemeChoiceBridge(QDialog* dialog, QString heading, QString description,
                                     QString acceptLabel, QVariantList themes, int chosen)
    : QObject(dialog), m_dialog(dialog), m_heading(std::move(heading)),
      m_description(std::move(description)), m_acceptLabel(std::move(acceptLabel)),
      m_themes(std::move(themes)), m_chosen(chosen)
{}

void ThemeChoiceBridge::choose(int index)
{
  if (index < 0 || index >= m_themes.size() || index == m_chosen) {
    return;
  }
  m_chosen = index;
  emit chosenChanged();
}

void ThemeChoiceBridge::accept()
{
  m_dialog->accept();
}

void ThemeChoiceBridge::reject()
{
  m_dialog->reject();
}

namespace QuickUi
{

bool chooseTheme(QWidget* parent, const QString& windowTitle, const QString& heading,
                 const QString& description, const QString& acceptLabel,
                 const QVariantList& themes, int current, std::optional<int>& result)
{
  if (!enabled()) {
    return false;
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("ThemeChoiceDialog"));
  dialog.setWindowTitle(windowTitle);

  auto* bridge = new ThemeChoiceBridge(&dialog, heading, description, acceptLabel, themes,
                                       current);
  auto* view   = createView(&dialog, QStringLiteral("ThemeChoice"),
                            {{QStringLiteral("bridge"), QVariant::fromValue(bridge)}});
  if (view == nullptr) {
    return false;
  }

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(view);
  // the size the QML asks for (its implicit size)
  const QSize wanted = view->initialSize();
  dialog.resize(wanted.isValid() && !wanted.isEmpty() ? wanted : QSize(560, 400));
  dialog.setMinimumSize(dialog.size());
  view->setFocus();

  if (dialog.exec() != QDialog::Accepted) {
    result.reset();
  } else {
    result = bridge->chosen();
  }
  return true;
}

}  // namespace QuickUi
