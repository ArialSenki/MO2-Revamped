"""The Cyberpunk 2077 game plugin over a fake game folder, mods and Overwrite."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import time
import unittest
from pathlib import Path

import mobase
from PyQt6.QtCore import QDir
from PyQt6.QtWidgets import QMessageBox

from basic_games.games import game_cyberpunk2077 as plugin_module
from basic_games.games.cyberpunk2077 import frameworks
from basic_games.games.cyberpunk2077.plugins import CyberpunkFrameworkCheck
from fake_organizer import FakeOrganizer
from harness import make_tree

Game = plugin_module.Cyberpunk2077Game
NAME = Game.Name


def make_game_dir(root: Path, extra: list[str] = ()) -> Path:
    game = root / "Cyberpunk 2077"
    for relative in ["bin/x64/Cyberpunk2077.exe", "REDprelauncher.exe", "r6/cache/final.redscripts",
                     "r6/cache/tweakdb.bin", "archive/pc/content/basegame_1_engine.archive", *extra]:
        file = game / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(b"game:" + relative.encode())
    return game


class PluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="cp77-test-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self, extra: list[str] = ()) -> Path:
        game_dir = make_game_dir(self.tmp, extra)
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def run_game(self, args: str = "--launcher-skip -modded") -> bool:
        exe = Path(self.game.gameDirectory().absolutePath(), self.game.binaryName())
        return self.game._on_about_to_run(str(exe), QDir(str(exe.parent)), args)

    def set(self, key: str, value) -> None:
        self.organizer.setPluginSetting(NAME, key, value)

    # identity

    def test_identity_matches_basic_games(self) -> None:
        self.assertEqual(self.game.name(), "Cyberpunk 2077 Support Plugin")
        self.assertEqual(self.game.gameName(), "Cyberpunk 2077")
        self.assertEqual(self.game.gameShortName(), "cyberpunk2077")
        self.assertEqual(self.game.gameNexusName(), "cyberpunk2077")
        self.assertEqual(self.game.steamAPPId(), "1091500")
        self.assertEqual(self.game.gogAPPId(), "1423049311")
        keys = [setting.key for setting in self.game.settings()]
        for key in ("skipStartScreen", "enforce_archive_load_order", "reverse_archive_load_order",
                    "enforce_redmod_load_order", "reverse_redmod_load_order", "auto_deploy_redmod",
                    "clear_cache_after_game_update", "configure_RootBuilder"):
            self.assertIn(key, keys)

    # no game

    def test_without_the_game(self) -> None:
        self.assertFalse(self.game.isInstalled())
        self.assertEqual(self.game.executables(), [])
        self.assertEqual(self.game.executableForcedLoads(), [])
        self.assertEqual(self.game.gameVersion(), "")
        self.assertTrue(self.game._on_about_to_run("C:/x/Cyberpunk2077.exe", QDir(), "-modded"))
        self.assertEqual(self.game.listSaves(QDir(str(self.tmp / "nothing"))), [])

    def test_executables(self) -> None:
        self.install()
        titles = [(exe.title(), exe.arguments()) for exe in self.game.executables()]
        self.assertEqual(
            titles,
            [
                ("Cyberpunk 2077", ["--launcher-skip -modded"]),
                ("Cyberpunk 2077 - skip REDmod deploy", ["--launcher-skip "]),
                ("Manually deploy REDmod", ["deploy -reportProgress -force %modlist%"]),
                ("REDprelauncher", [""]),  # as basic_games passed it
            ],
        )

    def test_forced_loads(self) -> None:
        self.install()
        self.assertEqual(self.game.executableForcedLoads(), [])
        self.organizer.add_mod("CET", ["bin/x64/version.dll", "bin/x64/plugins/cyber_engine_tweaks.asi"])
        self.organizer.add_mod("RED4ext", ["root/bin/x64/winmm.dll", "root/red4ext/RED4ext.dll"])
        loads = sorted(Path(load.library()).name for load in self.game.executableForcedLoads())
        self.assertEqual(loads, ["version.dll", "winmm.dll"])

    def test_forced_loader_in_overwrite_root(self) -> None:
        # root/ is read from disk (the data tree leaves it out): Overwrite's too
        self.install()
        loader = self.organizer.overwrite / "root/bin/x64/winmm.dll"
        loader.parent.mkdir(parents=True, exist_ok=True)
        loader.write_bytes(b"")
        loads = [Path(load.library()).name for load in self.game.executableForcedLoads()]
        self.assertEqual(loads, ["winmm.dll"])

    # archive load order

    def test_archive_load_order(self) -> None:
        self.install()
        self.organizer.add_mod("A", ["archive/pc/mod/b.archive", "archive/pc/mod/shared.archive"])
        self.organizer.add_mod("B", ["archive/pc/mod/a.archive", "archive/pc/mod/shared.archive"])
        self.organizer.add_mod("Off", ["archive/pc/mod/off.archive"], active=False)
        listing = self.organizer.overwrite / "archive/pc/mod/modlist.txt"
        self.assertTrue(self.run_game())
        self.assertFalse(listing.exists())  # off by default

        self.set("enforce_archive_load_order", True)
        self.assertTrue(self.run_game())
        self.assertEqual(listing.read_bytes(), b"b.archive\r\na.archive\r\nshared.archive\r\n")
        self.set("reverse_archive_load_order", True)
        self.run_game()
        self.assertEqual(listing.read_bytes(), b"shared.archive\r\na.archive\r\nb.archive\r\n")

        # turned off: the list this plugin wrote goes, a list of the user's stays
        self.set("enforce_archive_load_order", False)
        self.run_game()
        self.assertFalse(listing.exists())
        listing.write_text("mine.archive\n")
        self.run_game()
        self.assertTrue(listing.exists())

    def test_archive_list_never_in_a_mod(self) -> None:
        self.install()
        own = self.organizer.add_mod("A", ["archive/pc/mod/a.archive", "archive/pc/mod/modlist.txt"])
        self.organizer.add_mod("B", ["archive/pc/mod/b.archive"])
        self.set("enforce_archive_load_order", True)
        self.run_game()
        self.assertEqual((own / "archive/pc/mod/modlist.txt").read_text(), "archive/pc/mod/modlist.txt")
        self.assertTrue((self.organizer.overwrite / "archive/pc/mod/modlist.txt").is_file())

    # REDmod

    def test_redmod_without_dlc_starts_the_game(self) -> None:
        self.install()
        self.organizer.add_mod("Red", ["mods/Red/info.json", "mods/Red/archives/a.archive"])
        self.assertTrue(self.run_game())
        self.assertEqual(self.organizer.started, [])

    def test_redmod_deploys_only_on_changes(self) -> None:
        game_dir = self.install(["tools/redmod/bin/redMod.exe"])
        red = self.organizer.add_mod("Red", ["mods/Red/info.json", "mods/Red/archives/a.archive"])
        self.organizer.add_mod("Red2", ["mods/Red2/info.json"])
        self.organizer.add_mod("NotRed", ["mods/Folder/readme.txt"])
        self.assertTrue(self.run_game())
        self.assertEqual(len(self.organizer.started), 1)
        binary, args, cwd = self.organizer.started[0]
        self.assertEqual(Path(binary), game_dir / "tools/redmod/bin/redMod.exe")
        self.assertIn("-modlist=", args[0])
        order = (self.organizer.overwrite / "r6/cache/modded/MO_REDmod_load_order.txt").read_text()
        self.assertEqual(order.splitlines(), ["Red", "Red2"])

        self.assertTrue(self.run_game())
        self.assertEqual(len(self.organizer.started), 1)  # unchanged: no deploy

        time.sleep(0.01)
        (red / "mods/Red/archives/a.archive").write_bytes(b"changed!")
        self.assertTrue(self.run_game())
        self.assertEqual(len(self.organizer.started), 2)

        # no -modded, or the setting off: no deploy
        (red / "mods/Red/archives/b.archive").write_bytes(b"new")
        self.run_game("--launcher-skip ")
        self.assertEqual(len(self.organizer.started), 2)

    def test_redmod_order_change_redeploys_afresh(self) -> None:
        self.install(["tools/redmod/bin/redMod.exe"])
        self.organizer.add_mod("Red", ["mods/Red/info.json"])
        self.organizer.add_mod("Red2", ["mods/Red2/info.json"])
        self.run_game()
        stale = self.organizer.overwrite / "r6/cache/modded/stale.bin"
        stale.write_text("x")
        self.organizer.mod_order.reverse()
        self.run_game()
        self.assertEqual(len(self.organizer.started), 2)
        self.assertFalse(stale.exists())

    def test_redmod_failure_asks(self) -> None:
        self.install(["tools/redmod/bin/redMod.exe"])
        self.organizer.add_mod("Red", ["mods/Red/info.json"])
        self.organizer.exit_code = 3
        answers = []

        def question(*args, **kwargs):
            answers.append(args[2])
            return QMessageBox.StandardButton.No

        original = QMessageBox.question
        QMessageBox.question = question
        try:
            self.assertFalse(self.run_game())
            self.assertIn("exit code 3", answers[0])
        finally:
            QMessageBox.question = original

    def test_no_redmod_cleans_overwrite_only(self) -> None:
        game_dir = self.install(["r6/cache/modded/outside.bin"])
        ours = self.organizer.overwrite / "r6/cache/modded/final.redscripts"
        ours.parent.mkdir(parents=True)
        ours.write_text("old deploy")
        self.assertTrue(self.run_game())
        self.assertFalse(ours.exists())
        self.assertTrue((game_dir / "r6/cache/modded/outside.bin").exists())

    def test_manual_redmod_deploy(self) -> None:
        game_dir = self.install(["tools/redmod/bin/redMod.exe"])
        self.organizer.add_mod("Red", ["mods/Red/info.json"])
        redmod = game_dir / "tools/redmod/bin/redMod.exe"
        started = self.game._on_about_to_run(str(redmod), QDir(str(redmod.parent)), "deploy -force %modlist%")
        self.assertFalse(started)  # MO2 started it again with the list
        self.assertIn('-modlist="', self.organizer.started[0][1][0])

    # r6/cache

    def test_cache_files_go_to_overwrite(self) -> None:
        game_dir = self.install()
        self.run_game()
        copied = self.organizer.overwrite / "r6/cache/final.redscripts"
        self.assertEqual(copied.read_bytes(), b"game:r6/cache/final.redscripts")
        # the game updated: Overwrite's copies are refreshed
        copied.write_bytes(b"compiled by redscript")
        (game_dir / "r6/cache/final.redscripts").write_bytes(b"game 2.31")
        self.run_game()
        self.assertEqual(copied.read_bytes(), b"game 2.31")
        self.assertEqual((game_dir / "r6/cache/final.redscripts").read_bytes(), b"game 2.31")

    # saves

    def test_saves(self) -> None:
        saves = self.tmp / "saves"
        for index, name in enumerate(["ManualSave-1", "QuickSave-2", "AutoSave-0"]):
            folder = saves / name
            folder.mkdir(parents=True)
            (folder / "sav.dat").write_bytes(b"x")
            os.utime(folder / "sav.dat", (1_700_000_000 + index * 100,) * 2)
        (saves / "ManualSave-1/NamedSave.txt").write_text("Before the heist\n", encoding="utf-8")
        (saves / "QuickSave-2/metadata.9.json").write_text(
            json.dumps({"Data": {"metadata": {"name": "QuickSave-2", "level": 12, "lifePath": "Nomad",
                                              "playthroughTime": 3725.5, "trackedQuestEntry": "Misión"}}}),
            encoding="utf-8",
        )
        (saves / "not-a-save").mkdir()
        (saves / "stray.dat").write_bytes(b"")
        listed = self.game.listSaves(QDir(str(saves)))
        self.assertEqual([save.getName() for save in listed], ["AutoSave-0", "QuickSave-2", "Before the heist"])
        info = plugin_module.parse_cyberpunk_save_metadata(saves / "QuickSave-2", listed[1])
        self.assertEqual(info["Level"], 12)
        self.assertEqual(info["Play Time"], "01:02:05")
        self.assertEqual(info["Quest"], "Misión")
        self.assertIsNone(plugin_module.parse_cyberpunk_save_metadata(saves / "AutoSave-0", listed[0]))

    def test_absurd_save_numbers_are_unreadable_not_a_crash(self) -> None:
        saves = self.tmp / "saves"
        folder = saves / "QuickSave-9"
        folder.mkdir(parents=True)
        (folder / "sav.dat").write_bytes(b"x")
        listed = self.game.listSaves(QDir(str(saves)))
        for body in ('{"Data": {"metadata": {"name": "Q", "playthroughTime": 1e999}}}',
                     '{"Data": {"metadata": {"name": "Q", "level": 1e999}}}',
                     '{"Data": {"metadata": {"name": "Q", "level": "' + "9" * 5000 + '"}}}'):
            (folder / "metadata.9.json").write_text(body, encoding="utf-8")
            self.assertIsNone(plugin_module.parse_cyberpunk_save_metadata(folder, listed[0]))


class FrameworkTest(unittest.TestCase):
    def test_codeware_import_regex(self) -> None:
        found = frameworks._CODEWARE_IMPORT.search
        self.assertTrue(found(b"// x\nimport Codeware.UI.*\n"))
        self.assertTrue(found(b"\n  \timport   Codeware\n"))
        self.assertFalse(found(b"importCodeware\n"))
        self.assertFalse(found(b"import CodewareX\n"))
        # a block of blank lines used to make each line start scan to the end
        start = time.monotonic()
        self.assertFalse(found(b"\n" * 20000 + b"import Other\n"))
        self.assertLess(time.monotonic() - start, 2)

    def test_needs(self) -> None:
        tree = make_tree([
            "r6/scripts/M/a.reds", "r6/tweaks/M/a.yaml", "archive/pc/mod/a.archive.xl",
            "bin/x64/plugins/cyber_engine_tweaks/mods/M/init.lua", "red4ext/plugins/M/M.dll",
            "r6/input/a.xml", "mods/R/info.json",
        ])
        self.assertEqual(
            frameworks.needed_by_tree(tree),
            {"redscript", "tweakxl", "archivexl", "cet", "red4ext", "input_loader"},
        )
        self.assertTrue(frameworks.uses_redmod(tree))
        tweakxl = make_tree(["red4ext/plugins/TweakXL/TweakXL.dll"])
        self.assertEqual(frameworks.provided_by_tree(tweakxl), {"tweakxl"})
        self.assertEqual(frameworks.needed_by_tree(tweakxl), {"red4ext", "redscript"})

    def test_report(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="cp77-fw-"))
        try:
            organizer = FakeOrganizer(tmp / "instance")
            game_dir = make_game_dir(tmp, ["red4ext/RED4ext.dll"])  # RED4ext in the game folder
            organizer.game_dir = game_dir
            organizer.add_mod("Needs TweakXL", ["r6/tweaks/x.yaml"])
            organizer.add_mod("Uses Codeware", ["r6/scripts/M/a.reds"])
            (organizer.mods / "Uses Codeware/r6/scripts/M/a.reds").write_text("import Codeware.UI.*\n")
            organizer.add_mod("Plugin", ["red4ext/plugins/P/P.dll"])
            organizer.add_mod("Red", ["mods/Red/info.json"])
            organizer.add_mod("redscript", ["engine/tools/scc.exe", "engine/config/base/scripts.ini"])
            organizer.add_mod("CET mod off", ["bin/x64/plugins/cyber_engine_tweaks/mods/M/init.lua"], active=False)

            from basic_games.games.cyberpunk2077.plugins import active_mod_files

            report = frameworks.analyze(active_mod_files(organizer), organizer.virtualFileTree(), game_dir)
            self.assertEqual(report.missing, {"tweakxl": ["Needs TweakXL"], "codeware": ["Uses Codeware"]})
            self.assertEqual(report.redmod_without_dlc, ["Red"])
            self.assertFalse(report.outside_redmod_deploy)

            game = Game()
            organizer.game = game
            game.init(organizer)
            game.setGamePath(game_dir)
            check = CyberpunkFrameworkCheck()
            check.init(organizer)
            # Codeware's users are read in the background: first without it
            problems = check.activeProblems()
            self.assertEqual(len(problems), 2)
            from PyQt6.QtCore import QCoreApplication

            deadline = time.time() + 10
            while (check._scan is not None or check._report is not None) and time.time() < deadline:
                QCoreApplication.processEvents()
                time.sleep(0.05)
            problems = check.activeProblems()
            self.assertEqual(len(problems), 3)
            texts = [check.fullDescription(key) for key in problems]
            self.assertIn("Needs TweakXL", texts[0])
            self.assertIn("nexusmods.com/cyberpunk2077/mods/4197", texts[0])
            self.assertIn("REDmod", check.shortDescription(problems[2]))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
