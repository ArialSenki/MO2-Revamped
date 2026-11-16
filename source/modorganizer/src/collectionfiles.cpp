#include "collectionfiles.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>

namespace Collections
{

// ---------------------------------------------------------------------------
// CRC-32 (zlib's, reflected) and bzip2's (not reflected)

quint32 crc32(const QByteArray& data)
{
  static const auto table = [] {
    std::array<quint32, 256> t{};
    for (quint32 i = 0; i < 256; ++i) {
      quint32 c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      t[i] = c;
    }
    return t;
  }();

  quint32 crc = 0xFFFFFFFFu;
  for (const char ch : data) {
    crc = table[(crc ^ static_cast<quint8>(ch)) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::optional<quint32> parseCrc(const QString& text)
{
  const QString t = text.trimmed();
  if (t.isEmpty() || t.size() > 8) {
    return {};
  }
  bool ok        = false;
  const auto v   = t.toUInt(&ok, 16);
  if (!ok) {
    return {};
  }
  return static_cast<quint32>(v);
}

namespace
{

const std::array<quint32, 256>& bzCrcTable()
{
  static const auto table = [] {
    std::array<quint32, 256> t{};
    for (quint32 i = 0; i < 256; ++i) {
      quint32 c = i << 24;
      for (int k = 0; k < 8; ++k) {
        c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : c << 1;
      }
      t[i] = c;
    }
    return t;
  }();
  return table;
}

// reads bits from the most significant one, as bzip2 writes them
class BitReader
{
public:
  explicit BitReader(const QByteArray& data) : m_data(data) {}

  bool bits(int n, quint32& value)
  {
    value = 0;
    for (int i = 0; i < n; ++i) {
      if (m_pos >= static_cast<qint64>(m_data.size()) * 8) {
        return false;
      }
      const quint8 byte = static_cast<quint8>(m_data[static_cast<int>(m_pos >> 3)]);
      value = (value << 1) | ((byte >> (7 - (m_pos & 7))) & 1);
      ++m_pos;
    }
    return true;
  }

  bool bit(quint32& value) { return bits(1, value); }

private:
  const QByteArray& m_data;
  qint64 m_pos = 0;
};

constexpr int MaxCodeLength = 20;
constexpr int MaxGroups     = 6;
constexpr int MaxAlphaSize  = 258;
constexpr int GroupSize     = 50;

struct HuffmanTable
{
  std::array<int, MaxCodeLength + 2> limit{};
  std::array<int, MaxCodeLength + 2> base{};
  std::array<int, MaxAlphaSize> perm{};
  int minLen = 0;
  int maxLen = 0;
};

void buildTable(HuffmanTable& t, const std::array<quint8, MaxAlphaSize>& lengths,
                int alphaSize)
{
  t.minLen = 32;
  t.maxLen = 0;
  for (int i = 0; i < alphaSize; ++i) {
    t.minLen = std::min<int>(t.minLen, lengths[i]);
    t.maxLen = std::max<int>(t.maxLen, lengths[i]);
  }

  int pp = 0;
  for (int i = t.minLen; i <= t.maxLen; ++i) {
    for (int j = 0; j < alphaSize; ++j) {
      if (lengths[j] == i) {
        t.perm[pp++] = j;
      }
    }
  }

  std::array<int, MaxCodeLength + 2> count{};
  for (int i = 0; i < alphaSize; ++i) {
    ++count[lengths[i]];
  }

  // the first code of each length, and the last one
  int code = 0;
  int index = 0;
  for (int len = t.minLen; len <= t.maxLen; ++len) {
    t.base[len] = index - code;  // perm index = code + base
    code += count[len];
    index += count[len];
    t.limit[len] = code - 1;
    code <<= 1;
  }
}

}  // namespace

bool bunzip2(const QByteArray& in, QByteArray& out, QString* error, qint64 maxOut)
{
  auto fail = [error](const char* why) {
    if (error) {
      *error = QString::fromLatin1(why);
    }
    return false;
  };

  out.clear();
  if (in.size() < 4 || in[0] != 'B' || in[1] != 'Z' || in[2] != 'h' || in[3] < '1' ||
      in[3] > '9') {
    return fail("not a bzip2 stream");
  }
  const int blockMax = (in[3] - '0') * 100000;

  BitReader br(in);
  quint32 skip = 0;
  br.bits(32, skip);

  const auto& crcTable = bzCrcTable();
  std::vector<quint8> block;
  std::vector<quint32> tt;

  for (;;) {
    quint32 m1 = 0, m2 = 0;
    if (!br.bits(24, m1) || !br.bits(24, m2)) {
      return fail("truncated");
    }
    if (m1 == 0x177245 && m2 == 0x385090) {
      break;  // end of stream; the combined CRC follows
    }
    if (m1 != 0x314159 || m2 != 0x265359) {
      return fail("bad block header");
    }

    quint32 blockCrc = 0, randomised = 0, origPtr = 0;
    if (!br.bits(32, blockCrc) || !br.bit(randomised) || !br.bits(24, origPtr)) {
      return fail("truncated");
    }
    if (randomised) {
      return fail("randomised blocks are not supported");
    }

    // the symbols the block uses
    quint32 used16 = 0;
    if (!br.bits(16, used16)) {
      return fail("truncated");
    }
    std::array<quint8, 256> seqToUnseq{};
    int inUse = 0;
    for (int i = 0; i < 16; ++i) {
      if (used16 & (0x8000u >> i)) {
        quint32 used = 0;
        if (!br.bits(16, used)) {
          return fail("truncated");
        }
        for (int j = 0; j < 16; ++j) {
          if (used & (0x8000u >> j)) {
            seqToUnseq[inUse++] = static_cast<quint8>(i * 16 + j);
          }
        }
      }
    }
    if (inUse == 0) {
      return fail("no symbols");
    }
    const int alphaSize = inUse + 2;

    quint32 groups = 0, selectorCount = 0;
    if (!br.bits(3, groups) || !br.bits(15, selectorCount) || groups < 2 ||
        groups > MaxGroups || selectorCount < 1) {
      return fail("bad Huffman groups");
    }

    // the selectors, move-to-front coded
    std::vector<quint8> selectors(selectorCount);
    {
      std::array<quint8, MaxGroups> mtf{};
      for (quint32 i = 0; i < groups; ++i) {
        mtf[i] = static_cast<quint8>(i);
      }
      for (quint32 i = 0; i < selectorCount; ++i) {
        quint32 j = 0, b = 0;
        for (;;) {
          if (!br.bit(b)) {
            return fail("truncated");
          }
          if (!b) {
            break;
          }
          if (++j >= groups) {
            return fail("bad selector");
          }
        }
        const quint8 v = mtf[j];
        for (; j > 0; --j) {
          mtf[j] = mtf[j - 1];
        }
        mtf[0]       = v;
        selectors[i] = v;
      }
    }

    // the code lengths of each group, delta coded
    std::array<HuffmanTable, MaxGroups> tables;
    for (quint32 t = 0; t < groups; ++t) {
      std::array<quint8, MaxAlphaSize> lengths{};
      quint32 curr = 0;
      if (!br.bits(5, curr)) {
        return fail("truncated");
      }
      for (int i = 0; i < alphaSize; ++i) {
        for (;;) {
          if (curr < 1 || curr > MaxCodeLength) {
            return fail("bad code length");
          }
          quint32 b = 0;
          if (!br.bit(b)) {
            return fail("truncated");
          }
          if (!b) {
            break;
          }
          if (!br.bit(b)) {
            return fail("truncated");
          }
          curr = b ? curr - 1 : curr + 1;
        }
        lengths[i] = static_cast<quint8>(curr);
      }
      buildTable(tables[t], lengths, alphaSize);
    }

    // the symbols: move-to-front values with runs of the front one
    std::array<quint8, 256> mtf{};
    for (int i = 0; i < 256; ++i) {
      mtf[i] = static_cast<quint8>(i);
    }
    std::array<quint32, 256> counts{};
    block.clear();
    const int eob     = inUse + 1;
    quint32 selector  = 0;
    int groupLeft     = 0;
    const HuffmanTable* table = nullptr;
    quint32 run       = 0;
    quint32 runWeight = 1;

    auto flushRun = [&]() -> bool {
      if (run == 0) {
        return true;
      }
      const quint8 uc = seqToUnseq[mtf[0]];
      if (block.size() + run > static_cast<size_t>(blockMax)) {
        return false;
      }
      counts[uc] += run;
      block.insert(block.end(), run, uc);
      run       = 0;
      runWeight = 1;
      return true;
    };

    for (;;) {
      if (groupLeft == 0) {
        if (selector >= selectorCount) {
          return fail("too few selectors");
        }
        table     = &tables[selectors[selector++]];
        groupLeft = GroupSize;
      }
      --groupLeft;

      int len     = table->minLen;
      quint32 code = 0;
      if (!br.bits(len, code)) {
        return fail("truncated");
      }
      while (len <= table->maxLen && static_cast<int>(code) > table->limit[len]) {
        quint32 b = 0;
        if (!br.bit(b)) {
          return fail("truncated");
        }
        code = (code << 1) | b;
        ++len;
      }
      if (len > table->maxLen) {
        return fail("bad Huffman code");
      }
      const int index = static_cast<int>(code) + table->base[len];
      if (index < 0 || index >= alphaSize) {
        return fail("bad Huffman code");
      }
      const int sym = table->perm[index];

      if (sym == 0 || sym == 1) {
        // RUNA and RUNB: the run length in bijective base 2
        run += (sym == 0 ? 1 : 2) * runWeight;
        runWeight <<= 1;
        if (run > static_cast<quint32>(blockMax)) {
          return fail("run too long");
        }
        continue;
      }
      if (!flushRun()) {
        return fail("block too long");
      }
      if (sym == eob) {
        break;
      }
      const int pos   = sym - 1;
      const quint8 v  = mtf[pos];
      std::memmove(&mtf[1], &mtf[0], pos);
      mtf[0]          = v;
      const quint8 uc = seqToUnseq[v];
      if (block.size() >= static_cast<size_t>(blockMax)) {
        return fail("block too long");
      }
      ++counts[uc];
      block.push_back(uc);
    }

    const quint32 n = static_cast<quint32>(block.size());
    if (origPtr >= n) {
      return fail("bad origin pointer");
    }

    // the inverse Burrows-Wheeler transform
    std::array<quint32, 256> cftab{};
    quint32 sum = 0;
    for (int i = 0; i < 256; ++i) {
      cftab[i] = sum;
      sum += counts[i];
    }
    tt.assign(n, 0);
    for (quint32 i = 0; i < n; ++i) {
      tt[cftab[block[i]]++] = i;
    }

    // and the first run-length coding: four alike, then how many more
    quint32 crc   = 0xFFFFFFFFu;
    // never more than the caller allows: the last run-length stage can grow
    // a block fifty times, and there may be any number of blocks
    bool tooLong  = false;
    auto put      = [&](quint8 b) {
      if (out.size() >= maxOut) {
        tooLong = true;
        return;
      }
      out.append(static_cast<char>(b));
      crc = (crc << 8) ^ crcTable[((crc >> 24) ^ b) & 0xFF];
    };
    quint32 idx   = tt[origPtr];
    int same      = 0;
    int last      = -1;
    for (quint32 k = 0; k < n; ++k) {
      const quint8 b = block[idx];
      idx            = tt[idx];
      if (same == 4) {
        for (int r = 0; r < b; ++r) {
          put(static_cast<quint8>(last));
        }
        same = 0;
        last = -1;
        continue;
      }
      if (b == last) {
        ++same;
      } else {
        same = 1;
        last = b;
      }
      put(b);
      if (tooLong) {
        return fail("larger than allowed");
      }
    }
    if (tooLong) {
      return fail("larger than allowed");
    }
    crc = ~crc;
    if (crc != blockCrc) {
      return fail("block CRC mismatch");
    }
  }

  return true;
}

namespace
{

// bsdiff's signed 64-bit numbers: little-endian, the top bit the sign
qint64 offtin(const char* p)
{
  qint64 y = static_cast<quint8>(p[7]) & 0x7F;
  for (int i = 6; i >= 0; --i) {
    y = y * 256 + static_cast<quint8>(p[i]);
  }
  return (static_cast<quint8>(p[7]) & 0x80) ? -y : y;
}

}  // namespace

bool patchSizeAllowed(qint64 patchSize, qint64 sourceSize)
{
  // Vortex's own rule (MAX_PATCH_SIZE, PATCH_OVERHEAD): an edit, not a file
  return patchSize >= 0 && sourceSize >= 0 &&
         static_cast<double>(patchSize) - PatchOverhead <=
             static_cast<double>(sourceSize) * MaxPatchRatio;
}

bool bspatch(const QByteArray& old, const QByteArray& patch, QByteArray& out,
             QString* error)
{
  auto fail = [error](const QString& why) {
    if (error) {
      *error = why;
    }
    return false;
  };

  const qint64 patchSize = patch.size();
  const qint64 oldSize   = old.size();
  if (patchSize < 32 || !patch.startsWith("BSDIFF40")) {
    return fail(QStringLiteral("not a BSDIFF40 patch"));
  }
  if (!patchSizeAllowed(patchSize, oldSize)) {
    return fail(QStringLiteral("the patch is too large for its file"));
  }

  // every length against the room left, never as a sum that could wrap
  const qint64 ctrlLen = offtin(patch.constData() + 8);
  const qint64 diffLen = offtin(patch.constData() + 16);
  const qint64 newSize = offtin(patch.constData() + 24);
  if (ctrlLen < 0 || diffLen < 0 || newSize < 0 || ctrlLen > patchSize - 32 ||
      diffLen > patchSize - 32 - ctrlLen) {
    return fail(QStringLiteral("bad patch header"));
  }
  // a patched file is about the size of its original
  if (newSize > MaxPatchedSize || newSize > oldSize * 4 + (qint64(64) << 20)) {
    return fail(QStringLiteral("the patched file would be too large"));
  }

  QByteArray ctrl, diff, extra;
  QString why;
  const qint64 maxCtrl = std::min<qint64>(24 * (newSize + 1), qint64(256) << 20);
  if (!bunzip2(patch.mid(32, ctrlLen), ctrl, &why, maxCtrl) ||
      !bunzip2(patch.mid(32 + ctrlLen, diffLen), diff, &why, newSize) ||
      !bunzip2(patch.mid(32 + ctrlLen + diffLen), extra, &why, newSize)) {
    return fail(QStringLiteral("damaged patch: %1").arg(why));
  }

  out = QByteArray(newSize, '\0');
  char* dst             = out.data();
  const char* src       = old.constData();
  const char* diffData  = diff.constData();
  const char* extraData = extra.constData();
  const qint64 ctrlSize  = ctrl.size();
  const qint64 diffSize  = diff.size();
  const qint64 extraSize = extra.size();
  qint64 newPos = 0, oldPos = 0, ctrlPos = 0, diffPos = 0, extraPos = 0;

  while (newPos < newSize) {
    if (ctrlSize - ctrlPos < 24) {
      return fail(QStringLiteral("damaged patch: control data"));
    }
    const qint64 add  = offtin(ctrl.constData() + ctrlPos);
    const qint64 copy = offtin(ctrl.constData() + ctrlPos + 8);
    const qint64 seek = offtin(ctrl.constData() + ctrlPos + 16);
    ctrlPos += 24;

    if (add < 0 || copy < 0 || add > newSize - newPos || add > diffSize - diffPos) {
      return fail(QStringLiteral("damaged patch: diff data"));
    }
    for (qint64 i = 0; i < add; ++i) {
      char b         = diffData[diffPos + i];
      const qint64 o = oldPos + i;
      if (o >= 0 && o < oldSize) {
        b = static_cast<char>(static_cast<quint8>(b) + static_cast<quint8>(src[o]));
      }
      dst[newPos + i] = b;
    }
    newPos += add;
    oldPos += add;
    diffPos += add;

    if (copy > newSize - newPos || copy > extraSize - extraPos) {
      return fail(QStringLiteral("damaged patch: extra data"));
    }
    std::memcpy(dst + newPos, extraData + extraPos, static_cast<size_t>(copy));
    newPos += copy;
    extraPos += copy;

    // the old position stays near the old file: it can't run away and wrap
    if (seek > oldSize + newSize || seek < -(oldSize + newSize)) {
      return fail(QStringLiteral("damaged patch: seek"));
    }
    oldPos += seek;
    if (oldPos > oldSize + newSize || oldPos < -(oldSize + newSize)) {
      return fail(QStringLiteral("damaged patch: seek"));
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// paths

bool isSafeRelativePath(const QString& relative)
{
  if (relative.isEmpty()) {
    return false;
  }
  static const QString forbidden = QStringLiteral("<>:\"|?*");
  static const QRegularExpression device(
      QStringLiteral("^(con|prn|aux|nul|com[0-9\u00b9\u00b2\u00b3]|"
                     "lpt[0-9\u00b9\u00b2\u00b3]|conin\\$|conout\\$|clock\\$)$"),
      QRegularExpression::CaseInsensitiveOption);
  for (const QString& part : relative.split('/')) {
    if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') ||
        part.endsWith(' ')) {
      return false;
    }
    for (const QChar c : part) {
      if (c.unicode() < 32 || forbidden.contains(c)) {
        return false;
      }
    }
    // Windows' device names, alone or with an extension ("nul.txt")
    if (device.match(part.section('.', 0, 0).trimmed()).hasMatch()) {
      return false;
    }
  }
  const std::filesystem::path p(relative.toStdWString());
  return !p.has_root_name() && !p.has_root_directory() && p.is_relative();
}

// ---------------------------------------------------------------------------
// replicate installs

QString normalizedPath(const QString& path)
{
  QString p = path;
  p.replace('\\', '/');
  while (p.startsWith('/')) {
    p.remove(0, 1);
  }
  return p.toLower();
}

FileListResult compareFileList(const std::vector<FileHash>& listed,
                               const QStringList& present,
                               const std::function<QString(const QString&)>& md5Of)
{
  FileListResult r;
  std::map<QString, QString> have;  // normalized to as present
  for (const QString& p : present) {
    have.emplace(normalizedPath(p), p);
  }
  std::set<QString> wanted;
  for (const auto& f : listed) {
    const QString key = normalizedPath(f.path);
    wanted.insert(key);
    auto it = have.find(key);
    if (it == have.end()) {
      r.missing.append(f.path);
    } else if (!f.md5.isEmpty() && md5Of &&
               md5Of(it->second).compare(f.md5, Qt::CaseInsensitive) != 0) {
      r.mismatched.append(f.path);
    }
  }
  for (const auto& [key, original] : have) {
    if (!wanted.count(key)) {
      r.extra.append(original);
    }
  }

  // MO2 Revamped: a file the game plugin's installer moved into a folder
  // (Baldur's Gate 3 puts a top-level DWrite.dll in bin/, Public/... in
  // Data/Public/...) is listed at its old path and present at the new one, and
  // hiding it as extra left the mod without it. It counts as the listed file
  // when its path ends with the listed path, it's the only such file, and its
  // MD5 is the listed one (when the list has one).
  if (!r.missing.isEmpty() && !r.extra.isEmpty()) {
    std::map<QString, const FileHash*> missingByKey;
    const QSet<QString> missingPaths(r.missing.begin(), r.missing.end());
    for (const auto& f : listed) {
      if (missingPaths.contains(f.path)) {
        missingByKey.emplace(normalizedPath(f.path), &f);
      }
    }
    std::map<QString, QStringList> candidates;  // listed key -> extras ending so
    for (const QString& e : r.extra) {
      const QString n = normalizedPath(e);
      for (qsizetype slash = n.indexOf('/'); slash >= 0; slash = n.indexOf('/', slash + 1)) {
        const QString tail = n.mid(slash + 1);
        if (missingByKey.count(tail)) {
          candidates[tail].append(e);
        }
      }
    }
    QSet<QString> movedListed, movedPresent;
    std::map<QString, int> claims;  // extra -> listed files it could be
    for (const auto& [key, extras] : candidates) {
      for (const QString& e : extras) {
        ++claims[e];
      }
    }
    for (const auto& [key, extras] : candidates) {
      if (extras.size() != 1 || claims[extras.front()] != 1) {
        continue;
      }
      const FileHash* f = missingByKey.at(key);
      if (!f->md5.isEmpty() &&
          (!md5Of || md5Of(extras.front()).compare(f->md5, Qt::CaseInsensitive) != 0)) {
        continue;
      }
      movedListed.insert(f->path);
      movedPresent.insert(extras.front());
    }
    r.missing.removeIf([&](const QString& p) {
      return movedListed.contains(p);
    });
    r.extra.removeIf([&](const QString& p) {
      return movedPresent.contains(p);
    });
  }
  return r;
}

// ---------------------------------------------------------------------------
// mods for the game's own folder

bool isGameRootType(const QString& vortexType)
{
  return vortexType.compare(QStringLiteral("dinput"), Qt::CaseInsensitive) == 0 ||
         vortexType.compare(QStringLiteral("enb"), Qt::CaseInsensitive) == 0;
}

QString rootLayoutPath(const QString& gamePath, const QString& dataFolderName)
{
  QString p = gamePath;
  p.replace('\\', '/');
  while (p.startsWith('/')) {
    p.remove(0, 1);
  }
  const int slash = p.indexOf('/');
  if (slash > 0 && !dataFolderName.isEmpty() &&
      p.left(slash).compare(dataFolderName, Qt::CaseInsensitive) == 0) {
    return p.mid(slash + 1);
  }
  return QStringLiteral("Root/") + p;
}

QString gameRootBase(const QStringList& files, const QString& vortexType,
                     const std::vector<FileHash>& listed, const QString& dataFolderName)
{
  // the archive's files and folders, by their compared path
  std::set<QString> have;
  std::map<QString, QString> folders;  // compared, with a trailing slash -> as it is
  // the top: empty, not null (null is "can't tell")
  folders.emplace(QString(), QStringLiteral(""));
  for (QString f : files) {
    f.replace('\\', '/');
    while (f.startsWith('/')) {
      f.remove(0, 1);
    }
    have.insert(f.toLower());
    for (int slash = f.indexOf('/'); slash > 0; slash = f.indexOf('/', slash + 1)) {
      folders.emplace(f.left(slash + 1).toLower(), f.left(slash + 1));
    }
  }

  if (!listed.empty()) {
    // the folder where the collection's files are; the shallowest of equals
    QString best;
    std::size_t bestCount = 0;
    std::size_t bestDepth = 0;
    for (const auto& [key, original] : folders) {
      std::size_t count = 0;
      for (const FileHash& h : listed) {
        if (have.count(key + normalizedPath(h.path))) {
          ++count;
        }
      }
      const auto depth = std::size_t(key.count('/'));
      if (count > bestCount || (count == bestCount && count > 0 && depth < bestDepth)) {
        best      = original;
        bestCount = count;
        bestDepth = depth;
      }
    }
    if (bestCount * 2 > listed.size()) {
      return best;
    }
    // not laid out as the collection says: as Vortex finds it
  }

  // the folder of the shallowest file that matches, as it is in files
  const auto folderOf = [&files](const std::function<bool(const QString&)>& match) {
    QString found;
    int depth = -1;
    for (QString f : files) {
      f.replace('\\', '/');
      while (f.startsWith('/')) {
        f.remove(0, 1);
      }
      const QString name = f.section('/', -1).toLower();
      if (!match(name)) {
        continue;
      }
      const int d = int(f.count('/'));
      if (depth < 0 || d < depth) {
        depth = d;
        found = f.left(f.size() - name.size());
      }
    }
    return depth < 0 ? QString() : (found.isNull() ? QStringLiteral("") : found);
  };

  if (vortexType.compare(QStringLiteral("enb"), Qt::CaseInsensitive) == 0) {
    return folderOf([](const QString& name) {
      return name == u"enbseries.ini";
    });
  }
  if (vortexType == u"dinput-any") {
    return folderOf([](const QString& name) {
      return name.endsWith(u".exe") || name.endsWith(u".dll");
    });
  }
  QString base = folderOf([](const QString& name) {
    return name.endsWith(u"_loader.exe");
  });
  if (base.isNull()) {
    base = folderOf([](const QString& name) {
      return name == u"dinput8.dll";
    });
  }
  if (base.isNull()) {
    // not a plugin of the script extender's (Data/SKSE/Plugins/x.dll): the
    // game's folder is above its data folder
    QStringList outsideData;
    for (const QString& f : files) {
      QString p = QString(f).replace('\\', '/');
      while (p.startsWith('/')) {
        p.remove(0, 1);
      }
      const QStringList parts = p.split('/');
      bool inData             = false;
      for (int i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i].compare(dataFolderName, Qt::CaseInsensitive) == 0) {
          inData = true;
          break;
        }
      }
      if (!inData) {
        outsideData.append(p);
      }
    }
    base = gameRootBase(outsideData, QStringLiteral("dinput-any"), {}, dataFolderName);
  }
  return base;
}

// ---------------------------------------------------------------------------
// INI tweaks

namespace
{
const QRegularExpression& tweakName()
{
  static const QRegularExpression exp(
      QStringLiteral("^(\\(optional\\)\\.)?(.*)\\[([^\\[\\]]+\\.ini)\\]\\.ini$"),
      QRegularExpression::CaseInsensitiveOption);
  return exp;
}
}  // namespace

QString iniTweakTarget(const QString& fileName)
{
  const auto m = tweakName().match(fileName.trimmed());
  return m.hasMatch() ? m.captured(3).trimmed() : QString();
}

bool iniTweakOptional(const QString& fileName)
{
  const auto m = tweakName().match(fileName.trimmed());
  return m.hasMatch() && !m.captured(1).isEmpty();
}

QString mergeIni(const QString& target, const QString& tweak)
{
  // the tweak's settings by section, in order
  std::vector<std::pair<QString, std::vector<std::pair<QString, QString>>>> wanted;
  QString section;
  for (const QString& raw : tweak.split('\n')) {
    const QString line = raw.trimmed();
    if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) {
      continue;
    }
    if (line.startsWith('[') && line.endsWith(']')) {
      section = line.mid(1, line.size() - 2).trimmed();
      continue;
    }
    const int eq = line.indexOf('=');
    // a setting before any section has no place in the game's INI files
    if (eq <= 0 || section.isEmpty()) {
      continue;
    }
    auto it = std::find_if(wanted.begin(), wanted.end(), [&section](const auto& s) {
      return s.first.compare(section, Qt::CaseInsensitive) == 0;
    });
    if (it == wanted.end()) {
      wanted.push_back({section, {}});
      it = wanted.end() - 1;
    }
    it->second.emplace_back(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
  }

  const bool crlf = target.contains(QStringLiteral("\r\n"));
  QStringList lines = QString(target).remove('\r').split('\n');
  if (!lines.isEmpty() && lines.last().isEmpty()) {
    lines.removeLast();
  }

  for (const auto& [name, settings] : wanted) {
    // the section's lines: from its header to the next one
    int start = -1, end = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
      const QString l = lines[i].trimmed();
      if (l.startsWith('[') && l.endsWith(']')) {
        if (start >= 0) {
          end = i;
          break;
        }
        if (l.mid(1, l.size() - 2).trimmed().compare(name, Qt::CaseInsensitive) == 0) {
          start = i;
        }
      }
    }
    if (start < 0) {
      if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) {
        lines.append(QString());
      }
      lines.append(QStringLiteral("[%1]").arg(name));
      start = lines.size() - 1;
      end   = lines.size();
    }

    for (const auto& [key, value] : settings) {
      bool done = false;
      for (int i = start + 1; i < end; ++i) {
        const QString l = lines[i].trimmed();
        const int eq    = l.indexOf('=');
        if (eq > 0 && !l.startsWith(';') && !l.startsWith('#') &&
            l.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) {
          lines[i] = QStringLiteral("%1=%2").arg(l.left(eq).trimmed(), value);
          done     = true;
          break;
        }
      }
      if (!done) {
        // after the section's last setting, before its trailing blank lines
        int at = end;
        while (at - 1 > start && lines[at - 1].trimmed().isEmpty()) {
          --at;
        }
        lines.insert(at, QStringLiteral("%1=%2").arg(key, value));
        ++end;
      }
    }
  }

  QString result = lines.join(crlf ? QStringLiteral("\r\n") : QStringLiteral("\n"));
  result += crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
  return result;
}

QString decodeIni(const QByteArray& bytes, IniEncoding* encoding)
{
  IniEncoding e;
  QByteArray body = bytes;
  if (body.startsWith("\xEF\xBB\xBF")) {
    e.utf8 = true;
    e.bom  = true;
    body   = body.mid(3);
  }
  auto decoder = QStringDecoder(QStringDecoder::Utf8);
  QString text = decoder.decode(body);
  if (!e.bom && decoder.hasError()) {
    // the games write their INI files in the ANSI code page: read as Latin-1,
    // so every byte comes back the same
    text   = QString::fromLatin1(body);
    e.utf8 = false;
  } else {
    e.utf8 = true;
  }
  if (encoding != nullptr) {
    *encoding = e;
  }
  return text;
}

QByteArray encodeIni(const QString& text, const IniEncoding& encoding)
{
  if (!encoding.utf8) {
    return text.toLatin1();
  }
  return (encoding.bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + text.toUtf8();
}

// ---------------------------------------------------------------------------
// revisions

RevisionDiff diffRevisions(const std::vector<RevisionFile>& before,
                           const std::vector<RevisionFile>& after)
{
  RevisionDiff d;
  std::vector<bool> usedBefore(before.size(), false);
  std::vector<bool> usedAfter(after.size(), false);

  // the earlier files by file id and by mod id, in their order: the first
  // one not taken yet is the one a scan would find
  QHash<int, std::vector<int>> beforeByFile, beforeByMod;
  for (int b = 0; b < static_cast<int>(before.size()); ++b) {
    beforeByFile[before[b].fileId].push_back(b);
    beforeByMod[before[b].modId].push_back(b);
  }
  auto firstFree = [&usedBefore](std::vector<int>& list, size_t& from) {
    while (from < list.size() && usedBefore[list[from]]) {
      ++from;
    }
    return from < list.size() ? list[from] : -1;
  };
  QHash<int, size_t> fileFrom, modFrom;

  // the same file
  for (int a = 0; a < static_cast<int>(after.size()); ++a) {
    if (after[a].fileId <= 0) {
      continue;
    }
    auto it = beforeByFile.find(after[a].fileId);
    if (it == beforeByFile.end()) {
      continue;
    }
    const int b = firstFree(it.value(), fileFrom[after[a].fileId]);
    if (b >= 0) {
      d.same.emplace_back(b, a);
      usedBefore[b] = usedAfter[a] = true;
    }
  }
  // the same mod with another file (an update)
  for (int a = 0; a < static_cast<int>(after.size()); ++a) {
    if (usedAfter[a] || after[a].modId <= 0) {
      continue;
    }
    auto it = beforeByMod.find(after[a].modId);
    if (it == beforeByMod.end()) {
      continue;
    }
    const int b = firstFree(it.value(), modFrom[after[a].modId]);
    if (b >= 0) {
      d.changed.emplace_back(b, a);
      usedBefore[b] = usedAfter[a] = true;
    }
  }
  for (int a = 0; a < static_cast<int>(after.size()); ++a) {
    if (!usedAfter[a]) {
      d.added.push_back(a);
    }
  }
  for (int b = 0; b < static_cast<int>(before.size()); ++b) {
    if (!usedBefore[b]) {
      d.removed.push_back(b);
    }
  }
  return d;
}

}  // namespace Collections
