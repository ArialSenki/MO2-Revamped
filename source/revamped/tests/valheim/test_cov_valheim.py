"""The Valheim plugin's less trodden paths: the settings sync from Overwrite
(its thread and timers), the problem texts, missing or unreadable files and
the paths Proton hands (Z:, forward slashes, another case)."""

from __future__ import annotations

import shutil
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_valheim as plugin_module
from basic_games.games.revamped_shared import dotnet
from basic_games.games.valheim import checks as checks_module
from basic_games.games.valheim import configs
from basic_games.games.valheim import plugins as valheim_plugins
from basic_games.games.valheim.checks import ModFolder, analyze
from basic_games.games.valheim.content import Content, ValheimModDataContent, contents_of
from basic_games.games.valheim.plugins import MISSING, NO_BEPINEX, NO_INSLIMVML, ValheimCheck
from fake_organizer import FakeOrganizer
from harness import make_tree

Game = plugin_module.ValheimGame


def write(path: Path, data: bytes | str = "x") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
    return path


def declares(plugins=(), required=()) -> dotnet.AssemblyInfo:
    return dotnet.AssemblyInfo(frozenset(plugins), frozenset(required))


class FakeIndex(dotnet.AssemblyIndex):
    def __init__(self, declared: dict[str, dotnet.AssemblyInfo]) -> None:
        super().__init__()
        self.declared = declared

    def info(self, path: Path) -> dotnet.AssemblyInfo:
        return self.declared.get(path.name, dotnet.EMPTY)


class Timers:
    """QTimer.singleShot, run by hand."""

    def __init__(self) -> None:
        self.pending: list = []

    def singleShot(self, ms, callback) -> None:
        self.pending.append(callback)

    def run(self, limit: int = 20) -> None:
        while self.pending and limit:
            limit -= 1
            self.pending.pop(0)()


class Organizer(FakeOrganizer):
    def __init__(self, base: Path):
        super().__init__(base)
        self.refreshed = 0
        self.broken_mod_list = False
        self.broken_tree = False

    def refresh(self, *args) -> None:
        self.refreshed += 1

    def modList(self):
        if self.broken_mod_list:
            raise RuntimeError("profile changing")
        return super().modList()

    def virtualFileTree(self):
        if self.broken_tree:
            raise RuntimeError("no tree yet")
        return super().virtualFileTree()


class GameTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-cov-"))
        self.organizer = Organizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))
        self.timers = Timers()
        patcher = mock.patch.object(plugin_module, "QTimer", self.timers)
        patcher.start()
        self.addCleanup(patcher.stop)
        original = dotnet.SHARED.info
        dotnet.SHARED.info = lambda path: declares(["me.plugin"]) if path.name == "P.dll" else dotnet.EMPTY
        self.addCleanup(setattr, dotnet.SHARED, "info", original)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self) -> Path:
        game_dir = self.tmp / "Valheim"
        write(game_dir / "valheim.exe", "not really an exe")
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def plugin_with_config(self) -> tuple[Path, Path]:
        mod = self.organizer.add_mod("Plugin", ["BepInEx/plugins/P.dll"])
        config = write(self.organizer.overwrite / "BepInEx/config/me.plugin.cfg", "## Settings file\n")
        return mod, config

    def sync(self) -> None:
        self.game._sync_configs()
        thread = self.game._sync_thread
        if thread is not None:
            thread.join(10)
        self.timers.run()

    def test_without_the_game(self) -> None:
        self.assertEqual(self.game.gameVersion(), "")
        self.assertEqual(self.game.executables(), [])
        self.assertEqual(self.game.executableForcedLoads(), [])

    def test_with_the_game(self) -> None:
        game_dir = self.install()
        # mobase reads the exe's version resource (not on a fake exe here)
        with mock.patch.object(plugin_module.BasicGame, "gameVersion", return_value="0.218.21"):
            self.assertEqual(self.game.gameVersion(), "0.218.21")
        with mock.patch.object(plugin_module.BasicGame, "gameVersion", side_effect=OSError("locked")):
            self.assertEqual(self.game.gameVersion(), "")
        [executable] = self.game.executables()
        self.assertEqual(executable.title(), "Valheim")
        self.assertEqual(Path(executable.binary().absoluteFilePath()), game_dir / "valheim.exe")
        self.assertEqual(self.game.listSaves(QDir(str(self.tmp / "no saves"))), [])

    def test_forced_loads_never_stop_a_launch(self) -> None:
        self.install()
        self.organizer.add_mod("BepInExPack", ["winhttp.dll", "BepInEx/core/BepInEx.dll"])
        self.organizer.broken_tree = True
        self.assertEqual(self.game.executableForcedLoads(), [])

    def test_sync_moves_a_config_to_its_mod(self) -> None:
        self.install()
        mod, config = self.plugin_with_config()
        # a separator and an inactive mod are left out
        self.organizer.add_mod("Group_separator", [])
        self.organizer.add_mod("Off", ["BepInEx/plugins/P.dll"], active=False)
        self.sync()
        self.assertTrue((mod / "BepInEx/config/me.plugin.cfg").is_file())
        self.assertFalse(config.exists())
        self.assertEqual(self.organizer.refreshed, 1)
        self.assertIsNone(self.game._sync_thread)
        # nothing left: no refresh
        self.sync()
        self.assertEqual(self.organizer.refreshed, 1)

    def test_sync_only_when_wanted(self) -> None:
        self.install()
        _mod, config = self.plugin_with_config()
        self.organizer.settings[(self.game.name(), "sync_overwrite")] = False
        self.sync()
        self.assertTrue(config.exists())
        self.assertEqual(self.timers.pending, [])
        # another game managed: not ours to sync
        self.organizer.settings.clear()
        self.organizer.game = mock.Mock(**{"name.return_value": "Other Game"})
        self.sync()
        self.assertTrue(config.exists())

    def test_sync_errors_are_only_logged(self) -> None:
        self.install()
        _mod, config = self.plugin_with_config()
        self.organizer.broken_mod_list = True
        self.sync()
        self.assertTrue(config.exists())
        self.assertIsNone(self.game._sync_thread)
        self.organizer.broken_mod_list = False
        with mock.patch.object(configs, "plan", side_effect=OSError("overwrite unreadable")):
            self.sync()
        self.assertTrue(config.exists())
        self.assertEqual(self.organizer.refreshed, 0)
        self.assertIsNone(self.game._sync_thread)

    def test_sync_waits_for_its_thread(self) -> None:
        self.install()
        mod, _config = self.plugin_with_config()
        release = threading.Event()
        original = configs.plan

        def slow_plan(*args):
            release.wait(10)
            return original(*args)

        with mock.patch.object(configs, "plan", slow_plan):
            self.game._sync_configs()
            thread = self.game._sync_thread
            # a second start while one runs does nothing
            self.game._sync_configs()
            self.assertIs(self.game._sync_thread, thread)
            # the timer fires while the thread still works: it waits again
            self.timers.pending.pop(0)()
            self.assertEqual(len(self.timers.pending), 1)
            self.assertIs(self.game._sync_thread, thread)
            release.set()
            thread.join(10)
            self.timers.run()
        self.assertTrue((mod / "BepInEx/config/me.plugin.cfg").is_file())
        self.assertIsNone(self.game._sync_thread)

    def test_finished_run_of_the_game_syncs(self) -> None:
        game_dir = self.install()
        calls = []
        self.game._sync_configs = lambda: calls.append(1)
        exe = str(game_dir / "valheim.exe")
        # MO2 hands forward slashes; another case is the same file on Windows
        for app in (exe, exe.replace("\\", "/"), exe.upper()):
            self.game._on_finished_run(app, 0)
        self.game._on_finished_run(str(game_dir / "BepInEx/other.exe"), 0)
        self.game._on_finished_run(str(self.tmp / "Valheim Dedicated Server/valheim_server.exe"), 1)
        self.assertEqual(len(calls), 3)

    def test_proton_paths(self) -> None:
        # under Proton the game folder is on Wine's Z: drive
        self.game.setGamePath("Z:/home/deck/.local/share/Steam/steamapps/common/Valheim")
        calls = []
        self.game._sync_configs = lambda: calls.append(1)
        self.game._on_finished_run(r"Z:\home\deck\.local\share\Steam\steamapps\common\Valheim\valheim.exe", 0)
        self.game._on_finished_run(r"z:\HOME\deck\.local\share\Steam\steamapps\common\Valheim\valheim.exe", 0)
        self.assertEqual(len(calls), 2)
        self.assertEqual(self.game.executables()[0].title(), "Valheim")


class ConfigsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-cov-cfg-"))
        self.overwrite = self.tmp / "overwrite"
        self.mod = self.tmp / "mods/Mod"
        write(self.mod / "BepInEx/plugins/Mod.dll")
        self.index = FakeIndex({"Mod.dll": declares(["Mod.GUID"])})

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_header_with_a_bom_and_another_encoding(self) -> None:
        bom = write(self.overwrite / "BepInEx/config/a.cfg",
                    "\ufeff## Settings file was created by plugin Mod v1\n## Plugin GUID: mod.guid\n")
        latin = write(self.overwrite / "BepInEx/config/b.cfg",
                      "## Settings file was created by plugin Café\n## Plugin GUID: Mod.GUID\n".encode("cp1252"))
        self.assertEqual(configs.config_guid(bom, True), "mod.guid")
        self.assertEqual(configs.config_guid(latin, True), "Mod.GUID")
        self.assertEqual(configs.config_guid(latin, False), "b")
        self.assertEqual(configs.config_guid(self.tmp / "gone.cfg", True), "gone")

    def test_empty_or_missing_config_folder(self) -> None:
        self.assertEqual(configs.plan(self.overwrite, [("Mod", self.mod)], True, self.index), [])
        (self.overwrite / "BepInEx/config").mkdir(parents=True)
        write(self.overwrite / "BepInEx/config/BEPINEX.CFG")
        write(self.overwrite / "BepInEx/config/notes.txt")
        (self.overwrite / "BepInEx/config/folder.cfg").mkdir()
        self.assertEqual(configs.plan(self.overwrite, [("Mod", self.mod)], True, self.index), [])

    def test_upper_case_suffix_and_guid(self) -> None:
        write(self.overwrite / "BepInEx/config/MOD.GUID.CFG")
        [move] = configs.plan(self.overwrite, [("Mod", self.mod), ("Mod", self.mod)], True, self.index)
        self.assertEqual(move.mod, "Mod")

    def test_apply_skips_and_failures(self) -> None:
        source = write(self.overwrite / "BepInEx/config/x.cfg")
        taken = write(self.mod / "BepInEx/config/taken.cfg", "the mod's")
        moves = [
            configs.Move(self.overwrite / "BepInEx/config/gone.cfg", self.mod / "BepInEx/config/gone.cfg", "Mod"),
            configs.Move(source, taken, "Mod"),
        ]
        self.assertEqual(configs.apply(moves), 0)
        self.assertEqual(taken.read_text(), "the mod's")
        # the mod's BepInEx/config is a file: the move fails, logged only
        blocked = self.tmp / "mods/Blocked"
        write(blocked / "BepInEx/config", "a file")
        self.assertEqual(configs.apply([configs.Move(source, blocked / "BepInEx/config/x.cfg/x.cfg", "Blocked")]), 0)
        self.assertTrue(source.is_file())


class ChecksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-cov-chk-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_bepinex_in_the_game_folder(self) -> None:
        game = self.tmp / "game"
        write(game / "BepInEx/core/BepInEx.dll")
        write(game / "winhttp.dll")
        plugin = self.tmp / "mods/Plugin"
        write(plugin / "BepInEx/plugins/P.dll")
        report = analyze([ModFolder("Plugin", plugin)], game, FakeIndex({}))
        self.assertEqual(report.without_bepinex, [])

    def test_patchers_and_no_game_folder(self) -> None:
        patcher = self.tmp / "mods/Patcher"
        (patcher / "BepInEx/patchers").mkdir(parents=True)
        index = FakeIndex({"P.dll": declares(["p"], ["Missing.One"])})
        plugin = self.tmp / "mods/Plugin"
        write(plugin / "BepInEx/plugins/P.dll")
        write(plugin / "BepInEx/plugins/sub/P.dll")
        report = analyze([ModFolder("Patcher", patcher), ModFolder("Plugin", plugin)], None, index)
        self.assertEqual(report.without_bepinex, ["Patcher", "Plugin"])
        # required by two DLLs of one mod: the mod is named once
        self.assertEqual(report.missing, {"missing.one": ["Plugin"]})

    def test_inslimvml(self) -> None:
        vml = self.tmp / "mods/VML mod"
        write(vml / "InSlimVML/Mods/Thing_VML.DLL")
        other = self.tmp / "mods/Other"
        (other / "InSlimVML/Mods").mkdir(parents=True)
        mods = [ModFolder("VML mod", vml), ModFolder("Other", other)]
        self.assertEqual(analyze(mods, None, FakeIndex({})).without_inslimvml, ["VML mod"])
        with mock.patch.object(Path, "iterdir", side_effect=PermissionError("denied")):
            self.assertFalse(checks_module._vml_mods(vml))
        loader = self.tmp / "mods/Loader"
        write(loader / "inslimvml.ini")
        self.assertEqual(analyze(mods + [ModFolder("Loader", loader)], None, FakeIndex({})).without_inslimvml, [])


class ContentTest(unittest.TestCase):
    def test_contents_and_errors(self) -> None:
        content = ValheimModDataContent()
        self.assertEqual([c.id for c in content.getAllContents()], [int(c) for c in Content])
        self.assertEqual(content.getContentsFor(make_tree(["BepInEx/config/", "AdvancedBuilder"])), [])
        self.assertEqual(content.getContentsFor(None), [])
        self.assertEqual(contents_of(make_tree(["valheim_Data/sharedassets.assets"])), {Content.ASSEMBLIES})


class CheckPluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-cov-check-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.game.init(self.organizer)
        game_dir = self.tmp / "Valheim"
        write(game_dir / "valheim.exe")
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        self.check = ValheimCheck()
        self.check.init(self.organizer)
        original = dotnet.SHARED.info
        self.addCleanup(setattr, dotnet.SHARED, "info", original)
        dotnet.SHARED.info = self.info
        self.required: dict[str, list[str]] = {}

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def info(self, path: Path) -> dotnet.AssemblyInfo:
        return declares([path.stem], self.required.get(path.stem, []))

    def test_identity(self) -> None:
        check = self.check
        self.assertEqual(check.name(), "Valheim Mod Check")
        self.assertEqual(check.localizedName(), "Valheim Mod Check")
        self.assertEqual(check.author(), "MO2 Revamped")
        self.assertIn("BepInEx", check.description())
        self.assertIsInstance(check.version(), mobase.VersionInfo)
        self.assertEqual(len(check.requirements()), 1)
        self.assertEqual(check.settings(), [])
        self.assertFalse(check.hasGuidedFix(NO_BEPINEX))
        check.startGuidedFix(NO_BEPINEX)
        self.assertEqual(check.shortDescription(99), "")
        self.assertEqual([type(p) for p in valheim_plugins.createPlugins()], [ValheimCheck])

    def test_all_problems_and_long_lists(self) -> None:
        for index in range(15):
            self.organizer.add_mod(f"Plugin & {index}", [f"BepInEx/plugins/P{index}.dll"])
            self.required[f"P{index}"] = [f"needed.{index}"]
        self.organizer.add_mod("VML", ["InSlimVML/Mods/A_VML.dll"])
        self.assertEqual(self.check.activeProblems(), [NO_BEPINEX, MISSING, NO_INSLIMVML])
        for key in (NO_BEPINEX, MISSING, NO_INSLIMVML):
            self.assertTrue(self.check.shortDescription(key))
        no_bepinex = self.check.fullDescription(NO_BEPINEX)
        self.assertIn("Plugin &amp; 0", no_bepinex)
        self.assertIn("and 3 more", no_bepinex)
        self.assertIn("thunderstore.io", no_bepinex)
        missing = self.check.fullDescription(MISSING)
        self.assertIn("needed.0", missing)
        self.assertIn("and 3 more", missing)
        self.assertIn("VML", self.check.fullDescription(NO_INSLIMVML))

    def test_reports_are_kept_until_a_change(self) -> None:
        self.organizer.add_mod("Plugin", ["BepInEx/plugins/P.dll"])
        self.assertEqual(self.check.activeProblems(), [NO_BEPINEX])
        self.organizer.add_mod("BepInExPack", ["winhttp.dll", "BepInEx/core/BepInEx.dll"])
        # the same report until MO2 says something changed
        self.assertEqual(self.check.activeProblems(), [NO_BEPINEX])
        self.check._changed()
        self.assertEqual(self.check.activeProblems(), [])
        self.check._mod_files_changed("Plugin")
        self.assertEqual(self.check.activeProblems(), [])

    def test_another_game_or_a_failing_check(self) -> None:
        self.organizer.add_mod("Plugin", ["BepInEx/plugins/P.dll"])
        self.organizer.game = mock.Mock(**{"gameName.return_value": "Skyrim"})
        self.assertEqual(self.check.activeProblems(), [])
        self.check._changed()
        broken = mock.Mock(**{"gameName.return_value": "Valheim", "isInstalled.side_effect": RuntimeError("gone")})
        self.organizer.game = broken
        self.assertEqual(self.check.activeProblems(), [])
        self.check._changed()
        self.organizer.game = None
        self.assertEqual(self.check.activeProblems(), [])

    def test_active_mods(self) -> None:
        self.organizer.add_mod("On", ["a.txt"])
        self.organizer.add_mod("Off", ["a.txt"], active=False)
        self.organizer.add_mod("Group_separator", [])
        self.assertEqual([m.name for m in valheim_plugins.active_mods(self.organizer)], ["On"])


if __name__ == "__main__":
    unittest.main()
