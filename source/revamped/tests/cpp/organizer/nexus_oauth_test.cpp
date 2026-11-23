// MO2 Revamped: tests for the network-free parts of "Sign in with Nexus Mods"
// (nexusoauthcore.cpp, not shipped): the PKCE challenge against RFC 7636's
// test vector, the verifier and state, the authorize link, the loopback
// redirect (wrong path, state mismatch, error, code), the token and user
// answers, the credential pieces, and the redaction of tokens and codes.
// Build and run: run.ps1 in this folder, or from a Developer prompt with the
// Qt path of run.ps1:
//   cl /nologo /std:c++20 /EHsc /permissive- /utf-8 /MD /DUNICODE /D_UNICODE /DNOMINMAX
//      /I<qt>\include /I<qt>\include\QtCore /I<src> nexus_oauth_test.cpp
//      <src>\nexusoauthcore.cpp /link <qt>\lib\Qt6Core.lib
//   nexus_oauth_test
#include "nexusoauthcore.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QTimeZone>
#include <QUrlQuery>

#include <cstdio>

using namespace NexusOAuthCore;

namespace
{
int passed = 0;
int failed = 0;

void check(bool ok, const char* what, int line, const QString& detail = {})
{
  if (ok) {
    ++passed;
  } else {
    ++failed;
    std::printf("FAIL line %d: %s %s\n", line, what, qUtf8Printable(detail));
  }
}

#define CHECK(x) check((x), #x, __LINE__)
#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    const QString got_  = (a);                                                             \
    const QString want_ = (b);                                                             \
    check(got_ == want_, #a " == " #b, __LINE__,                                           \
          QStringLiteral("(got \"%1\", want \"%2\")").arg(got_, want_));                   \
  } while (false)

QByteArray get(const QByteArray& target)
{
  return "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1:50000\r\n\r\n";
}

void testPkce()
{
  // RFC 7636, appendix B
  CHECK_EQ(challengeS256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
           "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");

  static const QRegularExpression unreserved(QStringLiteral("^[A-Za-z0-9._~-]+$"));
  QSet<QString> seen;
  for (int i = 0; i < 50; ++i) {
    const QString v = makeVerifier();
    // 43 to 128 characters of the unreserved set (RFC 7636 section 4.1)
    check(v.size() >= 43 && v.size() <= 128, "verifier length", __LINE__, v);
    check(unreserved.match(v).hasMatch(), "verifier characters", __LINE__, v);
    seen.insert(v);
    const QString c = challengeS256(v);
    check(c.size() == 43 && !c.contains(u'='), "challenge form", __LINE__, c);
  }
  CHECK(seen.size() == 50);

  const QString s1 = makeState();
  const QString s2 = makeState();
  CHECK(s1.size() >= 22);
  CHECK(s1 != s2);
  CHECK(unreserved.match(s1).hasMatch());
}

void testAuthorizeUrl()
{
  CHECK_EQ(redirectUri(51234), "http://127.0.0.1:51234");

  const QUrl url = authorizeUrl("mo2_revamped", redirectUri(51234), "CHALLENGE", "STATE");
  CHECK_EQ(url.scheme() + "://" + url.host() + url.path(),
           "https://users.nexusmods.com/oauth/authorize");
  const QUrlQuery q(url);
  CHECK_EQ(q.queryItemValue("response_type"), "code");
  CHECK_EQ(q.queryItemValue("client_id"), "mo2_revamped");
  CHECK_EQ(q.queryItemValue("redirect_uri", QUrl::FullyDecoded), "http://127.0.0.1:51234");
  CHECK_EQ(q.queryItemValue("scope", QUrl::FullyDecoded), "openid profile email");
  CHECK_EQ(q.queryItemValue("code_challenge"), "CHALLENGE");
  CHECK_EQ(q.queryItemValue("code_challenge_method"), "S256");
  CHECK_EQ(q.queryItemValue("state"), "STATE");
  // spaces as %20 and the redirect encoded, not "+" or raw
  const QString encoded = QString::fromLatin1(url.toEncoded());
  CHECK(encoded.contains("scope=openid%20profile%20email"));
  CHECK(encoded.contains("redirect_uri=http%3A%2F%2F127.0.0.1%3A51234"));
}

void testBodies()
{
  CHECK_EQ(QString::fromLatin1(codeExchangeBody("cid", "http://127.0.0.1:5", "a+b/c", "v~1")),
           "grant_type=authorization_code&client_id=cid&redirect_uri=http%3A%2F%2F127.0.0.1%"
           "3A5&code=a%2Bb%2Fc&code_verifier=v~1");
  CHECK_EQ(QString::fromLatin1(refreshBody("cid", "r=t")),
           "grant_type=refresh_token&client_id=cid&refresh_token=r%3Dt");
  CHECK_EQ(QString::fromLatin1(revokeBody("cid", "rt")),
           "client_id=cid&token=rt&token_type_hint=refresh_token");
}

void testRedirect()
{
  const QString state = "S7a-_x";

  Redirect r = parseRedirect(get("/?code=THE_CODE&state=S7a-_x"), state);
  CHECK(r.kind == Redirect::Code);
  CHECK_EQ(r.code, "THE_CODE");

  // a state that isn't this sign-in's is refused
  r = parseRedirect(get("/?code=THE_CODE&state=other"), state);
  CHECK(r.kind == Redirect::StateMismatch);
  CHECK(r.code.isEmpty());
  r = parseRedirect(get("/?code=THE_CODE"), state);
  CHECK(r.kind == Redirect::StateMismatch);
  r = parseRedirect(get("/?code=THE_CODE&state=S7a-_x&state=S7a-_x"), state);
  CHECK(r.kind == Redirect::StateMismatch);
  r = parseRedirect(get("/?code=THE_CODE&state="), QString());
  CHECK(r.kind == Redirect::StateMismatch);
  r = parseRedirect(get("/?code=THE_CODE&state=S7a-_"), state);
  CHECK(r.kind == Redirect::StateMismatch);

  // other paths are refused and the listener waits on
  CHECK(parseRedirect(get("/favicon.ico"), state).kind == Redirect::WrongPath);
  CHECK(parseRedirect(get("/callback?code=a&state=S7a-_x"), state).kind ==
        Redirect::WrongPath);
  CHECK(parseRedirect(get("/callback?code=a&state=S7a-_x"), state, "/callback").kind ==
        Redirect::Code);

  // the user declined
  r = parseRedirect(get("/?error=access_denied&error_description=The+user+denied&state=S7a-_x"),
                    state);
  CHECK(r.kind == Redirect::Error);
  CHECK_EQ(r.error, "access_denied");
  CHECK_EQ(r.errorDescription, "The user denied");
  // an error with someone else's state is still a mismatch
  CHECK(parseRedirect(get("/?error=access_denied&state=x"), state).kind ==
        Redirect::StateMismatch);
  // no code
  CHECK(parseRedirect(get("/?state=S7a-_x"), state).kind == Redirect::Error);

  // not a GET, not HTTP, not finished
  CHECK(parseRedirect("POST /?code=a&state=S7a-_x HTTP/1.1\r\n\r\n", state).kind ==
        Redirect::Malformed);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x\r\n\r\n", state).kind ==
        Redirect::Malformed);
  CHECK(parseRedirect("GET http://evil/ HTTP/1.1\r\n\r\n", state).kind ==
        Redirect::Malformed);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\nHost: x\r\n", state).kind ==
        Redirect::Incomplete);
  CHECK(parseRedirect(QByteArray(20000, 'a'), state).kind == Redirect::Malformed);

  // the Host header, when the listener names it (DNS rebinding)
  const QString slash = QStringLiteral("/");
  const QString host  = redirectHost(50000);
  CHECK_EQ(host, "127.0.0.1:50000");
  CHECK(parseRedirect(get("/?code=a&state=S7a-_x"), state, slash, host).kind ==
        Redirect::Code);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\nhost: 127.0.0.1:50000\r\n\r\n",
                      state, slash, host)
            .kind == Redirect::Code);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\nHost: evil.example:50000\r\n\r\n",
                      state, slash, host)
            .kind == Redirect::Malformed);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\nHost: 127.0.0.1:50001\r\n\r\n",
                      state, slash, host)
            .kind == Redirect::Malformed);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\n\r\n", state, slash, host).kind ==
        Redirect::Malformed);
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\nHost: 127.0.0.1:50000\r\n"
                      "Host: evil.example\r\n\r\n",
                      state, slash, host)
            .kind == Redirect::Malformed);
  // without an expected host, as before
  CHECK(parseRedirect("GET /?code=a&state=S7a-_x HTTP/1.1\r\n\r\n", state).kind ==
        Redirect::Code);
}

void testResponse()
{
  const QByteArray page = httpResponse(200, "Signed in <b>", "You can close this tab.");
  CHECK(page.startsWith("HTTP/1.1 200 OK\r\n"));
  CHECK(page.contains("Connection: close\r\n"));
  CHECK(page.contains("Cache-Control: no-store\r\n"));
  CHECK(page.contains("Signed in &lt;b&gt;"));
  const qsizetype bodyStart = page.indexOf("\r\n\r\n") + 4;
  CHECK(page.contains("Content-Length: " + QByteArray::number(page.size() - bodyStart)));
  CHECK(httpResponse(404, "a", "b").startsWith("HTTP/1.1 404 Not Found\r\n"));
}

void testTokens()
{
  Tokens t = parseTokens(R"({"access_token":"AT","refresh_token":"RT","expires_in":21600,
                             "token_type":"Bearer","scope":"openid"})");
  CHECK(t.ok());
  CHECK_EQ(t.accessToken, "AT");
  CHECK_EQ(t.refreshToken, "RT");
  CHECK(t.expiresIn == 21600);

  t = parseTokens(R"({"error":"invalid_grant","error_description":"revoked"})");
  CHECK(!t.ok());
  CHECK_EQ(t.error, "invalid_grant");
  CHECK_EQ(t.errorDescription, "revoked");

  CHECK_EQ(parseTokens("<html>").error, "invalid_response");
  CHECK_EQ(parseTokens("{}").error, "invalid_response");

  // {"alg":"none"}.{"exp":2000000000,"user":{"id":1}}.
  const QString jwt =
      QString::fromLatin1(base64Url(R"({"alg":"none"})")) + "." +
      QString::fromLatin1(base64Url(R"({"exp":2000000000,"user":{"id":1}})")) + ".sig";
  CHECK(jwtExpiry(jwt) == QDateTime::fromSecsSinceEpoch(2000000000, QTimeZone::UTC));
  CHECK(!jwtExpiry("not-a-jwt").isValid());
  CHECK(!jwtExpiry("a.%%%.c").isValid());

  const QDateTime now = QDateTime::fromSecsSinceEpoch(1000, QTimeZone::UTC);
  Tokens withJwt;
  withJwt.accessToken = jwt;
  CHECK(expiryOf(withJwt, now) == QDateTime::fromSecsSinceEpoch(2000000000, QTimeZone::UTC));
  withJwt.expiresIn = 60;
  CHECK(expiryOf(withJwt, now) == now.addSecs(60));
  Tokens opaque;
  opaque.accessToken = "opaque";
  CHECK(expiryOf(opaque, now) == now.addSecs(FallbackLifetimeSeconds));
}

void testUserInfo()
{
  auto u = parseUserInfo(R"({"sub":"1234","name":"Someone","email":"x@y.z",
                             "membership_roles":["member","premium"]})");
  CHECK(u.has_value());
  CHECK_EQ(u->id, "1234");
  CHECK_EQ(u->name, "Someone");
  CHECK(u->premium);

  u = parseUserInfo(R"({"sub":42,"name":"N","membership_roles":["member","supporter"]})");
  CHECK(u.has_value());
  CHECK_EQ(u->id, "42");
  CHECK(!u->premium);

  CHECK(parseUserInfo(R"({"sub":"7","membership_roles":["lifetimepremium"]})")->premium);
  CHECK(!parseUserInfo(R"({"name":"no id"})").has_value());
  CHECK(!parseUserInfo("[]").has_value());
}

void testCredentialPieces()
{
  QString token;
  QDateTime expires;
  const QDateTime when = QDateTime::fromSecsSinceEpoch(1800000000, QTimeZone::UTC);
  CHECK(unpackAccess(packAccess("eyJ.abc.def", when), token, expires));
  CHECK_EQ(token, "eyJ.abc.def");
  CHECK(expires == when);
  CHECK(!unpackAccess("", token, expires));
  CHECK(!unpackAccess(R"({"token":"","expires":5})", token, expires));

  CHECK(splitForCredential("").isEmpty());
  const QString longValue(2500, u'x');
  const QStringList pieces = splitForCredential(longValue);
  CHECK(pieces.size() == 3);
  CHECK(pieces.join(QString()) == longValue);
  for (const QString& p : pieces) {
    // 2560 bytes at most in a generic credential, as UTF-16
    CHECK(p.size() * 2 <= 2560);
  }
}

void testRedaction()
{
  const QString jwt = "eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiIxMjM0In0.c2lnbmF0dXJlLXZhbHVl";

  QString out = redactSecrets(
      R"({"access_token":"AT-SECRET-1","refresh_token":"RT-SECRET-2","expires_in":3600})");
  CHECK(!out.contains("AT-SECRET-1"));
  CHECK(!out.contains("RT-SECRET-2"));
  CHECK(out.contains("\"expires_in\":3600"));

  out = redactSecrets("GET /?code=CODE-SECRET-3&state=abc HTTP/1.1");
  CHECK(!out.contains("CODE-SECRET-3"));
  CHECK(out.contains("state=abc"));

  out = redactSecrets("grant_type=authorization_code&code=CODE-4&code_verifier=VER-5");
  CHECK(!out.contains("CODE-4"));
  CHECK(!out.contains("VER-5"));
  CHECK(out.contains("grant_type=authorization_code"));

  out = redactSecrets("Authorization: Bearer " + jwt);
  CHECK(!out.contains(jwt));
  CHECK(!out.contains("c2lnbmF0dXJl"));

  out = redactSecrets("token " + jwt + " in a message");
  CHECK(!out.contains("eyJzdWIi"));

  out = redactSecrets("refresh_token=RT-6&client_id=mo2");
  CHECK(!out.contains("RT-6"));
  CHECK(out.contains("client_id=mo2"));

  // ordinary text stays
  CHECK_EQ(redactSecrets("exit code 0, error code: 5, tokens left: 3"),
           "exit code 0, error code: 5, tokens left: 3");
}

}  // namespace

int main()
{
  testPkce();
  testAuthorizeUrl();
  testBodies();
  testRedirect();
  testResponse();
  testTokens();
  testUserInfo();
  testCredentialPieces();
  testRedaction();
  std::printf("nexus_oauth_test: %d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
