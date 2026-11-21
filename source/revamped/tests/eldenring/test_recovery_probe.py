"""The cheap check that makes a stubbed Elden Ring plugin run its real
init() in another game's instance, so hidden Steam saves still come back."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from basic_games import lazy_games
from basic_games.games.eldenring import recovery_probe


class RecoveryProbe(unittest.TestCase):
    def setUp(self):
        self.appdata = Path(tempfile.mkdtemp(prefix="er-appdata-"))
        (self.appdata / "EldenRing").mkdir()

    def test_nothing_pending(self):
        self.assertFalse(recovery_probe.pending(str(self.appdata)))
        # an empty install folder is no session
        (self.appdata / ".MO2RevampedSaveRecovery" / "abc").mkdir(parents=True)
        self.assertFalse(recovery_probe.pending(str(self.appdata)))

    def test_session_pending(self):
        session = self.appdata / ".MO2RevampedSaveRecovery" / "abc" / "0123"
        session.mkdir(parents=True)
        self.assertTrue(recovery_probe.pending(str(self.appdata)))

    def test_only_unrouted_output(self):
        # seen on 2026-11-21: a session of another install, restored, that
        # still kept the game's output from outside the route
        session = self.appdata / ".MO2RevampedSaveRecovery" / "abc" / "0123"
        (session / "unrouted-global" / "76561198000000001").mkdir(parents=True)
        (session / "session.json").write_text('{"version": 1}', encoding="utf-8")
        self.assertFalse(recovery_probe.pending(str(self.appdata)))
        # a swap whose saves are still in the Steam folder
        (session / "session.json").write_text('{"swap": {"returned": false}}', encoding="utf-8")
        self.assertTrue(recovery_probe.pending(str(self.appdata)))
        # an account folder set aside
        (session / "session.json").write_text('{"version": 1}', encoding="utf-8")
        (session / "76561198000000001").mkdir()
        self.assertTrue(recovery_probe.pending(str(self.appdata)))

    def test_matches_the_plugin(self):
        from basic_games.games.game_eldenring_mo2 import EldenRingMo2Game

        self.assertEqual(recovery_probe.RECOVERY_FOLDER, EldenRingMo2Game.SaveRecoveryDirectoryName)
        self.assertIn(EldenRingMo2Game.SaveFolderName, recovery_probe.SAVE_FOLDERS)
        self.assertEqual(recovery_probe.UNROUTED_FOLDER, EldenRingMo2Game.UnroutedSaveDirectoryName)
        self.assertEqual(recovery_probe.MANIFEST, EldenRingMo2Game.SaveRecoveryManifestName)
        module, function = EldenRingMo2Game.ManifestInitProbe.split(":")
        self.assertEqual(module, ".games.eldenring.recovery_probe")
        self.assertTrue(callable(getattr(recovery_probe, function)))


class StubInit(unittest.TestCase):
    """lazy_games: the stub's init() loads the real plugin when the check
    says so, and only then."""

    def entry(self, probe):
        entry = {"class": "EldenRingMo2Game", "answers": {"gameName": "ELDEN RING", "name": "x"},
                 "attributes": {"Name": "Elden Ring MO2 Support", "Author": "x", "Version": "1.0.0",
                                "GameName": "ELDEN RING", "GameShortName": "eldenring",
                                "GameBinary": "Game/eldenring.exe", "GameDataPath": "Game"},
                 "version": "1.0.0", "settings": [], "overrides": []}
        if probe is not None:
            entry["init_probe"] = probe
        return entry

    def run_init(self, probe, says):
        cls = lazy_games.make_stub_class(".games.game_eldenring_mo2", self.entry(probe))
        stub = cls()
        loaded = []
        stub._real = lambda: loaded.append(True)
        saved_init, saved_probe = lazy_games.BasicGame.init, lazy_games._init_probe_says_so
        lazy_games.BasicGame.init = lambda self, organizer: True
        lazy_games._init_probe_says_so = lambda value: says and value is not None
        try:
            self.assertTrue(stub.init(None))
        finally:
            lazy_games.BasicGame.init, lazy_games._init_probe_says_so = saved_init, saved_probe
        return bool(loaded)

    def test_stub_init(self):
        probe = ".games.eldenring.recovery_probe:pending"
        self.assertTrue(self.run_init(probe, True))
        self.assertFalse(self.run_init(probe, False))
        self.assertFalse(self.run_init(None, True))

    def test_probe(self):
        self.assertTrue(lazy_games._init_probe_says_so(".games.eldenring.recovery_probe:pending")
                        in (True, False))
        self.assertFalse(lazy_games._init_probe_says_so(None))
        self.assertFalse(lazy_games._init_probe_says_so("nonsense"))
        # a check that fails: the real init() decides
        self.assertTrue(lazy_games._init_probe_says_so(".games.missing_module:pending"))
