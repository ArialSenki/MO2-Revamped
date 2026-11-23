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

#ifndef NXMACCESSMANAGER_H
#define NXMACCESSMANAGER_H

#include "apiuseraccount.h"
#include "ui_validationprogressdialog.h"
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QTimer>
#include <QJsonDocument>
#include <QWebSocket>
#include <memory>
#include <set>

namespace MOBase
{
class IPluginGame;
}
class NXMAccessManager;
class NexusOAuth;
class Settings;

class NexusSSOLogin
{
public:
  enum States
  {
    ConnectingToSSO,
    WaitingForToken,
    WaitingForBrowser,
    Finished,
    Timeout,
    ClosedByRemote,
    Cancelled,
    Error
  };

  std::function<void(QString)> keyChanged;
  std::function<void(States, QString)> stateChanged;

  static QString stateToString(States s, const QString& e);

  NexusSSOLogin();

  void start();
  void cancel();

  bool isActive() const;

private:
  QWebSocket m_socket;
  QString m_guid;
  bool m_keyReceived;
  bool m_active;
  QTimer m_timeout;

  void setState(States s, const QString& error = {});

  void close();
  void abort();

  void onConnected();
  void onMessage(const QString& s);
  void onDisconnected();
  void onError(QAbstractSocket::SocketError e);
  void onSslErrors(const QList<QSslError>& errors);
  void onTimeout();
};

// the message of a key Nexus refused (401, 403): trying the same key again
// can't help, so the failure says where to connect again
QString nexusKeyRefusedMessage();

// MO2 Revamped: the same for a Nexus sign-in (OAuth) whose token Nexus refused
QString nexusSignInRefusedMessage();

class ValidationAttempt
{
public:
  enum Result
  {
    None,
    Success,
    SoftError,
    HardError,
    Cancelled
  };

  std::function<void(APIUserAccount)> success;
  std::function<void()> failure;

  ValidationAttempt(std::chrono::seconds timeout);
  ValidationAttempt(const ValidationAttempt&)            = delete;
  ValidationAttempt& operator=(const ValidationAttempt&) = delete;

  // bearer: `key` is the access token of a Nexus sign-in (OAuth), checked at
  // the userinfo endpoint; otherwise an API key, checked at users/validate
  void start(NXMAccessManager& m, const QString& key, bool bearer = false);
  void cancel();

  bool done() const;

  // MO2 Revamped: the HTTP status of the answer; 0 without one (a timeout, no
  // connection, cancelled)
  int httpStatus() const { return m_httpStatus; }
  // started and still waiting for Nexus
  bool running() const;
  Result result() const;
  const QString& message() const;
  std::chrono::seconds timeout() const;
  QElapsedTimer elapsed() const;

private:
  QNetworkReply* m_reply;
  Result m_result;
  QString m_message;
  QTimer m_timeout;
  QElapsedTimer m_elapsed;
  bool m_bearer = false;
  int m_httpStatus = 0;

  bool sendRequest(NXMAccessManager& m, const QString& key);

  void onFinished();
  void onBearerFinished(int code, const QJsonDocument& doc,
                        const QList<QNetworkReply::RawHeaderPair>& headers);
  void onSslErrors(const QList<QSslError>& errors);
  void onTimeout();

  void setFailure(Result r, const QString& error);
  void setSuccess(const APIUserAccount& user);

  void cleanup();
};

class NexusKeyValidator
{
public:
  enum Behaviour
  {
    OneShot = 0,
    Retry
  };

  using FinishedCallback = void(ValidationAttempt::Result, const QString&,
                                std::optional<APIUserAccount>);

  std::function<FinishedCallback> finished;
  std::function<void(const ValidationAttempt&)> attemptFinished;

  NexusKeyValidator(Settings* s, NXMAccessManager& am);
  ~NexusKeyValidator();

  void start(const QString& key, Behaviour b);
  // MO2 Revamped: checks the Nexus sign-in (OAuth) instead of an API key; its
  // access token is renewed first when it has expired
  void startOAuth(Behaviour b);
  void cancel();

  bool isActive() const;
  const ValidationAttempt* lastAttempt() const;
  const ValidationAttempt* currentAttempt() const;

private:
  Settings* m_settings;
  NXMAccessManager& m_manager;
  QString m_key;
  bool m_bearer = false;
  // waiting for the sign-in's access token before an attempt; the guard tells
  // the token's callback whether this validator still exists
  bool m_waitingForToken = false;
  std::shared_ptr<int> m_alive = std::make_shared<int>(0);
  std::vector<std::unique_ptr<ValidationAttempt>> m_attempts;

  // the attempts of the check before: a new check can start from the callback
  // of the last one (Login failed, try again?), whose attempt is still running
  // its code, so they are only destroyed at the start after that
  std::vector<std::unique_ptr<ValidationAttempt>> m_previousAttempts;

  void createAttempts(const std::vector<std::chrono::seconds>& timeouts);
  std::vector<std::chrono::seconds> getTimeouts() const;

  bool nextTry();
  void onAttemptSuccess(const ValidationAttempt& a, const APIUserAccount& u);
  void onAttemptFailure(const ValidationAttempt& a);

  void setFinished(ValidationAttempt::Result r, const QString& message,
                   std::optional<APIUserAccount> user);
};

class ValidationProgressDialog : public QDialog
{
  Q_OBJECT;

public:
  ValidationProgressDialog(Settings* s, NexusKeyValidator& v);

  void setParentWidget(QWidget* w);

  void start();
  void stop();

protected:
  void showEvent(QShowEvent* e) override;
  void closeEvent(QCloseEvent* e) override;

private:
  std::unique_ptr<Ui::ValidationProgressDialog> ui;
  Settings* m_settings;
  NexusKeyValidator& m_validator;
  QTimer* m_updateTimer;
  bool m_first;

  void onHide();
  void onCancel();
  void onTimer();
  void updateProgress();
};

/**
 * @brief access manager extended to handle nxm links
 **/
class NXMAccessManager : public QNetworkAccessManager
{
  Q_OBJECT
public:
  NXMAccessManager(QObject* parent, Settings* s, const QString& moVersion);

  void setTopLevelWidget(QWidget* w);

  bool validated() const;

  bool validateAttempted() const;
  bool validateWaiting() const;

  void apiCheck(const QString& apiKey, bool force = false);

  // MO2 Revamped: checks what is stored: the Nexus sign-in (OAuth) when there
  // is one, else the API key; false when neither is
  bool startStoredCheck(bool force = false);

  // MO2 Revamped: the Nexus sign-in (OAuth); never null
  NexusOAuth* oauth() const { return m_oauth; }

  void clearCookies();

  QString userAgent(const QString& subModule = QString()) const;
  const QString& MOVersion() const;

  void clearApiKey();

  // an account validated elsewhere (Settings): applies it without checking again
  void setLinkedAccount(const APIUserAccount& user);

  void refuseValidation();

signals:

  /**
   * @brief emitted when a nxm:// link is opened
   *
   * @param url the nxm-link
   **/
  void requestNXMDownload(const QString& url);

  /**
   * @brief emitted after a successful login or if login was not necessary
   *
   * @param necessary true if a login was necessary and succeeded, false if the user is
   *still logged in
   **/
  void validateSuccessful(bool necessary);
  void validateFailed(const QString& message);
  void credentialsReceived(const APIUserAccount& user);

protected:
  virtual QNetworkReply* createRequest(QNetworkAccessManager::Operation operation,
                                       const QNetworkRequest& request,
                                       QIODevice* device);

private:
  enum States
  {
    NotChecked,
    Valid,
    Invalid
  };

  QWidget* m_TopLevel;
  Settings* m_Settings;
  mutable std::unique_ptr<ValidationProgressDialog> m_ProgressDialog;
  QString m_MOVersion;
  NexusKeyValidator m_validator;
  States m_validationState;
  NexusOAuth* m_oauth;

  void startValidationCheck(const QString& key);
  void startOAuthCheck();

  void onValidatorFinished(ValidationAttempt::Result r, const QString& message,
                           std::optional<APIUserAccount>);

  void onValidatorAttemptFinished(const ValidationAttempt& a);

  void startProgress();
  void stopProgress();
};

#endif  // NXMACCESSMANAGER_H
