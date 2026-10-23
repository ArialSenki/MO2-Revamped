#include "linktestdialog.h"

#include "fluentstyle.h"
#include "installinfo.h"

#include <nxmurl.h>

#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LinkTestDialog::LinkTestDialog(const std::list<HandlerInfo>& handlers,
                               const QString& registeredElsewhere, QWidget* parent)
    : QDialog(parent), m_Handlers(handlers), m_RegisteredElsewhere(registeredElsewhere)
{
  setObjectName(QStringLiteral("LinkTestDialog"));
  setWindowTitle(tr("Test a Nexus link"));
  setMinimumWidth(520);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("dialogContent"));
  content->setAttribute(Qt::WA_StyledBackground, true);
  auto* body = new QVBoxLayout(content);
  body->setContentsMargins(24, 20, 24, 24);
  body->setSpacing(6);

  auto* title = new QLabel(windowTitle(), content);
  title->setObjectName(QStringLiteral("pageTitle"));
  body->addWidget(title);
  auto* text = new QLabel(tr("See which program a Nexus link for a game would open in. "
                             "This is only a preview: nothing is downloaded and no "
                             "program is started."),
                          content);
  text->setObjectName(QStringLiteral("dialogText"));
  text->setWordWrap(true);
  body->addWidget(text);
  body->addSpacing(12);

  auto* gameLabel = new QLabel(tr("Game"), content);
  gameLabel->setObjectName(QStringLiteral("fieldLabel"));
  body->addWidget(gameLabel);
  m_Game = new QComboBox(content);
  m_Game->setEditable(true);
  m_Game->setInsertPolicy(QComboBox::NoInsert);

  // the known games, then every other game ID the list mentions
  QStringList ids;
  for (const auto& game : HandlerStorage::knownGames()) {
    if (std::get<1>(game) != QLatin1String("other")) {
      ids.append(std::get<1>(game));
    }
  }
  for (const HandlerInfo& info : m_Handlers) {
    for (const QString& game : info.games) {
      const QString id = game.trimmed().toLower();
      if (!id.isEmpty() && id != QLatin1String("other") && !ids.contains(id)) {
        ids.append(id);
      }
    }
  }
  for (const QString& id : ids) {
    m_Game->addItem(gameDisplayName(id), id);
  }
  body->addWidget(m_Game);
  m_Link = new QLabel(content);
  m_Link->setProperty("caption", true);
  m_Link->setTextInteractionFlags(Qt::TextSelectableByMouse);
  body->addWidget(m_Link);
  body->addSpacing(10);

  m_Result = new QFrame(content);
  Fluent::makeCard(m_Result);
  auto* result = new QVBoxLayout(m_Result);
  result->setContentsMargins(16, 12, 16, 14);
  result->setSpacing(2);
  m_ResultTitle = new QLabel(m_Result);
  m_ResultTitle->setProperty("caption", true);
  m_ResultValue = new QLabel(m_Result);
  m_ResultValue->setObjectName(QStringLiteral("resultValue"));
  m_ResultValue->setWordWrap(true);
  m_ResultPath = new QLabel(m_Result);
  m_ResultPath->setProperty("caption", true);
  m_ResultPath->setWordWrap(true);
  m_ResultPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_ResultReason = new QLabel(m_Result);
  m_ResultReason->setWordWrap(true);
  result->addWidget(m_ResultTitle);
  result->addWidget(m_ResultValue);
  result->addWidget(m_ResultPath);
  result->addSpacing(4);
  result->addWidget(m_ResultReason);
  body->addWidget(m_Result);

  m_Note = new QLabel(content);
  m_Note->setProperty("caption", true);
  m_Note->setWordWrap(true);
  body->addSpacing(4);
  body->addWidget(m_Note);
  body->addStretch(1);
  layout->addWidget(content, 1);

  auto* footer = new QFrame(this);
  footer->setObjectName(QStringLiteral("footer"));
  auto* buttons = new QHBoxLayout(footer);
  buttons->setContentsMargins(24, 20, 24, 20);
  buttons->addStretch(1);
  auto* close = new QPushButton(tr("Close"), footer);
  close->setProperty("primary", true);
  close->setDefault(true);
  close->setMinimumWidth(110);
  buttons->addWidget(close);
  layout->addWidget(footer);

  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_Game, &QComboBox::currentIndexChanged, this, &LinkTestDialog::update);
  connect(m_Game, &QComboBox::editTextChanged, this, &LinkTestDialog::update);

  update();
}

QString LinkTestDialog::currentGameId() const
{
  const QString text = m_Game->currentText().trimmed();
  for (int i = 0; i < m_Game->count(); ++i) {
    if (m_Game->itemText(i).compare(text, Qt::CaseInsensitive) == 0) {
      return m_Game->itemData(i).toString();
    }
  }
  return text.toLower();
}

void LinkTestDialog::update()
{
  const QString id   = currentGameId();
  const QString link = QStringLiteral("nxm://%1/mods/1/files/1").arg(id);

  // the same parsing and lookup as a real link (main.cpp)
  QString game;
  try {
    game = NXMUrl(link).game();
  } catch (const std::exception&) {
    game.clear();
  }

  m_Link->setText(tr("Test link: %1").arg(link));
  m_ResultTitle->setText(tr("This link would open in"));

  if (id.isEmpty() || game.isEmpty()) {
    m_ResultValue->setText(tr("Not a valid game ID"));
    m_ResultPath->setText(tr("Nexus game IDs use only letters and digits, for example "
                             "\"skyrimspecialedition\"."));
    m_ResultReason->clear();
    m_ResultReason->hide();
  } else {
    QStringList target           = HandlerStorage::resolveHandler(m_Handlers, game);
    const QString executable     = target.value(0);
    const QString arguments      = target.value(1);
    if (executable.isEmpty()) {
      m_ResultValue->setText(tr("No program"));
      m_ResultPath->setText(tr("Nothing in the list handles %1, and nothing handles "
                               "\"Other games\". Windows would show \"No handler found\".")
                                .arg(gameDisplayName(game)));
      m_ResultReason->hide();
    } else {
      const InstallInfo info = InstallInfo::describe(executable);
      QString value          = info.title();
      if (!info.layoutText().isEmpty()) {
        value += QStringLiteral(" · ") + info.layoutText();
      }
      m_ResultValue->setText(value);
      QString path = QDir::toNativeSeparators(executable);
      if (!arguments.trimmed().isEmpty()) {
        path += QLatin1Char(' ') + arguments;
      }
      m_ResultPath->setText(path);

      // was it picked for this game, or as the "Other games" fallback?
      bool direct = false;
      for (const HandlerInfo& entry : m_Handlers) {
        if (samePath(entry.executable, executable)) {
          for (const QString& g : entry.games) {
            if (g.compare(game, Qt::CaseInsensitive) == 0 ||
                gameDisplayName(g) == gameDisplayName(game)) {
              direct = true;
            }
          }
          break;
        }
      }
      QString reason = direct ? tr("It is the first program in the list that handles %1.")
                                    .arg(gameDisplayName(game))
                              : tr("No program handles %1 directly, so the first program "
                                   "for \"Other games\" gets it.")
                                    .arg(gameDisplayName(game));
      if (!info.exists) {
        reason += QLatin1Char(' ') + tr("This program no longer exists, so the link would fail.");
      }
      m_ResultReason->setText(reason);
      m_ResultReason->show();
    }
  }

  if (!m_RegisteredElsewhere.isEmpty()) {
    m_Note->setText(tr("Windows currently sends Nexus links to %1, not to this list. "
                       "Register this installation for the list to be used.")
                        .arg(m_RegisteredElsewhere));
    m_Note->show();
  } else {
    m_Note->hide();
  }
}
