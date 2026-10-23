#include "handlerwindow.h"
#include "handlerstorage.h"
#include <nxmurl.h>
#include <utility.h>
#include <QApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDateTime>
#include <QStandardPaths>
#include "logger.h"
#include "fluentstyle.h"
#include "handlereditdialog.h"
#include <QLocale>
#include <QPushButton>
#include <QTranslator>


#pragma comment(linker, "/manifestDependency:\"name='dlls' processorArchitecture='x86' version='1.0.0.0' type='win32' \"")

using MOBase::ToWString;

static QString g_LogFileName = "";

void logHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  if (g_LogFileName.isEmpty()) {
    return;
  }

  QFile file(g_LogFileName);

  // Prevent the log from growing infinitely
  if (file.size() > 10 * 1024 * 1024) {
    if (!file.open(QIODevice::WriteOnly)) {
      return;
    }
  } else if (!file.open(QIODevice::Append)) {
    return;
  }

  file.write(qUtf8Printable(QString("[%1] %2\n").arg(QDateTime::currentDateTime().toString()).arg(message)));
}

void handleLink(const QString &executable, const QString &arguments, const QString &link)
{
  QString quotedExecutable(executable);
  if (!quotedExecutable.contains(QRegularExpression("^\".*\"$"))) {
    quotedExecutable = '"' + quotedExecutable + '"';
  }

  QString quotedLink(link);
  if (!quotedLink.contains(QRegularExpression("^\".*\"$"))) {
    quotedLink = '"' + quotedLink + '"';
  }

  ::ShellExecute(nullptr, TEXT("open"), ToWString(quotedExecutable).c_str(),
                 ToWString(arguments + " " + quotedLink).c_str(),
                 ToWString(QFileInfo(quotedExecutable).absolutePath()).c_str(),
                 SW_SHOWNORMAL);
}

HandlerStorage *registerExecutable(const QDir &storagePath,
                                   const QString &handlerPath,
                                   const QString &handlerArgs)
{
  HandlerStorage *storage = nullptr;
  if (!handlerPath.isEmpty() && !handlerPath.endsWith("nxmhandler.exe", Qt::CaseInsensitive)) {
    // a foreign or global nxm handler, register ourself and use that handler as
    // an option - if this is another nxmhandler we could run into problems so skip it
    storage = new HandlerStorage(storagePath.path());
    storage->registerHandler(handlerPath, handlerArgs, false);
    storage->registerProxy(QCoreApplication::applicationFilePath());
  } else {
    // no handler registered yet or the existing handler is invalid -> overwrite
    storage = new HandlerStorage(storagePath.path());
    storage->registerProxy(QCoreApplication::applicationFilePath());
  }
  return storage;
}

// ensure a nxmhandler.exe is registered to handle nxm-links, then load the
// handler storage for that registered instance
// (even if it's different from the one actually being run)
HandlerStorage *loadStorage(bool forceReg, bool startupRegistration)
{
  HandlerStorage *storage = nullptr;

  QDir globalStorage(
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
  globalStorage.cd("../ModOrganizer");
  QDir baseDir;
  if (globalStorage.exists()) {
    baseDir = globalStorage;
  } else {
    baseDir = QDir(qApp->applicationDirPath());
  }
  NxmHandler::LoggerInit(baseDir.filePath("nxmhandler.log"));
  QSettings handlerReg("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\",
                       QSettings::NativeFormat);
  const QString handlerCommand = handlerReg.value("shell/open/command/Default").toString();
  QStringList handlerVals = HandlerStorage::stripCall(handlerCommand);
  QString handlerPath = handlerVals.front();
  handlerVals.pop_front();
  QString handlerArgs = handlerVals.join(" ");

  QDir handlerBaseDir = QFileInfo(handlerPath).absoluteDir();

  QSettings settings(baseDir.absoluteFilePath("nxmhandler.ini"),
                     QSettings::IniFormat);
  bool noRegister = settings.value("noregister", false).toBool();
  if (globalStorage.exists("nxmhandler.ini") &&
      handlerPath.endsWith("nxmhandler.exe", Qt::CaseInsensitive) &&
      QFile::exists(handlerPath)) {
    // global configuration avaible - use it
    storage = new HandlerStorage(globalStorage.path());
  } else if (handlerBaseDir.exists("nxmhandler.ini") &&
             handlerPath.endsWith("nxmhandler.exe", Qt::CaseInsensitive) &&
             QFile::exists(handlerPath)) {
    // a portable installation is registered to handle links, use its
    // configuration
    storage = new HandlerStorage(QFileInfo(handlerPath).absolutePath());
    if (forceReg &&
        (QString::compare(
            QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
            handlerPath, Qt::CaseInsensitive))) {
      if (QMessageBox::question(
              nullptr, QCoreApplication::translate("NxmHandler", "Change Handler?"),
              QCoreApplication::translate("NxmHandler", "A nxm handler from a different Mod Organizer "
                          "installation has been registered. Do you want to "
                          "replace it? This is usually not necessary unless "
                          "the other installation is defective."),
              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        storage->registerProxy(QCoreApplication::applicationFilePath());
      }
    }
  } else if (!forceReg && startupRegistration &&
             settings.contains("declinedcommand") &&
             settings.value("declinedcommand").toString() == handlerCommand) {
    // MO2 Revamped: Mod Organizer asks to register at every start; the user
    // already said no to this exact registration, so only ask again once it
    // changes (another program took the links, or it was removed)
  } else if (!noRegister || forceReg) {
    // no registration
    QMessageBox registerBox(
        QMessageBox::Question, QCoreApplication::translate("NxmHandler", "Register?"),
        QCoreApplication::translate("NxmHandler", "Mod Organizer is not set up to handle nxm links. "
                    "Associate it with nxm links?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Save);
    registerBox.button(QMessageBox::Save)
        ->setText(QCoreApplication::translate("NxmHandler", "No, don't ask again"));
    registerBox.setDefaultButton(QMessageBox::Yes);
    switch (registerBox.exec()) {
      case QMessageBox::Yes: {
        // base dir is either the global dir if it exists or the local application
        // dir
        storage = registerExecutable(baseDir, handlerPath, handlerArgs);
        settings.remove("declinedcommand");
      } break;
      case QMessageBox::Save: {
        settings.setValue("noregister", true);
      } break;
      case QMessageBox::No: {
        settings.setValue("noregister", false);
        settings.setValue("declinedcommand", handlerCommand);
      } break;
    }
  }
  return storage;
}

static void applyChromeFix()
{
  QString dataPath = QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
  QString fileName = QDir(dataPath + "/../google/chrome/user data/local state").canonicalPath();

  QFile chromeLocalState(fileName);

  if (!chromeLocalState.exists()) {
    // probably simply means that chrome isn't installed
    return;
  }

  if (!chromeLocalState.open(QIODevice::ReadOnly)) {
    // don't know, still no reason to report an error
    return;
  }

  QJsonParseError parseError;
  QJsonDocument document = QJsonDocument::fromJson(chromeLocalState.readAll(), &parseError);
  chromeLocalState.close();
  if (parseError.error == QJsonParseError::NoError) {
    QJsonObject docMap = document.object();
    QJsonObject handlers = docMap["protocol_handler"].toObject();
    // toObject returns empty object if the key doesn't exist. Therefore if excluded_schemes exists, protocol_handler existed as well
    if (handlers.contains("excluded_schemes")) {
      QJsonObject schemes = handlers["excluded_schemes"].toObject();
      if (schemes["nxm"].toBool(true)) {
        if (QMessageBox::question(nullptr, QCoreApplication::translate("NxmHandler", "Apply Chrome fix"),
                                  QCoreApplication::translate("NxmHandler",
                                  "Chrome may not support nexus links even though the association is set up correctly. "
                                  "Do you want to apply a fix for that (You have to close chrome before pressing yes or "
                                  "this will have no effect!)?"),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
          return;
        }
        schemes["nxm"] = false;
        handlers["excluded_schemes"] = schemes;
        docMap["protocol_handler"] = handlers;
        QByteArray result = QJsonDocument(docMap).toJson();
        chromeLocalState.open(QIODevice::WriteOnly | QIODevice::Truncate);
        chromeLocalState.write(result);
        chromeLocalState.close();
        qDebug("chrome fix applied");
      }
    }
  }
}

// MO2 Revamped: the interface language MO2 Revamped uses (Settings >
// General), read from its global settings the same way MO2 finds them; the
// system language otherwise
static QString interfaceLanguage()
{
  const QDir appDir(QCoreApplication::applicationDirPath());
  QString settingsPath;
  if (appDir.exists("eldenring-only.portable")) {
    settingsPath = appDir.filePath("EldenRingOnly.ini");
  } else if (appDir.exists("portable.txt") || appDir.exists("ModOrganizer.ini") ||
             appDir.exists("MO2-Revamped-Global.ini")) {
    settingsPath = appDir.filePath("MO2-Revamped-Global.ini");
  } else {
    settingsPath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                       .filePath("MO2 Revamped/MO2-Revamped-Global.ini");
  }
  if (QFileInfo(settingsPath).isFile()) {
    const QString language =
        QSettings(settingsPath, QSettings::IniFormat).value("Language").toString();
    if (!language.isEmpty()) {
      return QString(language).replace('-', '_');
    }
  }
  return QLocale::system().name();
}

// Qt's own strings, then nxmhandler's, then MO2 Revamped's (which also carry
// the text of this redesigned window); Qt searches the last one first
static void installTranslators(QApplication &app)
{
  const QString language = interfaceLanguage();
  if (language.startsWith("en", Qt::CaseInsensitive)) {
    return;
  }
  const QString directory = QCoreApplication::applicationDirPath() + "/translations";
  for (const char *name : {"qtbase", "nxmhandler", "revamped"}) {
    auto *translator = new QTranslator(&app);
    if (translator->load(QString("%1_%2").arg(QLatin1String(name), language), directory)) {
      app.installTranslator(translator);
    } else {
      delete translator;
    }
  }
}

// MO2 Revamped: a link arrived for a game nothing handles; offer to pick the
// program for it right away instead of only reporting it
static bool chooseHandlerFor(HandlerStorage &storage, const QString &game)
{
  QMessageBox box(QMessageBox::Warning,
                  QCoreApplication::translate("NxmHandler", "No handler found"),
                  QCoreApplication::translate("NxmHandler",
                      "Windows sent a Nexus link for \"%1\", but no program in the list "
                      "handles that game, so the link cannot be opened yet.")
                      .arg(game));
  box.setInformativeText(QCoreApplication::translate("NxmHandler",
      "Choose the Mod Organizer that should open links for this game and the link is "
      "passed on to it right away. If Mod Organizer should handle it, you can also go to "
      "Settings > Nexus there and click \"Associate with ... links\"."));
  QPushButton *choose = box.addButton(
      QCoreApplication::translate("NxmHandler", "Choose a program..."), QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Close);
  box.setDefaultButton(choose);
  box.exec();
  if (box.clickedButton() != choose) {
    return false;
  }

  // the known-games ID when the link uses the long Nexus name
  QString id = game.toLower();
  for (const auto &known : HandlerStorage::knownGames()) {
    if (game.compare(std::get<2>(known), Qt::CaseInsensitive) == 0) {
      id = std::get<1>(known);
    }
  }
  HandlerEditDialog dialog(HandlerEditDialog::Mode::Add);
  dialog.setValues(QStringList() << id, QString(), QString());
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  storage.registerHandler(dialog.gameIDs(), dialog.executable(), dialog.arguments(), true, false);
  return true;
}

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);
  Fluent::install(app);
  installTranslators(app);

  try {
    QStringList args = app.arguments();

    // No arguments probably means the user explictly ran this application
    // Set forcereg=True to allow them to register
    bool forceReg = (args.count() == 1) ||
                    ((args.count() > 1) && args.at(1) == "forcereg");

    const bool startupRegistration = (args.count() > 1) && args.at(1) == "reg";
    std::unique_ptr<HandlerStorage> storage(loadStorage(forceReg, startupRegistration));
    if (storage.get() == nullptr) {
      return 0;
    }

    // Log the arguments
    qDebug(qUtf8Printable( "\"" + args.join("\" \"") + "\""));

    // No other logs, close the log
    NxmHandler::LoggerDeinit();

    // Acceptable arguments
    //
    // nxmhandler.exe
    //    forces registration and spawns handler window
    //
    // nxmhandler.exe reg|forcereg game1,game2,game3 C:/path/to/binary
    //    reg:      register if noregister==false
    //    forcereg: force registration
    //
    // nxmhandler.exe nxm://link/to/mod
    //    forwards link to registered handler

    if (args.count() > 1) {
      if ((args.at(1) == "reg") || (args.at(1) == "forcereg")) {
        if (args.count() == 4) {
          storage->registerHandler(args.at(2).split(",", Qt::SkipEmptyParts), QDir::toNativeSeparators(args.at(3)), "", true, forceReg);
          if (forceReg) {
            applyChromeFix();
          }
          return 0;
        } else {
          QMessageBox::critical(nullptr, QCoreApplication::translate("NxmHandler", "Error"), QCoreApplication::translate("NxmHandler", "Invalid number of parameters"));
        }
      } else if (args.at(1).startsWith("nxm://")) {
        NXMUrl url(args.at(1));
        QStringList handlerVals = storage->getHandler(url.game());
        QString executable = handlerVals.front();
        handlerVals.pop_front();
        QString arguments = handlerVals.join(" ");
        if (!executable.isEmpty()) {
          handleLink(executable, arguments, args.at(1));
          return 0;
        } else {
          if (chooseHandlerFor(*storage, url.game())) {
            handlerVals = storage->getHandler(url.game());
            executable = handlerVals.front();
            handlerVals.pop_front();
            arguments = handlerVals.join(" ");
            if (!executable.isEmpty()) {
              handleLink(executable, arguments, args.at(1));
              return 0;
            }
          }
          return 1;
        }
      } else {
        QMessageBox::warning(nullptr, QCoreApplication::translate("NxmHandler", "Invalid Arguments"), QCoreApplication::translate("NxmHandler", "Invalid number of parameters"));
        return 1;
      }
      return 0;
    } else {
      HandlerWindow win;
      win.setHandlerStorage(storage.get());
      QSettings handlerReg("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\", QSettings::NativeFormat);
      QStringList handlerVals = HandlerStorage::stripCall(handlerReg.value("shell/open/command/Default").toString());
      QString handlerPath = handlerVals.front();
      handlerVals.pop_front();
      QString handerArgs = handlerVals.join(" ");
      win.setPrimaryHandler(handlerPath);
      win.show();

      return app.exec();
    }
  } catch (std::exception &e) {
    QMessageBox::critical(nullptr, QApplication::applicationName(),
                          QCoreApplication::translate("NxmHandler", "Uncaught exception:\n%1").arg(e.what()));
    throw;
  }
}
