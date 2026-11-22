/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Base Preview plugin for MO

Base Preview plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Base Preview plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Base Preview plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "previewbase.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QFontDatabase>
#include <QLabel>
#include <QMovie>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QVBoxLayout>
#include <QtPlugin>
#include <utility.h>

using namespace MOBase;

namespace
{

// MO2 Revamped: the picture scaled to fit the preview with its proportions,
// never above its own size, so a large picture never makes the window grow and
// a small one stays sharp. An animated GIF plays
class ImageView : public QWidget
{
public:
  ImageView(const QString& fileName, const QByteArray& fileData, QWidget* parent)
      : QWidget(parent)
  {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (!fileData.isNull()) {
      m_buffer.setData(fileData);
      m_buffer.open(QIODevice::ReadOnly);
      m_movie.setDevice(&m_buffer);
    } else {
      m_movie.setFileName(fileName);
    }
    if (m_movie.isValid() && m_movie.frameCount() > 1) {
      QObject::connect(&m_movie, &QMovie::frameChanged, this, [this] {
        m_picture = m_movie.currentPixmap();
        update();
      });
      m_movie.start();
      m_picture = m_movie.currentPixmap();
    } else if (!fileData.isNull()) {
      m_picture.loadFromData(fileData);
    } else {
      m_picture.load(fileName);
    }
  }

  QSize pictureSize() const { return m_picture.size(); }

  QSize sizeHint() const override
  {
    return (QSizeF(m_picture.size()) / devicePixelRatioF()).toSize().boundedTo(QSize(800, 600));
  }
  QSize minimumSizeHint() const override { return {64, 64}; }

protected:
  void paintEvent(QPaintEvent*) override
  {
    if (m_picture.isNull()) {
      return;
    }
    // at most one picture pixel per screen pixel, also with display scaling
    const QSize natural = (QSizeF(m_picture.size()) / devicePixelRatioF()).toSize();
    const QSize size = natural.scaled(rect().size(), Qt::KeepAspectRatio).boundedTo(natural);
    QRect target(QPoint(0, 0), size);
    target.moveCenter(rect().center());
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, size != natural);
    painter.drawPixmap(target, m_picture);
  }

private:
  QBuffer m_buffer;
  QMovie m_movie;
  QPixmap m_picture;
};

}  // namespace

PreviewBase::PreviewBase() : m_MOInfo(nullptr) {}

bool PreviewBase::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;

  const QStringList& blacklist =
      m_MOInfo->pluginSetting(name(), "blacklisted_extensions")
          .toString()
          .toLower()
          .split(',');

  // set up image reader to be used for all image types qt (the current installation)
  // supports
  auto imageReader =
      std::bind(&PreviewBase::genImagePreview, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3);

  foreach (const QByteArray& fileType, QImageReader::supportedImageFormats()) {
    auto strFileType = QString(fileType).toLower();

    // skip dds as that one is handled by the dds preview plugin.
    if (strFileType == "dds" || blacklist.contains(strFileType))
      continue;

    m_PreviewGenerators[strFileType] = imageReader;
  }

  const QStringList supportedTextFormats = {"txt", "ini", "json", "log", "cfg", "psc"};

  auto textReader = std::bind(&PreviewBase::genTxtPreview, this, std::placeholders::_1,
                              std::placeholders::_2, std::placeholders::_3);

  foreach (const QString fileType, supportedTextFormats) {

    // skip blacklisted ones
    if (blacklist.contains(fileType))
      continue;

    m_PreviewGenerators[fileType] = textReader;
  }

  return true;
}

QString PreviewBase::name() const
{
  return "Preview Base";
}

QString PreviewBase::localizedName() const
{
  return tr("Preview Base");
}

QString PreviewBase::author() const
{
  return "Tannin";
}

QString PreviewBase::description() const
{
  return tr("Supports previewing various types of data files");
}

MOBase::VersionInfo PreviewBase::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<MOBase::PluginSetting> PreviewBase::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting(
      "enabled",
      tr("Enable previewing of basic file types, such as images and text files."),
      QVariant(true)));
  result.push_back(
      PluginSetting("blacklisted_extensions",
                    tr("Specify a list of comma separated extensions (without \".\") "
                       "that should not be previewed by this plugin."),
                    QVariant("")));
  return result;
}

std::set<QString> PreviewBase::supportedExtensions() const
{
  std::set<QString> extensions;
  for (const auto& generator : m_PreviewGenerators) {
    extensions.insert(generator.first);
  }

  return extensions;
}

QWidget* PreviewBase::genFilePreview(const QString& fileName,
                                     const QSize& maxSize) const
{
  return genDataPreview(nullptr, fileName, maxSize);
}

QWidget* PreviewBase::genDataPreview(const QByteArray& fileData,
                                     const QString& fileName,
                                     const QSize& maxSize) const
{
  auto iter = m_PreviewGenerators.find(QFileInfo(fileName).suffix().toLower());
  if (iter != m_PreviewGenerators.end()) {
    return iter->second(fileName, maxSize, fileData);
  } else {
    return nullptr;
  }
}

QWidget* PreviewBase::genImagePreview(const QString& fileName, const QSize&,
                                      const QByteArray& fileData) const
{
  // MO2 Revamped: the picture fits the window (2.5.2 showed it at full size,
  // only capped to 80% of the screen's height), with its size below it
  auto* wrapper = new QWidget();
  auto* layout  = new QVBoxLayout(wrapper);
  layout->setContentsMargins(12, 12, 12, 8);
  layout->setSpacing(8);
  auto* view = new ImageView(fileName, fileData, wrapper);
  layout->addWidget(view, 1);
  auto* size = new QLabel(wrapper);
  size->setAlignment(Qt::AlignCenter);
  size->setText(view->pictureSize().isEmpty()
                    ? tr("This picture can't be read.")
                    : tr("%1 \xC3\x97 %2 pixels")  // "%1 × %2 pixels" in UTF-8
                          .arg(view->pictureSize().width())
                          .arg(view->pictureSize().height()));
  layout->addWidget(size);
  return wrapper;
}

QWidget* PreviewBase::genTxtPreview(const QString& fileName, const QSize&,
                                    const QByteArray& fileData) const
{
  // MO2 Revamped: plain text. 2.5.2's QTextEdit::setText read a file that
  // looked like HTML as HTML. Settings, logs and scripts in a fixed-width font
  // without wrapping, so their columns line up; a .txt wraps like prose
  auto* edit = new QPlainTextEdit();
  edit->setReadOnly(true);
  // only the start of a big file: a log of a few GB was read whole, decoded and
  // copied several times over (GBs of memory, the window frozen for minutes)
  constexpr qint64 limit = 8 * 1024 * 1024;
  QByteArray bytes;
  bool cut = false;
  if (!fileData.isNull()) {
    cut   = fileData.size() > limit;
    bytes = cut ? fileData.left(limit) : fileData;
  } else if (QFileInfo(fileName).size() > limit) {
    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly)) {
      bytes = file.read(limit);
      cut   = true;
    }
  }
  if (cut) {
    // at the last line break: a cut inside a UTF-8 character made the whole
    // start read as the system code page (Cyrillic or CJK logs garbled)
    const qsizetype end = bytes.lastIndexOf('\n');
    if (end > 0) {
      bytes.truncate(end + 1);
    }
  }
  QString text;
  if (cut) {
    text = MOBase::decodeTextData(bytes) + QStringLiteral("\n\n") +
           tr("Only the first %1 MB of this file are shown.").arg(limit / (1024 * 1024));
  } else {
    text = fileData.isNull() ? MOBase::readFileText(fileName)
                             : MOBase::decodeTextData(fileData);
  }
  edit->setPlainText(text);
  if (QFileInfo(fileName).suffix().compare("txt", Qt::CaseInsensitive) != 0) {
    // a rule of its own: the theme's font for text fields would win over setFont()
    edit->setStyleSheet(QStringLiteral("QPlainTextEdit { font-family: \"%1\"; }")
                            .arg(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
  }
  return edit;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(previewBase, PreviewBase)
#endif
