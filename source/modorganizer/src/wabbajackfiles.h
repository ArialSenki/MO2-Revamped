#ifndef WABBAJACKFILES_H
#define WABBAJACKFILES_H

// MO2 Revamped: the pieces of the Wabbajack installer that need only Qt: the
// paths under the install folder, the list's binary patches, and the zEdit
// merges (MergedPatch) built with them. Apart from the installer so their
// tests build without 7-Zip, libbsarch or DirectXTex.

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace Wabbajack
{

// a path of the list ("mods\X\a.esp") under the install folder; empty when it
// would lead out of it
QString installPath(const QString& installFolder, const QString& relative);

// the folder where an install keeps its own files (temporary ones, the MO2
// plugins set aside)
QString installStateFolder(const QString& installFolder);

// a patch the list carries applied to basisPath, written to outPath: OctoDiff
// (Octopus Deploy's format, which Wabbajack uses) or BSDiff (old lists).
// expected is the size the list gives the result, never written past (0: the
// collections' cap); false and the reason in *error when it can't
bool applyPatch(const QString& basisPath, const QString& patchPath, const QString& outPath,
                qint64 expected, QString* error);

// ---- zEdit merges (MergedPatch) ---------------------------------------------
//
// Wabbajack doesn't run zEdit: the merged plugin is the list's patch applied to
// its sources joined end to end, in the list's order. The sources are files the
// list installed before, read from the install folder.

struct MergeSource
{
  QString relativePath;  // as the list writes it, with backslashes
  uint64_t hash = 0;     // 0 when the list doesn't give one
};

// the Sources of a MergedPatch (Directive::extra, {"Sources":[...]}), in order;
// false and the reason in *error when there are none or they can't be read
bool readMergeSources(const QByteArray& extra, std::vector<MergeSource>* sources,
                      QString* error);

// where a source of the list is on disk; empty and the reason in *why when it
// isn't one to read (a path out of the install folder, say)
using MergeSourcePath = std::function<QString(const QString& relative, QString* why)>;

struct MergeRequest
{
  QByteArray sources;    // Directive::extra
  QString patchPath;     // the list's patch (its PatchID), taken out of the list
  QString joinedPath;    // a temporary file for the sources joined end to end
  QString target;        // the merged plugin
  uint64_t hash = 0;     // the list's hash of it, 0 when none
  qint64 size   = 0;     // the list's size of it, 0 when none
};

// builds a zEdit merge: its sources joined, the patch applied, the result
// checked against the list's hash and size. Nothing stays at target when it
// fails (or when cancel is set); *error names the source that is missing or
// isn't the list's, when that's why
bool buildMergedPatch(const MergeRequest& request, const MergeSourcePath& sourcePath,
                      const std::atomic_bool* cancel, QString* error);

}  // namespace Wabbajack

#endif  // WABBAJACKFILES_H
