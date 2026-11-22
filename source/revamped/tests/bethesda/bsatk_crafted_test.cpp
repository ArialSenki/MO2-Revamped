// MO2 Revamped: crafted BSA/BA2 archives against bsatk (not shipped). Each must
// be refused with an exception or an error code, never crash or take minutes
// or gigabytes (the BSA preview and the archive checks read mods' archives).
// With archive paths as arguments it opens those instead and counts their
// files: real archives must still open.
// Build (Developer prompt): compile bsatk's sources and this file with the
// include paths and defines of the bsatk project, and link DirectXTex, boost
// thread/chrono/atomic, zlib and lz4 (liblz4.dll beside the program), plus
// psapi.lib.
#include "bsaarchive.h"
#include "bsaexception.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>
#include <psapi.h>

static void put32(std::vector<char>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(char((x >> (8 * i)) & 0xff)); }
static void put16(std::vector<char>& v, uint16_t x) { v.push_back(char(x & 0xff)); v.push_back(char(x >> 8)); }
static void put64(std::vector<char>& v, uint64_t x) { put32(v, uint32_t(x)); put32(v, uint32_t(x >> 32)); }

static std::string write(const char* name, const std::vector<char>& data)
{
  const std::string path = (std::filesystem::temp_directory_path() / name).string();
  std::ofstream(path, std::ios::binary).write(data.data(), data.size());
  return path;
}

static int failures = 0;
static void run(const char* what, const std::string& path, bool shouldOpen)
{
  const auto t0 = std::chrono::steady_clock::now();
  bool opened = false;
  std::string why;
  try {
    BSA::Archive archive;
    opened = archive.read(path.c_str(), false) == BSA::ERROR_NONE;
  } catch (const std::exception& e) {
    why = e.what();
  }
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  PROCESS_MEMORY_COUNTERS pmc{};
  GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
  const bool ok = opened == shouldOpen && s < 5.0;
  if (!ok) ++failures;
  std::printf("%s  %-48s opened=%d %.2fs peak=%zu MB %s\n", ok ? "PASS" : "FAIL", what, opened, s,
              pmc.PeakWorkingSetSize >> 20, why.c_str());
}

static unsigned countFiles(BSA::Folder::Ptr folder)
{
  unsigned n = folder->getNumFiles();
  for (unsigned i = 0; i < folder->getNumSubFolders(); ++i) n += countFiles(folder->getSubFolder(i));
  return n;
}

int main(int argc, char** argv)
{
  if (argc > 1) {
    // real archives: each must still open, with its files
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
      try {
        BSA::Archive archive;
        const auto r = archive.read(argv[i], false);
        const unsigned n = r == BSA::ERROR_NONE ? countFiles(archive.getRoot()) : 0;
        if (r != BSA::ERROR_NONE || n == 0) { ++bad; std::printf("FAIL %s (%d)\n", argv[i], int(r)); }
        else std::printf("ok %u %s\n", n, argv[i]);
      } catch (const std::exception& e) {
        ++bad; std::printf("FAIL %s: %s\n", argv[i], e.what());
      }
    }
    std::printf("%d of %d failed\n", bad, argc - 1);
    return bad ? 1 : 0;
  }

  // 1. Morrowind name table going backwards (heap overflow before)
  {
    std::vector<char> v;
    put32(v, 0x100); put32(v, 12 * 2 + 16); put32(v, 2);       // id, offset, fileCount
    put32(v, 4); put32(v, 0); put32(v, 4); put32(v, 4);          // size/offset pairs
    put32(v, 8); put32(v, 7);                                    // name offsets: 8 then 7
    for (int i = 0; i < 64; ++i) v.push_back('A');
    run("Morrowind: name table goes backwards", write("mw-back.bsa", v), false);
  }
  // 2. Morrowind with 4 billion files
  {
    std::vector<char> v;
    put32(v, 0x100); put32(v, 16); put32(v, 0xFFFFFFF0u);
    for (int i = 0; i < 256; ++i) v.push_back(0);
    run("Morrowind: 4 billion files claimed", write("mw-huge.bsa", v), false);
  }
  // 3. Morrowind offset smaller than the records (wrap of `last`)
  {
    std::vector<char> v;
    put32(v, 0x100); put32(v, 4); put32(v, 2);
    for (int i = 0; i < 64; ++i) v.push_back(0);
    run("Morrowind: offset smaller than its records", write("mw-wrap.bsa", v), false);
  }
  // 4. a valid little Morrowind archive still opens
  {
    std::vector<char> v;
    const char* names[] = {"meshes\\a.nif", "textures\\b.dds"};
    std::string table;
    std::vector<uint32_t> offs;
    for (auto n : names) { offs.push_back(uint32_t(table.size())); table += n; table.push_back('\0'); }
    const uint32_t count = 2;
    const uint32_t hashOffset = 12 * count + uint32_t(table.size());
    put32(v, 0x100); put32(v, hashOffset); put32(v, count);
    put32(v, 1); put32(v, 0); put32(v, 1); put32(v, 1);
    for (auto o : offs) put32(v, o);
    v.insert(v.end(), table.begin(), table.end());
    for (uint32_t i = 0; i < count; ++i) put64(v, i + 1);  // hashes
    v.push_back('x'); v.push_back('y');
    run("Morrowind: a valid archive still opens", write("mw-ok.bsa", v), true);
  }
  // 5. BA2 DX10 texture with zero chunks
  {
    std::vector<char> v;
    v.insert(v.end(), {'B', 'T', 'D', 'X'}); put32(v, 1);
    v.insert(v.end(), {'D', 'X', '1', '0'}); put32(v, 1);
    const uint64_t nameTable = 24 + 24;
    put64(v, nameTable);
    put32(v, 1); v.insert(v.end(), {'d', 'd', 's', 0}); put32(v, 2);
    v.push_back(0); v.push_back(0); put16(v, 24); put16(v, 4); put16(v, 4);
    v.push_back(1); v.push_back(0); v.push_back(0); v.push_back(0);
    put16(v, 5); v.insert(v.end(), {'a', '\\', 'b', '.', 'd'});
    run("BA2 DX10: texture with no chunks", write("ba2-nochunk.ba2", v), false);
  }
  // 6. BA2 GNRL with a name of 60000 nested folders (stack overflow before)
  {
    std::vector<char> v;
    v.insert(v.end(), {'B', 'T', 'D', 'X'}); put32(v, 1);
    v.insert(v.end(), {'G', 'N', 'R', 'L'}); put32(v, 1);
    put64(v, 24 + 36);
    put32(v, 1); v.insert(v.end(), {'t', 'x', 't', 0}); put32(v, 2); put32(v, 0);
    put64(v, 0); put32(v, 0); put32(v, 0); put32(v, 0);
    std::string deep;
    while (deep.size() < 60000) deep += "a\\";
    deep += "x.txt";
    put16(v, uint16_t(deep.size()));
    v.insert(v.end(), deep.begin(), deep.end());
    run("BA2 GNRL: a name 30000 folders deep", write("ba2-deep.ba2", v), false);
  }
  // 7. Skyrim LE: 300 folder records all pointing at one region with 1M files each
  {
    std::vector<char> v;
    v.insert(v.end(), {'B', 'S', 'A', 0}); put32(v, 104); put32(v, 36);
    const uint32_t folders = 300;
    put32(v, 3); put32(v, folders); put32(v, 1000); put32(v, 0); put32(v, 0); put32(v, 0);
    for (uint32_t i = 0; i < folders; ++i) { put64(v, i); put32(v, 1000000); put32(v, 36 + folders * 16); }
    v.push_back(1); v.push_back(0);
    for (int i = 0; i < 16 * 1000; ++i) v.push_back(1);
    run("Skyrim: folders listing more files than it has", write("sky-overlap.bsa", v), false);
  }
  std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
