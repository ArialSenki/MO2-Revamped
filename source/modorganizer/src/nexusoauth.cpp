#include "nexusoauth.h"

#include "nexusoauthcore.h"
#include "nxmaccessmanager.h"
#include "revampedversion.h"
#include "selfupdater.h"
#include "settings.h"

#include <log.h>
#include <utility.h>

#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>

#include <algorithm>

using namespace MOBase;
using namespace std::chrono_literals;
namespace core = NexusOAuthCore;

namespace
{

// a request goes with the token while it has this long left; otherwise it is
// refreshed first
constexpr qint64 RequestMarginSeconds = 60;

// the browser opens a few connections (preconnects); more than this at once
// isn't a browser
constexpr std::size_t MaxConnections = 8;

QDateTime nowUtc()
{
  return QDateTime::currentDateTimeUtc();
}

// what the token endpoint said, without anything secret
QString describe(const core::Tokens& t)
{
  QString s = t.error;
  if (!t.errorDescription.isEmpty()) {
    s += QStringLiteral(": ") + t.errorDescription;
  }
  return core::redactSecrets(s);
}

// Nexus refused the refresh token itself (RFC 6749 section 5.2): trying again
// can't help
bool refusedGrant(int status, const QString& error)
{
  return error == QLatin1String("invalid_grant") ||
         error == QLatin1String("invalid_client") ||
         error == QLatin1String("unauthorized_client") ||
         ((status == 400 || status == 401) && error != QLatin1String("invalid_response"));
}

}  // namespace

NexusOAuth::NexusOAuth(NXMAccessManager& network, QObject* parent)
    : QObject(parent), m_network(network)
{
  m_signInTimeout.setSingleShot(true);
  m_signInTimeout.setInterval(std::chrono::seconds(core::SignInTimeoutSeconds));
  connect(&m_signInTimeout, &QTimer::timeout, this, [this] {
    log::warn("nexus: the browser sign-in got no answer in {} minutes",
              core::SignInTimeoutSeconds / 60);
    finishSignIn(false, tr("No answer from the browser in 5 minutes. Try again."));
  });

  m_refreshTimer.setSingleShot(true);
  connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
    // renewed ahead of its expiry, so no request waits for it
    if (hasSession() && !m_refresh) {
      startRefresh();
    }
  });
}

NexusOAuth::~NexusOAuth()
{
  m_signInTimeout.stop();
  m_refreshTimer.stop();
  for (auto* socket : findChildren<QTcpSocket*>()) {
    socket->disconnect(this);
  }
  m_heads.clear();
}

bool NexusOAuth::available()
{
  return !clientId().isEmpty();
}

QString NexusOAuth::clientId()
{
  return QStringLiteral(MO2R_NEXUS_OAUTH_CLIENT_ID);
}

void NexusOAuth::load() const
{
  if (m_loaded) {
    return;
  }
  m_loaded = true;

  if (!available()) {
    return;
  }

  QString refresh, access;
  if (GlobalSettings::nexusOAuthTokens(refresh, access)) {
    m_refreshToken = refresh;
    if (!core::unpackAccess(access, m_accessToken, m_accessExpires)) {
      m_accessToken.clear();
      m_accessExpires = {};
    }
  }
}

void NexusOAuth::store()
{
  GlobalSettings::setNexusOAuthTokens(m_refreshToken,
                                      core::packAccess(m_accessToken, m_accessExpires));
}

void NexusOAuth::forget()
{
  m_loaded = true;
  m_refreshToken.clear();
  m_accessToken.clear();
  m_accessExpires = {};
  m_refreshTimer.stop();
  GlobalSettings::clearNexusOAuthTokens();
}

bool NexusOAuth::hasSession() const
{
  load();
  return available() && !m_refreshToken.isEmpty();
}

bool NexusOAuth::signInActive() const
{
  return m_server != nullptr || !m_exchange.isNull();
}

QString NexusOAuth::accessToken() const
{
  return tokenFresh() ? m_accessToken : QString();
}

bool NexusOAuth::tokenFresh() const
{
  load();
  return !m_accessToken.isEmpty() && m_accessExpires.isValid() &&
         nowUtc().secsTo(m_accessExpires) > RequestMarginSeconds;
}

void NexusOAuth::invalidateAccessToken()
{
  m_accessToken.clear();
  m_accessExpires = {};
}

void NexusOAuth::scheduleRefresh()
{
  if (!m_accessExpires.isValid()) {
    return;
  }
  // a fifth of what is left, at most RefreshMarginSeconds, before it expires
  const qint64 left   = qMax<qint64>(0, nowUtc().secsTo(m_accessExpires));
  const qint64 margin = qMin<qint64>(core::RefreshMarginSeconds, left / 5);
  // never past a day: a timer that long would come late after a sleep anyway
  const qint64 wait = std::clamp<qint64>(left - margin, 10, 24 * 3600);
  m_refreshTimer.start(std::chrono::seconds(wait));
}

// --- browser sign-in -------------------------------------------------------

void NexusOAuth::signIn()
{
  if (!available()) {
    emit signInFinished(false, tr("This build of MO2 Revamped has no Nexus sign-in."));
    return;
  }
  if (signInActive()) {
    return;
  }

  m_verifier = core::makeVerifier();
  m_state    = core::makeState();

  // the loopback address only, never another interface, at a port the system
  // picks (RFC 8252 section 7.3)
  m_server = new QTcpServer(this);
  m_server->setMaxPendingConnections(static_cast<int>(MaxConnections));
  if (!m_server->listen(QHostAddress(QHostAddress::LocalHost), 0)) {
    const QString error = m_server->errorString();
    log::error("nexus: the sign-in couldn't listen on 127.0.0.1: {}", error);
    finishSignIn(false, tr("MO2 Revamped couldn't wait for the browser's answer: %1")
                            .arg(error));
    return;
  }
  connect(m_server, &QTcpServer::newConnection, this, &NexusOAuth::onNewConnection);

  m_redirectUri = core::redirectUri(m_server->serverPort());
  const QUrl url =
      core::authorizeUrl(clientId(), m_redirectUri, core::challengeS256(m_verifier), m_state);

  log::debug("nexus: browser sign-in started, waiting on 127.0.0.1:{}",
             m_server->serverPort());
  m_signInTimeout.start();
  emit signInProgress(tr("Opened Nexus in your browser."));
  emit signInProgress(tr("Sign in there and allow MO2 Revamped."));

  if (!shell::Open(url).success()) {
    finishSignIn(false, tr("The browser couldn't be opened."));
  }
}

void NexusOAuth::cancelSignIn()
{
  if (!signInActive()) {
    return;
  }
  if (m_exchange) {
    QNetworkReply* reply = m_exchange;
    m_exchange           = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }
  finishSignIn(false, tr("Cancelled."));
}

void NexusOAuth::onNewConnection()
{
  while (m_server && m_server->hasPendingConnections()) {
    QTcpSocket* socket = m_server->nextPendingConnection();
    if (!socket) {
      break;
    }
    // owned here, so an answer still being sent survives the server closing
    socket->setParent(this);

    if (m_heads.size() >= MaxConnections ||
        !socket->peerAddress().isLoopback()) {
      socket->abort();
      socket->deleteLater();
      continue;
    }

    m_heads[socket] = {};
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
      onSocketData(socket);
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QObject::destroyed, this, [this, socket] {
      m_heads.erase(socket);
    });
    // a connection that sends nothing (a preconnect) doesn't stay open
    QTimer::singleShot(15s, socket, [socket] {
      socket->abort();
      socket->deleteLater();
    });
  }
}

void NexusOAuth::onSocketData(QTcpSocket* socket)
{
  auto it = m_heads.find(socket);
  if (it == m_heads.end()) {
    return;
  }
  it->second += socket->readAll();

  // the request's line and headers hold the code: they never go to the log
  const core::Redirect r = core::parseRedirect(it->second, m_state);

  switch (r.kind) {
  case core::Redirect::Incomplete:
    return;

  case core::Redirect::Malformed:
    answer(socket, 400, tr("Bad request"), tr("This address only takes Nexus' answer."));
    return;

  case core::Redirect::WrongPath:
    answer(socket, 404, tr("Not found"), tr("This address only takes Nexus' answer."));
    return;

  case core::Redirect::StateMismatch:
    log::warn("nexus: the browser's answer wasn't for this sign-in; refused");
    answer(socket, 400, tr("Sign-in refused"),
           tr("This answer isn't for the sign-in MO2 Revamped started. Start it again "
              "from MO2 Revamped."));
    finishSignIn(false, tr("The browser's answer didn't match this sign-in, so it was "
                           "refused. Try again."));
    return;

  case core::Redirect::Error: {
    const QString reason = core::redactSecrets(
        r.errorDescription.isEmpty() ? r.error
                                     : r.error + QStringLiteral(": ") + r.errorDescription);
    log::warn("nexus: the sign-in ended in the browser: {}", reason);
    answer(socket, 200, tr("Not signed in"),
           tr("Nexus didn't give MO2 Revamped access. You can close this tab."));
    finishSignIn(false, r.error == QLatin1String("access_denied")
                            ? tr("Access wasn't allowed on Nexus.")
                            : tr("Nexus answered: %1").arg(reason));
    return;
  }

  case core::Redirect::Code:
    answer(socket, 200, tr("Signed in to Nexus Mods"),
           tr("You can close this tab and go back to MO2 Revamped."));
    // one redirect only: nothing more is taken
    stopListening();
    emit signInProgress(tr("Nexus answered. Getting access..."));
    exchangeCode(r.code);
    return;
  }
}

void NexusOAuth::answer(QTcpSocket* socket, int status, const QString& title,
                        const QString& message)
{
  // nothing more is read from this connection
  disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
  m_heads.erase(socket);

  socket->write(core::httpResponse(status, title, message));
  socket->disconnectFromHost();
}

void NexusOAuth::stopListening()
{
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
  // connections that sent nothing yet (preconnects) are closed; answers still
  // being sent finish on their own
  std::vector<QTcpSocket*> open;
  for (auto& [socket, head] : m_heads) {
    open.push_back(socket);
  }
  m_heads.clear();
  for (QTcpSocket* socket : open) {
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
  }
}

void NexusOAuth::finishSignIn(bool ok, const QString& message)
{
  stopListening();
  m_signInTimeout.stop();
  m_verifier.clear();
  m_state.clear();
  m_redirectUri.clear();
  emit signInFinished(ok, message);
}

void NexusOAuth::exchangeCode(const QString& code)
{
  m_signInTimeout.stop();
  m_exchange =
      postForm(core::TokenUrl, core::codeExchangeBody(clientId(), m_redirectUri, code,
                                                      m_verifier));
  QNetworkReply* reply = m_exchange;
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    onExchangeFinished(reply);
  });
}

void NexusOAuth::onExchangeFinished(QNetworkReply* reply)
{
  reply->deleteLater();
  if (m_exchange != reply) {
    return;
  }
  m_exchange = nullptr;

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray body = reply->readAll();

  QString error;
  if (status == 0) {
    error = core::redactSecrets(reply->errorString());
    log::error("nexus: the sign-in's token request failed: {}", error);
  } else if (acceptTokens(body, error)) {
    if (m_refreshToken.isEmpty()) {
      error = tr("Nexus gave no refresh token");
      log::error("nexus: the sign-in's answer had no refresh token");
    } else {
      store();
      scheduleRefresh();
      log::info("nexus: signed in with Nexus Mods (OAuth)");
      finishSignIn(true, tr("Signed in."));
      emit sessionChanged();
      return;
    }
  }

  invalidateAccessToken();
  finishSignIn(false, tr("Nexus didn't give access: %1").arg(error));
}

// --- refresh ----------------------------------------------------------------

void NexusOAuth::ensureFreshToken(TokenCallback done)
{
  if (!hasSession()) {
    if (done) {
      done(false, tr("Not signed in to Nexus."));
    }
    return;
  }
  if (tokenFresh()) {
    if (!m_refreshTimer.isActive()) {
      scheduleRefresh();
    }
    if (done) {
      done(true, {});
    }
    return;
  }

  if (done) {
    m_waiting.push_back(std::move(done));
  }
  if (!m_refresh) {
    startRefresh();
  }
}

void NexusOAuth::startRefresh()
{
  log::debug("nexus: refreshing the sign-in's access token");
  m_refresh = postForm(core::TokenUrl, core::refreshBody(clientId(), m_refreshToken));
  QNetworkReply* reply = m_refresh;
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    onRefreshFinished(reply);
  });
}

void NexusOAuth::onRefreshFinished(QNetworkReply* reply)
{
  reply->deleteLater();
  if (m_refresh != reply) {
    return;
  }
  m_refresh = nullptr;

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray body = reply->readAll();

  if (status == 0) {
    // no answer (offline, a firewall): the session stays, the next request
    // tries again
    const QString error = core::redactSecrets(reply->errorString());
    log::warn("nexus: refreshing the sign-in failed: {}", error);
    m_refreshTimer.start(2min);
    finishWaiting(false, tr("Couldn't reach Nexus to renew the sign-in: %1").arg(error));
    return;
  }

  const core::Tokens t = core::parseTokens(body);
  QString error;
  if (status == 200 && acceptTokens(body, error)) {
    store();
    scheduleRefresh();
    finishWaiting(true, {});
    return;
  }

  const QString reason = describe(t);
  if (refusedGrant(status, t.error)) {
    log::warn("nexus: Nexus refused the sign-in's refresh ({}): {}", status, reason);
    forget();
    const QString message =
        tr("Nexus ended the sign-in. Sign in again in Settings, Nexus.");
    finishWaiting(false, message);
    emit sessionChanged();
    emit sessionEnded(message);
    return;
  }

  log::warn("nexus: refreshing the sign-in failed ({}): {}", status, reason);
  m_refreshTimer.start(2min);
  finishWaiting(false, tr("Nexus couldn't renew the sign-in (%1).").arg(status));
}

void NexusOAuth::finishWaiting(bool ok, const QString& message)
{
  std::vector<TokenCallback> waiting;
  waiting.swap(m_waiting);
  for (auto& callback : waiting) {
    callback(ok, message);
  }
}

// --- sign-out ---------------------------------------------------------------

void NexusOAuth::signOut()
{
  cancelSignIn();

  if (m_refresh) {
    QNetworkReply* reply = m_refresh;
    m_refresh            = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }

  load();
  const QString refresh = m_refreshToken;
  forget();
  finishWaiting(false, tr("Signed out of Nexus."));

  if (!refresh.isEmpty() && available()) {
    // RFC 7009, at the revocation endpoint Nexus' discovery document lists;
    // the tokens are already gone here, whatever Nexus answers
    QNetworkReply* reply = postForm(core::RevokeUrl, core::revokeBody(clientId(), refresh));
    connect(reply, &QNetworkReply::finished, reply, [reply] {
      log::debug("nexus: sign-out revocation answered {}",
                 reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
      reply->deleteLater();
    });
  }

  log::info("nexus: signed out of Nexus Mods (OAuth)");
  emit sessionChanged();
}

// --- helpers ----------------------------------------------------------------

QNetworkReply* NexusOAuth::postForm(const char* url, const QByteArray& body)
{
  QNetworkRequest request{QUrl(QString::fromLatin1(url))};
  // the body holds the code, the verifier or a token: never sent anywhere else
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::SameOriginRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                       QNetworkRequest::AlwaysNetwork);
  request.setTransferTimeout(30000);
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    "application/x-www-form-urlencoded; charset=UTF-8");
  request.setRawHeader("Accept", "application/json");
  request.setHeader(QNetworkRequest::UserAgentHeader, m_network.userAgent());
  request.setRawHeader("Application-Name", "MO2 Revamped");
  request.setRawHeader("Application-Version",
                       SelfUpdater::revampedVersionString().toUtf8());
  return m_network.post(request, body);
}

bool NexusOAuth::acceptTokens(const QByteArray& json, QString& error)
{
  const core::Tokens t = core::parseTokens(json);
  if (!t.ok()) {
    error = describe(t);
    log::warn("nexus: the token endpoint refused: {}", error);
    return false;
  }

  load();
  m_accessToken   = t.accessToken;
  m_accessExpires = core::expiryOf(t, nowUtc());
  // Nexus may rotate the refresh token: the newest one is kept
  if (!t.refreshToken.isEmpty()) {
    m_refreshToken = t.refreshToken;
  }
  return true;
}
