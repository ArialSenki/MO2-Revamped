#include "settingsdialognexus.h"
#include "circularuihelpers.h"
#include "fluenticons.h"
#include "log.h"
#include "messagedialog.h"

#include <QFormLayout>
#include <QMessageBox>
#include "nexusinterface.h"
#include "nexusoauth.h"
#include "revampedversion.h"
#include "serverinfo.h"
#include "shared/util.h"
#include "ui_nexusmanualkey.h"
#include "ui_settingsdialog.h"
#include <utility.h>

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QAbstractScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{

QLayout* findContainingLayout(QLayout* layout, QWidget* widget)
{
  if (!layout || !widget) {
    return nullptr;
  }

  for (int index = 0; index < layout->count(); ++index) {
    QLayoutItem* item = layout->itemAt(index);
    if (item->widget() == widget) {
      return layout;
    }
    if (QLayout* nested = item->layout()) {
      if (QLayout* containing = findContainingLayout(nested, widget)) {
        return containing;
      }
    }
  }
  return nullptr;
}

QWidget* wrapNexusGroupInCard(QGroupBox* group, const QString& cardObjectName,
                              const QString& headingObjectName,
                              const QString& iconPath,
                              const QString& iconObjectName,
                              const QString& fallback)
{
  if (!group || !group->parentWidget() || !group->parentWidget()->layout()) {
    return group;
  }

  QLayout* rootLayout = group->parentWidget()->layout();
  QLayout* parentLayout = findContainingLayout(rootLayout, group);
  if (!parentLayout) {
    return group;
  }
  QWidget* card = new QWidget(group->parentWidget());
  card->setObjectName(cardObjectName);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  auto* cardLayout = new QVBoxLayout(card);
  // the inset of the other Settings pages' cards
  cardLayout->setContentsMargins(20, 22, 20, 18);
  cardLayout->setSpacing(6);

  auto* heading = new QLabel(group->title(), card);
  heading->setObjectName(headingObjectName);
  cardLayout->addWidget(heading);
  MOBase::CircularUi::addIconToHeading(heading, iconPath, iconObjectName, 26,
                                       fallback);

  const QString title = group->title();
  group->setTitle(QString());
  if (QLayout* contentLayout = group->layout()) {
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
  }

  QLayoutItem* oldItem = parentLayout->replaceWidget(group, card);
  if (!oldItem) {
    group->setTitle(title);
    delete card;
    return group;
  }
  delete oldItem;

  cardLayout->addWidget(group);
  // Keep content at the top when a side-by-side card is taller.
  cardLayout->addStretch(1);
  if (auto* boxLayout = qobject_cast<QBoxLayout*>(parentLayout);
      boxLayout && boxLayout->direction() == QBoxLayout::LeftToRight) {
    const int index = boxLayout->indexOf(card);
    if (index >= 0) {
      boxLayout->setStretch(index, 1);
    }
  }
  return card;
}

// MO2 Revamped: the Nexus sign-in (OAuth), or null before the Nexus
// interface exists (the instance wizard of a first start)
NexusOAuth* sharedOAuth()
{
  return NexusInterface::exists() ? NexusInterface::instance().getAccessManager()->oauth()
                                  : nullptr;
}

}  // namespace

template <typename T>
class ServerItem : public QListWidgetItem
{
public:
  ServerItem(const QString& text, int sortRole = Qt::DisplayRole,
             QListWidget* parent = 0, int type = Type)
      : QListWidgetItem(text, parent, type), m_SortRole(sortRole)
  {}

  virtual bool operator<(const QListWidgetItem& other) const
  {
    return this->data(m_SortRole).value<T>() < other.data(m_SortRole).value<T>();
  }

private:
  int m_SortRole;
};

class NexusManualKeyDialog : public QDialog
{
public:
  NexusManualKeyDialog(QWidget* parent)
      : QDialog(parent), ui(new Ui::NexusManualKeyDialog)
  {
    ui->setupUi(this);
    ui->key->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    connect(ui->openBrowser, &QPushButton::clicked, [&] {
      openBrowser();
    });
    connect(ui->paste, &QPushButton::clicked, [&] {
      paste();
    });
    connect(ui->clear, &QPushButton::clicked, [&] {
      clear();
    });

    // disconnecting is the Disconnect button's job
    auto updateOk = [this] {
      ui->buttonBox->button(QDialogButtonBox::Ok)
          ->setEnabled(!ui->key->toPlainText().trimmed().isEmpty());
    };
    connect(ui->key, &QPlainTextEdit::textChanged, this, updateOk);
    updateOk();
  }

  void accept() override
  {
    // a key has no spaces: a pasted one can bring a line break or a space along
    m_key = ui->key->toPlainText().simplified().remove(QLatin1Char(' '));
    QDialog::accept();
  }

  const QString& key() const { return m_key; }

  void openBrowser()
  {
    shell::Open(QUrl("https://www.nexusmods.com/users/myaccount?tab=api"));
  }

  void paste()
  {
    const auto text = QApplication::clipboard()->text();
    if (!text.isEmpty()) {
      ui->key->setPlainText(text);
    }
  }

  void clear() { ui->key->clear(); }

private:
  std::unique_ptr<Ui::NexusManualKeyDialog> ui;
  QString m_key;
};

NexusConnectionUI::NexusConnectionUI(QWidget* parent, Settings* s,
                                     QAbstractButton* connectButton,
                                     QAbstractButton* disconnectButton,
                                     QAbstractButton* manualButton,
                                     QListWidget* logList)
    : m_parent(parent), m_settings(s), m_connect(connectButton),
      m_disconnect(disconnectButton), m_manual(manualButton), m_log(logList)
{
  // MO2 Revamped: Nexus' sign-in page asks for access in the name of the
  // registered application; until Nexus registers MO2 Revamped's, it would be
  // the original Mod Organizer 2's, so only the personal API key is offered
  if (m_connect && QStringLiteral(MO2R_NEXUS_SSO_SLUG).isEmpty()) {
    m_connect->hide();
    m_connect = nullptr;
  }

  if (m_connect) {
    QObject::connect(m_connect, &QPushButton::clicked, [&] {
      connect();
    });
  }

  if (m_disconnect) {
    QObject::connect(m_disconnect, &QPushButton::clicked, [&] {
      disconnect();
    });
  }

  if (m_manual) {
    QObject::connect(manualButton, &QPushButton::clicked, [&] {
      manual();
    });
  }

  // MO2 Revamped: "Sign in with Nexus Mods" beside "Enter API key manually",
  // only when this build has an OAuth client id; without one the page stays
  // as it was
  if (m_manual && NexusOAuth::available() && sharedOAuth() != nullptr) {
    QWidget* holder = m_manual->parentWidget();
    auto* box = holder ? qobject_cast<QBoxLayout*>(
                             findContainingLayout(holder->layout(), m_manual))
                       : nullptr;
    if (box) {
      auto* button = new QPushButton(tr("Sign in with Nexus Mods"), holder);
      button->setObjectName(QStringLiteral("nexusOAuthSignIn"));
      button->setSizePolicy(m_manual->sizePolicy());
      button->setMinimumSize(m_manual->minimumSize());
      button->setToolTip(tr("Opens Nexus in your browser to allow MO2 Revamped, "
                            "without copying an API key."));
      box->insertWidget(box->indexOf(m_manual), button);
      m_signIn = button;

      QObject::connect(button, &QPushButton::clicked, this, [this] {
        signIn();
      });

      NexusOAuth* oauth = sharedOAuth();
      QObject::connect(oauth, &NexusOAuth::signInProgress, this,
                       [this](const QString& message) {
                         if (m_signInStarted) {
                           addLog(message);
                         }
                       });
      QObject::connect(oauth, &NexusOAuth::signInFinished, this,
                       [this](bool ok, const QString& message) {
                         onSignInFinished(ok, message);
                       });
      QObject::connect(oauth, &NexusOAuth::sessionChanged, this, [this] {
        updateState();
      });
    }
  }

  if (GlobalSettings::hasNexusCredentials()) {
    addLog(tr("Connected."));
  } else {
    addLog(tr("Not connected."));
  }

  updateState();
}

void NexusConnectionUI::connect()
{
  if (m_nexusLogin && m_nexusLogin->isActive()) {
    m_nexusLogin->cancel();
    return;
  }

  if (!m_nexusLogin) {
    m_nexusLogin.reset(new NexusSSOLogin);

    m_nexusLogin->keyChanged = [&](auto&& s) {
      onSSOKeyChanged(s);
    };

    m_nexusLogin->stateChanged = [&](auto&& s, auto&& e) {
      onSSOStateChanged(s, e);
    };
  }

  m_log->clear();
  m_nexusLogin->start();
  updateState();
}

void NexusConnectionUI::manual()
{
  if (m_nexusValidator && m_nexusValidator->isActive()) {
    m_nexusValidator->cancel();
    return;
  }

  NexusManualKeyDialog d(m_parent);
  if (d.exec() != QDialog::Accepted) {
    return;
  }

  m_log->clear();
  validateKey(d.key());
}

NexusConnectionUI::~NexusConnectionUI()
{
  // closing Settings (or the wizard) while the browser sign-in waits: nothing
  // would check its account or show its result, and the listener would stay
  // open for minutes, so it is cancelled
  if (m_signInStarted) {
    NexusOAuth* oauth = sharedOAuth();
    if (oauth && oauth->signInActive()) {
      // its signals no longer reach this page (its widgets may be gone)
      QObject::disconnect(oauth, nullptr, this, nullptr);
      m_signInStarted = false;
      oauth->cancelSignIn();
    }
  }
}

void NexusConnectionUI::signIn()
{
  NexusOAuth* oauth = sharedOAuth();
  if (!oauth) {
    return;
  }

  if (oauth->signInActive()) {
    oauth->cancelSignIn();
    return;
  }

  m_log->clear();
  m_signInStarted = true;
  oauth->signIn();
  updateState();
}

void NexusConnectionUI::onSignInFinished(bool ok, const QString& message)
{
  if (!m_signInStarted) {
    updateState();
    return;
  }
  m_signInStarted = false;

  if (!ok) {
    addLog(message);
    updateState();
    return;
  }

  // the account behind the sign-in, checked as an API key is: its name and
  // whether it is Premium
  if (!m_nexusValidator) {
    m_nexusValidator.reset(new NexusKeyValidator(
        m_settings, *NexusInterface::instance().getAccessManager()));

    m_nexusValidator->finished = [&](auto&& r, auto&& m, auto&& u) {
      onValidatorFinished(r, m, u);
    };
  }

  addLog(tr("Checking the Nexus account..."));
  m_checkingSignIn = true;
  m_nexusValidator->startOAuth(NexusKeyValidator::OneShot);
  updateState();
}

void NexusConnectionUI::disconnect()
{
  clearKey();
  m_log->clear();
  addLog(tr("Disconnected."));
}

void NexusConnectionUI::validateKey(const QString& key)
{
  if (!m_nexusValidator) {
    m_nexusValidator.reset(new NexusKeyValidator(
        m_settings, *NexusInterface::instance().getAccessManager()));

    m_nexusValidator->finished = [&](auto&& r, auto&& m, auto&& u) {
      onValidatorFinished(r, m, u);
    };
  }

  addLog(tr("Checking API key..."));
  m_checkingSignIn = false;
  m_nexusValidator->start(key, NexusKeyValidator::OneShot);
}

void NexusConnectionUI::onSSOKeyChanged(const QString& key)
{
  if (key.isEmpty()) {
    clearKey();
  } else {
    addLog(tr("Received API key."));
    validateKey(key);
  }
}

void NexusConnectionUI::onSSOStateChanged(NexusSSOLogin::States s, const QString& e)
{
  if (s != NexusSSOLogin::Finished) {
    // finished state is handled in onSSOKeyChanged()
    const auto log = NexusSSOLogin::stateToString(s, e);

    for (auto&& line : log.split("\n")) {
      addLog(line);
    }
  }

  updateState();
}

void NexusConnectionUI::onValidatorFinished(ValidationAttempt::Result r,
                                            const QString& message,
                                            std::optional<APIUserAccount> user)
{
  const bool fromSignIn = m_checkingSignIn;
  m_checkingSignIn      = false;
  NexusOAuth* oauth     = sharedOAuth();

  if (user && fromSignIn) {
    // MO2 Revamped: one way to Nexus at a time: the sign-in replaces a stored
    // API key
    GlobalSettings::clearNexusApiKey();
    NexusInterface::instance().getAccessManager()->setLinkedAccount(*user);
    addLog(tr("Signed in to Nexus Mods as %1.").arg(user->name()));
    updateState();
    emit keyChanged();
    return;
  }

  if (user) {
    // a key entered by hand replaces a Nexus sign-in
    if (oauth && oauth->hasSession()) {
      oauth->signOut();
    }

    // applies at once: the Nexus requests, the status bar and the window title
    // all follow this signal, so linking needs no restart
    NexusInterface::instance().getAccessManager()->setLinkedAccount(*user);
    addLog(tr("Received user account information"));

    if (setKey(user->apiKey())) {
      addLog(tr("Linked with Nexus successfully."));
    } else {
      addLog(tr("Failed to set API key"));
    }
  } else {
    if (oauth && fromSignIn && r != ValidationAttempt::Cancelled) {
      // the tokens of an account that couldn't be read aren't kept
      oauth->signOut();
    }
    if (message.isEmpty()) {
      // shouldn't happen
      addLog(tr("Unknown error"));
    } else {
      addLog(message);
    }
  }

  updateState();
}

void NexusConnectionUI::addLog(const QString& s)
{
  m_log->addItem(s);
  m_log->scrollToBottom();
}

bool NexusConnectionUI::setKey(const QString& key)
{
  const bool ret = GlobalSettings::setNexusApiKey(key);
  updateState();

  emit keyChanged();

  return ret;
}

bool NexusConnectionUI::clearKey()
{
  const auto ret = GlobalSettings::clearNexusApiKey();

  // MO2 Revamped: Disconnect also ends a Nexus sign-in (OAuth)
  NexusOAuth* oauth = sharedOAuth();
  if (oauth && (oauth->hasSession() || oauth->signInActive())) {
    oauth->signOut();
  }

  NexusInterface::instance().getAccessManager()->clearApiKey();
  updateState();

  emit keyChanged();

  return ret;
}

void NexusConnectionUI::updateState()
{
  auto setButton = [&](QAbstractButton* b, bool enabled, QString caption = {}) {
    if (b) {
      b->setEnabled(enabled);
      if (!caption.isEmpty()) {
        b->setText(caption);
      }
    }
  };

  const NexusOAuth* oauth = sharedOAuth();
  const QString signInText = tr("Sign in with Nexus Mods");

  if (oauth && oauth->signInActive()) {
    // MO2 Revamped: waiting for the browser's sign-in
    setButton(m_signIn, m_signInStarted, QObject::tr("Cancel"));
    setButton(m_connect, false, QObject::tr("Connect to Nexus"));
    setButton(m_disconnect, false);
    setButton(m_manual, false, QObject::tr("Enter API key manually"));
  } else if (m_nexusLogin && m_nexusLogin->isActive()) {
    // api key is in the process of being retrieved
    setButton(m_connect, true, QObject::tr("Cancel"));
    setButton(m_disconnect, false);
    setButton(m_manual, false, QObject::tr("Enter API key manually"));
    setButton(m_signIn, false, signInText);
  } else if (m_nexusValidator && m_nexusValidator->isActive()) {
    // api key is in the process of being tested
    setButton(m_connect, false, QObject::tr("Connect to Nexus"));
    setButton(m_disconnect, false);
    setButton(m_manual, true, QObject::tr("Cancel"));
    setButton(m_signIn, false, signInText);
  } else if (GlobalSettings::hasNexusCredentials()) {
    // api key or sign-in present
    setButton(m_connect, false, QObject::tr("Connect to Nexus"));
    setButton(m_disconnect, true);
    setButton(m_manual, false, QObject::tr("Enter API key manually"));
    setButton(m_signIn, false, signInText);
  } else {
    // api key not present
    setButton(m_connect, true, QObject::tr("Connect to Nexus"));
    setButton(m_disconnect, false);
    setButton(m_manual, true, QObject::tr("Enter API key manually"));
    setButton(m_signIn, true, signInText);
  }

  emit stateChanged();
}

NexusSettingsTab::NexusSettingsTab(Settings& s, SettingsDialog& d) : SettingsTab(s, d)
{
  MOBase::CircularUi::addIconToHeading(
      ui->nexusPageTitle, ":/MO/gui/mainwindow/mod-page.svg",
      QStringLiteral("nexusPageIcon"), 36, QStringLiteral("N"));

  if (auto* pageLayout = qobject_cast<QVBoxLayout*>(
          ui->nexusScrollAreaWidgetContents->layout())) {
    // the title starts at the page's own margin, like the other Settings pages
    pageLayout->setContentsMargins(0, 0, 0, 8);
    pageLayout->setSpacing(10);
  }

  QWidget* accountCard = wrapNexusGroupInCard(
      ui->nexusAccountGroup, QStringLiteral("nexusAccountCard"),
      QStringLiteral("nexusAccountTitle"),
      QStringLiteral(":/MO/gui/mainwindow/profile.svg"),
      QStringLiteral("nexusAccountIcon"), QStringLiteral("A"));
  wrapNexusGroupInCard(
      ui->nexusUsageGroup, QStringLiteral("nexusUsageCard"),
      QStringLiteral("nexusUsageTitle"),
      QStringLiteral(":/MO/gui/mainwindow/github-updates.svg"),
      QStringLiteral("nexusUsageIcon"), QStringLiteral("U"));
  // the two lists of values: one column for the values, with room after the
  // longest label
  for (QWidget* group : {static_cast<QWidget*>(ui->nexusAccountGroup),
                         static_cast<QWidget*>(ui->nexusUsageGroup)}) {
    for (auto* form : group->findChildren<QFormLayout*>()) {
      form->setHorizontalSpacing(20);
      form->setVerticalSpacing(8);
      for (int row = 0; row < form->rowCount(); ++row) {
        if (QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole)) {
          if (item->widget() != nullptr) {
            item->widget()->setMinimumWidth(120);
          }
        }
      }
    }
  }
  wrapNexusGroupInCard(
      ui->nexusConnectionGroup, QStringLiteral("nexusConnectionCard"),
      QStringLiteral("nexusConnectionTitle"),
      QStringLiteral(":/MO/gui/mainwindow/mod-page.svg"),
      QStringLiteral("nexusConnectionIcon"), QStringLiteral("C"));
  wrapNexusGroupInCard(
      ui->nexusOptionsGroup, QStringLiteral("nexusOptionsCard"),
      QStringLiteral("nexusOptionsTitle"),
      QStringLiteral(":/MO/gui/mainwindow/tools.svg"),
      QStringLiteral("nexusOptionsIcon"), QStringLiteral("M"));
  wrapNexusGroupInCard(
      ui->nexusServersGroup, QStringLiteral("nexusServersCard"),
      QStringLiteral("nexusServersTitle"),
      QStringLiteral(":/MO/gui/mainwindow/install.svg"),
      QStringLiteral("nexusServersIcon"), QStringLiteral("S"));

  // the account's and the usage's lines are rows, as in every other card
  ui->nexusAccountGroup->setProperty("fluentValueRows", true);
  ui->nexusUsageGroup->setProperty("fluentValueRows", true);

  if (accountCard) {
    auto* accountRow = qobject_cast<QHBoxLayout*>(findContainingLayout(
        ui->nexusScrollAreaWidgetContents->layout(), accountCard));
    if (accountRow) {
      accountRow->setContentsMargins(0, 0, 0, 0);
      accountRow->setSpacing(10);
      accountRow->setStretch(0, 1);
      accountRow->setStretch(1, 1);
    }
  }

  if (auto* connectionLayout =
          qobject_cast<QHBoxLayout*>(ui->nexusConnectionGroup->layout())) {
    connectionLayout->setContentsMargins(0, 0, 0, 0);
    // with the inset of each column's content, 12 between them
    connectionLayout->setSpacing(4);
    connectionLayout->setStretch(0, 3);
    connectionLayout->setStretch(1, 5);
  }
  if (auto* actionsLayout =
          qobject_cast<QVBoxLayout*>(ui->nexusConnectionActions->layout())) {
    actionsLayout->setSpacing(8);
  }
  if (auto* activityLayout =
          qobject_cast<QVBoxLayout*>(ui->nexusActivityPanel->layout())) {
    activityLayout->setSpacing(8);
    activityLayout->setStretch(1, 1);
  }
  // one column, as the other pages: the switches end at the card's edge, with
  // the two tools side by side below them
  if (auto* optionsLayout =
          qobject_cast<QHBoxLayout*>(ui->nexusOptionsGroup->layout())) {
    optionsLayout->setDirection(QBoxLayout::TopToBottom);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(14);
  }
  if (auto* featuresLayout =
          qobject_cast<QVBoxLayout*>(ui->nexusFeatureOptions->layout())) {
    featuresLayout->setContentsMargins(0, 0, 0, 0);
    featuresLayout->setSpacing(0);
  }
  if (auto* toolsLayout =
          qobject_cast<QVBoxLayout*>(ui->nexusIntegrationTools->layout())) {
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(8);
    toolsLayout->removeWidget(ui->associateButton);
    toolsLayout->removeWidget(ui->clearCacheButton);
    auto* buttons = new QHBoxLayout;
    // a gap of 12, as between the columns of the blocks around it
    buttons->setSpacing(12);
    buttons->setProperty("fluentKeepSpacing", true);
    buttons->addWidget(ui->associateButton, 1);
    buttons->addWidget(ui->clearCacheButton, 1);
    toolsLayout->addLayout(buttons);
  }
  if (auto* connectionActions = ui->nexusConnectionActions->layout()) {
    connectionActions->setContentsMargins(0, 0, 0, 0);
  }
  if (auto* activity = ui->nexusActivityPanel->layout()) {
    activity->setContentsMargins(0, 0, 0, 0);
  }
  if (auto* serversLayout =
          qobject_cast<QHBoxLayout*>(ui->nexusServersGroup->layout())) {
    serversLayout->setContentsMargins(0, 0, 0, 0);
    serversLayout->setSpacing(4);
    serversLayout->setStretch(0, 1);
    serversLayout->setStretch(1, 1);
  }

  // the account and usage cards stack: their values start in one column
  {
    const QList<QLabel*> labels = {ui->label_2, ui->label_3, ui->label_11, ui->label_30,
                                   ui->label_31};
    int width = 0;
    for (QLabel* label : labels) {
      width = std::max(width, label->sizeHint().width());
    }
    for (QLabel* label : labels) {
      label->setMinimumWidth(width);
    }
  }

  ui->nexusActivityPanel->setMinimumWidth(0);
  ui->nexusConnectionActions->setMinimumWidth(0);
  // the log takes the height of the actions beside it
  ui->nexusLog->setMinimumHeight(0);
  ui->nexusLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
  ui->nexusLog->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
  ui->knownServersList->setMinimumHeight(150);
  ui->preferredServersList->setMinimumHeight(150);
  ui->knownServersList->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);
  ui->preferredServersList->setSizePolicy(QSizePolicy::Expanding,
                                          QSizePolicy::Expanding);
  ui->label_18->setMinimumHeight(24);
  ui->label_21->setMinimumHeight(24);

  ui->endorsementBox->setChecked(settings().nexus().endorsementIntegration());
  ui->trackedBox->setChecked(settings().nexus().trackedIntegration());
  ui->categoryMappingsBox->setChecked(settings().nexus().categoryMappings());
  ui->hideAPICounterBox->setChecked(settings().interface().hideAPICounter());

  // display server preferences
  for (const auto& server : s.network().servers()) {
    QString descriptor = server.name();

    if (!descriptor.compare("CDN", Qt::CaseInsensitive)) {
      //: %1 is the server's name
      descriptor = QObject::tr("%1 (automatic)").arg(descriptor);
    }

    const auto averageSpeed = server.averageSpeed();
    if (averageSpeed > 0) {
      descriptor += QString(" (%1)").arg(MOBase::localizedByteSpeed(averageSpeed));
    }

    QListWidgetItem* newItem = new ServerItem<int>(descriptor, Qt::UserRole + 1);

    newItem->setData(Qt::UserRole, server.name());
    newItem->setData(Qt::UserRole + 1, server.preferred());

    if (server.preferred() > 0) {
      ui->preferredServersList->addItem(newItem);
    } else {
      ui->knownServersList->addItem(newItem);
    }

    ui->preferredServersList->sortItems(Qt::DescendingOrder);
  }

  m_connectionUI.reset(new NexusConnectionUI(&dialog(), &settings(), ui->nexusConnect,
                                             ui->nexusDisconnect, ui->nexusManualKey,
                                             ui->nexusLog));
  if (QStringLiteral(MO2R_NEXUS_SSO_SLUG).isEmpty() && NexusOAuth::available()) {
    ui->nexusConnectionDescription->setText(
        QObject::tr("Sign in with Nexus Mods in your browser, or connect with the "
                    "personal API key from your Nexus account page. Connection "
                    "messages appear beside these actions."));
  } else if (QStringLiteral(MO2R_NEXUS_SSO_SLUG).isEmpty()) {
    ui->nexusConnectionDescription->setText(
        QObject::tr("Connect with the personal API key from your Nexus account page. "
                    "Connection messages appear beside these actions."));
  }

  QObject::connect(
      m_connectionUI.get(), &NexusConnectionUI::stateChanged, &d,
      [&] {
        updateNexusData();
      },
      Qt::QueuedConnection);

  QObject::connect(ui->clearCacheButton, &QPushButton::clicked, [&] {
    clearCache();
  });
  QObject::connect(ui->associateButton, &QPushButton::clicked, [&] {
    associate();
  });

  updateNexusData();
}

void NexusSettingsTab::update()
{
  settings().nexus().setEndorsementIntegration(ui->endorsementBox->isChecked());
  settings().nexus().setTrackedIntegration(ui->trackedBox->isChecked());
  settings().nexus().setCategoryMappings(ui->categoryMappingsBox->isChecked());
  settings().interface().setHideAPICounter(ui->hideAPICounterBox->isChecked());

  auto servers = settings().network().servers();

  // store server preference
  for (int i = 0; i < ui->knownServersList->count(); ++i) {
    const QString key = ui->knownServersList->item(i)->data(Qt::UserRole).toString();

    bool found = false;

    for (auto& server : servers) {
      if (server.name() == key) {
        server.setPreferred(0);
        found = true;
        break;
      }
    }

    if (!found) {
      log::error("while setting preferred to 0, server '{}' not found", key);
    }
  }

  const int count = ui->preferredServersList->count();

  for (int i = 0; i < count; ++i) {
    const QString key =
        ui->preferredServersList->item(i)->data(Qt::UserRole).toString();
    const int newPreferred = count - i;

    bool found = false;

    for (auto& server : servers) {

      if (server.name() == key) {
        server.setPreferred(newPreferred);
        found = true;
        break;
      }
    }

    if (!found) {
      log::error("while setting preference to {}, server '{}' not found", newPreferred,
                 key);
    }
  }

  settings().network().updateServers(servers);
}

void NexusSettingsTab::clearCache()
{
  auto& paths         = Settings::instance().paths();
  const QString cache = paths.cache();
  const auto key      = [](const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower() + QLatin1Char('/');
  };

  // Paths accepts any folder for the cache: never one holding the instance's data
  bool holdsData = false;
  for (const QString& folder :
       {paths.base(), paths.mods(), paths.downloads(), paths.profiles(), paths.overwrite()}) {
    holdsData = holdsData || key(folder).startsWith(key(cache));
  }
  if (holdsData) {
    QMessageBox::warning(parentWidget(), QObject::tr("Clear Nexus cache"),
                         QObject::tr("The cache folder also holds this instance's mods or "
                                     "profiles, so only the Nexus data in memory was "
                                     "cleared:\n%1")
                             .arg(QDir::toNativeSeparators(cache)));
    NexusInterface::instance().clearCache();
    return;
  }

  // Paths accepts any folder, so what is in it goes to the Recycle Bin, where a
  // folder chosen by mistake can be brought back
  if (QMessageBox::question(parentWidget(), QObject::tr("Clear Nexus cache"),
                            QObject::tr("Move everything in this folder to the Recycle "
                                        "Bin?\n%1")
                                .arg(QDir::toNativeSeparators(cache)),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  QStringList entries;
  const QDir folder(cache);
  for (const QString& name : folder.entryList(QDir::AllEntries | QDir::Hidden |
                                              QDir::System | QDir::NoDotAndDotDot)) {
    entries.append(folder.filePath(name));
  }
  if (!entries.isEmpty()) {
    const auto result = MOShared::shellRemove(entries, true, parentWidget());
    if (!result.aborted && !result.removed()) {
      log::warn("clear Nexus cache: {} item(s) still in '{}'", result.remaining.size(),
                QDir::toNativeSeparators(cache));
    }
  }
  NexusInterface::instance().clearCache();
}

void NexusSettingsTab::associate()
{
  // on failure the error was already shown over Settings
  if (!Settings::instance().nexus().registerAsNXMHandler(true, parentWidget())) {
    return;
  }
  MessageDialog::showMessage(QObject::tr("Registered for Nexus download links"),
                             parentWidget());
}

void NexusSettingsTab::updateNexusData()
{
  const auto user = NexusInterface::instance().getAPIUserAccount();

  if (user.isValid()) {
    ui->nexusUserID->setText(user.id());
    ui->nexusName->setText(user.name());
    ui->nexusAccount->setText(localizedUserAccountType(user.type()));

    ui->nexusDailyRequests->setText(QString("%1/%2")
                                        .arg(user.limits().remainingDailyRequests)
                                        .arg(user.limits().maxDailyRequests));

    ui->nexusHourlyRequests->setText(QString("%1/%2")
                                         .arg(user.limits().remainingHourlyRequests)
                                         .arg(user.limits().maxHourlyRequests));
  } else {
    ui->nexusUserID->setText(QObject::tr("N/A"));
    ui->nexusName->setText(QObject::tr("N/A"));
    ui->nexusAccount->setText(QObject::tr("N/A"));
    ui->nexusDailyRequests->setText(QObject::tr("N/A"));
    ui->nexusHourlyRequests->setText(QObject::tr("N/A"));
  }
}

