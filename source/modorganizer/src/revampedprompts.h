#pragma once

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <optional>

namespace MOBase::RevampedPrompts {

class PromptDialog final : public QDialog
{
public:
  PromptDialog(QWidget* parent, const QString& windowTitle,
               const QString& objectName, int minimumWidth)
      : QDialog(parent), m_layout(new QVBoxLayout(this))
  {
    setWindowTitle(windowTitle);
    setObjectName(objectName);
    setModal(true);
    setSizeGripEnabled(false);
    setMinimumWidth(minimumWidth);
    setAttribute(Qt::WA_StyledBackground, true);

    m_layout->setContentsMargins(16, 16, 16, 14);
    m_layout->setSpacing(12);
  }

  QVBoxLayout* contentLayout() const { return m_layout; }

  void addHeader(const QString& title, const QString& description)
  {
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("promptHeaderPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);

    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 12, 14, 12);
    panelLayout->setSpacing(5);

    auto* heading = new QLabel(title, panel);
    heading->setObjectName(QStringLiteral("promptHeading"));
    heading->setWordWrap(true);
    panelLayout->addWidget(heading);

    auto* detail = new QLabel(description, panel);
    detail->setObjectName(QStringLiteral("promptDescription"));
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::NoTextInteraction);
    panelLayout->addWidget(detail);

    m_layout->addWidget(panel);
  }

private:
  QVBoxLayout* m_layout;
};

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
  PromptDialog dialog(parent, windowTitle,
                      QStringLiteral("revampedChoicePrompt"), 480);
  dialog.addHeader(heading, description);

  QPushButton* preferred = nullptr;
  for (const auto& choice : choices) {
    auto* button = new QPushButton(choice.label, &dialog);
    button->setObjectName(QStringLiteral("promptChoiceButton"));
    button->setProperty("preferredChoice", choice.preferred);
    button->setProperty("secondaryChoice", !choice.preferred);
    button->setMinimumHeight(42);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int result = choice.result;
    QObject::connect(button, &QPushButton::clicked, &dialog,
                     [&dialog, result] { dialog.done(result); });
    dialog.contentLayout()->addWidget(button);
    if (choice.preferred && preferred == nullptr) {
      preferred = button;
    }
  }
  if (preferred != nullptr) {
    preferred->setDefault(true);
    preferred->setFocus();
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
  PromptDialog dialog(parent, windowTitle,
                      QStringLiteral("revampedErrorPrompt"), 480);
  dialog.addHeader(heading, description);

  auto* footer = new QHBoxLayout;
  footer->setContentsMargins(0, 0, 0, 0);
  footer->setSpacing(8);

  QPlainTextEdit* detailsView = nullptr;
  if (!details.isEmpty()) {
    auto* toggle = new QPushButton(detailsLabel, &dialog);
    toggle->setObjectName(QStringLiteral("promptDetailsButton"));
    toggle->setCheckable(true);
    footer->addWidget(toggle);

    detailsView = new QPlainTextEdit(details, &dialog);
    detailsView->setObjectName(QStringLiteral("promptDetails"));
    detailsView->setReadOnly(true);
    detailsView->setMinimumHeight(150);
    detailsView->setVisible(false);
    QObject::connect(toggle, &QPushButton::toggled, &dialog,
                     [&dialog, detailsView](bool shown) {
                       detailsView->setVisible(shown);
                       dialog.layout()->activate();
                       dialog.adjustSize();
                     });
  }

  footer->addStretch(1);
  auto* close = new QPushButton(closeLabel, &dialog);
  close->setObjectName(QStringLiteral("promptChoiceButton"));
  close->setProperty("preferredChoice", true);
  close->setMinimumHeight(34);
  close->setDefault(true);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  footer->addWidget(close);

  dialog.contentLayout()->addLayout(footer);
  if (detailsView != nullptr) {
    dialog.contentLayout()->addWidget(detailsView);
  }
  close->setFocus();
  // word-wrapped labels need the final width before the first show, otherwise
  // the initial geometry is below the layout minimum and Qt logs a warning
  dialog.layout()->activate();
  dialog.adjustSize();
  dialog.exec();
}

}  // namespace MOBase::RevampedPrompts
