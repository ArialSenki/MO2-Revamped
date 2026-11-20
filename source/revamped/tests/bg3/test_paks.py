"""Pak reading, the pak cache, the load order and modsettings.lsx."""

from __future__ import annotations

import os
import shutil
import tempfile
import time
import unittest
from pathlib import Path

from basic_games.games.baldursgate3 import loadorder, lspk, modsettings, paks
from paktools import meta_lsx, mod_pak, write_pak

U = {name: f"00000000-0000-0000-0000-{i:012d}" for i, name in enumerate("ABCDEFGH", 1)}


class Tmp(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg3-paks-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)


class PakReading(Tmp):
    def test_module_with_dependencies_and_se(self):
        pak = mod_pak(self.tmp / "a.pak", "ModA", U["A"], se=True, deps=[("ModB", U["B"]), ("GustavDev", "28ac9ce2-2aba-8cda-b3b5-6e922f71b6b8")])
        info = paks.read_pak(pak, None)
        self.assertEqual(info["kind"], "module")
        self.assertTrue(info["needs_se"])
        module = info["module"]
        self.assertEqual((module["name"], module["uuid"], module["folder"]), ("ModA", U["A"], "ModA"))
        self.assertEqual([d["uuid"] for d in module["dependencies"]], [U["B"], "28ac9ce2-2aba-8cda-b3b5-6e922f71b6b8"])

    def test_override_with_meta_only(self):
        info = paks.read_pak(mod_pak(self.tmp / "o.pak", "Over", U["C"], override=True), None)
        self.assertEqual(info["kind"], "override")

    def test_override_without_meta(self):
        pak = write_pak(self.tmp / "n.pak", {"Public/Game/GUI/x.xaml": b"<x/>"})
        self.assertEqual(paks.read_pak(pak, None)["kind"], "override")

    def test_version_15_and_16(self):
        for version in (15, 16):
            pak = write_pak(
                self.tmp / f"v{version}.pak",
                {"Mods/Old/meta.lsx": meta_lsx("Old", U["D"]), "Mods/Old/Story/x.txt": b"x"},
                version=version,
            )
            info = paks.read_pak(pak, None)
            self.assertEqual(info["kind"], "module", version)
            self.assertEqual(info["module"]["uuid"], U["D"])

    def test_unknown_version_needs_lslib_and_divine_is_asked(self):
        pak = write_pak(self.tmp / "v13.pak", {"x": b"x"})
        data = bytearray(pak.read_bytes())
        data[4] = 13
        pak.write_bytes(bytes(data))
        with self.assertRaises(paks.NeedsLSLib):
            paks.read_pak(pak, None)
        asked = []

        def divine(path):
            asked.append(path)
            return ["Mods/X/meta.lsx", "Mods/X/a.txt"], meta_lsx("X", U["E"])

        self.assertEqual(paks.read_pak(pak, divine)["module"]["uuid"], U["E"])
        self.assertEqual(asked, [pak])

    def test_cut_pak_is_damaged(self):
        pak = mod_pak(self.tmp / "cut.pak", "Cut", U["F"])
        pak.write_bytes(pak.read_bytes()[:-20])
        with self.assertRaises(paks.DamagedPak):
            paks.read_pak(pak, None)

    def test_meta_with_bad_xml_is_read_loosely(self):
        pak = mod_pak(self.tmp / "amp.pak", "Tattoo", U["G"], raw_description="Tattoo & Makeup", deps=[("ModB", U["B"])])
        module = paks.read_pak(pak, None)["module"]
        self.assertEqual(module["uuid"], U["G"])
        self.assertEqual([d["uuid"] for d in module["dependencies"]], [U["B"]])

    def test_old_32_bit_version(self):
        data = meta_lsx("V", U["H"]).replace(b'id="Version64" type="int64" value="36028797018963968"', b'id="Version" type="int32" value="268435456"')
        module = paks.parse_meta_lsx(data)
        self.assertEqual(module["version64"], 1 << 55)
        self.assertEqual(paks.version64_text(module["version64"]), "1.0.0.0")

    def test_builtin_modules(self):
        self.assertTrue(paks.is_builtin({"uuid": "cb555efe-2d9e-131f-8195-a89329d218ea"}))
        self.assertTrue(paks.is_builtin({"uuid": "x", "folder": "SharedDev"}))
        self.assertTrue(paks.is_builtin({"uuid": "x", "folder": "DiceSet_09"}))
        self.assertFalse(paks.is_builtin({"uuid": "x", "folder": "ImprovedUI"}))


class Cache(Tmp):
    def test_read_once_then_again_after_a_change(self):
        pak = mod_pak(self.tmp / "m" / "a.pak", "ModA", U["A"])
        cache = paks.PakCache(self.tmp / "cache.json")
        reads = []
        real = lspk.Package

        class Counting(real):
            def __init__(self, path):
                reads.append(path)
                super().__init__(path)

        lspk.Package = Counting
        try:
            cache.info(pak, None)
            cache.save()
            again = paks.PakCache(self.tmp / "cache.json")
            self.assertEqual(again.info(pak, None)["module"]["uuid"], U["A"])
            self.assertEqual(len(reads), 1)
            # the mod was updated: same name, new content
            mod_pak(pak, "ModA2", U["B"])
            os.utime(pak, ns=(time.time_ns(), time.time_ns() + 10**9))
            self.assertEqual(again.info(pak, None)["module"]["uuid"], U["B"])
            self.assertEqual(len(reads), 2)
        finally:
            lspk.Package = real

    def test_stale_info_keeps_an_unreadable_mod(self):
        pak = mod_pak(self.tmp / "m" / "Mod A" / "a.pak", "ModA", U["A"])
        cache = paks.PakCache(self.tmp / "cache.json")
        cache.info(pak, None)
        pak.write_bytes(b"LSPK broken now")
        entries = loadorder.read_mods([("Mod A", pak.parent)], cache, None)
        self.assertEqual(entries[0].stale, ["a.pak"])
        self.assertEqual([m["uuid"] for m in entries[0].modules], [U["A"]])

    def test_corrupt_cache_file_is_ignored(self):
        (self.tmp / "cache.json").write_text("{not json", encoding="utf-8")
        cache = paks.PakCache(self.tmp / "cache.json")
        pak = mod_pak(self.tmp / "a.pak", "ModA", U["A"])
        self.assertEqual(cache.info(pak, None)["kind"], "module")


def entry(name: str, *modules: dict) -> loadorder.ModEntry:
    e = loadorder.ModEntry(name, Path(name))
    e.modules = [dict(m, mod=name) for m in modules]
    return e


def module(name: str, uuid: str, deps=(), version64: int = 1, conflicts=()) -> dict:
    return {
        "name": name,
        "folder": name,
        "uuid": uuid,
        "version64": version64,
        "md5": "",
        "publish_handle": 0,
        "dependencies": [
            {"uuid": u, "name": n, "folder": n, "version64": v} for n, u, v in deps
        ],
        "conflicts": [{"uuid": u, "name": n} for n, u in conflicts],
    }


class Plan(unittest.TestCase):
    def test_order_follows_mo2_priority(self):
        plan = loadorder.make_plan(
            [entry("low", module("A", U["A"])), entry("high", module("B", U["B"]))], None
        )
        self.assertEqual([m["uuid"] for m in plan.all_modules()], [modsettings.GUSTAVX["uuid"], U["A"], U["B"]])

    def test_missing_late_older_and_builtin_dependencies(self):
        plan = loadorder.make_plan(
            [
                entry("needs", module("A", U["A"], deps=[("B", U["B"], 5), ("C", U["C"], 0), ("GustavDev", "28ac9ce2-2aba-8cda-b3b5-6e922f71b6b8", 0)])),
                entry("lib", module("B", U["B"], version64=3)),
            ],
            None,
        )
        self.assertEqual(plan.missing, [("needs (A)", "C")])
        self.assertEqual(plan.late, [("needs (A)", "B", "lib")])
        self.assertEqual(plan.older[0][:2], ("needs (A)", "B"))

    def test_duplicate_module_higher_priority_wins(self):
        plan = loadorder.make_plan(
            [entry("old", module("A", U["A"])), entry("x", module("X", U["C"])), entry("new", module("A", U["A"]))],
            None,
        )
        self.assertEqual([m["mod"] for m in plan.modules], ["x", "new"])
        self.assertEqual(plan.duplicates, [("A", "old", "new")])

    def test_conflicts(self):
        plan = loadorder.make_plan(
            [entry("a", module("A", U["A"], conflicts=[("B", U["B"])])), entry("b", module("B", U["B"]))], None
        )
        self.assertEqual(plan.conflicts, [("a (A)", "B")])

    def test_external_mods_kept_only_with_their_pak(self):
        previous = modsettings.ModSettings()
        previous.version = (4, 8, 0, 700)
        previous.modules = [
            dict(modsettings.GUSTAVX, version64=99),
            module("ModIo", U["D"]) | {"publish_handle": 4335717},
            module("ByHand", U["E"]),
            module("Gone", U["F"]),
            module("A", U["A"]),
        ]
        plan = loadorder.make_plan([entry("a", module("A", U["A"]))], previous, external_uuids={U["D"], U["E"]})
        self.assertEqual([m["uuid"] for m in plan.external], [U["D"], U["E"]])
        self.assertEqual(plan.version, (4, 8, 0, 700))
        self.assertEqual(plan.base[0]["version64"], 99)
        off = loadorder.make_plan([entry("a", module("A", U["A"]))], previous, external_uuids={U["D"]}, keep_external=False)
        self.assertEqual(off.external, [])

    def test_script_extender_needed(self):
        e = entry("se", module("A", U["A"]))
        e.needs_se = True
        self.assertEqual(loadorder.make_plan([e], None, has_script_extender=False).needs_se, ["se"])
        self.assertEqual(loadorder.make_plan([e], None, has_script_extender=True).needs_se, [])

    def test_render_and_parse_round_trip(self):
        plan = loadorder.make_plan([entry("a", module("Tattoo & Makeup <1>", U["A"]))], None)
        parsed = modsettings.parse(plan.text())
        self.assertEqual(parsed.version, modsettings.DEFAULT_VERSION)
        self.assertEqual([m["name"] for m in parsed.modules], ["GustavX", "Tattoo & Makeup <1>"])
        self.assertEqual(modsettings.parse("<save><broken").modules, [])


class Writing(Tmp):
    def test_backups_and_no_needless_write(self):
        target = self.tmp / "profile" / "modsettings.lsx"
        backups = self.tmp / "profile" / "modsettings-backups"
        self.assertTrue(loadorder.write(target, "one", backups))
        self.assertFalse(backups.exists())
        self.assertFalse(loadorder.write(target, "one", backups))
        self.assertTrue(loadorder.write(target, "two", backups))
        saved = list(backups.glob("*.lsx"))
        self.assertEqual([p.read_text() for p in saved], ["one"])
        for i in range(15):
            loadorder.write(target, f"v{i}", backups)
        self.assertLessEqual(len(list(backups.glob("*.lsx"))), loadorder.BACKUPS_KEPT)
        self.assertEqual(target.read_text(), "v14")
        self.assertFalse(list(target.parent.glob("*.part")))

    def test_dropped_by_game(self):
        now = modsettings.ModSettings()
        now.modules = [dict(modsettings.GUSTAVX), module("A", U["A"])]
        self.assertEqual(loadorder.dropped_by_game([U["A"].casefold(), U["B"].casefold()], now), [U["B"].casefold()])


class Canceling(Tmp):
    def test_cancel_between_paks(self):
        mod = self.tmp / "m"
        mod_pak(mod / "a.pak", "A", U["A"])
        with self.assertRaises(loadorder.Canceled):
            loadorder.read_mods([("m", mod)], paks.PakCache(self.tmp / "c.json"), None, canceled=lambda: True)
