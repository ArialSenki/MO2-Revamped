/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "nxmaccessmanager.h"
#include "iplugingame.h"
#include "nexusinterface.h"
#include "nexusoauth.h"
#include "nexusoauthcore.h"
#include "nxmurl.h"
#include "persistentcookiejar.h"
#include "report.h"
#include "revampedversion.h"
#include "selfupdater.h"
#include "settings.h"
#include "utility.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QPushButton>
#include <QThread>
#include <QUrlQuery>

using namespace MOBase;
using namespace std::chrono_literals;

const QString NexusBaseUrl("https://api.nexusmods.com/v1");
const QString NexusSSO("wss://sso.nexusmods.com");
// MO2 Revamped: the application Nexus registered (see revampedversion.h); while
// there is none, Settings doesn't offer this sign-in
const QString NexusSSOPage("https://www.nexusmods.com/sso?id=%1&application=" +
                           QStringLiteral(MO2R_NEXUS_SSO_SLUG));

// MO2 Revamped: an SSL error on Nexus' own servers almost always means
// something in between answered instead of them
static QString sslInterceptedMessage()
{
  return QObject::tr("The secure connection to Nexus was intercepted (a firewall, "
                     "antivirus or internet provider block). Try again later or use "
                     "a VPN.");
}

ValidationProgressDialog::ValidationProgressDialog(Settings* s, NexusKeyValidator& v)
    : m_settings(s), m_validator(v), m_updateTimer(nullptr), m_first(true)
{
  ui.reset(new Ui::ValidationProgressDialog);
  ui->setupUi(this);

  connect(ui->hide, &QPushButton::clicked, [&] {
    onHide();
  });
  connect(ui->cancel, &QPushButton::clicked, [&] {
    onCancel();
  });
}

void ValidationProgressDialog::setParentWidget(QWidget* w)
{
  const auto wasVisible = isVisible();

  hide();
  setParent(w, windowFlags() | Qt::Dialog);
  setModal(false);

  if (w && wasVisible) {
    setVisible(true);
    raise();
  }
}

void ValidationProgressDialog::start()
{
  if (!m_updateTimer) {
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, [&] {
      onTimer();
    });
    m_updateTimer->setInterval(100ms);
  }

  updateProgress();
  m_updateTimer->start();

  show();
}

void ValidationProgressDialog::stop()
{
  if (m_updateTimer) {
    m_updateTimer->stop();
  }

  hide();
}

void ValidationProgressDialog::showEvent(QShowEvent* e)
{
  // MO2 Revamped: MOApplication places it over the window in front (the
  // instance wizard during sign-in) and keeps it above that window; centred
  // on the main window's monitor without an owner, it opened behind the
  // wizard
  m_first = false;
  QDialog::showEvent(e);
}

void ValidationProgressDialog::closeEvent(QCloseEvent* e)
{
  hide();
  e->ignore();
}

void ValidationProgressDialog::onHide()
{
  hide();
}

void ValidationProgressDialog::onCancel()
{
  m_validator.cancel();
}

void ValidationProgressDialog::onTimer()
{
  updateProgress();
}

void ValidationProgressDialog::updateProgress()
{
  const auto* current = m_validator.currentAttempt();

  if (current) {
    ui->progress->setRange(0, current->timeout().count());
    ui->progress->setValue(current->elapsed().elapsed() / 1000);
  } else {
    // indeterminate
    ui->progress->setRange(0, 0);
  }

  if (const auto* a = m_validator.lastAttempt()) {
    ui->label->setText(a->message() + ". " + tr("Trying again..."));
  } else if (current) {
    ui->label->setText(tr("Connecting to Nexus..."));
  } else {
    ui->label->setText("?");
  }
}

NexusSSOLogin::NexusSSOLogin() : m_keyReceived(false), m_active(false)
{
  m_timeout.setInterval(10s);
  m_timeout.setSingleShot(true);

  QObject::connect(&m_socket, &QWebSocket::connected, [&] {
    onConnected();
  });

  QObject::connect(&m_socket,
                   qOverload<QAbstractSocket::SocketError>(&QWebSocket::error),
                   [&](auto&& e) {
                     onError(e);
                   });

  QObject::connect(&m_socket, &QWebSocket::sslErrors, [&](auto&& errors) {
    onSslErrors(errors);
  });

  QObject::connect(&m_socket, &QWebSocket::textMessageReceived, [&](auto&& s) {
    onMessage(s);
  });

  QObject::connect(&m_socket, &QWebSocket::disconnected, [&] {
    onDisconnected();
  });

  QObject::connect(&m_timeout, &QTimer::timeout, [&] {
    onTimeout();
  });
}

QString NexusSSOLogin::stateToString(States s, const QString& e)
{
  switch (s) {
  case ConnectingToSSO:
    return QObject::tr("Connecting to Nexus...");

  case WaitingForToken:
    return QObject::tr("Waiting for Nexus...");

  case WaitingForBrowser:
    return QObject::tr("Opened Nexus in browser.") + "\n" +
           QObject::tr("Switch to your browser and accept the request.");

  case Finished:
    return QObject::tr("Finished.");

  case Timeout:
    return QObject::tr("No answer from Nexus.") + "\n" +
           QObject::tr("A firewall might be blocking Mod Organizer.");

  case ClosedByRemote:
    return QObject::tr("Nexus closed the connection.") + "\n" +
           QObject::tr("A firewall might be blocking Mod Organizer.");

  case Cancelled:
    return QObject::tr("Cancelled.");

  case Error:  // fall-through
  default: {
    if (e.isEmpty()) {
      return QString("%1").arg(s);
    } else {
      return e;
    }
  }
  }
}

void NexusSSOLogin::start()
{
  // MO2 Revamped: without a slug Nexus registered for MO2 Revamped, its SSO
  // page would ask in no application's name (or another's): never opened
  if (QStringLiteral(MO2R_NEXUS_SSO_SLUG).isEmpty()) {
    setState(Error, QObject::tr("This build of MO2 Revamped has no Nexus SSO "
                                "registration; use the API key or sign in."));
    return;
  }

  m_active = true;
  setState(ConnectingToSSO);
  m_timeout.start();
  m_socket.open(NexusSSO);
}

void NexusSSOLogin::cancel()
{
  if (m_active) {
    abort();
    setState(Cancelled);
  }
}

void NexusSSOLogin::close()
{
  if (m_active) {
    m_active = false;
    m_timeout.stop();
    m_socket.close();
  }
}

void NexusSSOLogin::abort()
{
  m_active = false;
  m_timeout.stop();
  m_socket.abort();
}

bool NexusSSOLogin::isActive() const
{
  return m_active;
}

void NexusSSOLogin::setState(States s, const QString& error)
{
  if (stateChanged) {
    stateChanged(s, error);
  }
}

void NexusSSOLogin::onConnected()
{
  setState(WaitingForToken);

  m_keyReceived = false;

  boost::uuids::random_generator generator;
  boost::uuids::uuid sessionId = generator();
  m_guid                       = boost::uuids::to_string(sessionId).c_str();

  QJsonObject data;
  data.insert(QString("id"), QJsonValue(m_guid));
  data.insert(QString("protocol"), 2);

  const QString message = QJsonDocument(data).toJson();
  m_socket.sendTextMessage(message);
}

void NexusSSOLogin::onMessage(const QString& s)
{
  const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
  const QVariantMap root  = doc.object().toVariantMap();

  if (!root["success"].toBool()) {
    close();

    setState(Error, QObject::tr("Nexus sign-in couldn't start: %1")
                        .arg(root["error"].toString()));

    return;
  }

  const QVariantMap data = root["data"].toMap();

  if (data.contains("connection_token")) {
    // first answer

    // open browser
    const QUrl url = NexusSSOPage.arg(m_guid);
    shell::Open(url);

    m_timeout.stop();
    setState(WaitingForBrowser);
  } else {
    // second answer
    const auto key = data["api_key"].toString();
    close();

    if (keyChanged) {
      keyChanged(key);
    }

    setState(Finished);
  }
}

void NexusSSOLogin::onDisconnected()
{
  if (m_active) {
    if (!m_keyReceived) {
      close();
      setState(ClosedByRemote);
    } else {
      m_active = false;
    }
  }
}

void NexusSSOLogin::onError(QAbstractSocket::SocketError e)
{
  if (m_active) {
    close();
    setState(Error, m_socket.errorString());
  }
}

void NexusSSOLogin::onSslErrors(const QList<QSslError>& errors)
{
  if (m_active) {
    // MO2 Revamped: the connection isn't trusted: it closes, and the user is
    // told once (it stayed open, the state set again for each error)
    for (const auto& e : errors) {
      log::error("nexus: sso ssl error: {}", e.errorString());
    }
    close();
    setState(Error, sslInterceptedMessage());
  }
}

void NexusSSOLogin::onTimeout()
{
  abort();
  setState(Timeout);
}

ValidationAttempt::ValidationAttempt(std::chrono::seconds timeout)
    : m_reply(nullptr), m_result(None)
{
  m_timeout.setSingleShot(true);
  m_timeout.setInterval(timeout);

  QObject::connect(&m_timeout, &QTimer::timeout, [&] {
    onTimeout();
  });
}

void ValidationAttempt::start(NXMAccessManager& m, const QString& key, bool bearer)
{
  m_bearer = bearer;
  if (!sendRequest(m, key)) {
    return;
  }

  m_elapsed.start();
  m_timeout.start();

  log::debug("nexus: attempt started with timeout of {} seconds", timeout().count());
}

bool ValidationAttempt::sendRequest(NXMAccessManager& m, const QString& key)
{
  // MO2 Revamped: a Nexus sign-in (OAuth) is checked where Vortex checks it,
  // at the userinfo endpoint with its access token; an API key at
  // users/validate, as before
  const QString requestUrl(m_bearer ? QString::fromLatin1(NexusOAuthCore::UserInfoUrl)
                                    : NexusBaseUrl + "/users/validate");
  QNetworkRequest request(requestUrl);

  if (m_bearer) {
    request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
  } else {
    request.setRawHeader("APIKEY", key.toUtf8());
  }
  // MO2 Revamped: a redirect elsewhere would take the key along
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::SameOriginRedirectPolicy);
  request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader,
                    m.userAgent().toUtf8());
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  request.setRawHeader("Protocol-Version", "1.0.0");
  request.setRawHeader("Application-Name", "MO2 Revamped");
  request.setRawHeader("Application-Version",
                       SelfUpdater::revampedVersionString().toUtf8());

  m_reply = m.get(request);

  if (!m_reply) {
    setFailure(SoftError, QObject::tr("Failed to request %1").arg(requestUrl));
    return false;
  }

  QObject::connect(m_reply, &QNetworkReply::finished, [&] {
    onFinished();
  });

  QObject::connect(m_reply, &QNetworkReply::sslErrors, [&](auto&& errors) {
    onSslErrors(errors);
  });

  return true;
}

void ValidationAttempt::cancel()
{
  if (!m_reply || m_result != None) {
    // not running
    return;
  }

  setFailure(Cancelled, QObject::tr("Cancelled"));

  if (m_reply) {
    m_reply->abort();
  }

  cleanup();
}

bool ValidationAttempt::done() const
{
  return (m_result != None);
}

bool ValidationAttempt::running() const
{
  return (m_reply != nullptr) && (m_result == None);
}

ValidationAttempt::Result ValidationAttempt::result() const
{
  return m_result;
}

const QString& ValidationAttempt::message() const
{
  return m_message;
}

std::chrono::seconds ValidationAttempt::timeout() const
{
  return std::chrono::duration_cast<std::chrono::seconds>(
      m_timeout.intervalAsDuration());
}

QElapsedTimer ValidationAttempt::elapsed() const
{
  return m_elapsed;
}

void ValidationAttempt::onFinished()
{
  if (m_result == Cancelled) {
    return;
  }

  log::debug("nexus: request has finished");

  if (!m_reply) {
    // shouldn't happen
    log::error("nexus: reply is null");
    setFailure(HardError, QObject::tr("Internal error"));
    return;
  }

  const auto code =
      m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  m_httpStatus = code;

  if (code == 0) {
    // request wasn't even sent
    log::error("nexus: code is 0");
    setFailure(SoftError, m_reply->errorString());
    return;
  }

  const auto doc       = QJsonDocument::fromJson(m_reply->readAll());
  const auto headers   = m_reply->rawHeaderPairs();
  const auto httpError = m_reply->errorString();

  const QJsonObject data = doc.object();

  if (m_bearer) {
    onBearerFinished(code, doc, headers);
    return;
  }

  // the key itself: revoked, changed on Nexus, or mistyped. A 403 that isn't
  // Nexus' own JSON answer comes from something in between (a firewall or
  // proxy page) and is retried as any other failed request
  if (code == 401 || (code == 403 && doc.isObject())) {
    setFailure(HardError, nexusKeyRefusedMessage() + QString(" (%1)").arg(code));
    return;
  }

  if (code != 200) {
    // http request failed

    QString s = m_reply->errorString();

    const auto nexusMessage = data.value("message").toString();
    if (!nexusMessage.isEmpty()) {
      if (!s.isEmpty()) {
        s += ", ";
      }

      s += nexusMessage;
    }

    if (s.isEmpty()) {
      s = QObject::tr("HTTP code %1").arg(code);
    } else {
      s += QString(" (%1)").arg(code);
    }

    setFailure(SoftError, s);
    return;
  }

  if (doc.isNull()) {
    setFailure(HardError, QObject::tr("Invalid JSON"));
    return;
  }

  if (!data.contains("user_id")) {
    setFailure(HardError, QObject::tr("Bad response"));
    return;
  }

  const int id       = data.value("user_id").toInt();
  const QString key  = data.value("key").toString();
  const QString name = data.value("name").toString();
  const bool premium = data.value("is_premium").toBool();

  if (key.isEmpty()) {
    setFailure(HardError, QObject::tr("API key is empty"));
    return;
  }

  const auto user =
      APIUserAccount()
          .apiKey(key)
          .id(QString("%1").arg(id))
          .name(name)
          .type(premium ? APIUserAccountTypes::Premium : APIUserAccountTypes::Regular)
          .limits(NexusInterface::parseLimits(headers));

  setSuccess(user);
}

void ValidationAttempt::onBearerFinished(int code, const QJsonDocument& doc,
                                         const QList<QNetworkReply::RawHeaderPair>& headers)
{
  // the token expired or was revoked: the validator drops it and the next
  // attempt renews it first; when Nexus refuses the renewal too, the session
  // ends there (NexusOAuth::sessionEnded)
  if (code == 401) {
    setFailure(SoftError, nexusSignInRefusedMessage() + QString(" (%1)").arg(code));
    return;
  }
  if (code == 403 && doc.isObject()) {
    setFailure(HardError, nexusSignInRefusedMessage() + QString(" (%1)").arg(code));
    return;
  }

  if (code != 200) {
    QString s = NexusOAuthCore::redactSecrets(m_reply->errorString());
    if (s.isEmpty()) {
      s = QObject::tr("HTTP code %1").arg(code);
    } else {
      s += QString(" (%1)").arg(code);
    }
    setFailure(SoftError, s);
    return;
  }

  const auto info = NexusOAuthCore::parseUserInfo(doc.toJson(QJsonDocument::Compact));
  if (!info) {
    setFailure(HardError, QObject::tr("Bad response"));
    return;
  }

  // userinfo isn't on the API's server, so it carries no request counts: they
  // come with the first API answer (NexusInterface::parseLimits)
  const auto user =
      APIUserAccount()
          .oauth(true)
          .id(info->id)
          .name(info->name)
          .type(info->premium ? APIUserAccountTypes::Premium : APIUserAccountTypes::Regular)
          .limits(NexusInterface::parseLimits(headers));

  setSuccess(user);
}

void ValidationAttempt::onSslErrors(const QList<QSslError>& errors)
{
  log::error("nexus: ssl errors");

  for (auto& e : errors) {
    log::error("  . {}", e.errorString());
  }

  setFailure(HardError, sslInterceptedMessage());
}

void ValidationAttempt::onTimeout()
{
  setFailure(SoftError, QObject::tr("Timed out"));
}

void ValidationAttempt::setFailure(Result r, const QString& error)
{
  if (r != Cancelled) {
    // don't spam the log
    log::error("nexus: {}", error);
  }

  cleanup();

  m_result  = r;
  m_message = error;

  if (failure) {
    failure();
  }
}

void ValidationAttempt::setSuccess(const APIUserAccount& user)
{
  log::debug("nexus connection successful");
  cleanup();

  m_result  = Success;
  m_message = "";

  if (success) {
    success(user);
  }
}

void ValidationAttempt::cleanup()
{
  m_timeout.stop();

  if (m_reply) {
    m_reply->disconnect();
    m_reply->deleteLater();
    m_reply = nullptr;
  }
}

NexusKeyValidator::NexusKeyValidator(Settings* s, NXMAccessManager& am)
    : m_settings(s), m_manager(am)
{}

NexusKeyValidator::~NexusKeyValidator()
{
  cancel();
}

std::vector<std::chrono::seconds> NexusKeyValidator::getTimeouts() const
{
  if (m_settings) {
    return m_settings->nexus().validationTimeouts();
  } else {
    return {10s, 15s, 20s};
  }
}

void NexusKeyValidator::start(const QString& key, Behaviour b)
{
  if (isActive()) {
    log::debug("nexus: trying to start while ongoing; ignoring");
    return;
  }

  m_key    = key;
  m_bearer = false;

  const auto timeouts = getTimeouts();

  switch (b) {
  case OneShot: {
    createAttempts({timeouts[0]});
    break;
  }

  case Retry: {
    createAttempts(timeouts);
    break;
  }
  }

  nextTry();
}

void NexusKeyValidator::startOAuth(Behaviour b)
{
  if (isActive()) {
    log::debug("nexus: trying to start while ongoing; ignoring");
    return;
  }

  m_key.clear();
  m_bearer = true;

  const auto timeouts = getTimeouts();
  createAttempts(b == OneShot ? std::vector<std::chrono::seconds>{timeouts[0]}
                              : timeouts);

  nextTry();
}

void NexusKeyValidator::createAttempts(
    const std::vector<std::chrono::seconds>& timeouts)
{
  // kept one more check, see m_previousAttempts
  m_previousAttempts = std::move(m_attempts);
  m_attempts.clear();

  for (auto&& t : timeouts) {
    m_attempts.push_back(std::make_unique<ValidationAttempt>(t));
  }
}

void NexusKeyValidator::cancel()
{
  log::debug("nexus: connection cancelled");

  // a token still being renewed: its answer is ignored
  if (m_waitingForToken) {
    m_waitingForToken = false;
    m_alive           = std::make_shared<int>(0);
    setFinished(ValidationAttempt::Cancelled, QObject::tr("Cancelled"), {});
  }

  for (auto&& a : m_attempts) {
    a->cancel();
  }
}

bool NexusKeyValidator::isActive() const
{
  // the attempts left over after a cancel or a refused key never start: one
  // still not done would keep every later check from starting, and
  // NXMAccessManager::validated() showing a progress that never ends.
  // The next attempt starts as the one before fails, so there is no gap
  if (m_waitingForToken) {
    return true;
  }
  for (auto&& a : m_attempts) {
    if (a->running()) {
      return true;
    }
  }

  return false;
}

const ValidationAttempt* NexusKeyValidator::lastAttempt() const
{
  const ValidationAttempt* last = nullptr;

  for (auto&& a : m_attempts) {
    if (a->done()) {
      last = a.get();
    } else {
      break;
    }
  }

  return last;
}

const ValidationAttempt* NexusKeyValidator::currentAttempt() const
{
  for (auto&& a : m_attempts) {
    if (!a->done()) {
      return a.get();
    }
  }

  return nullptr;
}

bool NexusKeyValidator::nextTry()
{
  for (auto&& a : m_attempts) {
    if (!a->done()) {
      a->success = [&](auto&& user) {
        onAttemptSuccess(*a, user);
      };
      a->failure = [&] {
        onAttemptFailure(*a);
      };

      if (!m_bearer) {
        a->start(m_manager, m_key);
        return true;
      }

      // MO2 Revamped: a Nexus sign-in: its access token, renewed first when
      // it has expired. The answer may come at once or later
      ValidationAttempt* attempt = a.get();
      std::weak_ptr<int> alive   = m_alive;
      m_waitingForToken          = true;
      m_manager.oauth()->ensureFreshToken(
          [this, attempt, alive](bool ok, const QString& message) {
            if (alive.expired() || !m_waitingForToken) {
              return;
            }
            m_waitingForToken = false;
            if (!ok) {
              cancel();
              setFinished(ValidationAttempt::HardError, message, {});
              return;
            }
            attempt->start(m_manager, m_manager.oauth()->accessToken(), true);
          });
      return true;
    }
  }

  // no more
  return false;
}

void NexusKeyValidator::onAttemptSuccess(const ValidationAttempt& a,
                                         const APIUserAccount& u)
{
  if (attemptFinished) {
    attemptFinished(a);
  }

  setFinished(ValidationAttempt::Success, "", u);
}

void NexusKeyValidator::onAttemptFailure(const ValidationAttempt& a)
{
  if (attemptFinished) {
    attemptFinished(a);
  }

  // MO2 Revamped: Nexus refused the sign-in's token (401): the next attempt
  // renews it before asking again. A timeout or a network error says nothing
  // about the token, so it is kept
  if (m_bearer && a.httpStatus() == 401) {
    m_manager.oauth()->invalidateAccessToken();
  }

  switch (a.result()) {
  case ValidationAttempt::SoftError: {
    if (!nextTry()) {
      setFinished(a.result(), a.message(), {});
    }

    break;
  }

  case ValidationAttempt::HardError: {
    cancel();
    setFinished(a.result(), a.message(), {});
    break;
  }

  case ValidationAttempt::Cancelled: {
    setFinished(ValidationAttempt::Cancelled, QObject::tr("Cancelled"), {});
    break;
  }
  }
}

void NexusKeyValidator::setFinished(ValidationAttempt::Result r, const QString& message,
                                    std::optional<APIUserAccount> user)
{
  if (finished) {
    finished(r, message, user);
  }
}

QString nexusKeyRefusedMessage()
{
  return QObject::tr("Nexus refused the API key: it may have been revoked or changed. "
                     "Connect again in Settings, Nexus");
}

QString nexusSignInRefusedMessage()
{
  return QObject::tr("Nexus refused the sign-in: it may have expired or been revoked. "
                     "Sign in again in Settings, Nexus");
}

NXMAccessManager::NXMAccessManager(QObject* parent, Settings* s,
                                   const QString& moVersion)
    : QNetworkAccessManager(parent), m_Settings(s), m_MOVersion(moVersion),
      m_validator(s, *this), m_validationState(NotChecked),
      m_oauth(new NexusOAuth(*this, this))
{
  // MO2 Revamped: Nexus refused to renew the sign-in: signed out, the status
  // bar and the window title follow
  connect(m_oauth, &NexusOAuth::sessionEnded, this, [this](const QString& message) {
    log::warn("{}", message);
    clearApiKey();
    // a stored API key (kept while signed in) takes over again
    if (GlobalSettings::hasNexusApiKey()) {
      startStoredCheck(true);
    }
  });

  m_validator.finished = [&](auto&& r, auto&& m, auto&& u) {
    onValidatorFinished(r, m, u);
  };

  m_validator.attemptFinished = [&](auto&& a) {
    onValidatorAttemptFinished(a);
  };

  if (m_Settings) {
    setCookieJar(new PersistentCookieJar(QDir::fromNativeSeparators(
        m_Settings->paths().cache() + "/nexus_cookies.dat")));
  }
}

void NXMAccessManager::setTopLevelWidget(QWidget* w)
{
  if (w) {
    if (m_ProgressDialog) {
      m_ProgressDialog->setParentWidget(w);
    }
  } else {
    m_ProgressDialog.reset();
    m_validator.cancel();
  }
}

QNetworkReply*
NXMAccessManager::createRequest(QNetworkAccessManager::Operation operation,
                                const QNetworkRequest& request, QIODevice* device)
{
  if (request.url().scheme() != "nxm") {
    return QNetworkAccessManager::createRequest(operation, request, device);
  }
  if (operation == GetOperation) {
    emit requestNXMDownload(request.url().toString());

    // eat the request, everything else will be done by the download manager
    return QNetworkAccessManager::createRequest(QNetworkAccessManager::GetOperation,
                                                QNetworkRequest(QUrl()));
  } else if (operation == PostOperation) {
    return QNetworkAccessManager::createRequest(operation, request, device);
    ;
  } else {
    return QNetworkAccessManager::createRequest(operation, request, device);
  }
}

void NXMAccessManager::clearCookies()
{
  PersistentCookieJar* jar = qobject_cast<PersistentCookieJar*>(cookieJar());
  if (jar != nullptr) {
    jar->clear();
  } else {
    log::warn("failed to clear cookies, invalid cookie jar");
  }
}

void NXMAccessManager::startValidationCheck(const QString& key)
{
  m_validationState = NotChecked;
  m_validator.start(key, NexusKeyValidator::Retry);

  if (m_ProgressDialog) {
    // don't show the progress dialog on startup for the first attempt; the
    // dialog will be shown in onValidatorAttemptFinished() if it failed
    startProgress();
  }
}

void NXMAccessManager::onValidatorFinished(ValidationAttempt::Result r,
                                           const QString& message,
                                           std::optional<APIUserAccount> user)
{
  stopProgress();

  if (user) {
    m_validationState = Valid;
    emit credentialsReceived(*user);
    emit validateSuccessful(true);
  } else {
    if (r == ValidationAttempt::Cancelled) {
      m_validationState = NotChecked;
    } else {
      m_validationState = Invalid;
      emit validateFailed(message);
    }
  }
}

void NXMAccessManager::onValidatorAttemptFinished(const ValidationAttempt& a)
{
  if (!m_ProgressDialog) {
    switch (a.result()) {
    case ValidationAttempt::SoftError:
    case ValidationAttempt::HardError: {
      startProgress();
      break;
    }

    case ValidationAttempt::None:
    case ValidationAttempt::Success:
    case ValidationAttempt::Cancelled:
    default: {
      // don't show the dialog
      break;
    }
    }
  }
}

bool NXMAccessManager::validated() const
{
  if (m_validationState == Valid) {
    return true;
  }

  if (m_validator.isActive()) {
    const_cast<NXMAccessManager*>(this)->startProgress();
  }

  return false;
}

void NXMAccessManager::refuseValidation()
{
  m_validationState = Invalid;
}

bool NXMAccessManager::validateAttempted() const
{
  return (m_validationState != NotChecked);
}

bool NXMAccessManager::validateWaiting() const
{
  return m_validator.isActive();
}

void NXMAccessManager::apiCheck(const QString& apiKey, bool force)
{
  if (m_validator.isActive()) {
    return;
  }

  if (m_Settings && m_Settings->network().offlineMode()) {
    m_validationState = NotChecked;
    return;
  }

  if (force) {
    m_validationState = NotChecked;
  }

  if (m_validationState == Valid) {
    emit validateSuccessful(false);
    return;
  }

  startValidationCheck(apiKey);
}

bool NXMAccessManager::startStoredCheck(bool force)
{
  if (m_oauth->hasSession()) {
    if (m_validator.isActive()) {
      return true;
    }
    if (m_Settings && m_Settings->network().offlineMode()) {
      m_validationState = NotChecked;
      return true;
    }
    if (force) {
      m_validationState = NotChecked;
    }
    if (m_validationState == Valid) {
      emit validateSuccessful(false);
      return true;
    }
    startOAuthCheck();
    return true;
  }

  QString apiKey;
  if (GlobalSettings::nexusApiKey(apiKey)) {
    apiCheck(apiKey, force);
    return true;
  }

  return false;
}

void NXMAccessManager::startOAuthCheck()
{
  m_validationState = NotChecked;
  m_validator.startOAuth(NexusKeyValidator::Retry);

  if (m_ProgressDialog) {
    startProgress();
  }
}

const QString& NXMAccessManager::MOVersion() const
{
  return m_MOVersion;
}

QString NXMAccessManager::userAgent(const QString& subModule) const
{
  QStringList comments;
  QString os;
  if (QSysInfo::productType() == "windows")
    comments << ((QSysInfo::kernelType() == "winnt") ? "Windows_NT " : "Windows ") +
                    QSysInfo::kernelVersion();
  else
    comments << QSysInfo::kernelType().left(1).toUpper() + QSysInfo::kernelType().mid(1)
             << QSysInfo::productType().left(1).toUpper() +
                    QSysInfo::kernelType().mid(1) + " " + QSysInfo::productVersion();
  if (!subModule.isEmpty()) {
    comments << "module: " + subModule;
  }
  comments << ((QSysInfo::buildCpuArchitecture() == "x86_64") ? "x64" : "x86");

  // Nexus asks every app to name itself accurately: say MO2 Revamped and
  // which Mod Organizer it is based on, not the original MO2
  comments.prepend("based on Mod Organizer " + m_MOVersion);
  return QString("MO2-Revamped/%1 (%2) Qt/%3")
      .arg(SelfUpdater::revampedVersionString(), comments.join("; "), qVersion());
}

void NXMAccessManager::clearApiKey()
{
  m_validator.cancel();
  m_validationState = NotChecked;
  emit credentialsReceived(APIUserAccount());
}

void NXMAccessManager::setLinkedAccount(const APIUserAccount& user)
{
  // a check of the previous key may still be going (the one at startup): its
  // answer would replace this account or mark it as failed
  m_validator.cancel();
  m_validationState = Valid;
  emit credentialsReceived(user);
}

void NXMAccessManager::startProgress()
{
  if (!m_ProgressDialog) {
    m_ProgressDialog.reset(new ValidationProgressDialog(m_Settings, m_validator));
  }

  m_ProgressDialog->start();
}

void NXMAccessManager::stopProgress()
{
  if (m_ProgressDialog) {
    m_ProgressDialog->stop();
  }
}
