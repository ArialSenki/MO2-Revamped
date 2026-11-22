"""MO2's problem report for Cyberpunk 2077 (every problem's text, its error
paths, the Codeware scan in the background), the redscript log's odd paths,
the Content column's error path and the game plugin's smaller parts."""

from __future__ import annotations

import json
import shutil
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QCoreApplication, QDir
from PyQt6.QtWidgets import QMessageBox

from basic_games.games import game_cyberpunk2077 as plugin_module
from basic_games.games.cyberpunk2077 import content, frameworks, redscript_log, util
from basic_games.games.cyberpunk2077 import plugins as check_module
from basic_games.games.cyberpunk2077.plugins import (
    LISTED_ERRORS,
    LISTED_MODS,
    OUTSIDE_DEPLOY,
    REDMOD_DLC,
    REDSCRIPT_ERRORS,
    CyberpunkFrameworkCheck,
    active_mod_files,
)
from fake_organizer import FakeOrganizer
from harness import make_tree
from test_plugin import NAME, Game, make_game_dir


class OtherGame:
    def gameName(self):
        return "Other"

    def name(self):
        return "Other Game Plugin"


class Base(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="cp77-cov-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.ui_ready = []
        self.organizer.onUserInterfaceInitialized = lambda callback: self.ui_ready.append(callback) or True
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self, extra=()) -> Path:
        game_dir = make_game_dir(self.tmp, list(extra))
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def run_game(self, args: str = "--launcher-skip -modded") -> bool:
        exe = Path(self.game.gameDirectory().absolutePath(), self.game.binaryName())
        return self.game._on_about_to_run(str(exe), QDir(str(exe.parent)), args)

    def set(self, key, value) -> None:
        self.organizer.setPluginSetting(NAME, key, value)


class CheckTest(Base):
    def check(self) -> CyberpunkFrameworkCheck:
        check = CyberpunkFrameworkCheck()
        self.assertTrue(check.init(self.organizer))
        return check

    def test_identity(self):
        check = self.check()
        self.assertEqual(check.name(), "Cyberpunk 2077 Framework Check")
        self.assertTrue(check.localizedName())
        self.assertEqual(check.author(), "MO2 Revamped")
        self.assertTrue(check.description())
        self.assertIsInstance(check.version(), mobase.VersionInfo)
        self.assertEqual(len(check.requirements()), 1)
        self.assertEqual(check.settings(), [])
        self.assertFalse(check.hasGuidedFix(REDMOD_DLC))
        check.startGuidedFix(REDMOD_DLC)

    def test_every_problem_described(self):
        check = self.check()
        names = [f"Mod {i}" for i in range(LISTED_MODS + 3)]
        check._report = frameworks.Report(
            missing={frameworks.FRAMEWORKS[0].key: ["Needs it"]},
            redmod_without_dlc=names,
            outside_redmod_deploy=True,
        )
        errors = [("Mod <b>", redscript_log.ScriptError("r6/scripts/M/a.reds", 3, "bad & worse"))]
        errors += [("", redscript_log.ScriptError("", 0, f"error {i}")) for i in range(LISTED_ERRORS + 1)]
        check._redscript_cache = errors
        self.assertEqual(check.activeProblems(), [0, REDMOD_DLC, OUTSIDE_DEPLOY, REDSCRIPT_ERRORS])
        for key in (0, REDMOD_DLC, OUTSIDE_DEPLOY, REDSCRIPT_ERRORS):
            self.assertTrue(check.shortDescription(key), key)
        self.assertIn(frameworks.FRAMEWORKS[0].name, check.shortDescription(0))
        self.assertIn("Needs it", check.fullDescription(0))
        self.assertIn(frameworks.FRAMEWORKS[0].url, check.fullDescription(0))
        redmod = check.fullDescription(REDMOD_DLC)
        self.assertIn("• Mod 0", redmod)
        self.assertIn("and 3 more", redmod)
        self.assertTrue(check.fullDescription(OUTSIDE_DEPLOY))
        scripts = check.fullDescription(REDSCRIPT_ERRORS)
        self.assertIn("<b>Mod &lt;b&gt;</b>: r6/scripts/M/a.reds:3 — bad &amp; worse", scripts)
        self.assertIn("game folder", scripts)
        self.assertIn("?", scripts)
        self.assertIn("and 2 more", scripts)

    def test_other_game_or_a_failing_analysis_is_an_empty_report(self):
        check = self.check()
        self.organizer.game = OtherGame()
        self.assertEqual(check.activeProblems(), [])
        self.organizer.game = self.game
        self.install()
        check._changed()

        def broken(*args):
            raise RuntimeError("bug")

        with mock.patch.object(check_module, "analyze", broken):
            self.assertEqual(check.activeProblems(), [])
        self.assertEqual(check._report.missing, {})

    def test_log_that_cant_be_read(self):
        check = self.check()
        self.install()

        def broken(path, pattern):
            raise OSError("locked")

        self.organizer.findFiles = broken
        self.assertEqual(check._redscript_errors(), [])

    def test_codeware_scan_in_the_background(self):
        check = self.check()
        candidate = frameworks.CodewareCandidate("Uses Codeware", self.tmp, ("a.reds",))

        def slow_and_broken(candidates):
            time.sleep(0.4)
            raise OSError("unreadable")

        with mock.patch.object(check_module, "scan_codeware", slow_and_broken):
            self.assertEqual(check._codeware_users([candidate]), [])
            # one scan at a time
            first = check._scan
            check._codeware_users([candidate])
            self.assertIs(check._scan, first)
            deadline = time.time() + 10
            while check._scan is not None and time.time() < deadline:
                QCoreApplication.processEvents()
                time.sleep(0.05)
        self.assertIsNone(check._scan)
        # a mod that couldn't be read counts as not using Codeware
        self.assertEqual(check._codeware, {candidate.key: False})
        self.assertEqual(check._codeware_users([candidate]), [])

    def test_active_mod_files_skip_what_isnt_a_mod(self):
        self.organizer.add_mod("Group_separator", [])
        self.organizer.add_mod("Gone", ["a.txt"])
        self.organizer.add_mod("Kept", ["r6/scripts/a.reds"])
        real = self.organizer.modList

        class ModList:
            def __getattr__(self, name):
                return getattr(real(), name)

            def getMod(self, name):
                mod = real().getMod(name)
                if name == "Gone":
                    def broken():
                        raise RuntimeError("being removed")

                    mod.fileTree = broken
                return mod

        self.organizer.modList = lambda: ModList()
        self.assertEqual([m.name for m in active_mod_files(self.organizer)], ["Kept"])


class RedscriptPathsTest(unittest.TestCase):
    def test_proton_and_network_paths(self):
        game = "Z:\\home\\deck\\.steam\\steamapps\\common\\Cyberpunk 2077"
        log = (
            f"[ERROR - x] At {game}\\r6\\scripts\\Mod\\main.reds:3:1:\n    bad\n"
            "[ERROR - x] At Z:/home/deck/other/r6/scripts/Two/b.reds:4:1:\n    bad2\n"
            "[ERROR - x] At \\\\server\\share\\Cyberpunk 2077\\red4ext\\plugins\\P\\d.reds:6:1:\n  bad4\n"
            "[ERROR - x] At D:\\elsewhere\\x.reds:7:1:\n  bad5\n"
            "[ERROR - x] something without a script\n"
            "[ERROR - x] REDscript compilation has failed.\n"
        )
        errors = redscript_log.parse(log, "z:/HOME/deck/.steam/steamapps/common/Cyberpunk 2077/")
        self.assertEqual(
            [(e.script, e.line) for e in errors],
            [("r6/scripts/Mod/main.reds", 3), ("r6/scripts/Two/b.reds", 4), ("red4ext/plugins/P/d.reds", 6),
             ("D:/elsewhere/x.reds", 7), ("", 0)],
        )
        self.assertTrue(redscript_log.failed(log))

    def test_unix_paths(self):
        # a native Linux redscript names the scripts by their Unix path
        log = (
            "[ERROR - x] At /home/deck/.steam/steamapps/common/Cyberpunk 2077/r6/scripts/Three/c.reds:5:1:\n"
            "    bad\n"
            "[ERROR - x] At /mnt/games/red4ext/plugins/P/d.reds:6:1:\n  bad2\n"
            "[ERROR - x] At Mod/main.reds:2:1:\n  bad3\n"
        )
        errors = redscript_log.parse(log, "Z:\\home\\deck\\.steam\\steamapps\\common\\Cyberpunk 2077")
        self.assertEqual(
            [(e.script, e.line) for e in errors],
            [("r6/scripts/Three/c.reds", 5), ("red4ext/plugins/P/d.reds", 6), ("r6/scripts/Mod/main.reds", 2)],
        )

    def test_logs_that_say_nothing(self):
        self.assertFalse(redscript_log.failed(""))
        self.assertFalse(redscript_log.failed("plain text\nno headers\n"))
        self.assertTrue(redscript_log.failed("[ERROR - x] something\n"))
        self.assertFalse(redscript_log.failed("[INFO - x] Compilation complete\n"))
        self.assertTrue(redscript_log.failed("[WARN - x] compilation failed\n"))


class ContentAndUtilTest(unittest.TestCase):
    def test_content_errors_are_no_content(self):
        class Broken:
            def __iter__(self):
                raise RuntimeError("files changing")

        self.assertEqual(content.CyberpunkModDataContent().getContentsFor(Broken()), [])

    def test_redmod_with_custom_sounds_and_r6_config(self):
        found = content.contents_of(make_tree([
            "mods/R/info.json", "mods/R/customSounds/a.wav", "mods/NotRed/readme.txt",
            "r6/input/a.xml", "r6/config/b.json", "r6/audioware/x/a.yml",
        ]))
        self.assertIn(content.Content.REDMOD, found)
        self.assertIn(content.Content.AUDIO, found)
        self.assertIn(content.Content.CONFIG, found)

    def test_root_builder_and_known_folders(self):
        class Raises:
            def isPluginEnabled(self, name):
                raise RuntimeError("not ready")

        self.assertFalse(util.root_builder_enabled(Raises()))
        # a GUID Windows doesn't know: the fallback, with its variables
        with mock.patch.dict("os.environ", {"CP77_TEST": "C:\\Here"}):
            self.assertEqual(util.known_folder("{00000000-0000-0000-0000-000000000001}", "%CP77_TEST%\\x"), "C:/Here/x")
            self.assertEqual(util.known_folder("not a guid", "%CP77_TEST%"), "C:/Here")
        self.assertTrue(util.saved_games_folder())
        self.assertTrue(util.local_app_data_folder())
        util.log_info("Ánimo: an accented message goes through logging")


class GamePartsTest(Base):
    def test_root_builder_configured_once_when_the_ui_is_ready(self):
        self.install()
        self.organizer.enabled_plugins.add("RootBuilder")
        window = object()
        for callback in self.ui_ready:
            callback(window)
        self.assertIs(self.game._window, window)
        self.assertEqual(self.organizer.settings[("RootBuilder", "exclusions")], "archive,setup_redlauncher.exe,tools")
        self.assertFalse(self.organizer.settings[(NAME, "configure_RootBuilder")])
        # not again
        self.organizer.settings[("RootBuilder", "exclusions")] = "mine"
        self.game._configure_root_builder_once()
        self.assertEqual(self.organizer.settings[("RootBuilder", "exclusions")], "mine")

    def test_root_builder_not_there(self):
        self.install()
        self.game._configure_root_builder_once()
        self.assertNotIn(("RootBuilder", "exclusions"), self.organizer.settings)

    def test_version_and_ini_files(self):
        self.install()
        self.assertEqual(self.game.iniFiles(), ["UserSettings.json"])
        with mock.patch.object(plugin_module.mobase, "getProductVersion", lambda exe: " 2.21 "):
            self.assertEqual(self.game.gameVersion(), "2.21")
        with mock.patch.object(plugin_module.mobase, "getProductVersion", lambda exe: "2.21 Hotfix"), \
                mock.patch.object(plugin_module.mobase, "getFileVersion", lambda exe: "3.0.88.46235"):
            self.assertEqual(self.game.gameVersion(), "3.0.88.46235")

        def broken(exe):
            raise OSError("unreadable")

        with mock.patch.object(plugin_module.mobase, "getProductVersion", broken), \
                mock.patch.object(plugin_module.mobase, "getFileVersion", broken):
            self.assertEqual(self.game.gameVersion(), "")

    def test_saves_with_odd_files(self):
        saves = self.tmp / "saves"
        for name, named in (("ManualSave-1", "\ufeffMy save\n"), ("AutoSave-2", None)):
            folder = saves / name
            folder.mkdir(parents=True)
            (folder / "sav.dat").write_bytes(b"x")
            if named is not None:
                (folder / "NamedSave.txt").write_text(named, encoding="utf-8")
        (saves / "Latin").mkdir()
        (saves / "Latin" / "sav.dat").write_bytes(b"x")
        (saves / "Latin" / "NamedSave.txt").write_bytes(b"Caf\xe9\n")
        listed = {Path(s.getFilepath()).name: s for s in self.game.listSaves(QDir(str(saves)))}
        # a name saved with a BOM (Notepad) has no invisible character in front
        self.assertEqual(listed["ManualSave-1"].getName(), "My save")
        self.assertEqual(listed["Latin"].getName(), "Latin")
        self.assertEqual(listed["AutoSave-2"].getName(), "AutoSave-2")
        self.assertEqual(len(listed["AutoSave-2"].allFiles()), 1)

    def test_save_metadata_of_a_newer_game(self):
        folder = self.tmp / "save"
        folder.mkdir()
        (folder / "sav.dat").write_bytes(b"x")
        meta = {"Data": {"metadata": {"name": "", "level": "12", "streetCred": 3.0, "bodyGender": "Female"}}}
        (folder / "metadata.11.json").write_text(json.dumps(meta), encoding="utf-8")
        (folder / "metadata.10.json").write_text("{}", encoding="utf-8")
        (folder / "metadata.x.json").write_text("{}", encoding="utf-8")
        save = plugin_module.CyberpunkSaveGame(folder)
        info = plugin_module.parse_cyberpunk_save_metadata(folder, save)
        self.assertEqual(info["Name"], "save")
        self.assertEqual((info["Level"], info["Street Cred"], info["Gender"]), (12, 3, "Female / "))
        empty = self.tmp / "empty"
        empty.mkdir()
        self.assertIsNone(plugin_module.parse_cyberpunk_save_metadata(empty, save))

    def test_forced_loads_skip_inactive_mods_and_survive_errors(self):
        self.install()
        self.organizer.add_mod("Off", ["root/bin/x64/version.dll"], active=False)
        self.assertEqual(self.game.executableForcedLoads(), [])
        self.organizer.add_mod("On", ["root/bin/x64/winmm.dll"])
        self.assertEqual(len(self.game.executableForcedLoads()), 1)

        def broken():
            raise RuntimeError("VFS not ready")

        self.organizer.virtualFileTree = broken
        self.assertEqual(self.game.executableForcedLoads(), [])

    def test_archive_order_skips_separators_and_reverses(self):
        self.install()
        self.organizer.add_mod("Group_separator", ["archive/pc/mod/sep.archive"])
        self.organizer.add_mod("A", ["archive/pc/mod/a.archive"])
        self.organizer.add_mod("B", ["archive/pc/mod/b.archive"])
        self.set("reverse_archive_load_order", True)
        self.set("enforce_archive_load_order", True)
        self.assertTrue(self.run_game("--launcher-skip"))
        text = (self.organizer.overwrite / "archive/pc/mod/modlist.txt").read_text(encoding="utf-8")
        self.assertEqual(text.split(), ["b.archive", "a.archive"])

    def test_archive_list_that_cant_be_written_doesnt_stop_the_game(self):
        self.install()

        def broken():
            raise OSError("disk full")

        self.game._update_archive_list = broken
        self.assertTrue(self.run_game("--launcher-skip"))

    def test_redmod_order_reversed_and_deploy_that_cant_start(self):
        self.install(["tools/redmod/bin/redMod.exe"])
        self.organizer.add_mod("Red", ["mods/Red/info.json"])
        self.organizer.add_mod("Red2", ["mods/Red2/info.json"])
        self.set("reverse_redmod_load_order", True)
        self.assertEqual(self.game._redmod_names()[0], ["Red2", "Red"])
        answers = []

        def question(*args, **kwargs):
            answers.append(args[2])
            return QMessageBox.StandardButton.Yes

        def broken(names):
            raise OSError("read-only Overwrite")

        self.game._write_redmod_list = broken
        with mock.patch.object(QMessageBox, "question", question):
            self.assertTrue(self.run_game())
        self.assertIn("read-only Overwrite", answers[0])

    def test_manual_deploy_that_fails(self):
        game_dir = self.install(["tools/redmod/bin/redMod.exe"])
        self.organizer.add_mod("Red", ["mods/Red/info.json"])
        self.organizer.exit_code = 2
        redmod = str(game_dir / "tools/redmod/bin/redMod.exe")
        self.assertFalse(self.game._on_about_to_run(redmod, QDir(), "deploy %MODLIST%"))
        self.assertIn("-modlist=", self.organizer.started[-1][1][0])
        # no REDmod active: the placeholder goes, nothing else
        self.organizer.active.discard("Red")
        self.game._on_about_to_run(redmod, QDir(), "deploy %modlist% -x")
        self.assertEqual(self.organizer.started[-1][1][0], "deploy  -x")

    def test_game_without_a_cache_folder(self):
        game_dir = self.install()
        shutil.rmtree(game_dir / "r6/cache")
        self.assertTrue(self.run_game("--launcher-skip"))
        self.assertFalse((self.organizer.overwrite / "r6/cache").exists())

    def test_cache_file_checks(self):
        game_dir = self.install()
        data = game_dir
        # not served by the VFS at all
        self.organizer.findFiles = lambda path, pattern: []
        self.assertFalse(self.game._is_cache_file_updated(Path("r6/cache/tweakdb.bin"), data))
        # served from a file that is gone
        self.organizer.findFiles = lambda path, pattern: [str(self.tmp / "gone.bin")]
        self.assertFalse(self.game._is_cache_file_updated(Path("r6/cache/tweakdb.bin"), data))


if __name__ == "__main__":
    unittest.main()
