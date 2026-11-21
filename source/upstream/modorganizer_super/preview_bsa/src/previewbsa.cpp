/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Bsa Preview plugin for MO

Bsa Preview plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Bsa Preview plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Bsa Preview plugin.  If not, see <http://www.gnu.org/licenses/>.
*/
#define QT

#include "previewbsa.h"
#include "simplefiletreemodel.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QScreen>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtPlugin>
#include <filterwidget.h>
#include <utility.h>

#include <log.h>
#include <dataarchives.h>
#include <igamefeatures.h>
#include <pluginrequirements.h>

using namespace MOBase;

PreviewBsa::PreviewBsa() : m_MOInfo(nullptr) {}

bool PreviewBsa::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;

  auto bsaPreview = std::bind(&PreviewBsa::genBsaPreview, this, std::placeholders::_1,
                              std::placeholders::_2);

  m_PreviewGenerators["bsa"] = bsaPreview;
  m_PreviewGenerators["ba2"] = bsaPreview;

  return true;
}

// a game without BSA/BA2 archives has none to preview (MO2 Revamped)
std::vector<std::shared_ptr<const MOBase::IPluginRequirement>> PreviewBsa::requirements() const
{
  return {MOBase::PluginRequirementFactory::basic(
      [](MOBase::IOrganizer* organizer) {
        return organizer->gameFeatures()->gameFeature<MOBase::DataArchives>() != nullptr;
      },
      QObject::tr("This game has no BSA or BA2 archives."))};
}

QString PreviewBsa::name() const
{
  return "Preview Bsa";
}

QString PreviewBsa::localizedName() const
{
  return tr("Preview BSA");
}

QString PreviewBsa::author() const
{
  return "AL12 & MO2 Team";
}

QString PreviewBsa::description() const
{
  return tr("Supports previewing contents of Bethesda Archive files, BSAs and BA2s");
}

MOBase::VersionInfo PreviewBsa::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<MOBase::PluginSetting> PreviewBsa::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting(
      "enabled", tr("Enable previewing of BSA file contents"), QVariant(true)));
  return result;
}

std::set<QString> PreviewBsa::supportedExtensions() const
{
  std::set<QString> extensions;
  for (const auto& generator : m_PreviewGenerators) {
    extensions.insert(generator.first);
  }

  return extensions;
}

QWidget* PreviewBsa::genFilePreview(const QString& fileName, const QSize& maxSize) const
{
  auto iter = m_PreviewGenerators.find(QFileInfo(fileName).suffix().toLower());
  if (iter != m_PreviewGenerators.end()) {
    return iter->second(fileName, maxSize);
  } else {
    return nullptr;
  }
}

void PreviewBsa::readFiles(const BSA::Folder::Ptr archiveFolder)
{
  const auto fileCount = archiveFolder->getNumFiles();
  for (unsigned int i = 0; i < fileCount; ++i) {
    const BSA::File::Ptr file = archiveFolder->getFile(i);

    m_Files << QString::fromStdString(file->getFilePath());
  }

  // recurse into subdirectories
  const auto dirCount = archiveFolder->getNumSubFolders();
  for (unsigned int i = 0; i < dirCount; ++i) {
    const BSA::Folder::Ptr folder = archiveFolder->getSubFolder(i);

    readFiles(folder);
  }
}

QWidget* PreviewBsa::genBsaPreview(const QString& fileName, const QSize&)
{
  m_Files.clear();
  // MO2 Revamped: a short line about the archive above its files, the filter
  // above the list (2.5.2 put it below, under a long line of raw fields), and a
  // message when the archive can't be read (2.5.2 set it on a label it never
  // showed, so the preview was empty)
  QWidget* wrapper    = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(wrapper);
  layout->setContentsMargins(12, 10, 12, 12);
  layout->setSpacing(8);

  QLabel* infoLabel = new QLabel(wrapper);
  infoLabel->setWordWrap(true);
  infoLabel->setTextFormat(Qt::PlainText);
  layout->addWidget(infoLabel);

  BSA::Archive arch;  // bs_archive_auto is easier to use, but is less performant when
                      // working with memory
  // MO2 Revamped: the wide path, so names outside the system code page work
  // bsatk throws for a file that isn't an archive at all ("not a bsa or ba2
  // file"), which MO2 showed as a bare error box
  BSA::EErrorCode res = BSA::ERROR_INVALIDDATA;
  try {
    res = arch.read(
        std::filesystem::path(QDir::toNativeSeparators(fileName).toStdWString()), true);
  } catch (const std::exception& e) {
    log::error("invalid bsa '{}': {}", fileName, e.what());
  }
  if ((res != BSA::ERROR_NONE) && (res != BSA::ERROR_INVALIDHASHES)) {
    log::error("invalid bsa '{}', error {}", fileName, res);
    infoLabel->setText(tr("MO2 can't read this archive: its format isn't one it knows, "
                          "or the file is damaged."));
    layout->addStretch(1);
    arch.close();
    return wrapper;
  }
  const BSA::Folder::Ptr archiveDir = arch.getRoot();
  readFiles(archiveDir);

  QString format;
  try {
    format = getFormat(arch.getType());
  } catch (const std::exception&) {
    format = tr("Unknown format");
  }
  const bool compressed = (arch.getFlags() & 0x00000004) != 0;
  infoLabel->setText(
      // the dots are UTF-8 middle dots
      (m_Files.size() == 1
           ? tr("%1 archive \xC2\xB7 1 file \xC2\xB7 %2")
                 .arg(format)
                 .arg(compressed ? tr("compressed") : tr("not compressed"))
           : tr("%1 archive \xC2\xB7 %2 files \xC2\xB7 %3")
                 .arg(format)
                 .arg(QLocale().toString(static_cast<qlonglong>(m_Files.size())))
                 .arg(compressed ? tr("compressed") : tr("not compressed"))));
  infoLabel->setToolTip(tr("Archive type %1, flags %2")
                            .arg(arch.getType())
                            .arg("0x" + QString::number(arch.getFlags(), 16)));

  QLineEdit* lineEdit = new QLineEdit(wrapper);
  lineEdit->setAccessibleName(tr("Filter the archive's files"));
  layout->addWidget(lineEdit);

  QTreeView* view = new QTreeView(wrapper);
  view->setAccessibleName(tr("Files in the archive"));
  // MO2 Revamped: from MO2 79cdca3
  SimpleFileTreeModel* model = new SimpleFileTreeModel(m_Files, view);

  view->setModel(model);
  layout->addWidget(view, 1);

  model->setFilterWidgetEdit(lineEdit);
  model->setFilterWidgetList(view);
  lineEdit->setPlaceholderText(tr("Filter files"));

  view->setSortingEnabled(true);
  view->sortByColumn(0, Qt::SortOrder::AscendingOrder);

  arch.close();
  return wrapper;
}

QString PreviewBsa::getFormat(ArchiveType type) const
{
  switch (type) {
  case ArchiveType::TYPE_MORROWIND:
    return "Morrowind";
  case ArchiveType::TYPE_OBLIVION:
    return "Oblivion";
  case ArchiveType::TYPE_FALLOUT3:
    return "Fallout 3, New Vegas, Skyrim LE";
  case ArchiveType::TYPE_SKYRIMSE:
    return "Skyrim Special Edition";
  case ArchiveType::TYPE_FALLOUT4:
    return "Fallout 4";
  case ArchiveType::TYPE_STARFIELD:
    return "Starfield";
  case ArchiveType::TYPE_STARFIELD_LZ4_TEXTURE:
    return "Starfield DDS LZ4";
  case ArchiveType::TYPE_FALLOUT4NG_7:
  case ArchiveType::TYPE_FALLOUT4NG_8:
    return "Fallout 4 NextGen";
  default:
    throw data_invalid_exception(makeString("invalid type %d", type));
  }
}

BSAULong PreviewBsa::getVersion(ArchiveType type) const
{
  switch (type) {
  case TYPE_MORROWIND:
    return 0x100;
  case TYPE_OBLIVION:
    return 0x67;
  case TYPE_FALLOUT3:
    return 0x68;
  case TYPE_SKYRIMSE:
    return 0x69;
  case TYPE_FALLOUT4:
    return 0x01;
  case TYPE_STARFIELD:
    return 0x02;
  case TYPE_STARFIELD_LZ4_TEXTURE:
    return 0x03;
  case TYPE_FALLOUT4NG_7:
    return 0x07;
  case TYPE_FALLOUT4NG_8:
    return 0x08;
  default:
    throw data_invalid_exception(makeString("invalid type %d", type));
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(previewBsa, PreviewBsa)
#endif
