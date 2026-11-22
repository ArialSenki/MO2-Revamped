"""The mod data checker (checker.py) on odd archive layouts: loader packages
(me3, Mod Engine 2), optional and preview folders, shipped logs, packages
it knows by name, the layout kept as in the archive, and the mods installed
that way."""

from __future__ import annotations

import unittest
from unittest import mock

import mobase
from basic_games.games import game_eldenring_mo2 as er

from fake_organizer import FakeOrganizer, appdata_folder
from harness import make_tree
from test_cov_game_fakes import use_mod_settings


def tree_paths(tree) -> list[str]:
    paths: list[str] = []

    def visit(path: str, entry) -> mobase.IFileTree.WalkReturn:
        full = (path + entry.name()).replace("\\", "/")
        if isinstance(entry, mobase.IFileTree):
            if len(entry) == 0:
                paths.append(full + "/")
        else:
            paths.append(full)
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(visit, "/")
    return sorted(paths)


class Layouts(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.checker = er.EldenRingModDataChecker(self.organizer, "Elden Ring MO2 Support")
        self.checker.previewing = True

    def normalize(self, paths, **options):
        return tree_paths(self.checker.normalize_standard_layout(make_tree(paths), **options))

    def test_loader_packages(self):
        cases = {
            # me3's own files and launcher scripts go; the payload stays
            ("Pack/me3/me3.exe", "Pack/me3/bin/x.dll", "Pack/mod.me3", "Pack/config_mod.toml",
             "Pack/launch.bat", "Pack/parts/a.dcx"): ["parts/a.dcx"],
            ("modengine2_launcher.exe", "modengine2/bin/me2.dll", "launchmod_eldenring.cmd", "chr/c.dcx"):
                ["chr/c.dcx"],
            # a me3 folder that isn't me3's own stays as game data
            ("Wrap/me3/readme.txt", "Wrap/parts/a.dcx"): ["parts/a.dcx"],
        }
        for paths, expected in cases.items():
            with self.subTest(paths=paths):
                result = self.normalize(list(paths))
                for path in expected:
                    self.assertIn(path, result)
                self.assertFalse(any(p.endswith((".exe", ".bat", ".cmd", ".me3", ".toml")) for p in result), result)

    def test_logs_and_previews(self):
        result = self.normalize(["Mod/parts/a.dcx", "Mod/logs/a.log", "Mod/logs/b.log",
                                 "Mod/screenshot.png", "Mod/Images/1.jpg", "Mod/Images/2.webp"])
        self.assertEqual(result, ["parts/a.dcx"])
        # a logs folder with something else in it isn't the author's logs
        kept = self.normalize(["Mod/parts/a.dcx", "Mod/logs/a.log", "Mod/logs/notes.ini"])
        self.assertIn("notes.ini", kept)
        self.assertIn("parts/a.dcx", kept)

    def test_optional_folder_that_is_all_there_is(self):
        result = self.normalize(["Optional/parts/a.dcx"])
        self.assertEqual(result, ["parts/a.dcx"])
        self.assertEqual(self.checker.optional_files_left_out, 0)
        result = self.normalize(["Main/parts/a.dcx", "Extras/parts/b.dcx", "Addons/parts/c.dcx"])
        self.assertEqual(result, ["parts/a.dcx"])
        self.assertEqual(self.checker.optional_files_left_out, 2)

    def test_wrappers_and_legacy_folders(self):
        cases = {
            # the game's own folder names at the top: their files were lost
            ("Game/parts/a.dcx", "Game/regulation.bin"): ["parts/a.dcx", "regulation.bin"],
            ("ELDEN RING/Game/parts/a.dcx",): ["parts/a.dcx"],
            ("natives/tool.dll", "natives/tool.ini"): ["DLLs/tool.dll", "DLLs/tool.ini"],
            ("mods/Pack/x.dll", "mods/Pack/x.toml"): ["DLLs/x.dll", "DLLs/x.toml"],
            ("Wrap/mod/menu/a.gfx", "Wrap/mod/msg/b.fmg"): ["menu/a.gfx", "msg/b.fmg"],
        }
        for paths, expected in cases.items():
            with self.subTest(paths=paths):
                self.assertEqual(self.normalize(list(paths)), expected)

    def test_mod_engine_paths_kept_on_request(self):
        result = self.normalize(["mod/menu/a.gfx"], recognize_mod_engine_paths=False)
        self.assertEqual(result, ["mod/menu/a.gfx"])

    def test_known_packages(self):
        known = er.EldenRingModDataChecker.known_package
        cases = {
            ("R/internals/modengine/dll/reforged.dll", "R/mod/regulation.bin"): "reforged",
            ("Conv/me3/convergence.me3",): "convergence",
            ("a/b/SeamlessCoop/nrsc.dll",): "seamless",
            ("FRZN/Drag Files Into Here/parts/a.dcx",): "frzn",
            ("a/b/c/SeamlessCoop/ersc.dll",): None,
            ("parts/a.dcx",): None,
        }
        for paths, expected in cases.items():
            with self.subTest(paths=paths):
                self.assertEqual(known(make_tree(list(paths))), expected)

    def test_preserved_layout(self):
        cases = {
            ("FRZN/Drag Files Into Here/parts/a.dcx", "FRZN/natives/x.dll"): ["natives/x.dll", "parts/a.dcx"],
            ("Top/Game/parts/a.dcx",): ["parts/a.dcx"],
            ("Top/SeamlessCoop/ersc.dll", "Top/ersc_launcher.exe"): ["SeamlessCoop/ersc.dll", "ersc_launcher.exe"],
            ("Top/SeamlessCoop/ersc.dll",): ["SeamlessCoop/ersc.dll"],
            ("A/parts/a.dcx", "B/parts/b.dcx"): ["A/parts/a.dcx", "B/parts/b.dcx"],
            ("readme.txt", "Wrap/regulation.bin"): ["readme.txt", "regulation.bin"],
        }
        for paths, expected in cases.items():
            with self.subTest(paths=paths):
                tree = self.checker.normalize_preserved_layout(make_tree(list(paths)))
                self.assertEqual(tree_paths(tree), expected)


class Validity(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.checker = er.EldenRingModDataChecker(self.organizer, "Elden Ring MO2 Support", watch_mod_list=True)

    def test_layout_kept_by_choice(self):
        self.organizer.settings[er.EldenRingMo2Game.ArchiveLayoutOverrideSetting] = True
        self.assertEqual(self.checker.dataLooksValid(make_tree(["Any/Folder/parts/a.dcx"])), self.checker.VALID)
        self.assertEqual(self.checker.dataLooksValid(make_tree(["notes.txt"])), self.checker.INVALID)
        tree = make_tree(["Any/Folder/parts/a.dcx"])
        self.assertIs(self.checker.fix(tree), tree)
        self.assertEqual(tree_paths(tree), ["Any/Folder/parts/a.dcx"])

    def test_installed_with_its_archive_layout(self):
        self.organizer.add_mod("Kept", ["Custom/parts/a.dcx", "meta.ini"])
        self.organizer.add_mod("Plain", ["Wrap/parts/b.dcx"])
        settings = {"Kept": {er.EldenRingMo2Game.PreserveArchiveLayoutSetting: True}}
        with use_mod_settings(self.organizer, settings):
            self.assertEqual(self.checker.dataLooksValid(make_tree(["Custom/parts/a.dcx"])), self.checker.VALID)
            # the cache answers until a mod is installed or removed
            self.assertIsNotNone(self.checker._saved_archive_layout_signatures)
            self.assertEqual(self.checker.dataLooksValid(make_tree(["Wrap/parts/b.dcx"])), self.checker.FIXABLE)
            self.checker._invalidate_saved_archive_layout_cache()
            self.assertIsNone(self.checker._saved_archive_layout_signatures)

    def test_mod_list_unavailable(self):
        with mock.patch.object(self.organizer, "modList", side_effect=RuntimeError("busy")):
            self.assertFalse(self.checker._is_saved_archive_layout_tree(make_tree(["a/b.dcx"])))
            # the checker of an install, without the mod list callbacks
            er.EldenRingModDataChecker(self.organizer, "x", watch_mod_list=True)

    def test_mod_that_changes_meanwhile(self):
        self.organizer.add_mod("Kept", ["Custom/parts/a.dcx"])
        settings = {"Kept": {er.EldenRingMo2Game.PreserveArchiveLayoutSetting: True}}
        with use_mod_settings(self.organizer, settings, {"Kept": RuntimeError("changing")}):
            self.assertFalse(self.checker._is_saved_archive_layout_tree(make_tree(["Custom/parts/a.dcx"])))
        self.assertIsNone(self.checker._saved_archive_layout_signatures)

    def test_more_layouts(self):
        valid, fixable, invalid = self.checker.VALID, self.checker.FIXABLE, self.checker.INVALID
        cases = [
            (["regulation.bin", "readme.md"], valid),
            (["Option A/parts/a.dcx", "Option B/parts/b.dcx"], fixable),
            (["deep/a/b/c/menu/x.gfx"], fixable),
            (["notes/info.txt"], invalid),
        ]
        for paths, expected in cases:
            with self.subTest(paths=paths):
                self.assertEqual(self.checker.dataLooksValid(make_tree(paths)), expected)

    def test_name_suffix(self):
        suffix = er.EldenRingModDataChecker._name_suffix
        self.assertEqual(suffix("a.dll"), ".dll")
        self.assertEqual(suffix(".hidden"), "")
        self.assertEqual(suffix("end."), "")
        self.assertEqual(suffix("dir/a.TXT"), ".TXT")
        self.assertFalse(er.EldenRingModDataChecker._signature_counts_file("x.dds.mohidden"))
        self.assertFalse(er.EldenRingModDataChecker._signature_counts_file("META.INI"))


if __name__ == "__main__":
    unittest.main()
