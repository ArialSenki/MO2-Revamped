"""The start and the end of a run (LSLib needs, packing workspaces, the
warnings, the clean-up), MO2's problem list, the Tools menu and the game
plugin's smaller parts. No window is shown and nothing is downloaded."""

from __future__ import annotations

import json
import os
import time
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_baldursgate3 as plugin_module
from basic_games.games.baldursgate3 import launch as launch_module
from basic_games.games.baldursgate3 import loadorder, modsettings
from basic_games.games.baldursgate3.plugins import createPlugins, diagnose, order_view, tools
from paktools import mod_pak, write_pak
from test_cov_lslib import FakeBox
from test_plugin import NAME, UA, UB, UC, FakeProgress, PluginBase


class OtherGame:
    def name(self):
        return "Other Game Plugin"


class FakeLSLib:
    def __init__(self, ok: bool):
        self.ok = ok
        self.reasons: list[str] = []
        self.forced: list[bool] = []

    def download_lslib_if_missing(self, force: bool = False, reason: str = "") -> bool:
        self.reasons.append(reason)
        self.forced.append(force)
        return self.ok


class FakeDivine:
    def __init__(self, available=True, pack_ok=True, read_error=None):
        self._available = available
        self.pack_ok = pack_ok
        self.read_error = read_error
        self.packed: list[tuple[Path, Path]] = []

    def available(self):
        return self._available

    def read(self, pak):
        if self.read_error:
            raise self.read_error
        return ["Mods/Packed/meta.lsx", "Mods/Packed/x.txt"], (
            b'<save><node id="ModuleInfo"><attribute id="UUID" type="guid" value="'
            + UC.encode() + b'"/><attribute id="Folder" type="LSString" value="Packed"/>'
            b'<attribute id="Name" type="LSString" value="Packed"/></node></save>'
        )

    def pack(self, folder, pak):
        self.packed.append((folder, pak))
        if self.pack_ok:
            divine_only_pak(pak)
        return self.pack_ok


def divine_only_pak(path: Path) -> Path:
    """A pak of an LSPK version only Divine reads."""
    write_pak(path, {"x": b"x"})
    data = bytearray(path.read_bytes())
    data[4] = 13
    path.write_bytes(bytes(data))
    return path


class LaunchBase(PluginBase):
    def setUp(self) -> None:
        super().setUp()
        FakeBox.shown = []
        FakeBox.accept = True
        patcher = mock.patch.object(launch_module, "QMessageBox", FakeBox)
        patcher.start()
        self.addCleanup(patcher.stop)

    def entry(self, name="M", needs_lslib=(), workspaces=(), paks=()) -> loadorder.ModEntry:
        entry = loadorder.ModEntry(name, self.organizer.mods / name)
        entry.needs_lslib = list(needs_lslib)
        entry.workspaces = list(workspaces)
        entry.paks = list(paks)
        return entry


class LSLibNeedsTest(LaunchBase):
    def test_nothing_needed_asks_nothing(self):
        self.assertTrue(self.game.launch()._fix_lslib_needs([self.entry()]))

    def test_declined_or_unavailable_leaves_them_out(self):
        pak = self.organizer.mods / "M" / "Old.pak"
        entry = self.entry(needs_lslib=["Old.pak"], paks=[pak])
        lslib = FakeLSLib(False)
        self.game.lslib = lambda: lslib
        self.assertTrue(self.game.launch()._fix_lslib_needs([entry]))
        self.assertEqual(lslib.reasons, ["M: Old.pak"])
        self.assertEqual(entry.needs_lslib, ["Old.pak"])
        lslib.ok = True
        self.game.divine = lambda: FakeDivine(available=False)
        self.assertTrue(self.game.launch()._fix_lslib_needs([entry]))
        self.assertEqual(entry.needs_lslib, ["Old.pak"])

    def test_divine_reads_what_lspk_cant(self):
        root = self.organizer.mods / "M"
        root.mkdir(parents=True)
        pak = divine_only_pak(root / "Old.pak")
        broken = divine_only_pak(root / "Broken.pak")
        good = self.entry(needs_lslib=["Old.pak", "Gone.pak"], paks=[pak])
        bad = self.entry("B", needs_lslib=["Broken.pak"], paks=[broken])
        self.game.lslib = lambda: FakeLSLib(True)
        divine = FakeDivine()
        self.game.divine = lambda: divine
        launch = self.game.launch()
        self.assertTrue(launch._fix_lslib_needs([good]))
        self.assertEqual(good.needs_lslib, ["Gone.pak"])
        self.assertEqual([m["uuid"] for m in good.modules], [UC])
        divine.read_error = OSError("Divine crashed")
        self.assertTrue(launch._fix_lslib_needs([bad]))
        self.assertEqual(bad.failed, [("Broken.pak", "Divine crashed")])

    def test_workspaces_are_packed_once(self):
        root = self.organizer.add_mod("W", ["Packed/Mods/Packed/meta.lsx", "Packed/Public/Packed/x.txt"])
        self.game.lslib = lambda: FakeLSLib(True)
        divine = FakeDivine()
        self.game.divine = lambda: divine
        launch = self.game.launch()
        entry = self.entry("W", workspaces=[root / "Packed"])
        self.assertTrue(launch._fix_lslib_needs([entry]))
        self.assertEqual(len(divine.packed), 1)
        self.assertEqual([m["uuid"] for m in entry.modules], [UC])
        # nothing changed: not packed again
        entry = self.entry("W", workspaces=[root / "Packed"])
        launch._fix_lslib_needs([entry])
        self.assertEqual(len(divine.packed), 1)

    def test_workspace_with_its_pak_there_or_failing(self):
        root = self.organizer.add_mod("W", ["Packed/Mods/Packed/meta.lsx", "Mods/Packed.pak"])
        self.game.lslib = lambda: FakeLSLib(True)
        divine = FakeDivine(pack_ok=False)
        self.game.divine = lambda: divine
        launch = self.game.launch()
        entry = self.entry("W", workspaces=[root / "Packed"])
        launch._pack(entry, root / "Packed", divine)
        self.assertEqual(divine.packed, [])
        (root / "Mods" / "Packed.pak").unlink()
        launch._pack(entry, root / "Packed", divine)
        self.assertEqual(entry.failed, [("Packed", "Divine couldn't pack it")])
        divine.pack_ok = True
        divine.read_error = ValueError("unreadable")
        entry.failed = []
        launch._pack(entry, root / "Packed", divine)
        self.assertEqual(entry.failed[0][1], "unreadable")


class StartTest(LaunchBase):
    def test_warnings_are_listed_and_cut(self):
        self.install()
        for i in range(20):
            bad = self.organizer.add_mod(f"Bad{i:02}", [])
            bad.mkdir(parents=True, exist_ok=True)
            (bad / f"Bad{i:02}.pak").write_bytes(b"LSPK\x12\x00\x00\x00 cut")
        self.assertTrue(self.run_game())
        self.assertIn("• and 5 more", FakeBox.shown[0])
        self.assertIn("Bad00", FakeBox.shown[0])
        self.assertNotIn("Bad19", FakeBox.shown[0])

    def test_missing_dependency_box(self):
        self.install()
        self.pak_mod("Needs", UA, deps=[("Lib", UB)])
        FakeBox.accept = False
        self.assertFalse(self.run_game())
        self.assertIn("Needs needs Lib", FakeBox.shown[-1])
        FakeBox.accept = True
        self.assertTrue(self.run_game())

    def test_late_and_duplicate_modules_are_logged(self):
        self.install()
        self.pak_mod("Early", UA, deps=[("Late", UB)])
        self.pak_mod("Late", UB)
        self.pak_mod("LateAgain", UB)
        self.assertTrue(self.run_game())
        plan = self.game.launch().last_plan
        self.assertEqual(plan.duplicates, [("LateAgain", "Late", "LateAgain")])
        self.assertEqual(plan.late[0][:2], ("Early", "Late"))
        self.assertEqual(launch_module.describe(plan)["modules"], [UA, UB])

    def test_a_reading_bug_doesnt_stop_the_game(self):
        self.install()
        self.pak_mod("A", UA)
        real = launch_module.loadorder.read_mods

        def broken(*a, **k):
            raise RuntimeError("bug")

        launch_module.loadorder.read_mods = broken
        try:
            self.assertTrue(self.run_game())
        finally:
            launch_module.loadorder.read_mods = real
        self.assertFalse(self.game.modsettings_path.exists())

    def test_force_reparse_and_odd_external_pak(self):
        self.install()
        self.pak_mod("A", UA)
        (self.docs / "Mods").mkdir(parents=True)
        (self.docs / "Mods" / "broken.pak").write_bytes(b"not a pak")
        launch = self.game.launch()
        exe = str(Path(self.game.gameDirectory().absolutePath(), "bin/bg3.exe"))
        self.assertTrue(launch.before_run(exe, force_reparse=True))
        self.assertEqual(launch.external_uuids(), set())
        self.assertEqual(self.written(), ["GustavX", "A"])

    def test_copy_for_the_diff_that_cant_be_written(self):
        self.install()
        self.pak_mod("A", UA)
        temp = self.game.plugin_data_path / "temp"
        temp.parent.mkdir(parents=True, exist_ok=True)
        temp.write_text("a file in the way")
        self.assertTrue(self.run_game())
        self.assertTrue(temp.is_file())

    def test_old_crash_checks_are_pruned(self):
        self.install()
        kept = self.game.plugin_data_path / "crash-checks"
        for i in range(12):
            (kept / f"2020010{i:02}-000000-documents").mkdir(parents=True)
        marker = self.organizer.overwrite / "ModCrashSanityCheck" / "x"
        marker.parent.mkdir(parents=True)
        marker.write_text("crash")
        self.game.launch().clear_crash_check()
        self.assertEqual(len([p for p in kept.iterdir() if p.is_dir()]), launch_module.CRASH_CHECKS_KEPT)
        self.assertFalse(marker.exists())

    def test_dotnet_check_before_bg3_mod_manager(self):
        self.install()
        exe = self.tmp / "BG3MM" / "BG3ModManager.exe"
        exe.parent.mkdir()
        exe.write_bytes(b"MZ")
        exe.with_name("BG3ModManager.runtimeconfig.json").write_text(json.dumps(
            {"runtimeOptions": {"framework": {"name": "Microsoft.WindowsDesktop.App", "version": "99.0.0"}}}))
        FakeBox.accept = False
        self.assertFalse(self.game._on_about_to_run(str(exe), QDir(), ""))
        self.assertIn("dotnet/99.0", FakeBox.shown[-1])
        FakeBox.accept = True
        self.assertTrue(self.game._on_about_to_run(str(exe), QDir(), ""))
        # its runtime is there: no question
        exe.with_name("BG3ModManager.runtimeconfig.json").write_text("{broken")
        FakeBox.shown = []
        self.assertTrue(launch_module.check_dotnet_before_run(self.game, str(exe)))
        self.assertEqual(FakeBox.shown, [])


class AfterRunTest(LaunchBase):
    def exe(self) -> str:
        return str(Path(self.game.gameDirectory().absolutePath(), "bin/bg3.exe"))

    def test_diff_logged_and_level_cache_cleaned(self):
        self.install()
        self.pak_mod("A", UA)
        self.organizer.setPluginSetting(NAME, "log_diff", True)
        self.organizer.setPluginSetting(NAME, "delete_levelcache_folders_older_than_x_days", 1)
        self.assertTrue(self.run_game())
        old = self.organizer.overwrite / "LevelCache" / "Old"
        new = self.organizer.overwrite / "LevelCache" / "New"
        old.mkdir(parents=True)
        new.mkdir()
        (old / "x").write_text("x")
        past = time.time() - 3 * 86400
        os.utime(old, (past, past))
        (self.organizer.overwrite / "a.log").write_text("log")
        logged = []
        with mock.patch.object(launch_module, "debug_enabled", lambda: True), \
                mock.patch.object(launch_module, "log_debug", logged.append):
            self.game.modsettings_path.write_text("changed by the game", encoding="utf-8")
            self.game._on_finished_run(self.exe(), 0)
        self.assertTrue(any(line.startswith("+changed by the game") for line in logged))
        self.assertTrue(any("moved log files" in line for line in logged))
        self.assertFalse(old.exists())
        self.assertTrue(new.exists())

    def test_diff_without_the_copy_and_odd_days(self):
        self.install()
        self.organizer.setPluginSetting(NAME, "log_diff", True)
        self.organizer.setPluginSetting(NAME, "delete_levelcache_folders_older_than_x_days", "3")
        old = self.organizer.overwrite / "LevelCache" / "Old"
        old.mkdir(parents=True)
        (old / "x").write_text("x")
        past = time.time() - 30 * 86400
        os.utime(old, (past, past))
        logged = []
        with mock.patch.object(launch_module, "debug_enabled", lambda: True), \
                mock.patch.object(launch_module, "log_debug", logged.append):
            self.game._on_finished_run(self.exe(), 0)
        self.assertTrue(any("diff skipped" in line for line in logged))
        # a setting that isn't a number of days: nothing deleted
        self.assertTrue(old.exists())

    def test_dropped_mods_named_and_problem_list_errors_ignored(self):
        self.install()
        self.pak_mod("A", UA)
        self.pak_mod("B", UB)
        self.assertTrue(self.run_game())
        launch = self.game.launch()
        launch.written_uuids.append("ffffffff-0000-0000-0000-000000000000")

        def broken():
            raise RuntimeError("MO2 not ready")

        self.game._problems = broken
        self.game._on_finished_run(self.exe(), 0)
        self.assertEqual(launch.dropped_last_run, ["ffffffff-0000-0000-0000-000000000000"])
        self.assertEqual(launch._names([UA.upper().casefold(), "x"]), ["A", "x"])
        launch.last_plan = None
        self.assertEqual(launch._names([UA]), [UA])

    def test_other_programs_only_tidy_overwrite(self):
        self.install()
        empty = self.organizer.overwrite / "Data" / "Mods"
        empty.mkdir(parents=True)
        self.game._on_finished_run(str(self.tmp / "BG3ModManager.exe"), 0)
        self.assertFalse(empty.exists())


class BackgroundTest(LaunchBase):
    def test_running_read_and_failed_quick_plan(self):
        self.install()
        launch = self.game.launch()

        class Alive:
            def is_alive(self):
                return True

        launch._background = Alive()
        self.assertTrue(launch.read_in_background())
        launch._background = None
        launch.plan = lambda entries: 1 / 0
        self.assertIsNone(launch.quick_plan())

    def test_reading_a_pak_that_disappears(self):
        self.install()
        launch = self.game.launch()
        unreadable: set = set()
        launch_module._read_paks(launch.cache, [self.tmp / "gone.pak"], unreadable)
        self.assertEqual(unreadable, set())
        self.assertIsNone(launch_module._stamp(self.tmp / "gone.pak"))

    def test_free_name(self):
        (self.tmp / "a.log").write_text("1")
        (self.tmp / "a-1.log").write_text("2")
        self.assertEqual(launch_module._free_name(self.tmp / "a.log").name, "a-2.log")
        self.assertEqual(launch_module._free_name(self.tmp / "b.log").name, "b.log")


class FakePlan(loadorder.Plan):
    def __init__(self, n: int = 1):
        super().__init__()
        self.missing = [(f"M{i}", f"D{i}") for i in range(n)]
        self.late = [("L", "D", "DMod"), ("L2", "D2", "d2")]
        self.needs_se = ["SE mod"]
        self.duplicates = [("Mod", "Old", "New")]
        self.conflicts = [("A", "B")]
        self.older = [("O", "Lib", "2.0.0.0", "1.0.0.0")]


class DiagnoseTest(LaunchBase):
    def check(self) -> diagnose.BG3LoadOrderCheck:
        check = diagnose.BG3LoadOrderCheck()
        self.assertTrue(check.init(self.organizer))
        return check

    def test_identity(self):
        check = self.check()
        self.assertEqual(check.name(), "BG3 Load Order Check")
        self.assertTrue(check.localizedName())
        self.assertEqual(check.author(), "MO2 Revamped")
        self.assertTrue(check.description())
        self.assertEqual(check.settings(), [])
        self.assertEqual(len(check.requirements()), 1)
        self.assertIsInstance(check.version(), mobase.VersionInfo)
        self.assertFalse(check.hasGuidedFix(diagnose.MISSING))
        check.startGuidedFix(diagnose.MISSING)
        self.assertEqual(check.shortDescription(99), "")
        self.assertEqual(len(createPlugins()), 5)

    def test_not_installed_or_other_game(self):
        check = self.check()
        self.assertEqual(check.activeProblems(), [])
        self.assertEqual(check.fullDescription(diagnose.MISSING), "")
        self.organizer.game = OtherGame()
        check._changed()
        self.assertEqual(check.activeProblems(), [])
        check._poll()

    def test_every_problem_described(self):
        self.install()
        check = self.check()
        launch = self.game.launch()
        launch.quick_plan = lambda: FakePlan(15)
        launch.read_in_background = lambda: False
        launch.dropped_last_run = [UA]
        launch.last_plan = None
        self.assertEqual(
            check.activeProblems(),
            [diagnose.MISSING, diagnose.LATE, diagnose.SCRIPT_EXTENDER, diagnose.DUPLICATES,
             diagnose.CONFLICTS, diagnose.OLDER, diagnose.DROPPED],
        )
        for key in range(7):
            self.assertTrue(check.shortDescription(key), key)
            self.assertTrue(check.fullDescription(key), key)
        missing = check.fullDescription(diagnose.MISSING)
        self.assertIn("M0 needs D0", missing)
        self.assertIn("and 3 more", missing)
        late = check.fullDescription(diagnose.LATE)
        self.assertIn("L needs D (DMod)", late)
        # the same name in another case isn't repeated
        self.assertIn("L2 needs D2<br>", late + "<br>")
        self.assertIn("Mod: in Old and New; New&#x27;s is used", check.fullDescription(diagnose.DUPLICATES))
        self.assertIn("O needs Lib 2.0.0.0, found 1.0.0.0", check.fullDescription(diagnose.OLDER))
        self.assertIn(UA, check.fullDescription(diagnose.DROPPED))
        self.assertEqual(check.fullDescription(42), "")
        # the plan is kept until something changes
        launch.quick_plan = lambda: self.fail("planned again")
        check.activeProblems()
        check._states_changed({"A": 1})
        self.assertFalse(check._fresh)

    def test_background_read_polled(self):
        self.install()
        check = self.check()
        launch = self.game.launch()
        launch.quick_plan = lambda: None
        polls = []
        check._poll_later = lambda: polls.append(1)
        launch.read_in_background = lambda: True
        check.activeProblems()
        self.assertEqual(polls, [1])

        class Alive:
            def __init__(self, alive):
                self.alive = alive

            def is_alive(self):
                return self.alive

        launch._background = Alive(True)
        check._poll()
        self.assertEqual(polls, [1, 1])
        launch._background = Alive(False)
        check._fresh = True
        check._poll()
        self.assertFalse(check._fresh)


class ToolsTest(LaunchBase):
    def plugins(self):
        found = {}
        for plugin in createPlugins():
            if isinstance(plugin, tools.BG3ToolPlugin):
                self.assertTrue(plugin.init(self.organizer))
                found[plugin.sub_name] = plugin
        return found

    def test_identity_and_icons(self):
        for name, plugin in self.plugins().items():
            self.assertEqual(plugin.name(), f"BG3 Tools: {name}")
            self.assertTrue(plugin.displayName().startswith("BG3 Tools/"))
            self.assertEqual(plugin.tooltip(), plugin.description())
            self.assertEqual(plugin.author(), "MO2 Revamped")
            self.assertEqual(plugin.settings(), [])
            self.assertEqual(len(plugin.requirements()), 1)
            self.assertIsInstance(plugin.version(), mobase.VersionInfo)
            # the SVG may not load without Qt's image plugins here; made once
            plugin.icon()
            self.assertIs(plugin.icon(), plugin.icon())
            plugin.setParentWidget(None)

    def test_other_game_does_nothing(self):
        self.organizer.game = OtherGame()
        for plugin in self.plugins().values():
            plugin.display()

    def test_check_for_updates_forces(self):
        lslib = FakeLSLib(True)
        self.game.lslib = lambda: lslib
        self.plugins()["Check For LsLib Updates"].display()
        self.assertEqual(lslib.forced, [True])

    def test_reparse_reads_every_pak_again(self):
        self.install()
        root = self.pak_mod("A", UA)
        self.game.divine = lambda: FakeDivine(available=False)
        launch = self.game.launch()
        launch.cache.put(root / "A.pak", {"kind": "override"})
        invalidated = []
        self.game._problems = lambda: invalidated.append(1)
        self.plugins()["Reparse Pak Metadata"].display()
        self.assertEqual(launch.cache.get(root / "A.pak")["module"]["uuid"], UA)
        self.assertEqual(invalidated, [1])

    def test_convert_jsons(self):
        self.install()
        root = self.organizer.add_mod("A", [])
        (root / "Script Extender").mkdir(parents=True)
        (root / "Script Extender" / "c.json").write_text('{"a": [1, {"b": null}]}', encoding="utf-8")
        (self.organizer.overwrite / "o.json").write_text("[]", encoding="utf-8")
        self.game.progress_window = lambda *a, **k: FakeProgress()
        self.plugins()["Convert JSONS to YAML"].display()
        self.assertEqual((root / "Script Extender" / "c.yaml").read_text(encoding="utf-8"), "a:\n  - 1\n  - b: null\n")
        self.assertEqual((self.organizer.overwrite / "o.yaml").read_text(encoding="utf-8"), "[]\n")

    def test_convert_jsons_canceled(self):
        self.install()
        self.organizer.add_mod("A", ["x.json"])
        (self.organizer.overwrite / "o.json").write_text("{}", encoding="utf-8")
        self.game.progress_window = lambda *a, **k: FakeProgress(cancel_after=0)
        self.plugins()["Convert JSONS to YAML"].display()
        self.assertFalse((self.organizer.overwrite / "o.yaml").exists())

    def test_load_order_window(self):
        opened = []

        class Dialog:
            def __init__(self, game, organizer, parent):
                opened.append((game, parent))

            def exec(self):
                opened.append("exec")

        with mock.patch.object(order_view, "LoadOrderDialog", Dialog):
            self.plugins()["Load Order"].display()
        self.assertEqual(opened, [(self.game, None), "exec"])


class GamePartsTest(LaunchBase):
    def test_version_from_the_exe(self):
        self.install()
        with mock.patch.object(plugin_module.mobase, "getProductVersion", lambda exe: "4.1.1.6758295"):
            self.assertEqual(self.game.gameVersion(), "4.1.1.6758295")

        def broken(exe):
            raise OSError("unreadable")

        with mock.patch.object(plugin_module.mobase, "getProductVersion", broken), \
                mock.patch.object(plugin_module.mobase, "getFileVersion", lambda exe: "1.0.0.0"):
            self.assertEqual(self.game.gameVersion(), "")

    def test_forced_loads_and_script_extender(self):
        self.install(["bin/DWrite.dll"])
        self.assertTrue(self.game.has_script_extender())
        self.organizer.add_mod("Wasd", ["bin/NativeMods/BG3WASD.dll"])
        loads = self.game.executableForcedLoads()
        self.assertEqual(sorted(l.process() for l in loads), ["bg3.exe", "bg3_dx11.exe"])
        self.organizer.setPluginSetting(NAME, "force_load_dlls", False)
        self.assertEqual(self.game.executableForcedLoads(), [])

    def test_script_extender_in_a_root_folder(self):
        self.assertFalse(self.game.has_script_extender())
        self.install()
        self.assertFalse(self.game.has_script_extender())
        self.organizer.add_mod("SE", ["Root/bin/DWrite.dll"])
        self.organizer.add_mod("Off", ["Root/bin/Other.dll"], active=False)
        self.assertTrue(self.game.has_script_extender())
        self.assertEqual(self.game.forced_dlls(), ["DWrite.dll"])
        self.organizer.enabled_plugins.add("Root Builder")
        self.assertEqual(self.game.forced_dlls(), [])

    def test_active_mods_skip_separators(self):
        self.organizer.add_mod("Group_separator", [])
        self.organizer.add_mod("A", ["a.pak"])
        self.assertEqual([m.name() for m in self.game.active_mods()], ["A"])

    def test_small_parts(self):
        self.install()
        window = object()
        self.game._on_ui(window)
        self.assertIs(self.game.main_window, window)
        self.game.main_window = None
        self.assertEqual(self.game.loadOrderMechanism(), mobase.LoadOrderMechanism.PLUGINS_TXT)
        self.assertEqual(self.game.tr("x"), "x")
        self.game._problems = None
        self.game.invalidate_problems()
        del self.game.lslib
        retriever = self.game.lslib()
        self.assertIs(self.game.lslib(), retriever)
        self.assertEqual(retriever._utils.tools_dir, self.game.plugin_data_path / "tools")
        self.assertEqual(self.game.divine().exe.name, "Divine.exe")
        self.game.process_events()

    def test_progress_windows(self):
        del self.game.progress_window
        progress = plugin_module.BG3Game.progress_window(self.game, "Reading", 3, cancelable=False)
        self.assertEqual(progress.maximum(), 3)
        progress.close()
        shown = []

        class Progress:
            def show(self):
                shown.append(1)

        self.game.progress_window = lambda title, maximum, cancelable: Progress()
        self.game.create_progress_window("t", 2, msg="m")
        self.assertEqual(shown, [1])

    def test_base_dlls_and_mod_installed_errors(self):
        self.install()
        self.assertIn("bink2w64.dll", self.game.base_dlls())

        class Broken:
            def absolutePath(self):
                raise RuntimeError("mod gone")

            def name(self):
                return "Broken"

        self.game._on_mod_installed(Broken())


if __name__ == "__main__":
    import unittest

    unittest.main()
