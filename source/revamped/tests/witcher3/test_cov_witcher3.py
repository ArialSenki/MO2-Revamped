"""The Witcher 3 plugin's less trodden paths: saves that vanish, Script
Merger's output in Overwrite, file lists with a BOM or locked, the problem
texts, odd mod lists, and the paths Proton hands."""

from __future__ import annotations

import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_witcher3 as plugin_module
from basic_games.games.witcher3 import checks
from basic_games.games.witcher3 import plugins as w3_plugins
from basic_games.games.witcher3.content import Content, Witcher3ModDataContent, contents_of
from basic_games.games.witcher3.layout import Witcher3ModDataChecker
from basic_games.games.witcher3.plugins import SCRIPT_CONFLICTS, UNLISTED_MENUS, Witcher3Check
from fake_organizer import FakeMod, FakeModList, FakeOrganizer
from harness import make_tree, tree_paths

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID
Game = plugin_module.Witcher3Game
MENU = "bin/config/r4game/user_config_matrix/pc"


def write(path: Path, data: bytes | str = "x") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
    return path


class BrokenMod(FakeMod):
    def fileTree(self):
        raise RuntimeError("being removed")


class OddModList(FakeModList):
    """Names MO2 can list without a usable mod behind them."""

    def getMod(self, name: str):
        if name == "Gone":
            return None
        if name == "Removing":
            return BrokenMod(name, self._organizer.mods / name)
        return super().getMod(name)


class Organizer(FakeOrganizer):
    def __init__(self, base: Path):
        super().__init__(base)
        self.root_builder = False

    def modList(self):
        return OddModList(self)

    def isPluginEnabled(self, name) -> bool:
        return self.root_builder and name == "RootBuilder"


class GameTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="w3-cov-"))
        self.organizer = Organizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_root_setting(self) -> None:
        self.assertTrue(self.game._root_reaches_game())
        self.organizer.settings[(self.game.name(), "root_folder_to_game_folder")] = False
        self.assertFalse(self.game._root_reaches_game())
        # Root Builder deploys root/ itself
        self.organizer.root_builder = True
        self.assertTrue(self.game._root_reaches_game())
        self.organizer.root_builder = False
        self.organizer.settings[(self.game.name(), "root_folder_to_game_folder")] = None
        self.assertTrue(self.game._root_reaches_game())

    def test_looks_valid_and_ini_files(self) -> None:
        game = self.tmp / "game"
        self.assertFalse(self.game.looksValid(QDir(str(game))))
        write(game / "bin/x64_dx12/witcher3.exe")
        self.assertTrue(self.game.looksValid(QDir(str(game))))
        self.assertEqual(self.game.iniFiles(), ["user.settings", "input.settings"])

    def test_game_version(self) -> None:
        write(self.tmp / "game/bin/x64/witcher3.exe")
        self.game.setGamePath(self.tmp / "game")
        with mock.patch.object(plugin_module.BasicGame, "gameVersion", return_value="4.4.0.0"):
            self.assertEqual(self.game.gameVersion(), "4.4.0.0")
        with mock.patch.object(plugin_module.BasicGame, "gameVersion", side_effect=OSError("locked")):
            self.assertEqual(self.game.gameVersion(), "")

    def test_the_classic_merger_name(self) -> None:
        game = self.tmp / "game"
        write(game / "bin/x64/witcher3.exe")
        write(game / "WitcherScriptMerger.exe")
        self.game.setGamePath(game)
        self.assertEqual([e.title() for e in self.game.executables()], ["The Witcher 3: Wild Hunt", "Script Merger"])

    def test_proton_paths(self) -> None:
        # Wine's Z: drive, with forward slashes: nothing there to see from
        # Windows, so the binary stays the DX11 one
        self.game.setGamePath("Z:/home/deck/.steam/steam/steamapps/common/The Witcher 3")
        self.assertEqual(self.game.binaryName(), "bin/x64/witcher3.exe")
        [executable] = self.game.executables()
        self.assertTrue(executable.binary().absoluteFilePath().endswith("The Witcher 3/bin/x64/witcher3.exe"))

    def test_saves_edge_cases(self) -> None:
        save = plugin_module.Witcher3SaveGame(self.tmp / "gone" / "QuickSave_0.sav")
        self.assertEqual(save.getCreationTime().toSecsSinceEpoch(), 0)
        self.assertEqual(save.allFiles(), [(self.tmp / "gone" / "QuickSave_0.sav").as_posix()])
        saves = self.tmp / "gamesaves"
        write(saves / "AutoSave_1.sav", b"SNFHFZLC")
        os.utime(saves / "AutoSave_1.sav", (1_700_000_000,) * 2)
        # a folder named like a save, and a screenshot without its save
        (saves / "folder.sav").mkdir()
        write(saves / "Orphan.png")
        [listed] = self.game.listSaves(QDir(str(saves)))
        self.assertEqual(listed.getCreationTime().toSecsSinceEpoch(), 1_700_000_000)
        with mock.patch.object(Path, "glob", side_effect=PermissionError("denied")):
            self.assertEqual(self.game.listSaves(QDir(str(saves))), [])


class ChecksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="w3-cov-chk-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_merged_scripts_in_overwrite(self) -> None:
        self.assertEqual(checks.overwrite_merged_scripts(self.tmp), set())
        scripts = self.tmp / "mod0000_MergedFiles/content/scripts"
        write(scripts / "game/player/R4Player.WS")
        write(scripts / "local/notes.txt")
        self.assertEqual(checks.overwrite_merged_scripts(self.tmp), {"game/player/r4player.ws"})
        mods = [
            checks.ModTree("A", make_tree(["modA/content/scripts/game/player/r4Player.ws"])),
            checks.ModTree("B", make_tree(["modB/content/scripts/game/player/r4player.ws"])),
        ]
        self.assertEqual(checks.script_conflicts(mods, checks.overwrite_merged_scripts(self.tmp)), [])
        self.assertEqual(len(checks.script_conflicts(mods)), 1)

    def test_odd_mod_folders(self) -> None:
        mods = [
            # no content/scripts, a file named like a mod, a folder that isn't a mod's
            checks.ModTree("A", make_tree(["modA/content/blob0.bundle", "modFile.ws", "other/content/scripts/x.ws"])),
            checks.ModTree("Merged", make_tree(["MOD0000_MERGEDFILES/content/scripts/x.ws"])),
            checks.ModTree("B", make_tree(["modB/content/scripts/x.ws", "MODC/content/scripts/X.WS"])),
        ]
        # merged by Script Merger in a mod: no conflict
        self.assertEqual(checks.script_conflicts(mods), [])
        without_merged = [mods[0], mods[2]]
        [conflict] = checks.script_conflicts(without_merged)
        self.assertEqual((conflict.script, conflict.folders), ("x.ws", [("modb", "B"), ("modc", "B")]))

    def test_menus_and_lists(self) -> None:
        mods = [
            checks.ModTree("No root", make_tree(["modA/content/x.bundle"])),
            checks.ModTree("Menu", make_tree([f"root/{MENU}/MyMenu.XML", f"root/{MENU}/Hud.xml",
                                              f"root/{MENU}/notes.txt", f"root/{MENU}/sub/"])),
        ]
        self.assertEqual(checks.menu_files(mods), {"mymenu.xml": "Menu"})
        # a BOM, spaces, Windows line ends, a path, no semicolon
        text = "\ufeffaudio.xml;\r\n  mods\\MyMenu.xml ;\r\nother.XML\r\n\r\n;"
        self.assertEqual(checks.listed_menus(text.lstrip("\ufeff")), {"audio.xml", "mymenu.xml", "other.xml"})
        self.assertEqual(checks.unlisted_menus({"a.xml": "A", "b.xml": "B"}, ["a.xml;", ""]), {"b.xml": "B"})

    def test_winning_filelists(self) -> None:
        game = self.tmp / "game"
        write(game / MENU / "dx11filelist.txt", "\ufeffaudio.xml;\n")
        low, high = self.tmp / "mods/Low", self.tmp / "mods/High"
        write(low / "root" / MENU / "dx12filelist.txt", "low.xml;\n")
        write(high / "root" / MENU / "dx12filelist.txt", "high.xml;\n")
        mods = [(checks.ModTree("Low", make_tree([])), low), (checks.ModTree("High", make_tree([])), high)]
        self.assertEqual(w3_plugins.winning_filelists(mods, game), ["audio.xml;\n", "high.xml;\n"])
        # the highest one is locked: the next one counts
        real_read = Path.read_text

        def read_text(path, *args, **kwargs):
            if path.is_relative_to(high):
                raise PermissionError("locked")
            return real_read(path, *args, **kwargs)

        with mock.patch.object(Path, "read_text", read_text):
            self.assertEqual(w3_plugins.winning_filelists(mods, game), ["audio.xml;\n", "low.xml;\n"])
        self.assertEqual(w3_plugins.winning_filelists([], self.tmp / "nowhere"), [])


class LayoutAndContentTest(unittest.TestCase):
    def test_inside_a_fixable_archive(self) -> None:
        checker = Witcher3ModDataChecker()
        tree = make_tree(["Foo/content/scripts/x.ws"])
        self.assertIs(checker.dataLooksValid(tree), FIXABLE)
        self.assertIs(checker.dataLooksValid(tree.find("Foo/content")), FIXABLE)

    def test_fix_of_a_wrapper_around_nothing(self) -> None:
        checker = Witcher3ModDataChecker()
        tree = make_tree(["Wrapper/readme.txt"])
        self.assertIs(checker.dataLooksValid(tree), INVALID)
        self.assertEqual(tree_paths(checker.fix(tree)), ["Wrapper/readme.txt"])

    def test_case_and_junk(self) -> None:
        checker = Witcher3ModDataChecker()
        self.assertIs(checker.dataLooksValid(make_tree(["MODFoo/CONTENT/blob0.bundle"])), VALID)
        tree = make_tree(["__MACOSX/._x", "Pack v2/MODS/modFoo/content/x.ws", ".DS_Store"])
        self.assertIs(checker.dataLooksValid(tree), FIXABLE)
        self.assertEqual(tree_paths(checker.fix(tree)), ["modFoo/content/x.ws"])

    def test_contents(self) -> None:
        content = Witcher3ModDataContent()
        self.assertEqual([c.id for c in content.getAllContents()], [int(c) for c in Content])
        self.assertEqual(content.getContentsFor(None), [])
        self.assertEqual(contents_of(make_tree(["modA/readme.txt", "root/DLC/", "root/bin/x64/readme.txt",
                                                "notes.txt"])), set())
        self.assertEqual(contents_of(make_tree(["root/bin/x64_dx12/plugin.ASI"])), {Content.NATIVE})


class CheckPluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="w3-cov-check-"))
        self.organizer = Organizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.game.init(self.organizer)
        self.game_dir = self.tmp / "The Witcher 3"
        write(self.game_dir / "bin/x64/witcher3.exe")
        self.organizer.game_dir = self.game_dir
        self.game.setGamePath(self.game_dir)
        self.check = Witcher3Check()
        self.check.init(self.organizer)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_identity(self) -> None:
        check = self.check
        self.assertEqual(check.name(), "The Witcher 3 Mod Check")
        self.assertEqual(check.localizedName(), "The Witcher 3 Mod Check")
        self.assertEqual(check.author(), "MO2 Revamped")
        self.assertIn("Script Merger", check.description())
        self.assertIsInstance(check.version(), mobase.VersionInfo)
        self.assertEqual(len(check.requirements()), 1)
        self.assertEqual(check.settings(), [])
        self.assertFalse(check.hasGuidedFix(SCRIPT_CONFLICTS))
        check.startGuidedFix(SCRIPT_CONFLICTS)
        self.assertTrue(check.shortDescription(SCRIPT_CONFLICTS))
        self.assertTrue(check.shortDescription(UNLISTED_MENUS))
        self.assertEqual([type(p) for p in w3_plugins.createPlugins()], [Witcher3Check])

    def test_long_lists(self) -> None:
        write(self.game_dir / "bin/x64_dx12/witcher3.exe")
        for index in range(14):
            self.organizer.add_mod(f"Mod {index}", [f"mod{index}/content/scripts/s{index:02}.ws",
                                                    f"mod{index}b/content/scripts/s{index:02}.ws",
                                                    f"root/{MENU}/menu{index:02}.xml"])
        self.assertEqual(self.check.activeProblems(), [SCRIPT_CONFLICTS, UNLISTED_MENUS])
        self.assertIn("and 2 more", self.check.fullDescription(SCRIPT_CONFLICTS))
        menus = self.check.fullDescription(UNLISTED_MENUS)
        self.assertIn("menu00.xml (Mod 0)", menus)
        self.assertIn("and 2 more", menus)

    def test_old_game_and_odd_mod_list(self) -> None:
        # the classic game (no DX12 build) loads every menu
        self.organizer.add_mod("Menu", [f"root/{MENU}/MyMenu.xml"])
        self.organizer.add_mod("Off", ["modX/content/scripts/a.ws"], active=False)
        self.organizer.add_mod("On", ["modY/content/scripts/a.ws"])
        self.organizer.add_mod("Group_separator", [])
        self.organizer.mod_order += ["Gone", "Removing"]
        self.organizer.active |= {"Gone", "Removing"}
        self.assertEqual([t.name for t, _ in w3_plugins.active_mods(self.organizer)], ["Menu", "On"])
        self.assertEqual(self.check.activeProblems(), [])

    def test_another_game_or_a_failure(self) -> None:
        self.organizer.add_mod("A", ["modA/content/scripts/a.ws"])
        self.organizer.add_mod("B", ["modB/content/scripts/a.ws"])
        self.organizer.game = mock.Mock(**{"gameName.return_value": "Cyberpunk 2077"})
        self.assertEqual(self.check.activeProblems(), [])
        self.check._changed()
        self.organizer.game = self.game
        with mock.patch.object(w3_plugins, "script_conflicts", side_effect=RuntimeError("tree changed")):
            self.assertEqual(self.check.activeProblems(), [])
        self.check._changed()
        self.assertEqual(self.check.activeProblems(), [SCRIPT_CONFLICTS])
        # not installed: the scripts still count, the menus can't be checked
        self.game.setGamePath("")
        self.check._changed()
        self.assertEqual(self.check.activeProblems(), [SCRIPT_CONFLICTS])


if __name__ == "__main__":
    unittest.main()
