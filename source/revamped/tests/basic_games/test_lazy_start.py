"""What a start costs basic_games for the games MO2 doesn't manage: no store
scan, no game module, no mappings, until something asks where a game is."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import mobase

from basic_games import basic_game, lazy_games
from basic_games.basic_game import BasicGame

STORES = ("steam_games", "gog_games", "origin_games", "epic_games", "eadesktop_games")

# as the package left them when it was imported (run_tests imports nothing
# else of basic_games first): nothing scanned yet
LAZY_AFTER_IMPORT = all(isinstance(vars(BasicGame)[name], basic_game._StoreGames) for name in STORES)


def entry(**attributes):
    values = {
        "Name": "Fake Game Support",
        "Author": "tests",
        "Version": "1.0.0",
        "GameName": "Fake Game",
        "GameShortName": "fakegame",
        "GameBinary": "fake.exe",
        "GameDataPath": "",
    }
    values.update(attributes)
    return {
        "class": "FakeGame",
        "attributes": values,
        "answers": {"gameName": values["GameName"], "name": values["Name"]},
        "version": "1.0.0",
        "settings": [],
        "overrides": [],
    }


class FakeGame(BasicGame):
    Name = "Fake Game Support"
    Author = "tests"
    Version = "1.0.0"
    GameName = "Fake Game"
    GameShortName = "fakegame"
    GameBinary = "fake.exe"
    GameDataPath = ""
    GameSteamId = 4242


class LazyStart(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp(prefix="fake-game-"))
        self.scans = 0
        self.saved_setup = BasicGame.__dict__["setup"]

        def fake_setup():
            self.scans += 1
            for name in STORES:
                setattr(BasicGame, name, {})
            BasicGame.steam_games = {"4242": self.folder}

        BasicGame.setup = staticmethod(fake_setup)
        # the lazy lists, as after the package is imported
        for name in STORES:
            lazy = basic_game._StoreGames()
            lazy.__set_name__(BasicGame, name)
            setattr(BasicGame, name, lazy)

    def tearDown(self):
        BasicGame.setup = self.saved_setup

    def test_import_scans_nothing(self):
        self.assertTrue(LAZY_AFTER_IMPORT)

    def test_a_failing_store_is_reported(self):
        # the real setup(), with Steam's library files unreadable: the error
        # goes to the message box with the others, the other stores are scanned
        from PyQt6.QtWidgets import QMessageBox

        from basic_games import gog_utils, steam_utils

        BasicGame.setup = self.saved_setup
        shown = []
        saved = (steam_utils.find_games, gog_utils.find_games, QMessageBox.critical)

        def broken():
            raise ValueError("bad libraryfolders.vdf")

        steam_utils.find_games = broken
        gog_utils.find_games = lambda: {"1": self.folder}
        QMessageBox.critical = staticmethod(lambda *args: shown.append(args[2]))
        try:
            self.assertEqual(BasicGame.gog_games, {"1": self.folder})
            self.assertEqual(BasicGame.steam_games, {})
        finally:
            steam_utils.find_games, gog_utils.find_games, QMessageBox.critical = saved
        self.assertEqual(len(shown), 1)
        self.assertIn("bad libraryfolders.vdf", shown[0])

    def test_detect_waits_for_the_path(self):
        game = FakeGame()
        game.detectGame()
        self.assertEqual(self.scans, 0)
        self.assertTrue(game.isInstalled())
        self.assertEqual(self.scans, 1)
        self.assertEqual(Path(game.gameDirectory().absolutePath()), self.folder)
        self.assertEqual(game.steamAPPId(), "4242")
        self.assertTrue(game.is_steam())
        self.assertEqual(self.scans, 1)

    def test_a_given_path_needs_no_scan(self):
        # what MO2 does with the managed game: detectGame(), then the instance's folder
        game = FakeGame()
        game.detectGame()
        other = Path(tempfile.mkdtemp(prefix="instance-game-"))
        game.setGamePath(str(other))
        self.assertTrue(game.isInstalled())
        self.assertEqual(Path(game.gameDirectory().absolutePath()), other)
        self.assertEqual(self.scans, 0)
        # its store id is looked up when asked for: not a store folder here
        self.assertFalse(game.is_steam())
        self.assertEqual(self.scans, 1)

    def test_store_id_of_a_given_path(self):
        game = FakeGame()
        game.setGamePath(str(self.folder))
        self.assertEqual(self.scans, 0)
        self.assertTrue(game.is_steam())
        self.assertEqual(game.steamAPPId(), "4242")

    def test_stub_builds_nothing_at_start(self):
        stub = lazy_games.make_stub_class(".games.game_fake", entry(GameSteamId=4242))()
        # what MO2 asks every game plugin when it loads them
        stub.init(None)
        stub.detectGame()
        self.assertEqual(stub.name(), "Fake Game Support")
        self.assertEqual(stub.gameName(), "Fake Game")
        self.assertEqual(stub.version().canonicalString(), mobase.VersionInfo("1.0.0").canonicalString())
        self.assertEqual(stub.settings(), [])
        self.assertEqual(self.scans, 0)
        self.assertNotIn("_revamped_stub_mappings", stub.__dict__)
        self.assertIsNone(stub._real_game)
        # the instance window asks: detected from the stores, without the module
        self.assertTrue(stub.isInstalled())
        self.assertEqual(Path(stub.gameDirectory().absolutePath()), self.folder)
        self.assertEqual(stub.steamAPPId(), "4242")
        self.assertIsNone(stub._real_game)
        self.assertEqual(self.scans, 1)

    def test_stub_of_a_class_with_its_own_detection(self):
        # Black & White 2 reads a Start menu shortcut in detectGame()
        from basic_games.games import game_blackandwhite2 as module

        made = []
        saved_init = module.BlackAndWhite2Game.__init__
        saved_detect = module.BlackAndWhite2Game.detectGame
        folder = self.folder

        def counting_init(game):
            made.append(game)
            saved_init(game)

        def detect_from_shortcut(game):
            # what its shortcut reading finds on a machine with the game
            BasicGame.detectGame(game)
            game.setGamePath(str(folder))

        module.BlackAndWhite2Game.__init__ = counting_init
        module.BlackAndWhite2Game.detectGame = detect_from_shortcut
        try:
            stub_entry = entry(Name="Black & White 2 Support Plugin", GameName="Black & White 2",
                               GameShortName="BW2", GameBinary="white.exe")
            stub_entry["class"] = "BlackAndWhite2Game"
            stub_entry["overrides"] = ["detectGame", "executables", "listSaves"]
            stub = lazy_games.make_stub_class(".games.game_blackandwhite2", stub_entry)()
            stub.init(None)
            stub.detectGame()
            self.assertEqual(made, [])
            # the path its own detection found reaches the stub
            self.assertTrue(stub.isInstalled())
            self.assertEqual(len(made), 1)
            self.assertIs(stub._real_game, made[0])
            self.assertEqual(Path(stub.gameDirectory().absolutePath()), folder)
            # asked again: the same real object
            stub.isInstalled()
            stub.gameDirectory()
            self.assertEqual(len(made), 1)
            self.assertEqual(self.scans, 0)
        finally:
            module.BlackAndWhite2Game.__init__ = saved_init
            module.BlackAndWhite2Game.detectGame = saved_detect

    def test_save_game_names_still_importable(self):
        # not a name of the module until a game module asks for it
        self.assertNotIn("BasicGameSaveGame", vars(basic_game))
        from basic_games.basic_game import BasicGameSaveGame, BasicGameSaveGameInfo

        self.assertTrue(callable(BasicGameSaveGame))
        self.assertTrue(callable(BasicGameSaveGameInfo))


if __name__ == "__main__":
    unittest.main()
