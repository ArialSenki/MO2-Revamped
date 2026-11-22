"""The pak reader's odd and broken inputs: LZ4 blocks with matches, cut or
crafted headers and entries, compressed files; meta.lsx in its many forms,
the pak cache's file, meta.ini's LoadOrder flag."""

from __future__ import annotations

import json
import os
import shutil
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from basic_games.games.baldursgate3 import lspk, paks
from paktools import _lz4_literals, meta_lsx, mod_pak, write_pak

UA = "30000000-0000-0000-0000-000000000001"
UB = "30000000-0000-0000-0000-000000000002"


def raw_pak(path: Path, files: list[dict], flags: int = 0, parts: int = 1, version: int = 18) -> Path:
    """A version 16/18 package whose entries say what the test wants: each
    file is {name, data, flags, part, size, size_on_disk} (sizes default to
    the data's)."""
    header_size = 4 + struct.calcsize("<IQIBB16sH")
    body = bytearray()
    entries = bytearray()
    for f in files:
        offset = header_size + len(body) + f.get("shift", 0)
        data = f.get("data", b"")
        body += data
        name = f["name"]
        raw_name = (name if isinstance(name, bytes) else name.encode("utf-8")).ljust(256, b"\0")
        on_disk, size = f.get("size_on_disk", len(data)), f.get("size", len(data))
        if version == 18:
            entries += struct.pack("<256sIHBBII", raw_name, offset & 0xFFFFFFFF, offset >> 32,
                                   f.get("part", 0), f.get("flags", 0), on_disk, size)
        else:
            entries += struct.pack("<256sQQQIIII", raw_name, offset, on_disk, size, f.get("part", 0), f.get("flags", 0), 0, 0)
    compressed = _lz4_literals(bytes(entries))
    list_offset = header_size + len(body)
    file_list = struct.pack("<ii", len(files), len(compressed)) + compressed
    header = b"LSPK" + struct.pack("<IQIBB16sH", version, list_offset, len(file_list), flags, 0, b"\0" * 16, parts)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(body) + file_list)
    return path


class Tmp(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg3-cov-paks-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)


class Lz4Test(unittest.TestCase):
    def test_matches(self):
        # "ab", then 10 bytes from 1 back (a run), then "z"
        block = bytes([0x26]) + b"ab" + b"\x01\x00" + bytes([0x10]) + b"z"
        self.assertEqual(lspk.lz4_block(block, 13), b"ab" + b"b" * 10 + b"z")
        # an overlapping copy from 2 back
        block = bytes([0x21]) + b"ab" + b"\x02\x00"
        self.assertEqual(lspk.lz4_block(block, 7), b"abababa")
        # a copy that doesn't overlap
        block = bytes([0x40]) + b"abcd" + b"\x04\x00"
        self.assertEqual(lspk.lz4_block(block, 8), b"abcdabcd")

    def test_long_lengths(self):
        literals = bytes(range(256)) + b"x" * 24
        block = bytes([0xF0, 255, 10]) + literals
        self.assertEqual(lspk.lz4_block(block, 280), literals)
        # a match of 15 + 255 + 3 + 4 bytes
        block = bytes([0x1F]) + b"a" + b"\x01\x00" + bytes([255, 3])
        self.assertEqual(lspk.lz4_block(block, 278), b"a" * 278)

    def test_broken_blocks_are_damaged(self):
        for block, size in (
            (bytes([0x10]) + b"a" + b"\x00\x00", 10),  # offset 0
            (bytes([0x10]) + b"a" + b"\x05\x00", 10),  # before the start
            (bytes([0x30]) + b"abc", 2),  # literals past the size
            (bytes([0x50]) + b"ab", 5),  # literals past the block
            (bytes([0x1F]) + b"a" + b"\x01\x00" + bytes([255, 255, 255]), 50),  # match past the size
            (bytes([0xF0]), 100),  # cut in a length
            (bytes([0x10]) + b"a" + b"\x01", 10),  # cut in an offset
            (bytes([0x30]) + b"abc", 5),  # shorter than it should be
        ):
            with self.assertRaises(lspk.Damaged, msg=block):
                lspk.lz4_block(block, size)


class PackageTest(Tmp):
    def test_cut_or_foreign_files(self):
        cases = {
            "empty.pak": b"",
            "short.pak": b"LSPK\x12\x00",
            "zip.pak": b"PK\x03\x04" + b"\0" * 60,
            # version 16 with a version 15 sized header
            "v16cut.pak": b"LSPK" + struct.pack("<I", 16) + b"\0" * 30,
        }
        for name, data in cases.items():
            (self.tmp / name).write_bytes(data)
            with self.assertRaises(lspk.Damaged, msg=name):
                lspk.Package(self.tmp / name)

    def test_solid_split_and_unknown_versions(self):
        with self.assertRaisesRegex(lspk.Unsupported, "solid"):
            lspk.Package(raw_pak(self.tmp / "solid.pak", [{"name": "a", "data": b"x"}], flags=0x04))
        with self.assertRaisesRegex(lspk.Unsupported, "split"):
            lspk.Package(raw_pak(self.tmp / "split.pak", [{"name": "a", "data": b"x"}], parts=2))
        pak = raw_pak(self.tmp / "v10.pak", [{"name": "a", "data": b"x"}])
        data = bytearray(pak.read_bytes())
        data[4] = 10
        pak.write_bytes(bytes(data))
        with self.assertRaises(lspk.Unsupported) as caught:
            lspk.Package(pak)
        self.assertNotIsInstance(caught.exception, lspk.Damaged)

    def test_bad_file_list(self):
        pak = raw_pak(self.tmp / "a.pak", [{"name": "a", "data": b"x"}])
        good = pak.read_bytes()
        list_offset = struct.unpack_from("<Q", good, 8)[0]
        # the list said to be past the end
        data = bytearray(good)
        struct.pack_into("<Q", data, 8, len(good) + 100)
        pak.write_bytes(bytes(data))
        with self.assertRaises(lspk.Damaged):
            lspk.Package(pak)
        # a negative count, a count LZ4 can't hold, a size past the end
        for count, size in ((-1, 10), (100000, 10), (1, 10**6)):
            data = bytearray(good)
            struct.pack_into("<ii", data, list_offset, count, size)
            pak.write_bytes(bytes(data))
            with self.assertRaises(lspk.Damaged, msg=(count, size)):
                lspk.Package(pak)

    def test_offset_that_overflows_is_damaged(self):
        pak = raw_pak(self.tmp / "a.pak", [{"name": "a", "data": b"x"}])
        data = bytearray(pak.read_bytes())
        struct.pack_into("<Q", data, 8, 2**63 - 1)
        pak.write_bytes(bytes(data))
        with self.assertRaises(lspk.Damaged):
            lspk.Package(pak)

    def test_compressed_files(self):
        text = b"<save>" + b"hello " * 50 + b"</save>"
        packed = zlib.compress(text)
        lz4 = _lz4_literals(text)
        pak = raw_pak(self.tmp / "c.pak", [
            {"name": "Mods/X/zlib.lsx", "data": packed, "flags": 1, "size": len(text)},
            {"name": "Mods/X/lz4.lsx", "data": lz4, "flags": 2, "size": len(text)},
            {"name": "Mods/X/META.LSX", "data": b"meta"},
        ])
        package = lspk.Package(pak)
        self.assertEqual(package.read_file("zlib.lsx"), text)
        self.assertEqual(package.read_file("LZ4.LSX"), text)
        # found by name in any folder, whatever its case
        self.assertEqual(package.read_file("meta.lsx"), b"meta")
        self.assertIsNone(package.read_file("missing.lsx"))

    def test_broken_entries(self):
        packed = zlib.compress(b"data")
        pak = raw_pak(self.tmp / "e.pak", [
            {"name": "part1", "data": b"x", "part": 1},
            {"name": "big", "data": b"x", "size": 10**9},
            {"name": "past", "data": b"x", "size_on_disk": 10**6},
            {"name": "badzlib", "data": b"not zlib", "flags": 1, "size": 4},
            {"name": "shortzlib", "data": packed, "flags": 1, "size": 10},
            {"name": "method3", "data": b"x", "flags": 3},
        ])
        package = lspk.Package(pak)
        self.assertRaises(lspk.Unsupported, package.read_file, "part1")
        self.assertRaises(lspk.Damaged, package.read_file, "big")
        self.assertRaises(lspk.Damaged, package.read_file, "past")
        self.assertRaises(lspk.Damaged, package.read_file, "badzlib")
        self.assertRaises(lspk.Damaged, package.read_file, "shortzlib")
        with self.assertRaisesRegex(lspk.Unsupported, "method 3"):
            package.read_file("method3")

    def test_version_16_and_non_utf8_names(self):
        pak = raw_pak(self.tmp / "v16.pak", [{"name": "Mods/Ánimo/meta.lsx", "data": b"m", "flags": 0x20}], version=16)
        package = lspk.Package(pak)
        self.assertEqual(package.version, 16)
        self.assertEqual(package.names, ["Mods/Ánimo/meta.lsx"])
        # the flags' high bits aren't the method
        self.assertEqual(package.read_file("meta.lsx"), b"m")
        # a Latin-1 byte in a name is replaced, not an error
        pak = raw_pak(self.tmp / "latin.pak", [{"name": b"Mods/\xe9/a.txt", "data": b"m"}])
        self.assertEqual(lspk.Package(pak).names, ["Mods/�/a.txt"])


class MetaLsxTest(unittest.TestCase):
    def test_no_module(self):
        self.assertIsNone(paks.parse_meta_lsx(b"<save><region id='Config'/></save>"))
        no_uuid = meta_lsx("X", "")
        self.assertIsNone(paks.parse_meta_lsx(no_uuid))
        self.assertIsNone(paks.parse_meta_lsx(b"<save>" + b" " * (paks._META_LIMIT + 1) + b"</save>"))

    def test_old_32_bit_version_and_publish_version(self):
        text = (
            '<save><node id="ModuleInfo">'
            f'<attribute id="UUID" type="FixedString" value="{UA}"/>'
            '<attribute id="Folder" type="LSString" value="Old"/>'
            '<attribute id="Version" type="int32" value="268435456"/>'
            "</node></save>"
        )
        module = paks.parse_meta_lsx(text.encode())
        self.assertEqual(module["version64"], paks.version32_to_64(268435456))
        self.assertEqual(paks.version64_text(module["version64"]), "1.0.0.0")
        self.assertEqual((module["dependencies"], module["conflicts"]), ([], []))
        text = (
            '<save><node id="ModuleInfo">'
            f'<attribute id="UUID" type="guid" value=" {UA} "/>'
            '<attribute id="PublishHandle" type="uint64" value="not a number"/>'
            '<children><node id="PublishVersion"><attribute id="Version64" type="int64" value="36028797018963968"/>'
            "</node></children></node></save>"
        )
        module = paks.parse_meta_lsx(text.encode())
        self.assertEqual(module["uuid"], UA)
        self.assertEqual(module["version64"], 36028797018963968)
        self.assertEqual(module["publish_handle"], 0)
        # no version at all
        self.assertEqual(paks.parse_meta_lsx(text.replace("PublishVersion", "Other").encode())["version64"], 0)

    def test_conflicts_and_dependencies_without_uuid(self):
        data = meta_lsx("A", UA, deps=[("B", UB), ("NoUuid", "")], conflicts=[("C", UB)])
        module = paks.parse_meta_lsx(data)
        self.assertEqual([d["uuid"] for d in module["dependencies"]], [UB])
        self.assertEqual([d["name"] for d in module["conflicts"]], ["C"])

    def test_non_utf8_meta_is_still_read(self):
        data = meta_lsx("A", UA, raw_description="Caf\xe9").replace("Caf\xe9".encode(), b"Caf\xe9")
        self.assertEqual(paks.parse_meta_lsx(data)["uuid"], UA)

    def test_loose_read(self):
        # not XML (an unescaped &): the looser scan, with the dependencies
        data = meta_lsx("A", UA, raw_description="R&D", deps=[("B", UB)])
        module = paks.parse_meta_lsx(data)
        self.assertEqual([d["uuid"] for d in module["dependencies"]], [UB])
        self.assertEqual(module["conflicts"], [])
        self.assertIsNone(paks.parse_meta_lsx(b"<broken & no module"))
        self.assertIsNone(paks.parse_meta_lsx(b'<x id="ModuleInfo"><attribute id="Name" value="x"/> & '))
        # no Dependencies node, and no <children> after the module
        text = f'<x & id="ModuleInfo"><attribute id="UUID" type="guid" value="{UA}"/>'
        self.assertEqual(paks.parse_meta_lsx(text.encode())["dependencies"], [])


class ReadPakTest(Tmp):
    def test_meta_too_big_is_damaged(self):
        big = b"<save>" + b" " * (paks._META_LIMIT + 10) + b"</save>"
        pak = write_pak(self.tmp / "big.pak", {"Mods/X/meta.lsx": big})
        with self.assertRaisesRegex(paks.DamagedPak, "too big"):
            paks.read_pak(pak, None)

    def test_missing_pak_is_damaged(self):
        with self.assertRaises(paks.DamagedPak):
            paks.read_pak(self.tmp / "gone.pak", None)

    def test_se_config_and_material_groups(self):
        pak = write_pak(self.tmp / "m.pak", {
            "Mods/X/meta.lsx": meta_lsx("X", UA),
            "Public/Engine/Timeline/MaterialGroups/x.lsx": b"x",
            "Mods/X/ScriptExtender/config.JSON": b"{}",
        })
        info = paks.read_pak(pak, None)
        self.assertEqual(info["kind"], "module")
        self.assertTrue(info["needs_se"])
        # a module without a folder is an override
        self.assertEqual(paks.judge(["a/b.txt"], {"folder": ""})["kind"], "override")

    def test_builtins(self):
        self.assertTrue(paks.is_builtin("CB555EFE-2D9E-131F-8195-A89329D218EA"))
        self.assertFalse(paks.is_builtin(UA))
        self.assertTrue(paks.is_builtin({"uuid": UA, "folder": "DiceSet_07"}))
        self.assertTrue(paks.is_builtin({"uuid": UA, "name": "gustavx"}))
        self.assertFalse(paks.is_builtin({"uuid": UA, "folder": "MyGustavMod"}))


class CacheTest(Tmp):
    def test_cache_file_that_cant_be_used(self):
        pak = mod_pak(self.tmp / "a.pak", "A", UA)
        file = self.tmp / "cache.json"
        for text in ("{not json", "[1, 2]", '{"version": 99, "paks": {}}', '{"version": 1, "paks": []}'):
            file.write_text(text, encoding="utf-8")
            cache = paks.PakCache(file)
            self.assertIsNone(cache.get(pak), text)
            self.assertEqual(cache.info(pak, None)["module"]["uuid"], UA)
        file.write_bytes(b"\xff\xfe not utf-8")
        self.assertIsNone(paks.PakCache(file).get(pak))

    def test_changed_and_gone_paks(self):
        pak = mod_pak(self.tmp / "a.pak", "A", UA)
        gone = mod_pak(self.tmp / "b.pak", "B", UB)
        cache = paks.PakCache(self.tmp / "cache.json")
        cache.info(pak, None)
        cache.info(gone, None)
        gone.unlink()
        self.assertIsNone(cache.get(gone))
        self.assertIsNotNone(cache.stale(gone))
        cache.put(gone, {"kind": "override"})  # no file: not kept
        cache.save()
        saved = json.loads((self.tmp / "cache.json").read_text(encoding="utf-8"))
        self.assertEqual([Path(k).name for k in saved["paks"]], ["a.pak"])
        # nothing new: not written again
        os.unlink(self.tmp / "cache.json")
        cache.save()
        self.assertFalse((self.tmp / "cache.json").exists())
        with open(pak, "ab") as f:
            f.write(b"more")
        self.assertIsNone(cache.get(pak))
        self.assertIsNone(paks.PakCache(self.tmp / "x.json").stale(pak))

    def test_save_that_fails_is_logged(self):
        pak = mod_pak(self.tmp / "a.pak", "A", UA)
        target = self.tmp / "cache-dir"
        target.mkdir()
        (target / "x").write_text("keeps it from being replaced")
        cache = paks.PakCache(target)
        cache.info(pak, None)
        cache.save()  # no exception
        self.assertTrue(target.is_dir())


class MetaIniTest(Tmp):
    def mod(self, ini: bytes | None) -> Path:
        mod = self.tmp / "mod"
        mod.mkdir(exist_ok=True)
        if ini is not None:
            (mod / "meta.ini").write_bytes(ini)
        return mod

    def test_load_order_flag(self):
        pak = Path("Auto Wares.pak")
        ini = b'[General]\nmodid=1\n[Auto%20Wares.pak]\nCacheVersion=1\nLoadOrder="True"\n'
        self.assertTrue(paks.forced_in_load_order(self.mod(ini), pak))
        self.assertFalse(paks.forced_in_load_order(self.mod(ini.replace(b"True", b"false")), pak))
        self.assertFalse(paks.forced_in_load_order(self.mod(ini.replace(b"CacheVersion=1\n", b"")), pak))
        self.assertFalse(paks.forced_in_load_order(self.mod(ini), Path("Other.pak")))

    def test_missing_odd_or_broken_ini(self):
        self.assertFalse(paks.forced_in_load_order(self.mod(None), Path("a.pak")))
        self.assertFalse(paks.forced_in_load_order(self.mod(b"no section\nkey=1\n"), Path("a.pak")))
        latin = b"[\xe9.pak]\ncacheVersion=1\nloadOrder=true\n"
        self.assertFalse(paks.forced_in_load_order(self.mod(latin), Path("e.pak")))
        # a BOM before the first section (an editor's) was "no section header"
        bom = b"\xef\xbb\xbf[a.pak]\ncacheVersion=1\nloadOrder=true\n"
        self.assertTrue(paks.forced_in_load_order(self.mod(bom), Path("a.pak")))

    def test_qsettings_unescape(self):
        self.assertEqual(paks.qsettings_unescape("Auto%20Wares.pak"), "Auto Wares.pak")
        self.assertEqual(paks.qsettings_unescape("a\\b.pak"), "a/b.pak")
        self.assertEqual(paks.qsettings_unescape("%U00E9.pak"), "é.pak")
        self.assertEqual(paks.qsettings_unescape("%UD83D%UDE00.pak"), "\U0001F600.pak")
        # half a pair is replaced, not an error
        self.assertEqual(paks.qsettings_unescape("%UD83D.pak"), "�.pak")

    def test_is_part(self):
        (self.tmp / "Big.pak").write_bytes(b"")
        self.assertTrue(paks.is_part(self.tmp / "Big_1.pak"))
        self.assertFalse(paks.is_part(self.tmp / "Small_1.pak"))
        self.assertFalse(paks.is_part(self.tmp / "Big.pak"))


if __name__ == "__main__":
    unittest.main()
