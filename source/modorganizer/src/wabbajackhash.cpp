// MO2 Revamped: the hashes and file copies of the Wabbajack list's code
// (declared in wabbajacklist.h): xxHash64 as Wabbajack writes it, the Base64 it
// is written in, and files hashed or copied in pieces. Apart from the list's
// reader so the tests build without boost::json or 7-Zip.

#include "wabbajacklist.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <QFile>

#include <cstring>

namespace Wabbajack
{

// ---- xxHash64 -------------------------------------------------------------

namespace
{

constexpr uint64_t Prime1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t Prime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t Prime3 = 0x165667B19E3779F9ULL;
constexpr uint64_t Prime4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t Prime5 = 0x27D4EB2F165667C5ULL;

inline uint64_t rotl(uint64_t x, int r)
{
  return (x << r) | (x >> (64 - r));
}

inline uint64_t read64(const unsigned char* p)
{
  uint64_t v;
  std::memcpy(&v, p, 8);  // x64 is little-endian, as xxHash wants
  return v;
}

inline uint32_t read32(const unsigned char* p)
{
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

inline uint64_t xxRound(uint64_t acc, uint64_t input)
{
  acc += input * Prime2;
  acc = rotl(acc, 31);
  return acc * Prime1;
}

inline uint64_t mergeRound(uint64_t acc, uint64_t val)
{
  acc ^= xxRound(0, val);
  return acc * Prime1 + Prime4;
}

}  // namespace

XxHash64::XxHash64()
{
  m_v[0] = Prime1 + Prime2;
  m_v[1] = Prime2;
  m_v[2] = 0;
  m_v[3] = 0 - Prime1;
}

void XxHash64::update(const void* data, std::size_t size)
{
  auto* p   = static_cast<const unsigned char*>(data);
  auto* end = p + size;
  m_total += size;

  if (m_buffered + size < 32) {
    std::memcpy(m_buffer + m_buffered, p, size);
    m_buffered += size;
    return;
  }

  if (m_buffered > 0) {
    const std::size_t fill = 32 - m_buffered;
    std::memcpy(m_buffer + m_buffered, p, fill);
    p += fill;
    for (int i = 0; i < 4; ++i) {
      m_v[i] = xxRound(m_v[i], read64(m_buffer + i * 8));
    }
    m_buffered = 0;
  }

  uint64_t v0 = m_v[0], v1 = m_v[1], v2 = m_v[2], v3 = m_v[3];
  while (end - p >= 32) {
    v0 = xxRound(v0, read64(p));
    v1 = xxRound(v1, read64(p + 8));
    v2 = xxRound(v2, read64(p + 16));
    v3 = xxRound(v3, read64(p + 24));
    p += 32;
  }
  m_v[0] = v0;
  m_v[1] = v1;
  m_v[2] = v2;
  m_v[3] = v3;

  m_buffered = std::size_t(end - p);
  if (m_buffered > 0) {
    std::memcpy(m_buffer, p, m_buffered);
  }
}

uint64_t XxHash64::digest() const
{
  uint64_t h;
  if (m_total >= 32) {
    h = rotl(m_v[0], 1) + rotl(m_v[1], 7) + rotl(m_v[2], 12) + rotl(m_v[3], 18);
    for (int i = 0; i < 4; ++i) {
      h = mergeRound(h, m_v[i]);
    }
  } else {
    h = m_v[2] + Prime5;  // the seed, 0
  }
  h += m_total;

  const unsigned char* p   = m_buffer;
  const unsigned char* end = m_buffer + m_buffered;
  while (end - p >= 8) {
    h ^= xxRound(0, read64(p));
    h = rotl(h, 27) * Prime1 + Prime4;
    p += 8;
  }
  if (end - p >= 4) {
    h ^= uint64_t(read32(p)) * Prime1;
    h = rotl(h, 23) * Prime2 + Prime3;
    p += 4;
  }
  while (p < end) {
    h ^= uint64_t(*p) * Prime5;
    h = rotl(h, 11) * Prime1;
    ++p;
  }

  h ^= h >> 33;
  h *= Prime2;
  h ^= h >> 29;
  h *= Prime3;
  h ^= h >> 32;
  return h;
}

uint64_t XxHash64::of(const void* data, std::size_t size)
{
  XxHash64 h;
  h.update(data, size);
  return h.digest();
}

uint64_t hashFromBase64(QStringView text, bool* ok)
{
  const QByteArray bytes = QByteArray::fromBase64(text.toLatin1());
  if (bytes.size() != 8) {
    if (ok != nullptr) {
      *ok = false;
    }
    return 0;
  }
  uint64_t v;
  std::memcpy(&v, bytes.constData(), 8);
  if (ok != nullptr) {
    *ok = true;
  }
  return v;
}

QString hashToBase64(uint64_t hash)
{
  return QString::fromLatin1(
      QByteArray(reinterpret_cast<const char*>(&hash), 8).toBase64());
}

LowPriority::LowPriority(Mode mode) : m_mode(mode)
{
  if (mode == Mode::Background) {
    // fails when the thread already is in background mode: nothing to undo then
    m_set = ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != 0;
  } else {
    m_previous = ::GetThreadPriority(::GetCurrentThread());
    m_set      = m_previous != THREAD_PRIORITY_ERROR_RETURN &&
            ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
  }
}

LowPriority::~LowPriority()
{
  // the pool thread goes back as it came, for the next work it gets
  if (!m_set) {
    return;
  }
  if (m_mode == Mode::Background) {
    ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
  } else {
    ::SetThreadPriority(::GetCurrentThread(), m_previous);
  }
}

std::optional<uint64_t> copyFile(const QString& source, const QString& target,
                                 const std::atomic_bool* cancel)
{
  QFile in(source);
  QFile out(target);
  if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
    return std::nullopt;
  }
  XxHash64 h;
  QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      return std::nullopt;
    }
    const qint64 n = in.read(buffer.data(), buffer.size());
    if (n < 0) {
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    if (out.write(buffer.constData(), n) != n) {
      return std::nullopt;
    }
    h.update(buffer.constData(), std::size_t(n));
  }
  if (!out.flush()) {
    return std::nullopt;
  }
  return h.digest();
}

std::optional<uint64_t> hashFile(const QString& path, const std::atomic_bool* cancel,
                                 const std::function<void(qint64)>& progress)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }

  XxHash64 h;
  QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
  qint64 done = 0;
  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      return std::nullopt;
    }
    const qint64 n = f.read(buffer.data(), buffer.size());
    if (n < 0) {
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    h.update(buffer.constData(), std::size_t(n));
    done += n;
    if (progress) {
      progress(done);
    }
  }
  return h.digest();
}


}  // namespace Wabbajack
