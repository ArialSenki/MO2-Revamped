#ifndef NEXUSOAUTHCORE_H
#define NEXUSOAUTHCORE_H

// MO2 Revamped: the parts of "Sign in with Nexus Mods" (OAuth 2.0 authorization
// code with PKCE, RFC 6749 / RFC 7636 / RFC 8252) that need no network: the
// verifier and its challenge, the state, the authorize link, the loopback
// redirect's request, the token and user answers, and the redaction of what
// must stay out of logs. Qt Core only, so they can be tested on their own
// (source/revamped/tests/cpp/organizer/nexus_oauth_test.cpp).
//
// What Nexus uses, as read on 2026-11-23:
//  - https://users.nexusmods.com/.well-known/openid-configuration (the
//    authorize, token, revoke and userinfo endpoints, S256 among the
//    challenge methods)
//  - Vortex, src/renderer/src/extensions/nexus_integration/util/oauth.ts and
//    constants.ts (github.com/Nexus-Mods/Vortex): a public client
//    ("vortex_loopback") with the redirect http://127.0.0.1:<random port>,
//    scope "openid profile email", form-encoded token requests without a
//    client secret, and refresh with grant_type=refresh_token
//  - node-nexus-api, src/Nexus.ts (github.com/Nexus-Mods/node-nexus-api): API
//    calls with "Authorization: Bearer <access token>" in place of "apikey",
//    for REST v1 and GraphQL v2 alike; download_link still takes the nxm
//    link's key and expires
//  - NexusMods.App, src/NexusMods.Networking.NexusWebApi/Auth/OAuth.cs: the
//    same parameters, and "the backend has a list of valid redirect URLs and
//    client IDs" (each client is registered by Nexus)

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace NexusOAuthCore
{

// endpoints, from the discovery document above
inline constexpr char AuthorizeUrl[] = "https://users.nexusmods.com/oauth/authorize";
inline constexpr char TokenUrl[]     = "https://users.nexusmods.com/oauth/token";
inline constexpr char RevokeUrl[]    = "https://users.nexusmods.com/oauth/revoke";
inline constexpr char UserInfoUrl[]  = "https://users.nexusmods.com/oauth/userinfo";

// the scopes Vortex and NexusMods.App ask for. MUST BE CONFIRMED WITH NEXUS:
// the discovery document lists only "public" and "openid" among its scopes;
// MO2 Revamped needs the user's id, name and membership (Premium or not), not
// the e-mail, so "openid profile" may be enough
inline constexpr char Scope[] = "openid profile email";

// the path of the loopback redirect: Vortex's registered redirect is
// "http://127.0.0.1:<port>" with no path, so the browser asks for "/".
// MUST BE CONFIRMED WITH NEXUS for MO2 Revamped's client (the redirect Nexus
// registers, and that it allows any port, as RFC 8252 section 7.3 asks)
inline constexpr char RedirectPath[] = "/";

// how long the loopback redirect waits for the browser
inline constexpr int SignInTimeoutSeconds = 5 * 60;

// the access token is refreshed this long before it expires
inline constexpr int RefreshMarginSeconds = 5 * 60;

// when neither expires_in nor the token's own "exp" says how long it lasts
inline constexpr int FallbackLifetimeSeconds = 30 * 60;

// base64url without padding (RFC 4648 section 5, as RFC 7636 appendix A asks)
QByteArray base64Url(const QByteArray& data);

// a code verifier: 32 random bytes as base64url, 43 characters from the
// unreserved set (RFC 7636 section 4.1)
QString makeVerifier();

// BASE64URL(SHA256(ASCII(verifier))) (RFC 7636 section 4.2, S256)
QString challengeS256(const QString& verifier);

// 16 random bytes as base64url, to tie the redirect to this sign-in
QString makeState();

// "http://127.0.0.1:<port>" plus RedirectPath when it isn't "/"
QString redirectUri(quint16 port);

// the browser's link to Nexus' consent page
QUrl authorizeUrl(const QString& clientId, const QString& redirectUri,
                  const QString& challenge, const QString& state,
                  const QString& scope = QString::fromLatin1(Scope));

// application/x-www-form-urlencoded bodies of the token endpoint
QByteArray codeExchangeBody(const QString& clientId, const QString& redirectUri,
                            const QString& code, const QString& verifier);
QByteArray refreshBody(const QString& clientId, const QString& refreshToken);
QByteArray revokeBody(const QString& clientId, const QString& token);

// what came to the loopback listener
struct Redirect
{
  enum Kind
  {
    // more bytes are needed before the request line and headers end
    Incomplete,
    // not an HTTP GET request, or too long
    Malformed,
    // another path (a favicon, a probe): refused, the listener waits on
    WrongPath,
    // the redirect, but its state isn't this sign-in's: refused, the
    // listener waits on for the real one
    StateMismatch,
    // the redirect with an error (the user declined, for one)
    Error,
    // the redirect with the authorization code
    Code
  };

  Kind kind = Incomplete;
  QString code;
  QString error;
  QString errorDescription;
};

// reads the request head the browser sent to the listener. The redirect is a
// GET to RedirectPath with code and state, or with error and state. With
// expectedHost ("127.0.0.1:<port>"), a request whose Host header names
// anything else (a DNS rebinding page, a request without Host) is Malformed
Redirect parseRedirect(const QByteArray& head, const QString& expectedState,
                       const QString& expectedPath = QString::fromLatin1(RedirectPath),
                       const QString& expectedHost = QString());

// the Host header the browser sends to the loopback redirect: "127.0.0.1:<port>"
QString redirectHost(quint16 port);

// a complete HTTP/1.1 answer for the browser's tab, closing the connection
QByteArray httpResponse(int status, const QString& title, const QString& message);

// the token endpoint's answer
struct Tokens
{
  QString accessToken;
  QString refreshToken;
  // seconds, 0 when the answer doesn't say
  qint64 expiresIn = 0;
  // RFC 6749 section 5.2, when it failed
  QString error;
  QString errorDescription;

  bool ok() const { return !accessToken.isEmpty() && error.isEmpty(); }
};

Tokens parseTokens(const QByteArray& json);

// the "exp" claim of a JWT, without checking its signature (only to know when
// to refresh); invalid when the token isn't a JWT or has none
QDateTime jwtExpiry(const QString& token);

// when an access token received at `now` expires: expires_in, else the JWT's
// exp, else FallbackLifetimeSeconds
QDateTime expiryOf(const Tokens& t, const QDateTime& now);

// the userinfo endpoint's answer
struct UserInfo
{
  QString id;
  QString name;
  bool premium = false;
};

// "sub", "name" and "membership_roles"; nothing when there is no "sub"
std::optional<UserInfo> parseUserInfo(const QByteArray& json);

// Premium for downloads: "premium" or "lifetimepremium" (Vortex,
// nexus_integration/util.ts). "supporter" alone is an old one-time supporter
// without Premium downloads, so it counts as a regular account: its nxm links
// still carry their key, as a free account's
bool isPremium(const QStringList& membershipRoles);

// the access token's credential: the token and its expiry in one string
QString packAccess(const QString& token, const QDateTime& expiresUtc);
bool unpackAccess(const QString& packed, QString& token, QDateTime& expiresUtc);

// splits a value into pieces of at most `size` characters, for Windows
// Credential Manager (a generic credential holds at most 2560 bytes, 1280
// UTF-16 characters, and a JWT can be longer); nothing for an empty value
QStringList splitForCredential(const QString& value, int size = 1200);

// the text with OAuth secrets taken out, for logs and messages: access and
// refresh tokens, codes and verifiers as query or form parameters or JSON
// values, "Bearer ..." and JSON web tokens
QString redactSecrets(QString text);

}  // namespace NexusOAuthCore

#endif  // NEXUSOAUTHCORE_H
