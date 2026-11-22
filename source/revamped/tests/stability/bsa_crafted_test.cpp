// MO2 Revamped: bsatk on crafted archives (not shipped). Each damaged archive
// must come back as an error (exception or error code), never as a crash or a
// write past a buffer; a well-formed one must still read.
// Built against bsatk.lib from the upstream build (see the report for the
// command line); run it from a folder it can write test files into.
#include <bsaarchive.h>
#include <bsaexception.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::vector<char> bytes;

void u8(uint8_t v) { bytes.push_back(static_cast<char>(v)); }
void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
void u64(uint64_t v) { u32(static_cast<uint32_t>(v)); u32(static_cast<uint32_t>(v >> 32)); }
void text(const char* s, bool nul)
{
  bytes.insert(bytes.end(), s, s + std::strlen(s));
  if (nul) {
    u8(0);
  }
}

std::filesystem::path save(const char* name)
{
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
  bytes.clear();
  return path;
}

// 0 read, 1 refused (error code or exception)
int readArchive(const std::filesystem::path& path, unsigned long* files = nullptr)
{
  BSA::Archive archive;
  try {
    const auto res = archive.read(path, false);
    if (res != BSA::ERROR_NONE && res != BSA::ERROR_INVALIDHASHES) {
      return 1;
    }
  } catch (const std::exception& e) {
    std::printf("      (refused: %s)\n", e.what());
    return 1;
  }
  if (files != nullptr) {
    *files = archive.getRoot()->countFiles();
  }
  return 0;
}

int failures = 0;
void check(bool ok, const char* what)
{
  std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
  failures += ok ? 0 : 1;
}

// a BA2 texture archive with one texture of the given number of chunks
std::filesystem::path ba2Texture(uint8_t chunks)
{
  text("BTDX", false);
  u32(1);
  text("DX10", false);
  u32(1);                                 // files
  u64(24 + 24 + 24ULL * chunks);          // name table
  u32(0); text("dds", true); u32(0);      // name hash, extension, dir hash
  u8(0); u8(chunks); u16(24);             // unknown, chunk count, chunk header size
  u16(4); u16(4); u8(1); u8(0x47); u8(0); u8(0);
  for (uint8_t i = 0; i < chunks; ++i) {
    u64(200); u32(16); u32(16); u16(0); u16(0); u32(0xBAADF00D);
  }
  u16(5); text("a.dds", false);
  return save(chunks == 0 ? "mo2r_ba2_nochunks.ba2" : "mo2r_ba2_ok.ba2");
}

// a Morrowind BSA with one file named name, the header's offset as given
std::filesystem::path morrowind(uint32_t offset, const char* file)
{
  u32(0x100);
  u32(offset);
  u32(1);
  u32(5); u32(0);   // size, offset of the data
  u32(0);           // the name's offset
  text("a.txt", true);
  u64(0);           // the hash table
  text("hello", false);
  return save(file);
}
}  // namespace

int main()
{
  unsigned long files = 0;

  check(readArchive(ba2Texture(1), &files) == 0 && files == 1,
        "a BA2 texture with a chunk still reads");
  check(readArchive(ba2Texture(0)) == 1, "a BA2 texture without chunks is refused");

  check(readArchive(morrowind(18, "mo2r_tes3_ok.bsa"), &files) == 0 && files == 1,
        "a Morrowind BSA still reads");
  // offset 11 < 12 * 1: the last name's length wrapped to 0xFFFFFFFF and its
  // buffer of new char[0] was written 4 GB past its end
  check(readArchive(morrowind(11, "mo2r_tes3_wrap.bsa")) == 1,
        "a Morrowind BSA whose name table wraps is refused");
  check(readArchive(morrowind(0x7FFFFFFF, "mo2r_tes3_huge.bsa")) == 1,
        "a Morrowind BSA whose name table is past its end is refused");

  // two names whose offsets go backwards
  u32(0x100); u32(12 * 2 + 4); u32(2);
  u32(1); u32(0); u32(1); u32(1);
  u32(2); u32(0);
  text("a", true); text("b", true);
  u64(0); u64(0);
  text("xy", false);
  check(readArchive(save("mo2r_tes3_back.bsa")) == 1,
        "a Morrowind BSA with name offsets going backwards is refused");

  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
