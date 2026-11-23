#ifndef NEXUSOAUTH_H
#define NEXUSOAUTH_H

// MO2 Revamped: "Sign in with Nexus Mods", OAuth 2.0 authorization code with
// PKCE for a public client and a loopback redirect (RFC 6749, 7636, 8252).
//
//  - signIn() makes a verifier, its S256 challenge and a state, listens on
//    127.0.0.1 at a random port, and opens the system browser on Nexus'
//    consent page with the redirect http://127.0.0.1:<port>
//  - the listener takes one redirect: other paths get a 404, a wrong state
//    ends the sign-in, and it closes after the redirect or after 5 minutes
//  - the code goes to the token endpoint with the verifier; the refresh token
//    and the access token with its expiry go to Windows Credential Manager
//    (GlobalSettings::setNexusOAuthTokens), never to an INI file or the log
//  - the access token is refreshed before it expires; Nexus refusing the
//    refresh token (revoked, or rotated elsewhere) ends the session
//  - signOut() removes the tokens and asks Nexus to revoke the refresh token
//
// Nothing of this runs while MO2R_NEXUS_OAUTH_CLIENT_ID is empty: available()
// is false, Settings hides the button and the API key works as before. The
// endpoints and parameters, and their sources, are in nexusoauthcore.h.

#include <QDateTime>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>
#include <map>
#include <vector>

class NXMAccessManager;
class QTcpServer;
class QTcpSocket;

class NexusOAuth : public QObject
{
  Q_OBJECT

public:
  // a fresh access token, or why there is none
  using TokenCallback = std::function<void(bool ok, const QString& message)>;

  explicit NexusOAuth(NXMAccessManager& network, QObject* parent = nullptr);
  ~NexusOAuth() override;

  // whether this build has an OAuth client id
  static bool available();
  static QString clientId();

  // a refresh token is stored (and the build has a client id)
  bool hasSession() const;

  // the browser sign-in is waiting for the redirect or for the tokens
  bool signInActive() const;

  void signIn();
  void cancelSignIn();

  // removes the tokens from Credential Manager and memory, and asks Nexus to
  // revoke the refresh token (best effort)
  void signOut();

  // the access token while it is valid for RefreshMarginSeconds or more,
  // otherwise empty
  QString accessToken() const;
  bool tokenFresh() const;

  // calls `done` with a fresh access token available, refreshing it first when
  // needed; callbacks that come while a refresh is running wait for it
  void ensureFreshToken(TokenCallback done);

  // Nexus answered 401 to the token: the next request refreshes it first
  void invalidateAccessToken();

  // changes each time a new access token arrives: a request remembers the one
  // it was sent with, so a 401 that comes after a renewal doesn't renew again
  quint64 tokenGeneration() const { return m_generation; }

  // invalidateAccessToken() only when the token is still the one of
  // `generation`
  void invalidateAccessToken(quint64 generation);

signals:
  // a line for the Settings log
  void signInProgress(const QString& message);

  // the browser sign-in ended: with tokens stored when ok
  void signInFinished(bool ok, const QString& message);

  // a session started, ended or its tokens were removed
  void sessionChanged();

  // Nexus refused the refresh token: the session is gone
  void sessionEnded(const QString& message);

private:
  NXMAccessManager& m_network;

  // the session, loaded from Credential Manager the first time it's needed
  mutable bool m_loaded = false;
  mutable QString m_refreshToken;
  mutable QString m_accessToken;
  mutable QDateTime m_accessExpires;

  // the browser sign-in
  QTcpServer* m_server = nullptr;
  std::map<QTcpSocket*, QByteArray> m_heads;
  QTimer m_signInTimeout;
  QString m_verifier;
  QString m_state;
  QString m_redirectUri;
  QPointer<QNetworkReply> m_exchange;

  // the refresh
  QPointer<QNetworkReply> m_refresh;
  std::vector<TokenCallback> m_waiting;
  QTimer m_refreshTimer;
  quint64 m_generation = 0;
  // after a refresh that failed without an answer from Nexus (offline, a 5xx):
  // no new refresh before this, doubling up to 15 minutes
  QDateTime m_refreshRetryAt;
  qint64 m_refreshBackoffSeconds = 0;
  void refreshFailedTemporarily();

  void load() const;
  void store();
  void forget();
  void scheduleRefresh();

  void onNewConnection();
  void onSocketData(QTcpSocket* socket);
  void answer(QTcpSocket* socket, int status, const QString& title,
              const QString& message);
  void stopListening();
  void finishSignIn(bool ok, const QString& message);

  void exchangeCode(const QString& code);
  void onExchangeFinished(QNetworkReply* reply);

  void startRefresh();
  void onRefreshFinished(QNetworkReply* reply);
  void finishWaiting(bool ok, const QString& message);

  QNetworkReply* postForm(const char* url, const QByteArray& body);
  bool acceptTokens(const QByteArray& json, QString& error);
};

#endif  // NEXUSOAUTH_H
