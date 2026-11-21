"""Crafted inputs that used to take minutes or gigabytes: a malformed
meta.lsx, a pak that claims far too many files or a far too big file, and
characters XML can't hold in modsettings.lsx."""

from __future__ import annotations

import shutil
import struct
import tempfile
import time
import unittest
from pathlib import Path

from basic_games.games.baldursgate3 import lspk, modsettings, paks
from paktools import meta_lsx, mod_pak, write_pak


class Tmp(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg3-sec-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)


class CraftedMeta(unittest.TestCase):
    def test_unclosed_attributes_finish_fast(self):
        data = b'<node id="ModuleInfo">' + b'<attribute id="a" ' * 20000
        start = time.monotonic()
        self.assertIsNone(paks.parse_meta_lsx(data))
        self.assertLess(time.monotonic() - start, 2)

    def test_huge_meta_is_not_parsed(self):
        data = b"<save>" + b" " * (5 * 1024 * 1024) + b"</save>"
        start = time.monotonic()
        self.assertIsNone(paks.parse_meta_lsx(data))
        self.assertLess(time.monotonic() - start, 2)

    def test_loose_read_still_works(self):
        data = meta_lsx("Tattoo", "00000000-0000-0000-0000-000000000001", raw_description="A & B")
        module = paks.parse_meta_lsx(data)
        self.assertEqual(module["name"], "Tattoo")
        self.assertEqual(module["uuid"], "00000000-0000-0000-0000-000000000001")


class CraftedPak(Tmp):
    def test_too_many_files_is_damaged(self):
        # 600,000 entries claimed, packed into 2.4 MB: passes the LZ4 ratio
        # check, not the cap on files
        compressed = b"\0" * 2_400_000
        file_list = struct.pack("<ii", 600_000, len(compressed)) + compressed
        header_size = 4 + struct.calcsize("<IQIBB16sH")
        header = b"LSPK" + struct.pack("<IQIBB16sH", 18, header_size, len(file_list), 0, 0, b"\0" * 16, 1)
        path = self.tmp / "many.pak"
        path.write_bytes(header + file_list)
        start = time.monotonic()
        with self.assertRaises(lspk.Damaged):
            lspk.Package(path)
        self.assertLess(time.monotonic() - start, 2)
        with self.assertRaises(paks.DamagedPak):
            paks.read_pak(path, None)

    def test_file_over_the_limit_is_damaged(self):
        path = write_pak(self.tmp / "a.pak", {"Mods/A/meta.lsx": b"x" * 100})
        package = lspk.Package(path)
        entry = package.find("meta.lsx")
        self.assertEqual(package.read(entry), b"x" * 100)
        with self.assertRaises(lspk.Damaged):
            package.read(entry, max_size=10)
        with self.assertRaises(lspk.Damaged):
            package.read_file("meta.lsx", max_size=10)
        entry.size = 1 << 40
        with self.assertRaises(lspk.Damaged):
            package.read(entry)

    def test_normal_pak_still_reads(self):
        pak = mod_pak(self.tmp / "m.pak", "ModA", "00000000-0000-0000-0000-000000000001")
        self.assertEqual(paks.read_pak(pak, None)["module"]["name"], "ModA")


class ModSettingsText(unittest.TestCase):
    def test_control_characters_are_dropped(self):
        self.assertEqual(modsettings._xml("A\x00B\x08C\ud800D\x1f"), "ABCD")

    def test_normal_text_is_the_same(self):
        self.assertEqual(modsettings._xml("Tattoo & Makeup\t\n\r\u00e9\U0001F600"), "Tattoo &amp; Makeup\t\n\r\u00e9\U0001F600")


if __name__ == "__main__":
    unittest.main()
