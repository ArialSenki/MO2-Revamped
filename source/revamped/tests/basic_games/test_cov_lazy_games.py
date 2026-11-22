"""basic_games' lazy start, its less trodden paths: the manifest's checks,
stubs handing calls to the real game (paths, variants, an init probe, a
detection pending), and BasicGame's mappings and store ids for every store,
with the paths Proton hands. No store is scanned for real: the lists are set
by the tests."""

from __future__ import annotations

import io
import json
import shutil
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QCoreApplication, QDir

from basic_games import basic_game, lazy_games
from basic_games.basic_game import BasicGame, BasicGameMapping, BasicGameMappings, replace_variables
from fake_organizer import FakeOrganizer

STORES = ("steam_games", "gog_games", "origin_games", "epic_games", "eadesktop_games")


class StoreLists(unittest.TestCase):
    """Sets every store's list, and puts the lazy ones back afterwards."""

    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg-cov-"))
        self.saved = {name: vars(BasicGame)[name] for name in STORES}
        self.saved_setup = vars(BasicGame)["setup"]
        BasicGame.setup = staticmethod(lambda: self.fail("a store scan"))
        for name in STORES:
            setattr(BasicGame, name, {})

    def tearDown(self) -> None:
        for name, value in self.saved.items():
            setattr(BasicGame, name, value)
        BasicGame.setup = self.saved_setup
        shutil.rmtree(self.tmp, ignore_errors=True)


class CovGame(BasicGame):
    Name = "Cov Game Support"
    Author = "tests"
    Version = "1.0.0"
    GameName = "Cov Game"
    GameShortName = "covgame"
    GameBinary = "cov.exe"
    GameDataPath = "Data"
    GameSteamId = [4242, "4243"]
    GameGogId = 77
    GameOriginManifestIds = "OFB-EAST:1"
    GameEpicId = "epicid"
    GameEaDesktopId = "eaid"
    GameLauncher = "launcher.exe"
    GameIniFiles = "a.ini, b.ini"
    GameValidShortNames = "cov, covalt"
    GameSaveExtension = "cov"


class DetectingGame(CovGame):
    """A game with its own detection (a shortcut, a registry key)."""

    found = ""
    made: list = []

    def __init__(self):
        super().__init__()
        self.calls: list = []
        DetectingGame.made.append(self)

    def init(self, organizer) -> bool:
        self.calls.append("init")
        return True

    def detectGame(self):
        self.calls.append("detect")
        if DetectingGame.found:
            self.setGamePath(DetectingGame.found)

    def setGameVariant(self, variant: str) -> None:
        self.calls.append(("variant", variant))

    def executables(self):
        return ["from the real game"]


# the game module a stub imports, and an init probe
_FAKE_MODULE = "basic_games.games._cov_fake_game"
_PROBE_MODULE = "basic_games.games._cov_probe"


def stub_entry(cls=DetectingGame, **extra):
    entry = {
        "class": cls.__name__,
        "attributes": {k: v for k, v in vars(CovGame).items() if k[0].isupper()},
        "answers": {"gameName": "Cov Game", "name": "Cov Game Support", "author": "tests"},
        "version": "1.2.3",
        "settings": [["a_key", "A setting", True], ["b_key", "B", 5]],
        "overrides": ["detectGame", "executables", "setGameVariant", "init"],
    }
    entry.update(extra)
    return entry


class LazyGamesTest(StoreLists):
    def setUp(self) -> None:
        super().setUp()
        module = types.ModuleType(_FAKE_MODULE)
        module.DetectingGame = DetectingGame
        sys.modules[_FAKE_MODULE] = module
        probe = types.ModuleType(_PROBE_MODULE)
        probe.yes = lambda: True
        probe.no = lambda: False

        def broken():
            raise OSError("no saves folder")

        probe.broken = broken
        sys.modules[_PROBE_MODULE] = probe
        DetectingGame.made = []
        DetectingGame.found = ""

    def tearDown(self) -> None:
        sys.modules.pop(_FAKE_MODULE, None)
        sys.modules.pop(_PROBE_MODULE, None)
        super().tearDown()

    def stub(self, **extra):
        return lazy_games.make_stub_class(".games._cov_fake_game", stub_entry(**extra))()

    def test_instance_game(self) -> None:
        app = QCoreApplication.instance()
        try:
            self.assertIsNone(lazy_games.instance_game())
            app.setProperty("revampedInstanceGame", "Valheim")
            self.assertEqual(lazy_games.instance_game(), "Valheim")
            app.setProperty("revampedInstanceGame", "")
            self.assertIsNone(lazy_games.instance_game())
            app.setProperty("revampedInstanceGame", 5)
            self.assertIsNone(lazy_games.instance_game())
        finally:
            app.setProperty("revampedInstanceGame", None)
        with mock.patch.object(QCoreApplication, "instance", return_value=None):
            self.assertIsNone(lazy_games.instance_game())
        with mock.patch.object(QCoreApplication, "instance", side_effect=RuntimeError("no Qt")):
            self.assertIsNone(lazy_games.instance_game())

    def test_file_hash(self) -> None:
        path = self.tmp / "game_x.py"
        path.write_bytes(b"abc")
        self.assertEqual(lazy_games.file_hash(str(path)),
                         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")

    def test_read_manifest(self) -> None:
        good = {
            "format": 1,
            "modules": {"game_x.py": {"sha256": "00", "classes": [{"class": "X", "answers": {"gameName": "X"}}]}},
            "tools": {"tool.py": ["X"]},
        }

        def read(value):
            path = self.tmp / "manifest.json"
            if isinstance(value, bytes):
                path.write_bytes(value)
            else:
                path.write_text(json.dumps(value), encoding="utf-8")
            return lazy_games.read_manifest(str(self.tmp))

        self.assertEqual(read(good), good)
        # no tools listed is fine
        self.assertEqual(read({k: v for k, v in good.items() if k != "tools"})["format"], 1)
        bad = [
            b"{ broken",
            b"\xff\xfe\x00",
            [],
            dict(good, format=2),
            dict(good, modules=[]),
            dict(good, tools=[]),
            dict(good, modules={"g.py": "x"}),
            dict(good, modules={"g.py": {"sha256": 5, "classes": []}}),
            dict(good, modules={"g.py": {"sha256": "0", "classes": {}}}),
            dict(good, modules={"g.py": {"sha256": "0", "classes": ["X"]}}),
            dict(good, modules={"g.py": {"sha256": "0", "classes": [{"class": "X", "answers": {}}]}}),
            dict(good, modules={"g.py": {"sha256": "0", "classes": [{"class": 1, "answers": {"gameName": "X"}}]}}),
            dict(good, tools={"t.py": "X"}),
            dict(good, tools={"t.py": [1]}),
        ]
        for value in bad:
            with self.subTest(value=value):
                self.assertIsNone(read(value))
        self.assertIsNone(lazy_games.read_manifest(str(self.tmp / "missing")))

    def test_answers_without_the_module(self) -> None:
        stub = self.stub()
        self.assertEqual(stub.version().canonicalString(), mobase.VersionInfo("1.2.3").canonicalString())
        self.assertEqual([(s.key, s.default_value) for s in stub.settings()], [("a_key", True), ("b_key", 5)])
        self.assertEqual(stub.author(), "tests")
        self.assertEqual(DetectingGame.made, [])
        # its mappings, when something asks or sets them
        mappings = BasicGameMappings(stub)
        stub._mappings = mappings
        self.assertIs(stub._mappings, mappings)

    def test_translated_description_isnt_answered(self) -> None:
        entry = stub_entry()
        entry["attributes"]["DescriptionContext"] = "CovGame"
        entry["attributes"]["Description"] = "Cov text"
        entry["answers"]["description"] = "the untranslated answer"
        stub = lazy_games.make_stub_class(".games._cov_fake_game", entry)()
        self.assertEqual(stub.description(), "Cov text")
        self.assertNotIn("description", vars(type(stub)))

    def test_forwarding_hands_path_variant_and_organizer(self) -> None:
        organizer = FakeOrganizer(self.tmp / "instance")
        stub = self.stub()
        self.assertTrue(stub.init(organizer))
        stub.setGameVariant("Steam")
        stub.setGamePath(self.tmp)
        self.assertEqual(DetectingGame.made, [])
        self.assertEqual(stub.executables(), ["from the real game"])
        [real] = DetectingGame.made
        self.assertEqual(real.calls, ["init", ("variant", "Steam")])
        self.assertEqual(Path(real.gameDirectory().absolutePath()), self.tmp)
        # later changes reach the real game too
        other = self.tmp / "other"
        stub.setGamePath(other)
        stub.setGameVariant("GOG")
        self.assertEqual(Path(real.gameDirectory().absolutePath()), other)
        self.assertEqual(real.calls[-1], ("variant", "GOG"))
        self.assertEqual(len(DetectingGame.made), 1)

    def test_a_detection_pending_when_a_call_is_forwarded(self) -> None:
        DetectingGame.found = str(self.tmp)
        stub = self.stub()
        stub.init(None)
        stub.detectGame()
        self.assertEqual(stub.executables(), ["from the real game"])
        [real] = DetectingGame.made
        self.assertEqual(real.calls, ["detect"])
        self.assertEqual(Path(stub.gameDirectory().absolutePath()), self.tmp)
        # nothing found: no path
        DetectingGame.found = ""
        DetectingGame.made = []
        empty = self.stub()
        empty.detectGame()
        empty.executables()
        self.assertFalse(empty.isInstalled())

    def test_game_directory_through_the_real_class(self) -> None:
        stub = self.stub(overrides=["setGamePath"])
        self.assertEqual(stub.gameDirectory().path(), QDir("").path())
        self.assertEqual(DetectingGame.made, [])
        stub.setGamePath(self.tmp)
        self.assertEqual(Path(stub.gameDirectory().absolutePath()), self.tmp)
        self.assertEqual(len(DetectingGame.made), 1)

    def test_origin_watcher_and_init_probes(self) -> None:
        organizer = FakeOrganizer(self.tmp / "instance")
        entry = stub_entry()
        entry["attributes"]["GameOriginWatcherExecutables"] = ["cov.exe"]
        stub = lazy_games.make_stub_class(".games._cov_fake_game", entry)()
        self.assertTrue(stub.init(organizer))
        self.assertTrue(hasattr(stub, "origin_watcher"))
        self.assertEqual(DetectingGame.made, [])
        # the probe says the real init() has work here
        self.assertTrue(self.stub(init_probe=".games._cov_probe:yes").init(organizer))
        self.assertEqual(len(DetectingGame.made), 1)
        self.assertTrue(self.stub(init_probe=".games._cov_probe:no").init(organizer))
        self.assertTrue(self.stub(init_probe="no colon").init(organizer))
        self.assertTrue(self.stub(init_probe=5).init(organizer))
        self.assertEqual(len(DetectingGame.made), 1)
        # a failing probe: unsure, the real init() decides
        errors = io.StringIO()
        with redirect_stderr(errors):
            self.assertTrue(self.stub(init_probe=".games._cov_probe:broken").init(organizer))
            self.assertTrue(self.stub(init_probe=".games._cov_no_such_module:x").init(organizer))
        self.assertEqual(len(DetectingGame.made), 3)
        self.assertIn("no saves folder", errors.getvalue())
        # the real game fails to load: the stub stays
        sys.modules.pop(_FAKE_MODULE)
        errors = io.StringIO()
        with redirect_stderr(errors):
            self.assertTrue(self.stub(init_probe=".games._cov_probe:yes").init(organizer))
        self.assertIn("its init() failed", errors.getvalue())


class MappingsTest(StoreLists):
    def test_values_with_variables(self) -> None:
        game = CovGame()
        game.setGamePath(self.tmp)
        documents = self.tmp / "docs"
        documents.mkdir()
        with mock.patch.object(CovGame, "documentsDirectory", lambda self: QDir(str(documents))):
            value = replace_variables("%GAME_PATH%/x|%GAME_DOCUMENTS%/y|%DOCUMENTS%|%USERPROFILE%", game)
        game_path, game_docs, docs, profile = value.split("|")
        self.assertEqual(Path(game_path), self.tmp / "x")
        self.assertEqual(Path(game_docs), documents / "y")
        self.assertNotIn("%", docs + profile)

    def test_mapping_conversions(self) -> None:
        game = CovGame()
        self.assertEqual(game.iniFiles(), ["a.ini", "b.ini"])
        self.assertEqual(game.validShortNames(), ["cov", "covalt"])
        self.assertEqual(game.getLauncherName(), "launcher.exe")
        self.assertEqual(game.getSupportURL(), "")
        self.assertEqual(game.version().canonicalString(), mobase.VersionInfo("1.0.0").canonicalString())
        self.assertEqual(game.nexusModOrganizerID(), 0)
        self.assertEqual(game.author(), "tests")
        self.assertEqual(game.description(), "Adds basic support for game Cov Game.")
        self.assertEqual(game._mappings.originWatcherExecutables.get(), [])
        # a Path value comes back as text, a QDir with its variables replaced
        game.SomePath = self.tmp / "a"
        self.assertEqual(BasicGameMapping(game, "SomePath", "name").get(), str(self.tmp / "a"))
        game.SomeDir = QDir("%GAME_PATH%/saves")
        game.setGamePath(self.tmp)
        self.assertEqual(Path(BasicGameMapping(game, "SomeDir", "name").get().path()), self.tmp / "saves")

    def test_bad_and_missing_properties(self) -> None:
        class BadId(CovGame):
            GameNexusId = "not a number"

        with self.assertRaisesRegex(ValueError, "invalid GameNexusId"):
            BadId()

        class NoName(BasicGame):
            Name = "x"
            Author = "x"
            Version = "1.0"
            GameShortName = "x"
            GameBinary = "x.exe"
            GameDataPath = ""

        with self.assertRaisesRegex(ValueError, "missing GameName"):
            NoName()

        class OwnName(NoName):
            def gameName(self) -> str:
                return "Own"

        self.assertEqual(OwnName().gameName(), "Own")

    def test_options(self) -> None:
        game = CovGame()
        ids = game._mappings.steamAPPId
        self.assertEqual(ids.current(), "4242")
        ids.set_index(1)
        self.assertEqual(ids.current(), "4243")
        ids.set_value("9999")
        self.assertFalse(ids.has_value())
        self.assertEqual(ids.current(), "4242")
        # no ids at all: the default
        empty = basic_game.BasicGameOptionsMapping(game, "NoSuchIds", "steamAPPId", default=lambda g: "none")
        self.assertEqual(empty.current(), "none")
        game.setGamePath(self.tmp)
        game.SomeDirs = [QDir("%GAME_PATH%/a")]
        dirs = basic_game.BasicGameOptionsMapping(game, "SomeDirs", "steamAPPId", default=lambda g: "")
        self.assertEqual(Path(dirs.current().path()), self.tmp / "a")
        game.SomeInts = [5]
        self.assertEqual(basic_game.BasicGameOptionsMapping(game, "SomeInts", "steamAPPId").current(), 5)

    def test_default_documents_directory(self) -> None:
        game = CovGame()
        documents = self.tmp / "Documents"
        with mock.patch.object(basic_game.QStandardPaths, "writableLocation", return_value=str(documents)):
            self.assertEqual(game.documentsDirectory().path(), QDir().path())
            (documents / "Cov Game").mkdir(parents=True)
            self.assertEqual(Path(game.documentsDirectory().absolutePath()), documents / "Cov Game")
            (documents / "My Games/Cov Game").mkdir(parents=True)
            self.assertEqual(Path(game.documentsDirectory().absolutePath()), documents / "My Games/Cov Game")
            self.assertEqual(Path(game.savesDirectory().absolutePath()), documents / "My Games/Cov Game")


class StoresTest(StoreLists):
    def test_every_store(self) -> None:
        for store, ids, getter, check in (
            ("gog_games", "77", "gogAPPId", "is_gog"),
            ("origin_games", "OFB-EAST:1", None, "is_origin"),
            ("epic_games", "epicid", "epicAPPId", "is_epic"),
            ("eadesktop_games", "eaid", "eaDesktopContentId", "is_eadesktop"),
        ):
            with self.subTest(store=store):
                for name in STORES:
                    setattr(BasicGame, name, {})
                setattr(BasicGame, store, {ids: self.tmp})
                game = CovGame()
                game.detectGame()
                self.assertEqual(Path(game.gameDirectory().absolutePath()), self.tmp)
                self.assertTrue(getattr(game, check)())
                self.assertFalse(game.is_steam())
                if getter:
                    self.assertEqual(getattr(game, getter)(), ids)

    def test_not_in_any_store(self) -> None:
        game = CovGame()
        game.detectGame()
        self.assertFalse(game.isInstalled())
        for check in ("is_steam", "is_gog", "is_origin", "is_epic", "is_eadesktop"):
            self.assertFalse(getattr(game, check)())

    def test_proton_paths(self) -> None:
        # Steam's library under Proton, as Wine shows it; MO2 may hand the
        # same folder with forward slashes or another case
        BasicGame.steam_games = {"4243": Path(r"Z:\home\deck\.local\share\Steam\steamapps\common\Cov Game")}
        game = CovGame()
        game.setGamePath("z:/HOME/deck/.local/share/Steam/steamapps/common/Cov Game/")
        self.assertTrue(game.is_steam())
        self.assertEqual(game.steamAPPId(), "4243")

    def test_a_scan_with_failing_stores(self) -> None:
        from PyQt6.QtWidgets import QMessageBox

        from basic_games import eadesktop_utils, epic_utils, gog_utils, origin_utils, steam_utils

        def raises(*args):
            raise OSError("unreadable")

        shown = []
        with mock.patch.object(steam_utils, "find_games", lambda: {"1": self.tmp}), \
             mock.patch.object(gog_utils, "find_games", raises), \
             mock.patch.object(origin_utils, "find_games", lambda: {}), \
             mock.patch.object(epic_utils, "find_games", raises), \
             mock.patch.object(eadesktop_utils, "find_games", lambda errors: {"e": self.tmp}), \
             mock.patch.object(QMessageBox, "critical", lambda *args: shown.append(args[2])):
            BasicGame._scan_stores()
            # what failed is said once the scan's lock is let go
            basic_game._report_store_errors()
        self.assertEqual(BasicGame.steam_games, {"1": self.tmp})
        self.assertEqual(BasicGame.gog_games, {})
        self.assertEqual(BasicGame.epic_games, {})
        self.assertEqual(BasicGame.eadesktop_games, {"e": self.tmp})
        self.assertEqual(len(shown), 1)
        self.assertIn("Gog", shown[0])
        self.assertIn("Epic", shown[0])


class GameTest(StoreLists):
    def test_init_with_an_origin_watcher(self) -> None:
        class OriginGame(CovGame):
            GameOriginWatcherExecutables = "cov.exe"

        organizer = FakeOrganizer(self.tmp / "instance")
        game = OriginGame()
        self.assertTrue(game.init(organizer))
        self.assertEqual(game.origin_watcher.executables, ["cov.exe"])
        errors = io.StringIO()
        with redirect_stderr(errors):
            with mock.patch.object(organizer, "onAboutToRun", return_value=False):
                self.assertFalse(OriginGame().init(organizer))
            with mock.patch.object(organizer, "onFinishedRun", return_value=False):
                self.assertFalse(OriginGame().init(organizer))
        self.assertIn("onAboutToRun", errors.getvalue())
        self.assertIn("onFinishedRun", errors.getvalue())

    def test_is_active(self) -> None:
        organizer = FakeOrganizer(self.tmp / "instance")
        game = CovGame()
        game.init(organizer)
        self.assertFalse(game.isActive())
        organizer.game = game
        self.assertTrue(game.isActive())

    def test_folders_executables_saves(self) -> None:
        game = CovGame()
        game.setGamePath(self.tmp)
        (self.tmp / "cov.exe").write_text("x")
        self.assertTrue(game.looksValid(QDir(str(self.tmp))))
        self.assertFalse(game.looksValid(QDir(str(self.tmp / "none"))))
        self.assertEqual(Path(game.dataDirectory().absolutePath()), self.tmp / "Data")
        self.assertEqual([Path(e.binary().absoluteFilePath()).name for e in game.executables()],
                         ["launcher.exe", "cov.exe"])
        self.assertEqual(game.executableForcedLoads(), [])
        game.setGameVariant("x")
        saves = self.tmp / "saves"
        (saves / "slot").mkdir(parents=True)
        (saves / "slot/one.cov").write_text("x")
        (saves / "two.sav").write_text("x")
        self.assertEqual([Path(s.getFilepath()).name for s in game.listSaves(QDir(str(saves)))], ["one.cov"])

    def test_initialize_profile(self) -> None:
        game = CovGame()
        documents = self.tmp / "docs"
        documents.mkdir()
        (documents / "a.ini").write_text("[a]\nx=1\n")
        profile = self.tmp / "profile"
        profile.mkdir()
        with mock.patch.object(CovGame, "documentsDirectory", lambda self: QDir(str(documents))):
            game.initializeProfile(QDir(str(profile)), mobase.ProfileSetting.CONFIGURATION)
        self.assertEqual((profile / "a.ini").read_text(), "[a]\nx=1\n")
        # missing in the documents: an empty one
        self.assertEqual((profile / "b.ini").read_text(), "")
        (profile / "a.ini").unlink()
        game.initializeProfile(QDir(str(profile)), mobase.ProfileSetting.MODS)
        self.assertFalse((profile / "a.ini").exists())


if __name__ == "__main__":
    unittest.main()
