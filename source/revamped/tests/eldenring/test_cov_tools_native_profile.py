"""The native DLL profile tool (eldenring_mo2_tools/native_profile_tool.py)
outside MO2: the profile file it reads and writes, which DLLs it finds in the
active mods, the PE export reader, and its windows driven by their buttons
(exec() returns at once; message boxes are recorded)."""

from __future__ import annotations

import configparser
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QComboBox,
    QDialog,
    QTableWidget,
    QTreeWidget,
)

from eldenring_mo2_tools import native_profile_tool

from fake_organizer import FakeMod, FakeOrganizer, appdata_folder, game_for

Tool = native_profile_tool.EldenRingMo2NativeProfileTool


def make_pe(names: list[str], *, pe32: bool = False, functions: int | None = None,
            names_rva_offset: int = 0, section_count: int = 1, signature: bytes = b"PE\0\0",
            magic: int | None = None, export_rva: int | None = None, name_count: int | None = None) -> bytes:
    """A minimal PE file with one section holding an export table."""
    optional_size = 224 if pe32 else 240
    directory_offset = 96 if pe32 else 112
    section_rva, section_raw = 0x1000, 0x200
    strings = b""
    string_offsets = []
    header_size = 40
    nf = len(names) if functions is None else functions
    nn = len(names) if name_count is None else name_count
    functions_offset = header_size
    names_offset = functions_offset + 4 * max(nf, 1)
    ordinals_offset = names_offset + 4 * len(names)
    strings_offset = ordinals_offset + 2 * len(names)
    for name in names:
        string_offsets.append(strings_offset + len(strings))
        strings += name.encode("latin-1") + b"\0"
    data = bytearray(strings_offset + len(strings))
    struct.pack_into("<IIIII", data, 20, nf, nn, section_rva + functions_offset,
                     section_rva + names_offset + names_rva_offset, section_rva + ordinals_offset)
    for index, offset in enumerate(string_offsets):
        struct.pack_into("<I", data, names_offset + 4 * index, section_rva + offset)
        struct.pack_into("<H", data, ordinals_offset + 2 * index, index)
    data[strings_offset:] = strings

    dos = bytearray(64)
    dos[:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)
    coff = bytearray(24)
    coff[:4] = signature
    struct.pack_into("<HHIIIHH", coff, 4, 0x14C if pe32 else 0x8664, section_count, 0, 0, 0, optional_size, 0x2022)
    optional = bytearray(optional_size)
    struct.pack_into("<H", optional, 0, magic if magic is not None else (0x10B if pe32 else 0x20B))
    struct.pack_into("<I", optional, 60, section_raw)
    struct.pack_into("<II", optional, directory_offset,
                     section_rva if export_rva is None else export_rva, len(data))
    sections = bytearray()
    for _ in range(max(section_count, 0) if section_count <= 96 else 0):
        header = bytearray(40)
        header[:8] = b".edata\0\0"
        struct.pack_into("<IIII", header, 8, len(data), section_rva, len(data), section_raw)
        sections += header
    head = bytes(dos + coff + optional + sections)
    return head + bytes(section_raw - len(head)) + bytes(data)


class NativeCase(unittest.TestCase):
    def setUp(self):
        self._appdata = os.environ.get("APPDATA")
        appdata_folder()
        self.organizer = FakeOrganizer()
        game_for(self.organizer)
        self.tool = Tool()
        self.assertTrue(self.tool.init(self.organizer))
        self.notices: list[tuple[str, str, str]] = []
        self.tool._show_notice = lambda parent, title, message, kind="info": self.notices.append(
            (title, message, kind)
        )
        self.config = self.organizer.profile_dir / Tool.ConfigFilename

    def tearDown(self):
        native_profile_tool._game_name = "Elden Ring"
        if self._appdata is None:
            os.environ.pop("APPDATA", None)
        else:
            os.environ["APPDATA"] = self._appdata

    def write_dll(self, mod: str, relative: str, data: bytes) -> Path:
        path = self.organizer.mods / mod / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path


class Identity(NativeCase):
    def test_plugin_answers_and_game_names(self):
        self.assertEqual(self.tool.name(), "Elden Ring MO2 Native Profile")
        self.assertEqual(self.tool.author(), "MO2 Revamped")
        self.assertTrue(self.tool.enabledByDefault())
        self.assertEqual(self.tool.settings(), [])
        self.assertTrue(self.tool.requirements())
        self.assertTrue(self.tool.version())
        self.assertTrue(self.tool.icon().isNull())
        self.assertIn("Elden Ring", self.tool.description())
        game_for(self.organizer, nightreign=True)
        self.assertTrue(self.tool.tooltip())
        self.assertIn("Nightreign", self.tool.description())
        self.assertEqual(native_profile_tool._tr("d'Elden Ring eldenring.exe"), "de Nightreign nightreign.exe")
        self.assertEqual(Tool._own_seamless_dll(), "nrsc.dll")
        self.assertTrue(self.tool.localizedName())
        self.organizer.managed = None
        self.tool._follow_managed_game()
        self.assertEqual(native_profile_tool._game_name, "Nightreign")
        broken = mock.Mock()
        broken.gameShortName.side_effect = RuntimeError("gone")
        self.organizer.managed = broken
        self.tool._follow_managed_game()
        self.tool.display()
        self.assertEqual(self.notices, [])

    def test_create_plugin(self):
        self.assertIsInstance(native_profile_tool.createPlugin(), Tool)


class ProfileFile(NativeCase):
    def test_missing_file_or_section(self):
        self.assertEqual(self.tool._read_profile_entries(self.config), [])
        self.config.write_text("[Startup]\nboot_boost = 1\n", encoding="utf-8")
        self.assertEqual(self.tool._read_profile_entries(self.config), [])

    def test_entries_in_order_with_defaults(self):
        self.config.write_text(
            "\ufeff[RevampedNativeProfile]\nversion = 2\nmod = Default Mod\n"
            "native_10 = DLLs\\b.dll\nenabled_10 = FALSE\n"
            "native_02 = /DLLs/a.dll/\nmod_02 = Other\ninitializer_02 = Init\noptional_02 = true\n",
            encoding="utf-8",
        )
        entries = self.tool._read_profile_entries(self.config)
        self.assertEqual(entries, [
            {"mod": "Other", "path": "DLLs/a.dll", "initializer": "Init", "enabled": True, "optional": True},
            {"mod": "Default Mod", "path": "DLLs/b.dll", "initializer": "", "enabled": False, "optional": False},
        ])

    def test_bad_files_are_refused(self):
        cases = [
            "[RevampedNativeProfile]\nweird = 1\n",
            "[RevampedNativeProfile]\nnative_x = a.dll\n",
            "[RevampedNativeProfile]\nnative_0 = a.dll\nmod_0 = A\n",
            "[RevampedNativeProfile]\nnative_1 = a.dll\nmod_1 = A\nenabled_1 = maybe\n",
            "[RevampedNativeProfile]\nnative_1 = a.dll\n",
            "[RevampedNativeProfile]\nmod_1 = A\n",
        ]
        for text in cases:
            self.config.write_text(text, encoding="utf-8")
            with self.assertRaises(ValueError, msg=text):
                self.tool._read_profile_entries(self.config)
        self.config.write_bytes(b"[RevampedNativeProfile]\nmod = Espa\xf1a\n")
        with self.assertRaises(UnicodeError):
            self.tool._read_profile_entries(self.config)
        self.config.write_text("[RevampedNativeProfile]\nmod = a\nmod = b\n", encoding="utf-8")
        with self.assertRaises(configparser.Error):
            self.tool._read_profile_entries(self.config)

    def test_archive_paths_are_read_as_installed(self):
        self.organizer.add_mod("Mod", ["DLLs/natives/x.dll"])
        index = {"source_to_installed": {"Wrap\\natives\\x.dll": "/DLLs/natives/x.dll"}}
        for stored in (json.dumps(index), index):
            with mock.patch.object(FakeMod, "pluginSettings",
                                   return_value={"installed_native_route_index": stored}):
                self.assertEqual(self.tool._canonical_route("Mod", "/Wrap/natives/x.dll"), "DLLs/natives/x.dll")
        for stored in ("{broken", "", ["x"], {"source_to_installed": ["x"]}):
            with mock.patch.object(FakeMod, "pluginSettings",
                                   return_value={"installed_native_route_index": stored}):
                self.assertEqual(self.tool._canonical_route("Mod", "Wrap/x.dll"), "Wrap/x.dll")
        self.assertEqual(self.tool._canonical_route("Not installed", "\\a.dll"), "a.dll")


class Detection(NativeCase):
    def test_only_the_winning_copy_of_each_dll(self):
        self.organizer.add_mod("Low", ["DLLs/shared.dll", "DLLs/low.dll", "low_root.dll"])
        self.organizer.add_mod("High", ["DLLs/shared.dll", "SeamlessCoop/ersc.dll", "SeamlessCoop/nrsc.dll",
                                        "DLLs/deep/nested.dll", "dinput8.dll", "steam_api64.dll",
                                        "parts/x.dll"])
        self.organizer.add_mod("Off", ["DLLs/off.dll"], active=False)
        found = {(c["mod"], c["path"]) for c in self.tool._detect_native_dlls()}
        self.assertEqual(found, {
            ("Low", "DLLs/low.dll"), ("Low", "low_root.dll"),
            ("High", "DLLs/shared.dll"), ("High", "SeamlessCoop/ersc.dll"),
        })

    def test_installer_index_adds_and_filters_routes(self):
        self.organizer.add_mod("Indexed", ["mods/a.dll", "DLLs/b.dll"])
        index = json.dumps({"dlls": ["mods/a.dll", "DLLs/B.DLL", "deep/x/y.dll", 3, "../evil.dll"]})
        with mock.patch.object(FakeMod, "pluginSettings", return_value={"installed_native_route_index": index}):
            routes = self.tool._installed_dll_routes(
                self.organizer.modList().getMod("Indexed"), self.organizer.mods / "Indexed")
        # a route out of the mod, or deeper than a launch looks, is left out
        self.assertEqual(routes, ["mods/a.dll", "DLLs/B.DLL"])
        with mock.patch.object(FakeMod, "pluginSettings", return_value={"installed_native_route_index": "{bad"}):
            routes = self.tool._installed_dll_routes(
                self.organizer.modList().getMod("Indexed"), self.organizer.mods / "Indexed")
        self.assertEqual(sorted(routes), ["DLLs/b.dll", "mods/a.dll"])

    def test_missing_mod_folders_and_failing_origins(self):
        self.organizer.add_mod("Gone", ["a.dll"])
        self.organizer.add_mod("Kept", ["b.dll"])
        (self.organizer.mods / "Gone" / "a.dll").unlink()
        (self.organizer.mods / "Gone").rmdir()
        self.assertEqual([c["mod"] for c in self.tool._detect_native_dlls()], ["Kept"])
        with mock.patch.object(FakeOrganizer, "getFileOrigins", side_effect=RuntimeError("vfs")):
            self.assertEqual(self.tool._detect_native_dlls(), [])

    def test_routes_the_launch_can_load(self):
        normalize = self.tool._normalize_native_route
        self.assertEqual(normalize("\\DLLs\\a.dll\\"), "DLLs/a.dll")
        self.assertEqual(normalize("a.DLL"), "a.DLL")
        for bad in ("", "a.txt", "../a.dll", "DLLs/../a.dll", "Z:/games/a.dll", "C:a.dll", "parts/a.dll",
                    "/home/user/a.dll"):
            self.assertIsNone(normalize(bad), bad)

    def test_safe_mod_files(self):
        root = self.organizer.add_mod("Mod", ["DLLs/a.dll"]).resolve()
        self.assertEqual(Tool._safe_mod_file(root, "DLLs/a.dll"), root / "DLLs" / "a.dll")
        self.assertIsNone(Tool._safe_mod_file(root, "DLLs/missing.dll"))
        self.assertIsNone(Tool._safe_mod_file(root, "DLLs"))
        outside = Path(tempfile.mkdtemp(prefix="er-cov-out-")) / "x.dll"
        outside.write_bytes(b"MZ")
        try:
            os.symlink(outside, root / "DLLs" / "link.dll")
        except OSError:
            self.skipTest("symbolic links need developer mode here")
        self.assertIsNone(Tool._safe_mod_file(root, "DLLs/link.dll"))

    def test_physical_paths_of_archive_routes(self):
        root = self.organizer.add_mod("Mod", ["DLLs/a.dll", "DLLs/natives/b.dll", "DLLs/c.dll", "d.txt"]).resolve()
        self.assertEqual(self.tool._physical_dll_path("Mod", "a.dll"), root / "DLLs" / "a.dll")
        self.assertEqual(self.tool._physical_dll_path("Mod", "natives/b.dll"), root / "DLLs" / "natives" / "b.dll")
        self.assertEqual(self.tool._physical_dll_path("Mod", "native/c.dll"), root / "DLLs" / "c.dll")
        self.assertIsNone(self.tool._physical_dll_path("Mod", "native"))
        self.assertIsNone(self.tool._physical_dll_path("Mod", "d.txt"))
        self.assertIsNone(self.tool._physical_dll_path("Nobody", "a.dll"))

    def test_hidden_entries_kept_in_the_file(self):
        self.organizer.add_mod("Winner", ["DLLs/a.dll"])
        self.organizer.add_mod("Overridden", ["DLLs/a.dll", "DLLs/renamed.dll"])
        (self.organizer.mods / "Overridden" / "DLLs" / "renamed.dll").unlink()
        entries = [
            {"mod": "Winner", "path": "DLLs/a.dll"},
            {"mod": "Off", "path": "DLLs/x.dll"},
            {"mod": "Overridden", "path": "DLLs/a.dll"},
            {"mod": "Overridden", "path": "DLLs/renamed.dll"},
            {"mod": "Winner", "path": "dinput8.dll"},
        ]
        kept = self.tool._hidden_entries_to_keep(entries, {("winner", "dlls/a.dll")}, {"winner", "overridden"})
        self.assertEqual(kept, [{"mod": "Off", "path": "DLLs/x.dll"}, {"mod": "Overridden", "path": "DLLs/a.dll"}])


class PeExports(unittest.TestCase):
    def read(self, data: bytes) -> list[str]:
        path = Path(tempfile.mkdtemp(prefix="er-cov-pe-")) / "x.dll"
        path.write_bytes(data)
        return Tool._read_pe_exports(path)

    def test_named_exports(self):
        self.assertEqual(self.read(make_pe(["Init", "b_second", "Alpha"])), ["Alpha", "b_second", "Init"])
        self.assertEqual(self.read(make_pe(["Start"], pe32=True)), ["Start"])

    def test_odd_names_and_ordinals_are_left_out(self):
        # an ordinal past the function table, a name that is no identifier,
        # one with a non-ASCII byte
        self.assertEqual(self.read(make_pe(["Good", "9bad", "caf\xe9"], functions=2)), ["Good"])
        self.assertEqual(self.read(make_pe([])), [])
        self.assertEqual(self.read(make_pe(["x"], export_rva=0)), [])

    def test_python_dll(self):
        dll = Path(sys.base_prefix) / "python3.dll"
        if not dll.is_file():
            self.skipTest("no python3.dll")
        self.assertIn("Py_Initialize", Tool._read_pe_exports(dll))

    def test_broken_files(self):
        good = make_pe(["Init"])
        cases = [
            b"ZZ" + good[2:],
            make_pe(["Init"], signature=b"NE\0\0"),
            make_pe(["Init"], section_count=0),
            make_pe(["Init"], magic=0x107),
            make_pe(["Init"], export_rva=0x9000),
            make_pe(["Init"], name_count=20000),
            make_pe(["Init"], names_rva_offset=0x10000),
            good[:0x210],
            good[:40],
        ]
        for data in cases:
            with self.assertRaises((ValueError, struct.error)):
                self.read(data)


class TableOps(NativeCase):
    def entries(self):
        return [
            {"enabled": True, "mod": "A", "path": "a.dll", "initializer": "", "optional": True},
            {"enabled": False, "mod": "B", "path": "DLLs\\b.dll", "initializer": " Init ", "optional": False},
        ]

    def test_round_trip_and_moves(self):
        table = QTableWidget(0, 4)
        Tool._populate_profile_table(table, self.entries())
        read = Tool._entries_from_table(table)
        self.assertEqual(read[1]["path"], "DLLs/b.dll")
        self.assertEqual(read[1]["initializer"], "Init")
        self.assertTrue(read[0]["optional"])
        self.assertFalse(read[1]["enabled"])
        table.setCurrentCell(0, 1)
        self.tool._move_selected_entry(table, 1)
        self.assertEqual([e["mod"] for e in Tool._entries_from_table(table)], ["B", "A"])
        self.tool._move_selected_entry(table, 5)
        table.setCurrentCell(1, 1)
        self.tool._remove_selected_entry(table)
        self.assertEqual([e["mod"] for e in Tool._entries_from_table(table)], ["B"])
        table.setCurrentCell(-1, -1)
        self.tool._remove_selected_entry(table)
        self.assertEqual(table.rowCount(), 1)
        table.setItem(0, 1, None)
        table.takeItem(0, 1)
        self.assertEqual(Tool._entries_from_table(table), [])

    def test_add_detected_candidates(self):
        tree = QTreeWidget()
        candidates = [
            {"mod": "A", "path": "a.dll", "physical_path": "C:/a.dll"},
            {"mod": "A", "path": "DLLs/b.dll", "physical_path": "C:/b.dll"},
            {"mod": "C", "path": "c.dll", "physical_path": "C:/c.dll"},
        ]
        Tool._populate_detected_tree(tree, candidates)
        self.assertEqual(tree.topLevelItemCount(), 2)
        self.assertIn("2 DLLs", tree.topLevelItem(0).text(0))
        self.assertIn("1 DLL", tree.topLevelItem(1).text(0))
        table = QTableWidget(0, 4)
        Tool._populate_profile_table(table, [{"mod": "a", "path": "A.DLL"}])
        tree.topLevelItem(0).setSelected(True)
        tree.topLevelItem(1).child(0).setSelected(True)
        self.tool._add_selected_candidates(tree, table)
        self.assertEqual([(e["mod"], e["path"]) for e in Tool._entries_from_table(table)],
                         [("a", "A.DLL"), ("A", "DLLs/b.dll"), ("C", "c.dll")])


class SaveProfile(NativeCase):
    def save(self, entries, stale=()):
        table = QTableWidget(0, 4)
        Tool._populate_profile_table(table, entries)
        table.stale_entries = list(stale)
        dialog = QDialog()
        self.tool._save_and_close(dialog, self.organizer.profile_dir, table)
        return dialog

    def entry(self, mod="A", path="DLLs/a.dll", enabled=True, initializer=""):
        return {"mod": mod, "path": path, "enabled": enabled, "initializer": initializer, "optional": False}

    def test_saved_with_the_other_sections_kept(self):
        self.config.write_text("[Startup]\nboot_boost = 0\n[RevampedNativeProfile]\nnative_1 = old.dll\nmod_1 = X\n",
                               encoding="utf-8")
        dialog = self.save([self.entry(), self.entry("B", "b.dll", False)],
                           stale=[{"mod": "Off", "path": "c.dll", "enabled": True, "initializer": "", "optional": True}])
        self.assertEqual(dialog.result(), QDialog.DialogCode.Accepted)
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.config, encoding="utf-8")
        self.assertEqual(config["Startup"]["boot_boost"], "0")
        section = config["RevampedNativeProfile"]
        self.assertEqual(section["native_01"], "DLLs/a.dll")
        self.assertEqual(section["enabled_02"], "false")
        self.assertEqual(section["optional_03"], "true")
        self.assertNotIn("native_1", section)
        self.assertEqual(self.tool._read_profile_entries(self.config)[2]["mod"], "Off")
        self.assertEqual(self.notices[-1][2], "success")

    def test_an_empty_list_drops_the_section(self):
        self.config.write_text("[RevampedNativeProfile]\nnative_1 = a.dll\nmod_1 = A\n", encoding="utf-8")
        self.save([])
        self.assertNotIn("RevampedNativeProfile", self.config.read_text(encoding="utf-8"))

    def test_refused_entries(self):
        cases = [
            ([self.entry(mod="")], "Incomplete native DLL entry"),
            ([self.entry(path="parts/a.dll")], "Invalid DLL path"),
            ([self.entry(), self.entry(path="dlls\\A.DLL")], "Duplicate DLL entry"),
            ([self.entry(), self.entry(mod="B")], "Duplicate DLL entry"),
            ([self.entry(initializer="not valid")], "Invalid initializer name"),
            ([self.entry(initializer="Ünï")], "Invalid initializer name"),
        ]
        for entries, title in cases:
            dialog = self.save(entries)
            self.assertEqual(self.notices[-1][0], title)
            self.assertNotEqual(dialog.result(), QDialog.DialogCode.Accepted)
        self.assertFalse(self.config.exists())

    def test_initializers_are_checked_against_the_exports(self):
        self.organizer.add_mod("A", [])
        dll = self.write_dll("A", "DLLs/a.dll", make_pe(["Init"]))
        self.save([self.entry(initializer="Missing")])
        self.assertEqual(self.notices[-1][0], "Initializer export not found")
        self.save([self.entry(initializer="Init")])
        self.assertEqual(self.notices[-1][2], "success")
        dll.write_bytes(b"MZ")
        self.save([self.entry(initializer="Init")])
        self.assertEqual(self.notices[-1][0], "Could not verify initializer export")
        # a disabled entry is not checked
        self.save([self.entry(initializer="Init", enabled=False)])
        self.assertEqual(self.notices[-1][2], "success")

    def test_a_file_that_cannot_be_read_is_not_replaced(self):
        self.config.write_bytes(b"[Startup]\nname = \xff\n")
        self.save([self.entry()])
        self.assertEqual(self.notices[-1][2], "error")
        self.assertEqual(self.config.read_bytes(), b"[Startup]\nname = \xff\n")
        self.config.write_text("[Startup]\n", encoding="utf-8")
        with mock.patch.object(native_profile_tool.os, "replace", side_effect=PermissionError("locked")):
            self.save([self.entry()])
        self.assertEqual(self.notices[-1][2], "error")
        self.assertEqual([p.name for p in self.organizer.profile_dir.glob(".eldenring_mo2_startup.*")], [])


class DrivenDialog(QDialog):
    steps: list = []

    def exec(self) -> int:
        # no event loop at all: timers other tests queued (the game plugin's
        # message boxes) must not run here
        if DrivenDialog.steps:
            DrivenDialog.steps.pop(0)(self)
        return self.result()


class Windows(NativeCase):
    def setUp(self):
        super().setUp()
        patcher = mock.patch.object(native_profile_tool, "QDialog", DrivenDialog)
        patcher.start()
        self.addCleanup(patcher.stop)
        DrivenDialog.steps = []
        self.addCleanup(lambda: setattr(DrivenDialog, "steps", []))

    def test_edit_and_save_the_profile(self):
        self.organizer.add_mod("A", [])
        self.write_dll("A", "a.dll", make_pe(["Init", "Other"]))
        self.write_dll("A", "DLLs/b.dll", make_pe([]))
        self.organizer.add_mod("Off", ["DLLs/off.dll"], active=False)
        self.config.write_text("[RevampedNativeProfile]\nnative_1 = DLLs/off.dll\nmod_1 = Off\n"
                               "native_2 = DLLs/b.dll\nmod_2 = A\n", encoding="utf-8")

        def window(dialog):
            table = dialog.findChild(QTableWidget, "nativeLoadOrderTable")
            tree = dialog.findChild(QTreeWidget, "detectedDllTree")
            assert table.rowCount() == 2, table.rowCount()
            tree.topLevelItem(0).setSelected(True)
            dialog.findChild(QAbstractButton, "addDetectedDllsButton").click()
            table.setCurrentCell(1, 1)
            dialog.findChild(QAbstractButton, "moveNativeEntryUpButton").click()
            dialog.findChild(QAbstractButton, "moveNativeEntryDownButton").click()
            table.setCurrentCell(0, 1)
            dialog.findChild(QAbstractButton, "detectNativeExportsButton").click()
            table.setCurrentCell(1, 1)
            dialog.findChild(QAbstractButton, "detectNativeExportsButton").click()
            dialog.findChild(QAbstractButton, "resetNativeProfileButton").click()
            dialog.findChild(QAbstractButton, "nativeSaveButton").click()

        def exports(dialog):
            combo = dialog.findChild(QComboBox, "nativeExportCombo")
            combo.setCurrentIndex(combo.findText("Init"))
            dialog.findChild(QAbstractButton, "nativeExportUseButton").click()

        DrivenDialog.steps = [window, exports]
        self.tool.display()
        self.assertEqual(DrivenDialog.steps, [])
        entries = self.tool._read_profile_entries(self.config)
        # the switched-off mod's entry is kept after the rows
        self.assertEqual(entries[-1]["mod"], "Off")
        self.assertEqual({e["path"]: e["initializer"] for e in entries if e["mod"] == "A"},
                         {"a.dll": "Init", "DLLs/b.dll": ""})
        self.assertTrue(any(title == "No initializer exports detected" for title, _m, _k in self.notices))

    def test_export_choices(self):
        self.organizer.add_mod("A", [])
        self.write_dll("A", "a.dll", b"MZ broken")
        table = QTableWidget(0, 4)
        Tool._populate_profile_table(table, [{"mod": "A", "path": "a.dll", "initializer": "Kept"},
                                             {"mod": "Gone", "path": "x.dll"}])
        self.tool._choose_initializer_export(table)
        self.assertEqual(self.notices[-1][0], "Select a DLL")
        table.setCurrentCell(0, 1)
        self.tool._choose_initializer_export(table)
        self.assertEqual(self.notices[-1][0], "Could not read DLL exports")
        table.setCurrentCell(1, 1)
        self.tool._choose_initializer_export(table)
        self.assertEqual(self.notices[-1][0], "DLL not found")
        # cancelled: the initializer stays
        self.write_dll("A", "a.dll", make_pe(["Init"]))
        table.setCurrentCell(0, 1)
        DrivenDialog.steps = [lambda dialog: dialog.reject()]
        self.tool._choose_initializer_export(table)
        self.assertEqual(Tool._entries_from_table(table)[0]["initializer"], "Kept")

    def test_a_profile_file_that_cannot_be_read(self):
        self.config.write_text("[RevampedNativeProfile]\nbad = 1\n", encoding="utf-8")
        self.tool.display()
        self.assertEqual(self.notices[-1][2], "error")
        self.organizer._profile = None
        self.tool.display()
        self.assertEqual(self.notices[-1][0], "No active Elden Ring profile")

    def test_empty_instance_and_cancel(self):
        DrivenDialog.steps = [lambda dialog: dialog.findChild(QAbstractButton, "nativeCancelButton").click()]
        self.tool.display()
        self.assertFalse(self.config.exists())

    def test_nightreign_window(self):
        game_for(self.organizer, nightreign=True)
        self.organizer.add_mod("Seamless", ["SeamlessCoop/nrsc.dll", "SeamlessCoop/ersc.dll"])
        DrivenDialog.steps = [lambda dialog: dialog.reject()]
        self.tool.display()
        self.assertEqual([c["path"] for c in self.tool._detect_native_dlls()], ["SeamlessCoop/nrsc.dll"])


if __name__ == "__main__":
    unittest.main()
