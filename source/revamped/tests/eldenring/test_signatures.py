"""Every signature of the bridge and me3_compat against every backed-up game
executable (signatures.py). Slow (about a minute): it runs only with
ER_SIGNATURES=1, or ER_TEST_PATTERN=test_signatures.py."""

from __future__ import annotations

import os
import unittest

import signatures

WANTED = os.environ.get("ER_SIGNATURES") == "1" or os.environ.get(
    "ER_TEST_PATTERN", ""
) == "test_signatures.py"


@unittest.skipUnless(WANTED, "set ER_SIGNATURES=1 to scan the backed-up executables")
class Signatures(unittest.TestCase):
    def test_every_backup(self):
        executables = signatures.backup_executables()
        if not executables:
            self.skipTest(f"no executables below {signatures.BACKUPS}")
        for path in executables:
            report = signatures.scan(path)
            with self.subTest(executable=str(path), version=report.image.file_version):
                self.assertEqual(
                    [f"{r.name}: {r.detail}" for r in report.failures], [], str(path)
                )


@unittest.skipUnless(WANTED, "set ER_SIGNATURES=1 to scan the backed-up executables")
class PluginPrecheck(unittest.TestCase):
    """The game plugin's own check before a launch (file_loader_found) says
    yes for every version, also through its fallback alone."""

    def test_file_loader_found(self):
        from basic_games.games.game_eldenring_mo2 import EldenRingMo2Game

        executables = signatures.backup_executables()
        if not executables:
            self.skipTest(f"no executables below {signatures.BACKUPS}")
        saved = EldenRingMo2Game.ResolverCallSites
        try:
            for exact in (saved, ()):
                EldenRingMo2Game.ResolverCallSites = exact
                for path in executables:
                    with self.subTest(executable=str(path), exact=bool(exact)):
                        self.assertTrue(EldenRingMo2Game.file_loader_found(path))
        finally:
            EldenRingMo2Game.ResolverCallSites = saved
