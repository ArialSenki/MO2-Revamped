#pragma once

#include "fluentdialogs.h"

#include <QList>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace MOBase::RevampedPrompts {

struct Choice
{
  QString label;
  int result;
  bool preferred = false;
};

// A question with stacked choice buttons; returns the chosen result or
// `dismissed` when the window is closed.
inline int choose(QWidget* parent, const QString& windowTitle,
                  const QString& heading, const QString& description,
                  const QList<Choice>& choices, int dismissed)
{
  FluentContentDialog dialog(parent, QStringLiteral("revampedChoicePrompt"), heading,
                             description);
  dialog.setWindowTitle(windowTitle);

  for (const auto& choice : choices) {
    auto* button = dialog.addButton(choice.label, choice.preferred);
    const int result = choice.result;
    QObject::connect(button, &QPushButton::clicked, &dialog,
                     [&dialog, result] { dialog.done(result); });
  }

  const int result = dialog.exec();
  for (const auto& choice : choices) {
    if (choice.result == result) {
      return result;
    }
  }
  return dismissed;
}

// An error with an optional collapsible technical details section.
inline void showError(QWidget* parent, const QString& windowTitle,
                      const QString& heading, const QString& description,
                      const QString& details, const QString& detailsLabel,
                      const QString& closeLabel)
{
  FluentContentDialog dialog(parent, QStringLiteral("revampedErrorPrompt"), heading,
                             description);
  dialog.setWindowTitle(windowTitle);

  if (!details.isEmpty()) {
    auto* detailsView = new QPlainTextEdit(details, &dialog);
    detailsView->setObjectName(QStringLiteral("promptDetails"));
    detailsView->setReadOnly(true);
    detailsView->setMinimumHeight(150);
    detailsView->setVisible(false);
    dialog.body()->addWidget(detailsView);

    auto* toggle = dialog.addButton(detailsLabel);
    toggle->setCheckable(true);
    QObject::connect(toggle, &QPushButton::toggled, &dialog,
                     [&dialog, detailsView](bool shown) {
                       detailsView->setVisible(shown);
                       dialog.layout()->activate();
                       dialog.adjustSize();
                     });
  }

  auto* close = dialog.addButton(closeLabel, true);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  close->setFocus();
  dialog.exec();
}

}  // namespace MOBase::RevampedPrompts
