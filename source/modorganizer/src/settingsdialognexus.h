#ifndef SETTINGSDIALOGNEXUS_H
#define SETTINGSDIALOGNEXUS_H

#include "nxmaccessmanager.h"
#include "settings.h"
#include "settingsdialog.h"

// used by the settings dialog and the create instance dialog
//
class NexusConnectionUI : public QObject
{
  Q_OBJECT;

public:
  NexusConnectionUI(QWidget* parent, Settings* s, QAbstractButton* connectButton,
                    QAbstractButton* disconnectButton, QAbstractButton* manualButton,
                    QListWidget* logList);

  void connect();
  void manual();
  void disconnect();
  // MO2 Revamped: "Sign in with Nexus Mods" (OAuth with PKCE), when the build
  // has a client id (MO2R_NEXUS_OAUTH_CLIENT_ID)
  void signIn();

signals:
  void stateChanged();
  void keyChanged();

private:
  QWidget* m_parent;
  Settings* m_settings;
  QAbstractButton* m_connect;
  QAbstractButton* m_disconnect;
  QAbstractButton* m_manual;
  QAbstractButton* m_signIn = nullptr;
  QListWidget* m_log;
  // this page started the sign-in in the browser (Settings and the instance
  // wizard share the one NexusOAuth)
  bool m_signInStarted = false;
  // the account being checked came from the sign-in, not from a key
  bool m_checkingSignIn = false;

  void onSignInFinished(bool ok, const QString& message);

  std::unique_ptr<NexusSSOLogin> m_nexusLogin;
  std::unique_ptr<NexusKeyValidator> m_nexusValidator;

  void addLog(const QString& s);

  void updateState();

  void validateKey(const QString& key);
  bool setKey(const QString& key);
  bool clearKey();

  void onSSOKeyChanged(const QString& key);
  void onSSOStateChanged(NexusSSOLogin::States s, const QString& e);

  void onValidatorFinished(ValidationAttempt::Result r, const QString& message,
                           std::optional<APIUserAccount> useR);
};

class NexusSettingsTab : public SettingsTab
{
public:
  NexusSettingsTab(Settings& settings, SettingsDialog& dialog);
  void update();

private:
  std::unique_ptr<NexusConnectionUI> m_connectionUI;

  void clearCache();
  void associate();

  void updateNexusData();
};

#endif  // SETTINGSDIALOGNEXUS_H
