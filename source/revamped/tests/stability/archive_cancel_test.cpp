// MO2 Revamped: the archive library's cancel (not shipped).
//   archive_cancel_test <archive with one big file> <empty output folder>
// Run from an MO2 folder (the library loads dlls\7z.dll from the current
// folder). Checks: cancel() before any extraction and after one doesn't crash,
// an extraction canceled midway leaves no file (it was left at its full size
// with a tail of zeros), and a whole extraction still works.
#include <archive.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{
int failures = 0;
void check(bool ok, const char* what)
{
  std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
  failures += ok ? 0 : 1;
}

// extracts every file into out; cancels once cancelAt of the bytes are written
// (never when cancelAt > 1). Returns extract()'s result
bool run(const std::wstring& path, const fs::path& out, double cancelAt,
         bool cancelFirst, uint64_t* fullSize)
{
  auto archive = CreateArchive();
  if (!archive->isValid() || !archive->open(path, nullptr)) {
    std::printf("can't open the archive (error %d)\n", static_cast<int>(archive->getLastError()));
    ++failures;
    return false;
  }
  if (cancelFirst) {
    archive->cancel();  // nothing is extracting: must do nothing
  }
  for (FileData* file : archive->getFileList()) {
    file->addOutputFilePath(file->getArchiveFilePath());
    if (fullSize != nullptr && file->getSize() > *fullSize) {
      *fullSize = file->getSize();
    }
  }
  Archive* raw      = archive.get();
  const bool result = archive->extract(
      out.native(),
      [raw, cancelAt](Archive::ProgressType, uint64_t done, uint64_t total) {
        if (total > 0 && static_cast<double>(done) / total >= cancelAt) {
          raw->cancel();
        }
      },
      nullptr, [](std::wstring const&) {});
  archive->cancel();  // after the extraction: its callback is gone
  return result;
}
}  // namespace

int wmain(int argc, wchar_t** argv)
{
  if (argc < 3) {
    std::printf("usage: archive_cancel_test <archive> <output folder>\n");
    return 2;
  }
  const std::wstring archive = argv[1];
  const fs::path out         = argv[2];
  fs::remove_all(out);
  fs::create_directories(out);

  uint64_t size = 0;
  check(!run(archive, out, 0.3, true, &size), "an extraction canceled at 30% fails");
  size_t left = 0;
  for (const auto& entry : fs::recursive_directory_iterator(out)) {
    if (entry.is_regular_file()) {
      ++left;
      std::printf("      left: %ls (%llu bytes)\n", entry.path().c_str(),
                  static_cast<unsigned long long>(entry.file_size()));
    }
  }
  check(left == 0, "the file it was writing is gone");

  check(run(archive, out, 2.0, false, nullptr), "a whole extraction works");
  uint64_t biggest = 0;
  for (const auto& entry : fs::recursive_directory_iterator(out)) {
    if (entry.is_regular_file() && entry.file_size() > biggest) {
      biggest = entry.file_size();
    }
  }
  check(size > 0 && biggest == size, "and its file has its full size");

  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
