"""The Witcher 3 plugin: layouts, content, checks, and the game plugin over a
fake game folder."""

from __future__ import annotations

import os
import shutil
import tempfile
import unittest
from pathlib import Path

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_witcher3 as plugin_module
from basic_games.games.witcher3 import checks
from basic_games.games.witcher3.content import Content, contents_of
from basic_games.games.witcher3.layout import Witcher3ModDataChecker
from basic_games.games.witcher3.plugins import SCRIPT_CONFLICTS, UNLISTED_MENUS, Witcher3Check
from fake_organizer import FakeOrganizer
from harness import make_tree, tree_paths

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID
Game = plugin_module.Witcher3Game
MENU = "bin/config/r4game/user_config_matrix/pc"


class LayoutTest(unittest.TestCase):
    def setUp(self) -> None:
        self.use_root = True
        self.checker = Witcher3ModDataChecker(lambda: self.use_root)

    def check(self, paths):
        return self.checker.dataLooksValid(make_tree(paths))

    def fixed(self, paths):
        tree = make_tree(paths)
        self.assertIs(self.checker.dataLooksValid(tree), FIXABLE, paths)
        return tree_paths(self.checker.fix(tree))

    def test_valid_layouts(self) -> None:
        for paths in (
            ["modFoo/content/scripts/game/a.ws", "readme.txt"],
            ["modFoo/content/blob0.bundle", "modBar/content/texture.cache"],
            ["modFoo/content/a.ws", "root/DLC/dlcFoo/content/b.bundle"],
            ["root/" + MENU + "/foo.xml"],
            ["mod0000_MergedFiles/content/scripts/game/r4Player.ws"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.check(paths), VALID)

    def test_fixes(self) -> None:
        self.assertEqual(self.fixed(["Mods/modFoo/content/a.ws"]), ["modFoo/content/a.ws"])
        self.assertEqual(
            self.fixed(["Mods/modFoo/content/a.ws", "DLC/dlcFoo/content/b.bundle", "readme.txt"]),
            ["modFoo/content/a.ws", "readme.txt", "root/DLC/dlcFoo/content/b.bundle"],
        )
        self.assertEqual(
            self.fixed(["modFoo/content/a.ws", "bin/config/r4game/user_config_matrix/pc/foo.xml"]),
            ["modFoo/content/a.ws", "root/" + MENU + "/foo.xml"],
        )
        self.assertEqual(self.fixed(["dlcFoo/content/b.bundle"]), ["root/DLC/dlcFoo/content/b.bundle"])
        # the game loads only folders starting with "mod"
        self.assertEqual(self.fixed(["BetterHair/content/b.bundle"]), ["modBetterHair/content/b.bundle"])
        # wrappers, a folder of its own name too (merging it crashed MO2)
        self.assertEqual(
            self.fixed(["Better Hair v2/modBetterHair/content/b.bundle", "Better Hair v2/readme.txt"]),
            ["modBetterHair/content/b.bundle", "readme.txt"],
        )
        self.assertEqual(self.fixed(["modX/modX/content/a.ws"]), ["modX/content/a.ws"])
        # an archive made on a Mac (a real Nexus mod, Red Dead Roach)
        self.assertEqual(
            self.fixed(["Red Dead Roach/.DS_Store", "Red Dead Roach/Install.txt",
                        "Red Dead Roach/mods/modRoach/content/a.ws", "Red Dead Roach/mods/.DS_Store",
                        "__MACOSX/Red Dead Roach/._Install.txt"]),
            ["Install.txt", "modRoach/content/a.ws"],
        )

    def test_left_to_the_manual_installer(self) -> None:
        for paths in (
            ["content/scripts/a.ws"],  # no folder name to give it
            ["Option A/modA/content/a.ws", "Option B/modB/content/b.ws"],
            ["readme.txt"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.check(paths), INVALID)

    def test_without_root_mapping(self) -> None:
        self.use_root = False
        self.assertIs(self.check(["DLC/dlcFoo/content/b.bundle"]), INVALID)
        # a DLC folder is never renamed into a mod
        self.assertIs(self.check(["dlcFoo/content/b.bundle"]), INVALID)
        # root/ does nothing then
        self.assertIs(self.check(["root/" + MENU + "/foo.xml"]), INVALID)

    def test_nested_fixes_in_one_go(self) -> None:
        # out of Mods, then the "mod" prefix
        self.assertEqual(self.fixed(["Mods/Foo/content/a.ws"]), ["modFoo/content/a.ws"])


class ContentTest(unittest.TestCase):
    def test_contents(self) -> None:
        self.assertEqual(
            contents_of(make_tree([
                "modFoo/content/scripts/a.ws", "modFoo/content/blob0.bundle",
                "root/DLC/dlcFoo/content/x.bundle", "root/" + MENU + "/foo.xml",
                "root/bin/x64/dinput8.dll", "mod0000_MergedFiles/content/scripts/b.ws",
            ])),
            {Content.SCRIPTS, Content.ASSETS, Content.DLC, Content.MENU, Content.NATIVE, Content.MERGED},
        )
        self.assertEqual(contents_of(make_tree(["readme.txt"])), set())


class ChecksTest(unittest.TestCase):
    def test_script_conflicts(self) -> None:
        mods = [
            checks.ModTree("Mod A", make_tree(["modA/content/scripts/game/player/r4Player.ws"])),
            checks.ModTree("Mod B", make_tree(["modB/content/scripts/game/player/R4Player.ws", "modB/content/scripts/b.ws"])),
            # the same game folder in two MO2 mods: one in the game
            checks.ModTree("Mod B patch", make_tree(["modB/content/scripts/b.ws"])),
        ]
        found = checks.script_conflicts(mods)
        self.assertEqual([c.script for c in found], ["game/player/r4Player.ws"])
        self.assertEqual(found[0].folders, [("moda", "Mod A"), ("modb", "Mod B")])
        merged = checks.ModTree("Merged", make_tree(["mod0000_MergedFiles/content/scripts/game/player/r4player.ws"]))
        self.assertEqual(checks.script_conflicts(mods + [merged]), [])
        # Script Merger's output still in Overwrite
        self.assertEqual(checks.script_conflicts(mods, {"game/player/r4player.ws"}), [])

    def test_menus(self) -> None:
        mods = [checks.ModTree("Menu mod", make_tree(["root/" + MENU + "/FooMenu.xml", "root/" + MENU + "/input.xml"]))]
        menus = checks.menu_files(mods)
        self.assertEqual(menus, {"foomenu.xml": "Menu mod"})
        self.assertEqual(checks.unlisted_menus(menus, ["audio.xml;\r\nhud.xml;\n"]), {"foomenu.xml": "Menu mod"})
        self.assertEqual(checks.unlisted_menus(menus, ["audio.xml;\n", "FooMenu.xml;\n"]), {})


def make_game(root: Path, dx12_only: bool = False) -> Path:
    game = root / "The Witcher 3"
    files = ["bin/x64_dx12/witcher3.exe", MENU + "/dx12filelist.txt", MENU + "/dx11filelist.txt"]
    if not dx12_only:
        files.append("bin/x64/witcher3.exe")
    for relative in files:
        path = game / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("audio.xml;\n" if relative.endswith(".txt") else "exe")
    (game / "Mods").mkdir()
    return game


class PluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="w3-test-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self, dx12_only: bool = False) -> Path:
        game_dir = make_game(self.tmp, dx12_only)
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def test_identity(self) -> None:
        self.assertEqual(self.game.name(), "Witcher 3 Support Plugin")
        self.assertEqual(self.game.gameName(), "The Witcher 3: Wild Hunt")
        self.assertEqual(self.game.gameShortName(), "witcher3")
        self.assertEqual(self.game.gameNexusName(), "witcher3")
        self.assertEqual(self.game.nexusGameID(), 952)
        self.assertEqual([s.key for s in self.game.settings()], ["root_folder_to_game_folder"])

    def test_without_the_game(self) -> None:
        self.assertEqual(self.game.executables(), [])
        self.assertEqual(self.game.gameVersion(), "")
        self.assertEqual(self.game.listSaves(QDir(str(self.tmp / "none"))), [])

    def test_executables(self) -> None:
        game_dir = self.install()
        (game_dir / "WitcherScriptMerger").mkdir()
        (game_dir / "WitcherScriptMerger/WitcherScriptMerger.exe").write_text("x")
        self.assertEqual(
            [e.title() for e in self.game.executables()],
            ["The Witcher 3: Wild Hunt", "The Witcher 3: Wild Hunt (DX12)", "Script Merger"],
        )
        self.assertEqual(self.game.dataDirectory().absolutePath(), QDir(str(game_dir / "Mods")).absolutePath())

    def test_dx12_only(self) -> None:
        self.install(dx12_only=True)
        self.assertEqual(self.game.binaryName(), "bin/x64_dx12/witcher3.exe")
        self.assertEqual([e.title() for e in self.game.executables()], ["The Witcher 3: Wild Hunt"])

    def test_saves(self) -> None:
        saves = self.tmp / "gamesaves"
        saves.mkdir()
        for index, name in enumerate(["AutoSave_0", "ManualSave_12", "QuickSave_1"]):
            (saves / f"{name}.sav").write_bytes(b"SNFHFZLC")
            os.utime(saves / f"{name}.sav", (1_700_000_000 + index,) * 2)
        (saves / "ManualSave_12.png").write_bytes(b"png")
        listed = self.game.listSaves(QDir(str(saves)))
        self.assertEqual([s.getName() for s in listed], ["QuickSave 1", "ManualSave 12", "AutoSave 0"])
        self.assertEqual(len(listed[1].allFiles()), 2)

    def test_check_plugin(self) -> None:
        self.install()
        self.organizer.add_mod("Mod A", ["modA/content/scripts/game/r4Player.ws"])
        self.organizer.add_mod("Mod B", ["modB/content/scripts/game/r4Player.ws"])
        self.organizer.add_mod("Menu", ["root/" + MENU + "/MyMenu.xml"])
        check = Witcher3Check()
        check.init(self.organizer)
        self.assertEqual(check.activeProblems(), [SCRIPT_CONFLICTS, UNLISTED_MENUS])
        self.assertIn("r4Player.ws", check.fullDescription(SCRIPT_CONFLICTS))
        self.assertIn("MyMenu.xml".casefold(), check.fullDescription(UNLISTED_MENUS))
        # a mod that lists the menu in both files
        self.organizer.add_mod("Menu lists", ["root/" + MENU + "/dx12filelist.txt", "root/" + MENU + "/dx11filelist.txt"])
        for name in ("dx11filelist.txt", "dx12filelist.txt"):
            (self.organizer.mods / "Menu lists/root" / MENU / name).write_text("audio.xml;\nmymenu.xml;\n")
        check._changed()
        self.assertEqual(check.activeProblems(), [SCRIPT_CONFLICTS])


if __name__ == "__main__":
    unittest.main()
