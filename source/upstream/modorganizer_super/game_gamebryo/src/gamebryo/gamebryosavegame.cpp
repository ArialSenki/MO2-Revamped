#include "gamebryosavegame.h"

#include "iplugingame.h"
#include "log.h"
#include "scriptextender.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QScopedArrayPointer>
#include <QTime>

#include <Windows.h>
#include <lz4.h>
#include <zlib.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "gamegamebryo.h"
#include "imoinfo.h"

// MO2 Revamped: the most a save body may decompress to (the biggest are
// tens of MB)
static constexpr qint64 MaxSaveBody = qint64(512) * 1024 * 1024;

#define CHUNK 16384

GamebryoSaveGame::GamebryoSaveGame(QString const& file, GameGamebryo const* game,
                                   bool const lightEnabled, bool const mediumEnabled)
    : m_FileName(file), m_CreationTime(QFileInfo(file).lastModified()), m_Game(game),
      m_MediumEnabled(mediumEnabled), m_LightEnabled(lightEnabled),
      m_DataFields([this]() {
        return fetchDataFields();
      })
{}

GamebryoSaveGame::~GamebryoSaveGame() {}

QString GamebryoSaveGame::getFilepath() const
{
  return m_FileName;
}

QDateTime GamebryoSaveGame::getCreationTime() const
{
  return m_CreationTime;
}

QString GamebryoSaveGame::getName() const
{
  return QObject::tr("%1, #%2, Level %3, %4")
      .arg(m_PCName)
      .arg(m_SaveNumber)
      .arg(m_PCLevel)
      .arg(m_PCLocation);
}

QString GamebryoSaveGame::getSaveGroupIdentifier() const
{
  return m_PCName;
}

QStringList GamebryoSaveGame::allFiles() const
{
  // This returns all valid files associated with this game
  QStringList res = {m_FileName};
  auto e = m_Game->m_Organizer->gameFeatures()->gameFeature<MOBase::ScriptExtender>();
  if (e != nullptr) {
    QFileInfo file(m_FileName);
    QFileInfo SEfile(file.absolutePath() + "/" + file.completeBaseName() + "." +
                     m_Game->savegameSEExtension());
    if (SEfile.exists()) {
      res.push_back(SEfile.absoluteFilePath());
    }
  }
  return res;
}

bool GamebryoSaveGame::hasScriptExtenderFile() const
{
  QFileInfo file(m_FileName);
  QFileInfo SEfile(file.absolutePath() + "/" + file.completeBaseName() + "." +
                   m_Game->savegameSEExtension());
  return SEfile.exists();
}

void GamebryoSaveGame::setCreationTime(_SYSTEMTIME const& ctime)
{
  QDate date;
  date.setDate(ctime.wYear, ctime.wMonth, ctime.wDay);
  QTime time;
  time.setHMS(ctime.wHour, ctime.wMinute, ctime.wSecond, ctime.wMilliseconds);

  m_CreationTime = QDateTime(date, time, Qt::UTC);
}

void GamebryoSaveGame::setCreationTimeLocal(_SYSTEMTIME const& ctime)
{
  QDate date;
  date.setDate(ctime.wYear, ctime.wMonth, ctime.wDay);
  QTime time;
  time.setHMS(ctime.wHour, ctime.wMinute, ctime.wSecond, ctime.wMilliseconds);

  m_CreationTime = QDateTime(date, time, Qt::LocalTime).toUTC();
}

GamebryoSaveGame::FileWrapper::FileWrapper(QString const& filepath,
                                           QString const& expected)
    : m_File(filepath), m_HasFieldMarkers(false),
      m_PluginString(StringType::TYPE_WSTRING),
      m_PluginStringFormat(StringFormat::UTF8), m_NextChunk(0)
{
  if (!m_File.open(QIODevice::ReadOnly)) {
    throw std::runtime_error(
        QObject::tr("failed to open %1").arg(filepath).toUtf8().constData());
  }

  std::vector<char> fileID(expected.length() + 1);
  m_File.read(fileID.data(), expected.length());
  fileID[expected.length()] = '\0';

  QString id(fileID.data());
  if (expected != id) {
    throw std::runtime_error(
        QObject::tr("wrong file format - expected %1 got \'%2\' for %3")
            .arg(expected)
            .arg(id)
            .arg(filepath)
            .toUtf8()
            .constData());
  }
}

void GamebryoSaveGame::FileWrapper::setHasFieldMarkers(bool state)
{
  m_HasFieldMarkers = state;
}

void GamebryoSaveGame::FileWrapper::setPluginString(StringType type)
{
  m_PluginString = type;
}

void GamebryoSaveGame::FileWrapper::setPluginStringFormat(StringFormat type)
{
  m_PluginStringFormat = type;
}

void GamebryoSaveGame::FileWrapper::readQDataStream(QDataStream& data, void* buff,
                                                    std::size_t length)
{
  // MO2 Revamped: after more data is decoded the stream is a new one, so it is
  // read through m_Data, not the old reference (readNextChunk() had freed it);
  // a failed chunk is no longer hidden by a shadowed result
  QDataStream* stream = &data;
  int read = stream->readRawData(static_cast<char*>(buff), static_cast<int>(length));
  if (read < 0) {
    read = 0;
  }
  while (static_cast<std::size_t>(read) != length && moreCompressedData()) {
    stream         = m_Data;
    const int more = stream->readRawData(static_cast<char*>(buff) + read,
                                         static_cast<int>(length - read));
    if (more <= 0) {
      break;
    }
    read += more;
  }
  if (static_cast<std::size_t>(read) != length) {
    throw std::runtime_error("unexpected end of file");
  }
}

template <typename T>
void GamebryoSaveGame::FileWrapper::readQDataStream(QDataStream& data, T& value)
{
  static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
  readQDataStream(data, &value, sizeof(T));
}

void GamebryoSaveGame::FileWrapper::skipQDataStream(QDataStream& data,
                                                    std::size_t length)
{
  QDataStream* stream = &data;
  int skip            = stream->skipRawData(static_cast<int>(length));
  if (skip < 0) {
    skip = 0;
  }
  while (static_cast<std::size_t>(skip) != length && moreCompressedData()) {
    stream         = m_Data;
    const int more = stream->skipRawData(static_cast<int>(length - skip));
    if (more <= 0) {
      break;
    }
    skip += more;
  }
  if (static_cast<std::size_t>(skip) != length) {
    throw std::runtime_error("unexpected end of file");
  }
}

bool GamebryoSaveGame::FileWrapper::moreCompressedData()
{
  if (m_CompressionType == 1) {
    return readNextChunk();
  } else if (m_CompressionType == 2) {
    return expandLz4();
  }
  return false;
}

bool GamebryoSaveGame::FileWrapper::expandLz4()
{
  if (m_Lz4Complete || m_Data == nullptr) {
    return false;
  }
  m_Lz4Complete = true;

  const qint64 position = m_Data->device()->pos();

  // MO2 Revamped: both sizes come from the file: the compressed body has to be
  // in it, and no save body is near the cap
  if (m_Lz4Offset < 0 || m_Lz4Offset + qint64(m_Lz4CompressedSize) > m_File.size() ||
      m_Lz4FullSize > MaxSaveBody ||
      qint64(m_Lz4FullSize) > qint64(m_Lz4CompressedSize) * 255 + 16) {
    return false;
  }
  QByteArray compressed;
  compressed.resize(m_Lz4CompressedSize);
  if (!m_File.seek(m_Lz4Offset) ||
      m_File.read(compressed.data(), m_Lz4CompressedSize) != m_Lz4CompressedSize) {
    return false;
  }
  QByteArray decompressed;
  decompressed.resize(m_Lz4FullSize);
  const int decoded = LZ4_decompress_safe_partial(
      compressed.constData(), decompressed.data(), static_cast<int>(m_Lz4CompressedSize),
      static_cast<int>(m_Lz4FullSize), static_cast<int>(m_Lz4FullSize));
  if (decoded <= position) {
    return false;
  }
  decompressed.truncate(decoded);

  delete m_Data;
  m_Data = new QDataStream(decompressed);
  return m_Data->device()->seek(position);
}

GamebryoSaveGame::FileWrapper::~FileWrapper()
{
  delete m_Data;
}

template <>
void GamebryoSaveGame::FileWrapper::read<QString>(QString& value)
{
  if (m_CompressionType == 0) {
    unsigned short length;
    if (m_PluginString == StringType::TYPE_BSTRING ||
        m_PluginString == StringType::TYPE_BZSTRING) {
      unsigned char len;
      read(len);
      length = m_PluginString == StringType::TYPE_BZSTRING ? len + 1 : len;
    } else {
      read(length);
    }

    if (m_HasFieldMarkers) {
      skip<char>();
    }

    QByteArray buffer;
    buffer.resize(length);

    read(buffer.data(),
         m_PluginString == StringType::TYPE_BZSTRING ? length - 1 : length);

    if (m_PluginString == StringType::TYPE_BZSTRING)
      buffer[length - 1] = '\0';

    if (m_HasFieldMarkers) {
      skip<char>();
    }

    if (m_PluginStringFormat == StringFormat::UTF8)
      value = QString::fromUtf8(buffer.constData());
    else
      value = QString::fromLocal8Bit(buffer.constData());
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    unsigned short length;
    if (m_PluginString == StringType::TYPE_BSTRING ||
        m_PluginString == StringType::TYPE_BZSTRING) {
      unsigned char len;
      readQDataStream(*m_Data, len);
      length = m_PluginString == StringType::TYPE_BZSTRING ? len + 1 : len;
    } else {
      readQDataStream(*m_Data, length);
    }

    if (m_HasFieldMarkers) {
      skip<char>();
    }

    QByteArray buffer;
    buffer.resize(length);

    readQDataStream(*m_Data, buffer.data(),
                    m_PluginString == StringType::TYPE_BZSTRING ? length - 1 : length);

    if (m_PluginString == StringType::TYPE_BZSTRING)
      buffer[length - 1] = '\0';

    if (m_HasFieldMarkers) {
      m_Data->skipRawData(1);
    }

    if (m_PluginStringFormat == StringFormat::UTF8)
      value = QString::fromUtf8(buffer.constData());
    else
      value = QString::fromLocal8Bit(buffer.constData());
  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
  }
}

void GamebryoSaveGame::FileWrapper::read(void* buff, std::size_t length)
{
  int read = m_File.read(static_cast<char*>(buff), length);
  if (read != length) {
    throw std::runtime_error("unexpected end of file");
  }
}

QImage GamebryoSaveGame::FileWrapper::readImage(int scale, bool alpha)
{
  unsigned long width;
  read(width);
  unsigned long height;
  read(height);
  return readImage(width, height, scale, alpha);
}

QImage GamebryoSaveGame::FileWrapper::readImage(unsigned long width,
                                                unsigned long height, int scale,
                                                bool alpha)
{
  int bpp = alpha ? 4 : 3;
  // MO2 Revamped: a damaged header can't ask for gigabytes (the screenshots
  // are a few hundred KB; 8K by 8K is far above any)
  if (width == 0 || height == 0) {
    return QImage();
  }
  if (width > 8192 || height > 8192) {
    throw std::runtime_error("invalid screenshot size");
  }
  // nor more than is left of the file (the picture is before the compressed body)
  if (qint64(width) * height * bpp > m_File.size() - m_File.pos()) {
    throw std::runtime_error("invalid screenshot size");
  }
  QScopedArrayPointer<unsigned char> buffer(new unsigned char[width * height * bpp]);
  read(buffer.data(), width * height * bpp);
  // MO2 Revamped: the rows are packed (width * bpp bytes each); without the
  // stride Qt reads them as 4-byte aligned, which skews an RGB picture whose
  // width * 3 isn't a multiple of 4
  QImage image(buffer.data(), static_cast<int>(width), static_cast<int>(height),
               static_cast<qsizetype>(width) * bpp,
               alpha ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGB888);

  // We need to copy the image here because QImage does not make a copy of the
  // buffer when constructed.
  if (scale != 0) {
    return image.copy().scaledToWidth(scale);
  } else {
    return image.copy();
  }
}

void GamebryoSaveGame::FileWrapper::setCompressionType(uint16_t compressionType)
{
  m_CompressionType = compressionType;
}

void GamebryoSaveGame::FileWrapper::closeCompressedData()
{
  if (m_CompressionType == 0) {
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    m_NextChunk        = 0;
    m_UncompressedSize = 0;
    if (m_Data != nullptr) {
      m_Data->device()->close();
      delete m_Data;
      m_Data = nullptr;
    }
  } else
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
}

bool GamebryoSaveGame::FileWrapper::openCompressedData(int bytesToIgnore)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    return false;
  } else if (m_CompressionType == 1) {
    read(m_NextChunk);
    read(m_UncompressedSize);
    QByteArray placeholder;
    m_Data      = new QDataStream(placeholder);
    bool result = readNextChunk();
    if (result)
      skipQDataStream(*m_Data, bytesToIgnore);
    return result;
  } else if (m_CompressionType == 2) {
    uint32_t uncompressedSize;
    read(uncompressedSize);
    uint32_t compressedSize;
    read(compressedSize);

    // MO2 Revamped: the fields MO2 reads (the plugin lists, a few hundred KB
    // at most even with 4,000 plugins) are at the start of the body, which is
    // tens of MB with a big load order. Only its first MB is decoded, from a
    // little more than a MB read; expandLz4() decodes the rest if a read
    // needs it. The whole body used to be read and decoded for every save.
    constexpr uint32_t prefixTarget = 1024 * 1024;
    m_Lz4Offset         = m_File.pos();
    m_Lz4CompressedSize = compressedSize;
    m_Lz4FullSize       = uncompressedSize;

    const uint32_t target = std::min(uncompressedSize, prefixTarget);
    // LZ4 grows data by under 0.4%, so this input holds the first `target`
    // bytes of output
    const uint32_t inputSize = static_cast<uint32_t>(
        std::min<uint64_t>(compressedSize, uint64_t(target) + target / 64 + 64 * 1024));
    m_Lz4Complete = (inputSize == compressedSize && target == uncompressedSize);

    QByteArray compressed;
    compressed.resize(inputSize);
    read(compressed.data(), inputSize);
    QByteArray decompressed;
    decompressed.resize(target);
    int decoded = LZ4_decompress_safe_partial(
        compressed.constData(), decompressed.data(), static_cast<int>(inputSize),
        static_cast<int>(target), static_cast<int>(target));
    compressed.clear();
    if (decoded < 0) {
      decoded = 0;
    }
    decompressed.truncate(decoded);

    m_Data = new QDataStream(decompressed);
    skipQDataStream(*m_Data, bytesToIgnore);

    return true;
  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
    return false;
  }
}

bool GamebryoSaveGame::FileWrapper::readNextChunk()
{
  uint32_t have;
  uint64_t read                     = 0;
  std::unique_ptr<char[]> inBuffer  = std::make_unique<char[]>(CHUNK);
  std::unique_ptr<char[]> outBuffer = std::make_unique<char[]>(CHUNK);
  QByteArray finalData;
  if (m_Data != nullptr) {
    m_Data->device()->close();
    delete m_Data;
    m_Data = nullptr;
  }
  // MO2 Revamped: a damaged chunk left m_Data null, and the next read through
  // it crashed MO2 on a save from the internet: it is an empty stream instead
  // (reads then fail, the save shows without details)
  const auto keepStream = qScopeGuard([this] {
    if (m_Data == nullptr) {
      m_Data = new QDataStream(QByteArray());
    }
  });
  const qint64 maxOutput = MaxSaveBody;
  z_stream stream{};
  try {
    stream.zalloc   = Z_NULL;
    stream.zfree    = Z_NULL;
    stream.opaque   = Z_NULL;
    stream.avail_in = 0;
    stream.next_in  = Z_NULL;
    if (m_NextChunk >= m_File.size() || finalData.size() == m_UncompressedSize)
      return false;
    m_File.seek(m_NextChunk);
    int zlibRet = inflateInit2(&stream, 15 + 32);
    if (zlibRet != Z_OK) {
      return false;
    }
    do {
      stream.avail_in = m_File.read(inBuffer.get(), CHUNK);
      read += stream.avail_in;
      if (!m_File.isReadable()) {
        (void)inflateEnd(&stream);
        return false;
      }
      if (stream.avail_in == 0)
        break;
      stream.next_in = reinterpret_cast<Bytef*>(inBuffer.get());
      do {
        stream.avail_out = CHUNK;
        stream.next_out  = reinterpret_cast<Bytef*>(outBuffer.get());
        zlibRet          = inflate(&stream, Z_NO_FLUSH);
        if ((zlibRet != Z_OK) && (zlibRet != Z_STREAM_END) &&
            (zlibRet != Z_BUF_ERROR)) {
          inflateEnd(&stream);
          return false;
        }
        have = CHUNK - stream.avail_out;
        finalData += QByteArray::fromRawData(outBuffer.get(), have);
        // MO2 Revamped: a zlib bomb stops at the cap
        if (finalData.size() > maxOutput) {
          inflateEnd(&stream);
          return false;
        }
      } while (stream.avail_out == 0);
      read -= stream.avail_in;
    } while (zlibRet != Z_STREAM_END);
    inflateEnd(&stream);
    uint64_t remainder = (m_NextChunk + read) % 16;
    uint64_t next      = m_NextChunk + read + 16 - (remainder == 0 ? 16 : remainder);
    m_NextChunk        = next;
  } catch (const std::exception&) {
    inflateEnd(&stream);
    return false;
  }
  m_Data = new QDataStream(finalData);
  return true;
}

uint8_t GamebryoSaveGame::FileWrapper::readChar(int bytesToIgnore)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    uint8_t version;
    read(version);
    return version;
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    // decompression already done by readSaveGameVersion
    skipQDataStream(*m_Data, bytesToIgnore);

    uint8_t version;
    readQDataStream(*m_Data, version);
    return version;

  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
    return 0;
  }
}

uint16_t GamebryoSaveGame::FileWrapper::readShort(int bytesToIgnore)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    uint16_t size;
    read(size);
    return size;
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    // decompression already done by readSaveGameVersion
    skipQDataStream(*m_Data, bytesToIgnore);

    uint16_t size;
    readQDataStream(*m_Data, size);
    return size;
  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
    return 0;
  }
}

uint32_t GamebryoSaveGame::FileWrapper::readInt(int bytesToIgnore)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    uint32_t size;
    read(size);
    return size;
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    // decompression already done by readSaveGameVersion
    skipQDataStream(*m_Data, bytesToIgnore);

    uint32_t size;
    readQDataStream(*m_Data, size);
    return size;
  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
    return 0;
  }
}

uint64_t GamebryoSaveGame::FileWrapper::readLong(int bytesToIgnore)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    uint64_t size;
    read(size);
    return size;
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    // decompression already done by readSaveGameVersion
    skipQDataStream(*m_Data, bytesToIgnore);

    uint64_t size;
    readQDataStream(*m_Data, size);
    return size;
  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
    return 0;
  }
}

float_t GamebryoSaveGame::FileWrapper::readFloat(int bytesToIgnore)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    float_t value;
    read(value);
    return value;
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    // decompression already done by readSaveGameVersion
    skipQDataStream(*m_Data, bytesToIgnore);

    float_t value;
    readQDataStream(*m_Data, value);
    return value;
  } else {
    MOBase::log::warn("Please create an issue on the MO github labeled \"Found unknown "
                      "Compressed\" with your savefile attached");
    return 0;
  }
}

QStringList GamebryoSaveGame::FileWrapper::readPlugins(int bytesToIgnore,
                                                       bool extraData,
                                                       const QStringList& corePlugins)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    uint8_t count;
    read(count);
    return readPluginData(count, extraData, corePlugins);
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    skipQDataStream(*m_Data, bytesToIgnore);
    uint8_t count;
    readQDataStream(*m_Data, count);
    return readPluginData(count, extraData, corePlugins);
  }
  return {};
}

QStringList
GamebryoSaveGame::FileWrapper::readLightPlugins(int bytesToIgnore, bool extraData,
                                                const QStringList& corePlugins)
{
  if (m_CompressionType == 0) {
    if (bytesToIgnore > 0)  // Just to make certain
      skip<char>(bytesToIgnore);
    uint16_t count;
    read(count);
    return readPluginData(count, extraData, corePlugins);
  } else if (m_CompressionType == 1 || m_CompressionType == 2) {
    skipQDataStream(*m_Data, bytesToIgnore);
    uint16_t count;
    readQDataStream(*m_Data, count);
    return readPluginData(count, extraData, corePlugins);
  }
  return {};
}

QStringList
GamebryoSaveGame::FileWrapper::readMediumPlugins(int bytesToIgnore, bool extraData,
                                                 const QStringList& corePlugins)
{
  if (m_CompressionType != 1) {
    return {};
  } else {
    skipQDataStream(*m_Data, bytesToIgnore);
    uint32_t count;
    readQDataStream(*m_Data, count);
    return readPluginData(count, extraData, corePlugins);
  }
}

QStringList GamebryoSaveGame::FileWrapper::readPluginData(uint32_t count,
                                                          bool extraData,
                                                          const QStringList corePlugins)
{
  QStringList plugins;
  plugins.reserve(count);
  if (m_CompressionType == 0) {
    for (std::size_t i = 0; i < count; ++i) {
      QString name;
      read(name);
      plugins.push_back(name);
    }
  } else {
    for (std::size_t i = 0; i < count; ++i) {
      QString name;
      read(name);
      plugins.push_back(name);
      if (extraData && !corePlugins.contains(name)) {
        QString creationName;
        QString creationId;
        uint16_t flagsSize;
        uint8_t isCreation;
        read(creationName);
        read(creationId);
        readQDataStream(*m_Data, flagsSize);
        skipQDataStream(*m_Data, flagsSize);
        readQDataStream(*m_Data, isCreation);
      }
    }
  }
  return plugins;
}

void GamebryoSaveGame::FileWrapper::close()
{
  m_File.close();
}
