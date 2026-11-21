"""redscript's compile log and the problem report built from it."""

from __future__ import annotations

import shutil
import tempfile
import time
import unittest
from pathlib import Path

from basic_games.games.cyberpunk2077 import redscript_log
from basic_games.games.cyberpunk2077.plugins import REDSCRIPT_ERRORS, CyberpunkFrameworkCheck
from fake_organizer import FakeOrganizer
from test_plugin import Game, make_game_dir

GAME = r"C:\Games\Cyberpunk 2077"

FAILED = rf"""[INFO - Fri, 20 Nov 2026 04:16:58 +0100] Using defaults for the script manifest
[INFO - Fri, 20 Nov 2026 04:16:58 +0100] Compiling files:
{GAME}\r6\scripts\Good\ok.reds
{GAME}\r6\scripts\Broken Mod\main.reds
[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] At {GAME}\r6\scripts\Broken Mod\main.reds:12:30:
    let x: Int32 = GetMissingThing();
                   ^^^^^^^^^^^^^^^
function GetMissingThing is not defined
[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] At {GAME}\r6\scripts\Broken Mod\main.reds:12:30:
    duplicate of the first error
[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] At {GAME}\red4ext\plugins\Old Plugin\Scripts\x.reds:3:1:
    class Foo is already defined
[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] REDscript compilation has failed.
"""

PASSED = rf"""[INFO - Fri, 20 Nov 2026 04:16:58 +0100] Compiling files:
{GAME}\r6\scripts\Good\ok.reds
[WARN - Fri, 20 Nov 2026 04:17:00 +0100] At {GAME}\r6\scripts\Good\ok.reds:1:1:
    unused variable
[INFO - Fri, 20 Nov 2026 04:17:00 +0100] Compilation complete
[INFO - Fri, 20 Nov 2026 04:17:00 +0100] Output successfully saved to {GAME}\r6\cache\final.redscripts.modded
"""


class RedscriptLogTest(unittest.TestCase):
    def test_parse_failed_log(self) -> None:
        self.assertTrue(redscript_log.failed(FAILED))
        errors = redscript_log.parse(FAILED, GAME)
        self.assertEqual(
            [(e.script, e.line) for e in errors],
            [("r6/scripts/Broken Mod/main.reds", 12), ("red4ext/plugins/Old Plugin/Scripts/x.reds", 3)],
        )
        self.assertIn("GetMissingThing is not defined", errors[0].message)
        self.assertNotIn("^^^", errors[0].message)

    def test_relative_script_path(self) -> None:
        log = (
            "[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] At Broken Mod\\main.reds:7:1:\n"
            "    unknown type Foo\n"
            "[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] REDscript compilation has failed.\n"
        )
        self.assertTrue(redscript_log.failed(log))
        self.assertEqual(
            [(e.script, e.line) for e in redscript_log.parse(log, GAME)],
            [("r6/scripts/Broken Mod/main.reds", 7)],
        )

    def test_long_lines_finish_fast(self) -> None:
        for filler in ("//" * 40000, " a" * 40000, "C:\\" * 30000, "x " * 40000):
            log = "[ERROR - Fri, 20 Nov 2026 04:17:00 +0100] " + filler + "\n" + filler + "\n" * 3
            log = log * 30
            start = time.monotonic()
            redscript_log.parse(log, GAME)
            redscript_log.failed(log)
            self.assertLess(time.monotonic() - start, 2)

    def test_long_log_keeps_its_tail(self) -> None:
        log = "[INFO - Fri, 20 Nov 2026 04:16:58 +0100] start\n" * 60000 + FAILED
        self.assertTrue(redscript_log.failed(log))
        self.assertEqual(len(redscript_log.parse(log, GAME)), 2)

    def test_passed_log(self) -> None:
        self.assertFalse(redscript_log.failed(PASSED))
        self.assertEqual(redscript_log.parse(PASSED, GAME), [])

    def test_report_names_the_mod(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="cp77-reds-"))
        try:
            organizer = FakeOrganizer(tmp / "instance")
            game_dir = make_game_dir(tmp)
            organizer.game_dir = game_dir
            organizer.add_mod("Broken Mod v2", ["r6/scripts/Broken Mod/main.reds"])
            game = Game()
            organizer.game = game
            game.init(organizer)
            game.setGamePath(game_dir)
            check = CyberpunkFrameworkCheck()
            check.init(organizer)
            self.assertNotIn(REDSCRIPT_ERRORS, check.activeProblems())
            log = organizer.overwrite / "r6/logs/redscript_rCURRENT.log"
            log.parent.mkdir(parents=True)
            log.write_text(FAILED.replace(GAME, str(game_dir)), encoding="utf-8")
            check._changed()
            self.assertIn(REDSCRIPT_ERRORS, check.activeProblems())
            text = check.fullDescription(REDSCRIPT_ERRORS)
            self.assertIn("Broken Mod v2", text)
            self.assertIn("main.reds:12", text)
            log.write_text(PASSED.replace(GAME, str(game_dir)), encoding="utf-8")
            check._changed()
            self.assertNotIn(REDSCRIPT_ERRORS, check.activeProblems())
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
