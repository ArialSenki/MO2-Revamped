#ifndef COLLECTIONFILES_H
#define COLLECTIONFILES_H

// MO2 Revamped: Nexus collections, phase 2. The file work a collection asks
// for after a mod is installed, as Vortex does it: binary patches (BSDIFF40,
// bzip2 inside), the exact file list of a "replicate" install, INI tweaks,
// and the difference between two revisions. Qt Core only, so it can be
// tested on its own.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Collections
{

// ---------------------------------------------------------------------------
// patches

// the CRC-32 Vortex keeps for a patched file's original (zlib's CRC-32)
quint32 crc32(const QByteArray& data);

// a CRC written as hex ("75737292", with or without leading zeros, any case);
// nothing when it isn't one
std::optional<quint32> parseCrc(const QString& text);

// one bzip2 stream, decompressed; false with the reason in error when it is
// damaged (every block's CRC is checked)
// at most maxOut bytes: past that it fails (a small stream can unpack to
// gigabytes)
bool bunzip2(const QByteArray& in, QByteArray& out, QString* error = nullptr,
             qint64 maxOut = qint64(256) << 20);

// Vortex's limits: a patch is at most this share of its file, plus overhead
constexpr double MaxPatchRatio = 0.2;
constexpr qint64 PatchOverhead = 130;
// and no patched file larger than this
constexpr qint64 MaxPatchedSize = qint64(1) << 30;

bool patchSizeAllowed(qint64 patchSize, qint64 sourceSize);

// a BSDIFF40 patch applied to old; false with the reason in error
bool bspatch(const QByteArray& old, const QByteArray& patch, QByteArray& out,
             QString* error = nullptr);

// a relative path from a collection, safe to use under a mod's folder: no
// root, drive, "..", empty or dot-ending parts, characters Windows refuses,
// or device names (CON, NUL, COM1...); forward slashes
bool isSafeRelativePath(const QString& relative);

// ---------------------------------------------------------------------------
// replicate installs

struct FileHash
{
  QString path;  // relative to the mod's folder, as collection.json has it
  QString md5;   // lowercase hex, may be empty
};

// a path as compared: forward slashes, no leading slash, lowercase
QString normalizedPath(const QString& path);

struct FileListResult
{
  QStringList missing;     // listed, not in the mod
  QStringList extra;       // in the mod, not listed (paths as in the mod)
  QStringList mismatched;  // listed with an MD5 the file doesn't have
};

// present: the mod's files (relative paths); md5Of gives a file's MD5, asked
// only for the listed files that have one
FileListResult compareFileList(const std::vector<FileHash>& listed,
                               const QStringList& present,
                               const std::function<QString(const QString&)>& md5Of);

// ---------------------------------------------------------------------------
// INI tweaks

// "<name> [<target ini>].ini" (Vortex's naming, "(optional)." in front for
// optional ones): the target ini, empty when the name isn't a tweak's
QString iniTweakTarget(const QString& fileName);
bool iniTweakOptional(const QString& fileName);

// the settings of tweak written into target: an existing key (without case)
// gets the tweak's value, a new one goes at the end of its section, a new
// section at the end. Lines, comments and order are kept
QString mergeIni(const QString& target, const QString& tweak);

// an INI file's text as it is on disk: UTF-8 (with its BOM, if any) when it
// decodes as such, else the ANSI code page read as Latin-1 so no byte changes
struct IniEncoding
{
  bool utf8 = true;
  bool bom  = false;
};
QString decodeIni(const QByteArray& bytes, IniEncoding* encoding);
QByteArray encodeIni(const QString& text, const IniEncoding& encoding);

// ---------------------------------------------------------------------------
// revisions

struct RevisionFile
{
  int modId  = 0;
  int fileId = 0;
};

struct RevisionDiff
{
  std::vector<std::pair<int, int>> same;     // (old index, new index), same file
  std::vector<std::pair<int, int>> changed;  // same mod, another file
  std::vector<int> added;                    // new index
  std::vector<int> removed;                  // old index
};

RevisionDiff diffRevisions(const std::vector<RevisionFile>& before,
                           const std::vector<RevisionFile>& after);

}  // namespace Collections

#endif  // COLLECTIONFILES_H
