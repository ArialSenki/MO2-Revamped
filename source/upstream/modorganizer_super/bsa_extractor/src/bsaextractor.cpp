#include "bsaextractor.h"

#include "iplugingame.h"
#include "imodlist.h"
#include <versioninfo.h>
#include <imodinterface.h>
#include <questionboxmemory.h>
#include <bsaarchive.h>
#include <report.h>
#include <dataarchives.h>
#include <igamefeatures.h>
#include <pluginrequirements.h>

#include <QDir>
#include <QFileInfoList>
#include <QCoreApplication>
#include <QApplication>
#include <QMainWindow>
#include <QStringList>

#include "fluentcontentdialog.h"
#include <QtPlugin>

#include <functional>

#include <boost/bind.hpp>

using namespace MOBase;
namespace bindph = std::placeholders;


BsaExtractor::BsaExtractor()
  : m_Organizer(nullptr)
{
}

bool BsaExtractor::init(MOBase::IOrganizer *moInfo)
{
  m_Organizer = moInfo;
  moInfo->modList()->onModInstalled(std::bind(&BsaExtractor::modInstalledHandler, this, bindph::_1));
  return true;
}

// a game without BSA/BA2 archives has nothing for this plugin (MO2 Revamped)
std::vector<std::shared_ptr<const MOBase::IPluginRequirement>> BsaExtractor::requirements() const
{
  return {MOBase::PluginRequirementFactory::basic(
      [](MOBase::IOrganizer* organizer) {
        return organizer->gameFeatures()->gameFeature<MOBase::DataArchives>() != nullptr;
      },
      QObject::tr("This game has no BSA or BA2 archives."))};
}

QString BsaExtractor::name() const
{
  return "BSA Extractor";
}

QString BsaExtractor::localizedName() const
{
  return tr("BSA Extractor");
}


QString BsaExtractor::author() const
{
  return "Tannin & MO2 Team";
}

QString BsaExtractor::description() const
{
  return tr("Offers a dialog during installation of a mod to unpack all its BSAs");
}

VersionInfo BsaExtractor::version() const
{
  return VersionInfo(1, 5, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> BsaExtractor::settings() const
{
  return {
    PluginSetting("only_alternate_source", "only trigger bsa extraction for alternate game sources", true)
  };
}


bool BsaExtractor::extractProgress(QProgressDialog &progress, int percentage, std::string fileName)
{
  // stopped: bsatk asks again until its threads end, and setValue would show
  // the dialog again
  if (progress.wasCanceled()) {
    return false;
  }
  progress.setLabelText(QString::fromStdString(fileName));
  progress.setValue(percentage);
  qApp->processEvents();
  return !progress.wasCanceled();
}


namespace
{

// MO2's window: the questions come after an installation, when the
// installer's dialog has closed
QWidget* mainWindow()
{
  for (QWidget* window : QApplication::topLevelWidgets()) {
    if (qobject_cast<QMainWindow*>(window) != nullptr && window->isVisible()) {
      return window;
    }
  }
  return nullptr;
}

}  // namespace

void BsaExtractor::modInstalledHandler(IModInterface *mod)
{

  if (m_Organizer->pluginSetting(name(), "only_alternate_source").toBool() &&
      !(m_Organizer->modList()->state(mod->name()) & IModList::STATE_ALTERNATE)) {
    return;
  }

  QWidget* parent = mainWindow();

  // MO2 Revamped: the windows are MO2's content dialogs, over MO2's window
  if (QFileInfo(mod->absolutePath()) == QFileInfo(m_Organizer->managedGame()->dataDirectory().absolutePath())) {
    RevampedFluent::showNotice(
        parent, tr("Can't extract this mod's archives"),
        tr("This mod's folder is the game's Data folder, so its archives can't be "
           "extracted. Remove the mod and install it again under another name."));
    return;
  }
  QDir dir(mod->absolutePath());

  QFileInfoList archives = dir.entryInfoList(QStringList({ "*.bsa", "*.ba2" }));
  if (archives.isEmpty()) {
    return;
  }
  QStringList names;
  for (const QFileInfo& archive : archives) {
    names.append(archive.fileName());
  }

  // the same questions, and remembered answers, as Mod Organizer 2's
  if (QuestionBoxMemory::query(
          parent, "unpackBSA", tr("Extract the archives?"),
          tr("%1 comes with archives: %2.\n\nExtract their files into the mod? Loose files "
             "follow the order of the mod list, and usually win over the files of every "
             "archive, wherever their mod is in the list. If you aren't sure, choose No.")
              .arg(mod->name(), names.join(", ")),
          QDialogButtonBox::Yes | QDialogButtonBox::No,
          QDialogButtonBox::No) != QDialogButtonBox::Yes) {
    return;
  }

  const bool removeBSAs =
      QuestionBoxMemory::query(
          parent, "removeUnpackedBSA", tr("Remove the archives afterwards?"),
          tr("Once their files are extracted, the archives only repeat them. Send the "
             "archives to the Recycle Bin when the extraction is done?"),
          QDialogButtonBox::Yes | QDialogButtonBox::No,
          QDialogButtonBox::No) == QDialogButtonBox::Yes;

  for (const QFileInfo& archiveInfo : archives) {
    BSA::Archive archive;
    // MO2 Revamped: wide paths, so names outside the system code page work
    BSA::EErrorCode result = BSA::ERROR_NONE;
    try {
      result = archive.read(
          std::filesystem::path(QDir::toNativeSeparators(archiveInfo.absoluteFilePath()).toStdWString()), true);
    } catch (const std::exception& e) {
      // a damaged file or one that isn't an archive at all
      reportError(tr("%1 couldn't be read: %2").arg(archiveInfo.fileName(), e.what()));
      return;
    }
    if ((result != BSA::ERROR_NONE) && (result != BSA::ERROR_INVALIDHASHES)) {
      reportError(tr("%1 couldn't be read (error %2).").arg(archiveInfo.fileName()).arg(result));
      return;
    }

    QProgressDialog progress(parent);
    progress.setWindowTitle(tr("Extracting %1").arg(archiveInfo.fileName()));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setMaximum(100);
    progress.setValue(0);
    progress.setCancelButtonText(tr("Stop"));
    progress.show();

    const BSA::EErrorCode extracted = archive.extractAll(
        std::filesystem::path(QDir::toNativeSeparators(mod->absolutePath()).toStdWString()),
        boost::bind(&BsaExtractor::extractProgress, this, boost::ref(progress), _1, _2),
        false);
    const bool stopped = progress.wasCanceled();
    progress.close();
    archive.close();

    if (stopped) {
      // part of the files are out: the archive stays, and the rest are left
      RevampedFluent::showNotice(
          parent, tr("Extraction stopped"),
          tr("Part of the files of %1 were extracted into the mod; the archive and the ones "
             "after it are as they were.")
              .arg(archiveInfo.fileName()));
      return;
    }

    // files that couldn't be written (a full disk): the archive stays
    if (extracted != BSA::ERROR_NONE) {
      reportError(tr("Not all the files of %1 could be extracted (error %2); the archive "
                     "stays in the mod.")
                      .arg(archiveInfo.fileName())
                      .arg(extracted));
      continue;
    }

    if (result == BSA::ERROR_INVALIDHASHES) {
      reportError(tr("%1 holds files with invalid names (hashes): some of the extracted "
                     "files may be broken.")
                      .arg(archiveInfo.fileName()));
    }

    if (removeBSAs && !QFile::moveToTrash(archiveInfo.absoluteFilePath())) {
      qCritical("failed to remove archive %s", archiveInfo.absoluteFilePath().toUtf8().constData());
      reportError(tr("%1 couldn't be sent to the Recycle Bin.").arg(archiveInfo.fileName()));
    }
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
Q_EXPORT_PLUGIN2(bsaextractor, BsaExtractor)
#endif
