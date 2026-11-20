#include "multiprocess.h"
#include "env.h"
#include "utility.h"
#include <QLocalSocket>
#include <QTimer>
#include <log.h>
#include <report.h>
#include <scopeguard.h>

#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <memory>

// the shared memory lock (shared with upstream MO2 on purpose: one or the
// other runs) and, before MO2 Revamped 1.1.0 beta 15, the pipe
static const char s_Key[]  = "mo-43d1a3ad-eeb0-4818-97c9-eda5216c29b5";
static const int s_Timeout = 5000;
// how long a link waits for a starting MO2 to take it
static const int s_AnswerTimeout = 60000;

using MOBase::reportError;

namespace
{

// the SID of the account this process runs as, as S-1-5-...
struct UserSid
{
  std::unique_ptr<BYTE[]> buffer;
  PSID sid = nullptr;
  QString text;
};

const UserSid& currentUserSid()
{
  static const UserSid user = [] {
    UserSid result;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      return result;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size > 0) {
      result.buffer = std::make_unique<BYTE[]>(size);
      if (GetTokenInformation(token, TokenUser, result.buffer.get(), size, &size)) {
        result.sid  = reinterpret_cast<TOKEN_USER*>(result.buffer.get())->User.Sid;
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(result.sid, &text)) {
          result.text = QString::fromWCharArray(text);
          LocalFree(text);
        }
      }
    }
    CloseHandle(token);
    return result;
  }();
  return user;
}

// MO2 Revamped: the pipe is per Windows account. Pipe names are the same for
// every account of the PC, so with the fixed name another account's MO2 (or
// a program of another account) took this one's links and command lines
QString pipeName()
{
  const QString& sid = currentUserSid().text;
  return sid.isEmpty() ? QString(s_Key) : QString(s_Key) + "-" + sid;
}

enum class ServerOwner
{
  ThisUser,
  OtherAccount,
  Unknown
};

// whether the pipe was made by this account (or, for an MO2 running as
// administrator, by Administrators): another account can create a pipe of
// any name, even this one's, and then read every nxm link with its key.
// Unknown when this process's account or the pipe's owner can't be read
ServerOwner serverOwner(QLocalSocket& socket)
{
  const UserSid& user = currentUserSid();
  if (user.sid == nullptr) {
    MOBase::log::error("couldn't read this process's Windows account");
    return ServerOwner::Unknown;
  }
  PSID owner                = nullptr;
  PSECURITY_DESCRIPTOR desc = nullptr;
  const auto error =
      GetSecurityInfo(reinterpret_cast<HANDLE>(socket.socketDescriptor()),
                      SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr,
                      nullptr, nullptr, &desc);
  if (error != ERROR_SUCCESS) {
    MOBase::log::error("couldn't check who runs the other MO2: error {}", error);
    return ServerOwner::Unknown;
  }
  const bool ok = owner != nullptr && (EqualSid(owner, user.sid) ||
                                       IsWellKnownSid(owner, WinBuiltinAdministratorsSid));
  if (!ok) {
    LPWSTR text = nullptr;
    QString name;
    if (owner != nullptr && ConvertSidToStringSidW(owner, &text)) {
      name = QString::fromWCharArray(text);
      LocalFree(text);
    }
    MOBase::log::error("the program answering on MO2's pipe runs as another account "
                       "({}); nothing was sent to it",
                       name);
  }
  LocalFree(desc);
  return ok ? ServerOwner::ThisUser : ServerOwner::OtherAccount;
}

}  // namespace

MOMultiProcess::MOMultiProcess(bool allowMultiple, QObject* parent)
    : QObject(parent), m_Ephemeral(false), m_OwnsSM(false)
{
  m_SharedMem.setKey(s_Key);

  if (!m_SharedMem.create(1)) {
    // MO2 Revamped: "permission denied" is another MO2 too, one running as
    // administrator: a link clicked then goes to it instead of crashing here
    // (the attach below fails the same way, and that is no error either)
    const auto error = m_SharedMem.error();
    if (error == QSharedMemory::AlreadyExists ||
        error == QSharedMemory::PermissionDenied) {
      if (!allowMultiple) {
        if (!m_SharedMem.attach()) {
          MOBase::log::debug("the other MO2's lock couldn't be opened: {}",
                             m_SharedMem.errorString());
        }
        m_Ephemeral = true;
      }
    } else {
      throw MOBase::MyException(tr("SHM error: %1").arg(m_SharedMem.errorString()));
    }
  } else {
    m_OwnsSM = true;
  }

  if (m_OwnsSM) {
    connect(&m_Server, SIGNAL(newConnection()), this, SLOT(receiveMessage()),
            Qt::QueuedConnection);
    // has to be called before listen
    // MO2 Revamped: only this Windows user, not every account on the PC. The
    // pipe takes whole command lines (run <program>, reload-plugin <dll>), so
    // world access let another account run programs as this user. The user's
    // own SID still lets a link reach an MO2 running as administrator
    m_Server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_Server.listen(pipeName())) {
      // links clicked while this MO2 runs then can't reach it
      MOBase::log::error("couldn't open the pipe for links from other MO2 "
                         "processes ({}): {}",
                         pipeName(), m_Server.errorString());
    }
    // the old name too, for this user only as well: upstream MO2 and MO2
    // Revamped before beta 15 share the lock and send there. If another
    // account took that name first, only those lose their way here
    if (pipeName() != s_Key) {
      connect(&m_LegacyServer, SIGNAL(newConnection()), this, SLOT(receiveMessage()),
              Qt::QueuedConnection);
      m_LegacyServer.setSocketOptions(QLocalServer::UserAccessOption);
      if (!m_LegacyServer.listen(s_Key)) {
        MOBase::log::warn("couldn't open the pipe older MO2 versions send to ({}): {}",
                          s_Key, m_LegacyServer.errorString());
      }
    }
  }
}

void MOMultiProcess::sendMessage(const QString& message)
{
  if (m_OwnsSM) {
    // nobody there to receive the message
    return;
  }
  QLocalSocket socket(this);

  bool connected = false;
  for (int i = 0; i < 2 && !connected; ++i) {
    if (i > 0) {
      Sleep(250);
    }

    // other process may be just starting up
    socket.connectToServer(pipeName(), QIODevice::ReadWrite);
    connected = socket.waitForConnected(s_Timeout);
    // an MO2 from before the per-account pipe (or upstream MO2, which shares
    // the lock) listens on the old name
    if (!connected && socket.error() == QLocalSocket::ServerNotFoundError &&
        pipeName() != s_Key) {
      socket.abort();
      socket.connectToServer(s_Key, QIODevice::ReadWrite);
      connected = socket.waitForConnected(s_Timeout);
    }
  }

  if (connected) {
    const ServerOwner owner = serverOwner(socket);
    // MO2 Revamped: Wine and Proton have no Windows accounts to keep apart
    // (every program of a prefix runs as the same Linux user); if a Wine
    // version can't report a pipe's owner, links mustn't be refused for it
    if (owner != ServerOwner::ThisUser && env::runningUnderWine()) {
      MOBase::log::debug("under Wine: the pipe's owner isn't checked");
    } else if (owner != ServerOwner::ThisUser) {
      socket.abort();
      reportError(owner == ServerOwner::OtherAccount
                      ? tr("Another Windows account's program answered instead of "
                           "your Mod Organizer; nothing was sent to it.")
                      : tr("Mod Organizer couldn't check that the Mod Organizer "
                           "that is open runs as your Windows account; nothing was "
                           "sent to it."));
      return;
    }
  }

  // MO2 Revamped: started as administrator with another account's password,
  // the open MO2 listens for that account only (links can't cross accounts)
  const QString otherAccount =
      tr("If Mod Organizer was started as administrator with another account's "
         "password, links can't reach it: start it with your own account.");

  if (!connected) {
    // MO2 Revamped: a link said plainly; the socket's error is for the log
    if (message.startsWith("nxm://", Qt::CaseInsensitive)) {
      MOBase::log::error("failed to connect to running process: {}",
                         socket.errorString());
      reportError(tr("Mod Organizer is running but didn't answer; the link wasn't "
                     "added. Try again in a moment.") +
                  "\n\n" + otherAccount);
    } else {
      reportError(
          tr("failed to connect to running process: %1").arg(socket.errorString()) +
          "\n\n" + otherAccount);
    }
    return;
  }

  socket.write(message.toUtf8());
  if (!socket.waitForBytesWritten(s_Timeout)) {
    if (socket.bytesToWrite()) {
      reportError(tr("failed to communicate with running process: %1")
                      .arg(socket.errorString()));
    }
  }

  // MO2 Revamped: the connection stays open until the running MO2 has read the
  // message: it answers, or closes the connection (an older one). Closed right
  // away, a message sent while that MO2 was still starting (a browser's links
  // while it opens) was lost: "failed to receive data from secondary process"
  socket.waitForReadyRead(s_AnswerTimeout);

  socket.disconnectFromServer();
  socket.waitForDisconnected();
}

void MOMultiProcess::receiveMessage()
{
  // MO2 Revamped: every connection waiting, each read without blocking. The
  // sender keeps its connection open until it gets the answer (see
  // sendMessage()), so waitForReadyRead() on a message that had already
  // arrived waited out its whole timeout: MO2 froze for five seconds on each
  // nxm link before "Download started"
  // the per-user pipe or the old name (see the constructor)
  auto* server = qobject_cast<QLocalServer*>(sender());
  if (server == nullptr) {
    server = &m_Server;
  }
  while (QLocalSocket* socket = server->nextPendingConnection()) {
    if (socket->bytesAvailable() > 0) {
      readMessage(socket);
      continue;
    }

    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
      readMessage(socket);
    });
    // closed with nothing sent, or nothing within the timeout: as before, said
    // once (the socket goes either way)
    const auto fail = [socket] {
      if (socket->property("mo2r.read").toBool()) {
        return;
      }
      socket->setProperty("mo2r.read", true);
      MOBase::log::error("failed to receive data from secondary process: {}",
                         socket->errorString());
      reportError(tr("failed to receive data from secondary process: %1")
                      .arg(socket->errorString()));
      socket->deleteLater();
    };
    connect(socket, &QLocalSocket::disconnected, this, [this, socket, fail] {
      // whatever came before it closed is still the message
      if (socket->bytesAvailable() > 0) {
        readMessage(socket);
      } else {
        fail();
      }
    });
    QTimer::singleShot(s_Timeout, socket, [this, socket, fail] {
      if (socket->bytesAvailable() > 0) {
        readMessage(socket);
      } else {
        fail();
      }
    });
  }
}

void MOMultiProcess::readMessage(QLocalSocket* socket)
{
  if (socket->property("mo2r.read").toBool()) {
    return;
  }
  socket->setProperty("mo2r.read", true);

  // the server owns each connection until it closes: one per message would
  // otherwise stay open until MO2 exits
  ON_BLOCK_EXIT([socket] {
    socket->deleteLater();
  });

  QString message = QString::fromUtf8(socket->readAll().constData());
  // the sender waits for this before closing (see sendMessage())
  if (socket->state() == QLocalSocket::ConnectedState) {
    socket->write("+");
    socket->flush();
  }
  emit messageSent(message);
  socket->disconnectFromServer();
}
