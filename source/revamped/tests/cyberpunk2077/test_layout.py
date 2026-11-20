"""The Cyberpunk 2077 mod data checker on real mobase file trees."""

from __future__ import annotations

import unittest

import mobase
from basic_games.games.cyberpunk2077.layout import CyberpunkModDataChecker
from harness import make_tree, tree_paths

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID


class LayoutTest(unittest.TestCase):
    def setUp(self) -> None:
        self.root_builder = False
        self.checker = CyberpunkModDataChecker(lambda: self.root_builder)

    def check(self, paths: list[str]):
        return self.checker.dataLooksValid(make_tree(paths))

    def fixed(self, paths: list[str]) -> list[str]:
        tree = make_tree(paths)
        self.assertIs(self.checker.dataLooksValid(tree), FIXABLE, paths)
        return tree_paths(self.checker.fix(tree))

    # layouts the Welcome to Night City collection installed: unchanged

    def test_collection_layouts_stay_valid(self) -> None:
        for paths in (
            ["archive/pc/mod/a.archive", "archive/pc/mod/a.archive.xl"],
            ["r6/scripts/Mod/a.reds", "LICENSE"],
            ["r6/tweaks/Mod/a.yaml", "Mod_readme.txt"],
            ["r6/input/a.xml", "r6/config/settings/x.json"],
            ["bin/x64/plugins/cyber_engine_tweaks/mods/Mod/init.lua"],
            ["bin/x64/version.dll", "bin/x64/global.ini", "bin/x64/plugins/cyber_engine_tweaks.asi"],
            ["red4ext/RED4ext.dll", "bin/x64/winmm.dll", "red4ext/LICENSE.txt"],
            ["red4ext/plugins/TweakXL/TweakXL.dll", "r6/scripts/TweakXL/a.reds"],
            ["engine/config/platform/pc/a.ini"],
            ["mods/Red/info.json", "mods/Red/archives/a.archive"],
            ["root/bin/x64/version.dll"],
            ["archive/pc/mod/sub/a.archive", "images/"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.check(paths), VALID)
                tree = make_tree(paths)
                self.assertEqual(tree_paths(self.checker.fix(tree)), sorted(paths))

    def test_nothing_for_the_game(self) -> None:
        for paths in (
            ["readme.txt", "preview.png"],
            ["init.lua", "db.lua"],  # a CET mod without its folder name
            ["info.json", "archives/a.archive"],  # a REDmod without its folder
            ["A/a.archive", "B/b.archive"],  # options: the manual installer asks
            ["a.yaml", "a.wav"],  # Audioware, not TweakXL
            ["fomod/ModuleConfig.xml", "Option/x.txt"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.check(paths), INVALID)

    # fixes

    def test_loose_files(self) -> None:
        self.assertEqual(
            self.fixed(["a.archive", "a.archive.xl", "b.reds", "c.yaml", "d.asi", "readme.txt"]),
            [
                "archive/pc/mod/a.archive",
                "archive/pc/mod/a.archive.xl",
                "bin/x64/plugins/d.asi",
                "r6/scripts/b.reds",
                "r6/tweaks/c.yaml",
                "readme.txt",
            ],
        )

    def test_wrapper_folders(self) -> None:
        self.assertEqual(
            self.fixed(["Mod/archive/pc/mod/a.archive", "readme.md", "Mod/LICENSE"]),
            ["LICENSE", "archive/pc/mod/a.archive", "readme.md"],
        )
        self.assertEqual(
            self.fixed(["Cyberpunk 2077/Mod v1.2/r6/scripts/M/a.reds", "Cyberpunk 2077/notes.txt"]),
            ["notes.txt", "r6/scripts/M/a.reds"],
        )
        # a wrapper around loose archives
        self.assertEqual(self.fixed(["Mod/a.archive", "Mod/b.png"]), ["archive/pc/mod/a.archive", "b.png"])

    def test_wrapper_depth_limit(self) -> None:
        self.assertIs(self.check(["1/2/3/4/archive/pc/mod/a.archive"]), INVALID)
        self.assertIs(self.check(["1/2/3/archive/pc/mod/a.archive"]), FIXABLE)

    def test_cet_mod_folders(self) -> None:
        self.assertEqual(
            self.fixed(["MyCet/init.lua", "MyCet/data/x.json"]),
            [
                "bin/x64/plugins/cyber_engine_tweaks/mods/MyCet/data/x.json",
                "bin/x64/plugins/cyber_engine_tweaks/mods/MyCet/init.lua",
            ],
        )
        self.assertEqual(
            self.fixed(["mods/MyCet/init.lua"]),
            ["bin/x64/plugins/cyber_engine_tweaks/mods/MyCet/init.lua"],
        )
        self.assertEqual(
            self.fixed(["cyber_engine_tweaks/mods/M/init.lua"]),
            ["bin/x64/plugins/cyber_engine_tweaks/mods/M/init.lua"],
        )
        self.assertEqual(
            self.fixed(["plugins/cyber_engine_tweaks/mods/M/init.lua"]),
            ["bin/x64/plugins/cyber_engine_tweaks/mods/M/init.lua"],
        )

    def test_redmod_folder(self) -> None:
        self.assertEqual(
            self.fixed(["Red/info.json", "Red/archives/a.archive", "archive/pc/mod/b.archive"]),
            ["archive/pc/mod/b.archive", "mods/Red/archives/a.archive", "mods/Red/info.json"],
        )

    def test_missing_prefixes(self) -> None:
        self.assertEqual(
            self.fixed(["x64/plugins/a.asi", "bin/x64/b.dll"]),
            ["bin/x64/b.dll", "bin/x64/plugins/a.asi"],
        )
        self.assertEqual(self.fixed(["pc/mod/a.archive"]), ["archive/pc/mod/a.archive"])
        self.assertEqual(self.fixed(["scripts/M/a.reds"]), ["r6/scripts/M/a.reds"])
        self.assertEqual(self.fixed(["tweaks/M/a.yaml"]), ["r6/tweaks/M/a.yaml"])

    def test_old_archive_patch_folder(self) -> None:
        self.assertEqual(
            self.fixed(["archive/pc/patch/a.archive", "archive/pc/patch/sub/b.archive"]),
            ["archive/pc/mod/a.archive", "archive/pc/mod/sub/b.archive"],
        )

    def test_red4ext_plugin_without_folder(self) -> None:
        self.assertEqual(
            self.fixed(["red4ext/plugins/Foo.dll", "red4ext/plugins/Bar/Bar.dll"]),
            ["red4ext/plugins/Bar/Bar.dll", "red4ext/plugins/Foo/Foo.dll"],
        )

    def test_root_builder_layout(self) -> None:
        cet = [
            "bin/x64/version.dll",
            "bin/x64/global.ini",
            "bin/x64/plugins/cyber_engine_tweaks.asi",
            "bin/x64/plugins/cyber_engine_tweaks/scripts/a.lua",
            "bin/x64/plugins/cyber_engine_tweaks/mods/M/init.lua",
        ]
        self.assertIs(self.check(cet), VALID)
        self.root_builder = True
        self.assertEqual(
            self.fixed(cet),
            [
                "bin/x64/plugins/cyber_engine_tweaks/mods/M/init.lua",
                "root/bin/x64/global.ini",
                "root/bin/x64/plugins/cyber_engine_tweaks.asi",
                "root/bin/x64/plugins/cyber_engine_tweaks/scripts/a.lua",
                "root/bin/x64/version.dll",
            ],
        )
        self.assertEqual(
            self.fixed(["red4ext/RED4ext.dll", "bin/x64/winmm.dll", "red4ext/plugins/A/A.dll"]),
            ["red4ext/plugins/A/A.dll", "root/bin/x64/winmm.dll", "root/red4ext/RED4ext.dll"],
        )

    def test_lone_folder_of_fixable_parent(self) -> None:
        tree = make_tree(["Mod/archive/pc/mod/a.archive", "readme.txt"])
        # the wrapper is fixable, so its lone folder says so too
        self.assertIs(self.checker.dataLooksValid(tree.find("Mod")), FIXABLE)
        tree = make_tree(["Mod/archive/pc/mod/a.archive", "Other/x.txt"])
        self.assertIs(self.checker.dataLooksValid(tree), INVALID)
        self.assertIs(self.checker.dataLooksValid(tree.find("Mod")), VALID)


if __name__ == "__main__":
    unittest.main()
