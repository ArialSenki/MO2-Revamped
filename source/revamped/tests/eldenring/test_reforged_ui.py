"""ELDEN RING Reforged's UI images (basic_games/games/eldenring/reforged_ui.py,
eldenring_mo2_tools/reforged_ui_tool.py): where the installer keeps Gideon,
how MO2 runs it and places its archives, the launch check and the
notifications. Gideon itself never runs here: a stand-in writes its files."""

from __future__ import annotations

import contextlib
import os
import tempfile
import unittest
from collections import namedtuple
from pathlib import Path
from unittest import mock

import mobase
from basic_games.games import game_eldenring_mo2 as er
from basic_games.games.eldenring import checks, reforged_ui
from eldenring_mo2_archive_installer.installer import EldenRingArchiveInstaller as Installer
from eldenring_mo2_tools import reforged_ui_tool

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from harness import make_tree
from test_cov_game_fakes import FakeBox
from test_plugin import tree_paths

RealRun = reforged_ui.GideonRun


@contextlib.contextmanager
def quiet_ui(*modules, answer=0, check=False):
    """FakeBox in place of the modules' QMessageBox (their timers stay)."""
    FakeBox.reset(answer, check)
    with contextlib.ExitStack() as stack:
        for module in modules:
            stack.enter_context(mock.patch.object(module, "QMessageBox", FakeBox))
        yield FakeBox

# what Gideon 2.6.0.0 printed in its pseudoconsole (run 2026-11-24, ERR 2.3.9.1)
GideonOutput = """===========================================
             Now setting up...
            ELDEN RING Reforged
===========================================
Welcome!

This tool will try to install the UI textures for ELDEN RING Reforged.
You'll need to run this again if you update the underlying mod, or this Gideon project.
Unpacking files from the ELDEN RING directory, please wait...

Successfully unpacked 2 files to mod folder.
Packing the Solo archive, please wait...
Solo archive: Successfully wrote 1713 files (1633 new files, 80 existing files replaced)

Process complete!
You can now launch the mod and enjoy it with the provided UI textures.
"""


def reforged_mod(organizer: FakeOrganizer, name="ELDEN RING Reforged", tools=True, version="1794976967",
                 online=False, active=True) -> Path:
    deploy = ("_ReforgedOnline/mod/menu/deploy" if online else "_ReforgedTools/menu/deploy")
    files = ["DLLs/reforged.dll", "menu/hi/01_common.tpf.dcx"]
    if tools:
        files += [f"{deploy}/Gideon.exe", f"{deploy}/projects/ELDENRINGReforged/project.ini"]
    root = organizer.add_mod(name, files, active=active)
    if tools:
        (root / deploy / "projects/ELDENRINGReforged/last-update.txt").write_text(version, encoding="utf-8")
    return root


def write_images(folder: Path, size: int = reforged_ui._MinimumArchive) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "00_Solo.tpfbhd").write_bytes(b"BHF4")
    with open(folder / "00_Solo.tpfbdt", "wb") as file:
        file.truncate(size)


class FakeRun:
    """GideonRun that writes Gideon's archives at once (or fails)."""

    instances: list = []
    outcome = "done"

    def __init__(self, tool: Path):
        self.tool = tool
        self.text = ""
        self.finished = False
        self.exit_code = None
        self.stopped = False
        self.answered = 0
        FakeRun.instances.append(self)

    def command(self, silent=False):
        return [str(self.tool / "Gideon.exe"), "--user", "--project", reforged_ui.Project]

    def start(self):
        made = self.tool.parent.parent / "menu" / "hi"
        if FakeRun.outcome in {"done", "waits"}:
            write_images(made)
            self.text = GideonOutput
        elif FakeRun.outcome == "fail":
            write_images(made, 10)
            self.text = "Unpacking files from the ELDEN RING directory, please wait...\n" \
                        "Something went wrong during the unpacking process:\nThe Game folder could not be located.\n"
        elif FakeRun.outcome == "hang":
            write_images(made)
            self.text = "Unpacking files from the ELDEN RING directory, please wait...\n"
            return
        self.exit_code = 0
        self.finished = FakeRun.outcome not in {"waits", "fail"}

    def poll(self):
        return self.text, 30.0

    def answer(self):
        self.answered += 1
        self.finished = True
        self.exit_code = 0

    def stop(self):
        self.stopped = True
        self.finished = True
        self.exit_code = 1

    def close(self):
        pass


class FakeProgress:
    canceled = False

    def __init__(self, *_args):
        self.labels = []

    def __getattr__(self, name):
        return lambda *args: None

    def setLabelText(self, text):
        self.labels.append(text)

    def wasCanceled(self):
        return FakeProgress.canceled


class FakeTimer:
    def __init__(self):
        self.slot = None
        outer = self

        class Signal:
            def connect(self, slot):
                outer.slot = slot

        self.timeout = Signal()
        FakeLoop.timers.append(self)

    def start(self, *_args):
        pass

    def stop(self):
        pass


class FakeLoop:
    """QEventLoop running only the run's own timer: no other test's timers."""

    timers: list = []

    def __init__(self):
        self.running = False

    def exec(self):
        self.running = True
        for _ in range(50):
            if not self.running:
                break
            for timer in list(FakeLoop.timers):
                if timer.slot is not None:
                    timer.slot()
        return 0

    def quit(self):
        self.running = False


class UiCase(unittest.TestCase):
    def setUp(self):
        self._appdata = os.environ.get("APPDATA")
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game_dir = self.organizer.game_dir
        (self.game_dir / "Game" / "Data0.bhd").write_bytes(b"BHD5")
        FakeRun.instances, FakeRun.outcome = [], "done"
        FakeProgress.canceled = False
        FakeLoop.timers = []
        for target, value in (("GideonRun", FakeRun), ("QProgressDialog", FakeProgress),
                              ("QEventLoop", FakeLoop), ("QTimer", FakeTimer)):
            patcher = mock.patch.object(reforged_ui, target, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def tearDown(self):
        if self._appdata is None:
            os.environ.pop("APPDATA", None)
        else:
            os.environ["APPDATA"] = self._appdata

    def create(self, root, organizer=None):
        with quiet_ui(reforged_ui) as box:
            result = reforged_ui.create_images(None, "ELDEN RING Reforged", root, self.game_dir, organizer)
        return result, box.shown


class Layout(unittest.TestCase):
    def setUp(self):
        self.checker = er.EldenRingModDataChecker(FakeOrganizer(), "Elden Ring MO2 Support")
        self.checker.previewing = True
        self.paths = ["ERR/mod/regulation.bin", "ERR/mod/menu/hi/01_common.tpf.dcx",
                      "ERR/mod/menu/deploy/Gideon.exe", "ERR/mod/menu/deploy/libzstd.dll",
                      "ERR/mod/menu/deploy/oo2core_6_win64.dll",
                      "ERR/mod/menu/deploy/projects/ELDENRINGReforged/solo/MENU_Tuto_1.tpf.dcx",
                      "ERR/internals/modengine/dll/reforged.dll",
                      "ERR/internals/launcher/ReforgedLauncher.exe", "ERR/README.md"]

    def normalize(self, **options):
        return tree_paths(self.checker.normalize_standard_layout(make_tree(self.paths), **options))

    def test_gideon_kept_out_of_the_game_files(self):
        paths = self.normalize()
        self.assertIn("_ReforgedTools/menu/deploy/Gideon.exe", paths)
        self.assertIn("_ReforgedTools/menu/deploy/projects/ELDENRINGReforged/solo/MENU_Tuto_1.tpf.dcx", paths)
        # its libraries are not natives, and nothing of it is at menu/deploy
        self.assertIn("_ReforgedTools/menu/deploy/libzstd.dll", paths)
        self.assertNotIn("DLLs/libzstd.dll", paths)
        self.assertFalse(any(p.startswith("menu/deploy") for p in paths))
        self.assertIn("menu/hi/01_common.tpf.dcx", paths)
        self.assertIn("DLLs/reforged.dll", paths)

    def test_online_release_keeps_it_in_place(self):
        paths = self.normalize(keep_reforged_online=True)
        self.assertIn("_ReforgedOnline/mod/menu/deploy/Gideon.exe", paths)
        self.assertFalse(any(p.startswith("_ReforgedTools") for p in paths))

    def test_tools_folder_is_valid_content(self):
        tree = make_tree(["_ReforgedTools/menu/deploy/Gideon.exe", "regulation.bin", "DLLs/reforged.dll"])
        self.assertEqual(self.checker.dataLooksValid(tree), self.checker.VALID)

    def test_installed_dll_routes_skip_the_tool(self):
        root = Path(tempfile.mkdtemp(prefix="er-ui-")) / "Mod"
        for relative in ("DLLs/reforged.dll", "_ReforgedTools/menu/deploy/libzstd.dll"):
            (root / relative).parent.mkdir(parents=True, exist_ok=True)
            (root / relative).write_bytes(b"MZ")
        self.assertEqual(Installer._collect_installed_dll_routes(root), ["DLLs/reforged.dll"])

    def test_summary_counts_the_tool(self):
        summary = Installer._classify_layout_paths(
            ["_ReforgedTools/menu/deploy/Gideon.exe", "_ReforgedTools/menu/deploy/a.dll", "parts/a.dcx"], False
        )
        self.assertEqual((summary["tools"], summary["dlls"], summary["other"], summary["game"]), (2, 0, 0, 1))


class Status(UiCase):
    def test_states(self):
        root = reforged_mod(self.organizer)
        self.assertEqual(reforged_ui.find_gideon(root), root / "_ReforgedTools/menu/deploy")
        self.assertEqual(reforged_ui.status(root), "missing")
        self.assertFalse(reforged_ui.up_to_date(root))
        # images copied by hand: taken as they are
        write_images(root / "menu/hi")
        self.assertEqual(reforged_ui.status(root), "ready")
        self.assertFalse(reforged_ui.up_to_date(root))
        stamp = root / "_ReforgedTools/menu/deploy/projects/ELDENRINGReforged" / reforged_ui.StampFile
        stamp.write_text("1794976967", encoding="utf-8")
        self.assertTrue(reforged_ui.up_to_date(root))
        stamp.write_text("1700000000", encoding="utf-8")
        self.assertEqual(reforged_ui.status(root), "outdated")
        # a cut-off archive is not one
        write_images(root / "menu/hi", 100)
        self.assertEqual(reforged_ui.status(root), "missing")

    def test_without_the_tool(self):
        root = reforged_mod(self.organizer, tools=False)
        self.assertIsNone(reforged_ui.find_gideon(root))
        self.assertTrue(reforged_ui.is_reforged_mod(root))
        self.assertEqual(reforged_ui.status(root), "no-tool")
        self.assertIn("Reinstall", reforged_ui.check_before_run(root, self.game_dir))

    def test_checks_before_running(self):
        root = reforged_mod(self.organizer)
        self.assertIsNone(reforged_ui.check_before_run(root, self.game_dir))
        self.assertIsNone(reforged_ui.check_before_run(root, self.game_dir / "Game"))
        self.assertIn("game folder", reforged_ui.check_before_run(root, None))
        self.assertIn("game folder", reforged_ui.check_before_run(root, self.game_dir / "elsewhere"))
        usage = namedtuple("usage", "total used free")
        with mock.patch.object(reforged_ui.shutil, "disk_usage", return_value=usage(1, 1, 2 * 1024**3)):
            text = reforged_ui.check_before_run(root, self.game_dir)
        self.assertIn("4 GB", text)
        self.assertIn("2.0 GB", text)

    def test_providers(self):
        root = reforged_mod(self.organizer)
        write_images(root / "menu/hi")
        conv = self.organizer.add_mod("The Convergence", ["menu/hi/00_solo.tpfbdt", "menu/hi/00_solo.tpfbhd"])
        self.organizer.add_mod("Off", ["menu/hi/00_Solo.tpfbhd"], active=False)
        mods = [(name, self.organizer.mods / name, name in self.organizer.active) for name in self.organizer.mod_order]
        self.assertEqual(reforged_ui.active_reforged(mods), ("ELDEN RING Reforged", root))
        others, winner = reforged_ui.other_providers(mods, "ELDEN RING Reforged")
        self.assertEqual((others, winner), (["The Convergence"], "The Convergence"))
        self.assertIn("higher priority than ELDEN RING Reforged", reforged_ui.conflict_text(others, winner, "ELDEN RING Reforged"))
        self.assertIn("Reforged's, which has the highest", reforged_ui.conflict_text(others, "ELDEN RING Reforged", "ELDEN RING Reforged"))
        self.assertTrue(conv.is_dir())

    def test_gideon_text(self):
        self.assertIsNone(reforged_ui.failure_text(GideonOutput))
        self.assertIn("Copying", reforged_ui.stage(GideonOutput[:GideonOutput.index("Successfully")]))
        self.assertIn("Adding", reforged_ui.stage(GideonOutput[:GideonOutput.index("Solo archive:")]))
        self.assertIn("Finishing", reforged_ui.stage(GideonOutput))
        failed = reforged_ui.failure_text("Unpacking...\nCould not locate your game folder. Please select it manually to proceed.\n")
        self.assertTrue(failed.startswith("Could not locate"))
        self.assertIsNotNone(reforged_ui.failure_text("Unhandled exception. System.IO.IOException: x"))

    def test_command(self):
        run = RealRun(Path("C:/x/deploy"))
        self.assertEqual(run.command()[1:], ["--user", "--project", "ELDENRINGReforged"])
        self.assertIn("--silent", run.command(silent=True))


class Create(UiCase):
    def test_images_placed_and_stamped(self):
        root = reforged_mod(self.organizer)
        # an old copy where Gideon writes would be its base: removed first
        write_images(root / "_ReforgedTools/menu/hi", 5)
        self.organizer.refresh = mock.Mock()
        result, shown = self.create(root, self.organizer)
        self.assertTrue(result, shown)
        self.assertEqual(shown, [])
        self.assertTrue((root / "menu/hi/00_Solo.tpfbdt").stat().st_size >= reforged_ui._MinimumArchive)
        self.assertFalse((root / "_ReforgedTools/menu/hi/00_Solo.tpfbdt").exists())
        self.assertTrue(reforged_ui.up_to_date(root))
        self.organizer.refresh.assert_called_once_with(True)
        self.assertGreaterEqual(FakeRun.instances[0].answered, 0)

    def test_replaces_another_spelling(self):
        root = reforged_mod(self.organizer)
        (root / "menu/hi/00_solo.tpfbdt").write_bytes(b"old")
        result, _shown = self.create(root)
        self.assertTrue(result)
        names = sorted(p.name for p in (root / "menu/hi").iterdir())
        self.assertEqual(names, ["00_Solo.tpfbdt", "00_Solo.tpfbhd", "01_common.tpf.dcx"])

    def test_online_release_gets_links(self):
        root = reforged_mod(self.organizer, online=True)
        result, _shown = self.create(root)
        self.assertTrue(result)
        placed = root / "menu/hi/00_Solo.tpfbdt"
        linked = root / "_ReforgedOnline/mod/menu/hi/00_Solo.tpfbdt"
        self.assertTrue(placed.is_file())
        self.assertTrue(linked.is_file())
        self.assertTrue(os.path.samefile(placed, linked))

    def test_waits_for_the_key(self):
        FakeRun.outcome = "waits"
        root = reforged_mod(self.organizer)
        result, _shown = self.create(root)
        self.assertTrue(result)
        self.assertEqual(FakeRun.instances[0].answered, 1)

    def test_failure_leaves_nothing(self):
        FakeRun.outcome = "fail"
        root = reforged_mod(self.organizer)
        result, shown = self.create(root)
        self.assertFalse(result)
        self.assertEqual(shown[0][0], "box")
        self.assertIn("The Game folder could not be located", shown[0][2])
        self.assertFalse((root / "_ReforgedTools/menu/hi/00_Solo.tpfbdt").exists())
        self.assertFalse((root / "menu/hi/00_Solo.tpfbdt").exists())
        self.assertTrue(FakeRun.instances[0].stopped)

    def test_cancel(self):
        FakeRun.outcome = "hang"
        FakeProgress.canceled = True
        root = reforged_mod(self.organizer)
        write_images(root / "menu/hi")
        before = (root / "menu/hi/00_Solo.tpfbdt").stat().st_mtime_ns
        result, shown = self.create(root)
        self.assertFalse(result)
        self.assertEqual(shown, [])
        self.assertTrue(FakeRun.instances[0].stopped)
        # the images it had stay as they were
        self.assertEqual((root / "menu/hi/00_Solo.tpfbdt").stat().st_mtime_ns, before)
        self.assertFalse((root / "_ReforgedTools/menu/hi/00_Solo.tpfbdt").exists())

    def test_refused_before_running(self):
        root = reforged_mod(self.organizer, tools=False)
        result, shown = self.create(root)
        self.assertFalse(result)
        self.assertEqual(shown[0][0], "warning")
        self.assertEqual(FakeRun.instances, [])


class LaunchCheck(UiCase):
    def setUp(self):
        super().setUp()
        self.game = game_for(self.organizer)
        self.app = str(self.organizer.game_dir / "Game" / "eldenring.exe")

    def test_no_reforged_asks_nothing(self):
        self.organizer.add_mod("Other", ["parts/a.dcx"])
        with quiet_ui(checks) as box:
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        self.assertEqual(box.shown, [])

    def test_missing_images_created(self):
        root = reforged_mod(self.organizer)
        self.organizer.refresh = mock.Mock()
        with quiet_ui(checks, reforged_ui, answer=0) as box:
            # made now; MO2 reads them for the next launch
            self.assertFalse(self.game._reforged_ui_images_ready(self.app))
        self.assertIn("missing", box.shown[0][2])
        self.assertEqual(box.shown[-1][0], "information")
        self.assertTrue(reforged_ui.up_to_date(root))
        with quiet_ui(checks, answer=2) as box:
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        self.assertEqual(box.shown, [])

    def test_start_anyway_remembered(self):
        reforged_mod(self.organizer)
        with quiet_ui(checks, answer=2) as box:
            self.assertFalse(self.game._reforged_ui_images_ready(self.app))
        with quiet_ui(checks, answer=1, check=True):
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        with quiet_ui(checks, answer=2) as box:
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        self.assertEqual(box.shown, [])
        self.assertEqual(FakeRun.instances, [])

    def test_old_install_without_the_tool(self):
        reforged_mod(self.organizer, tools=False)
        with quiet_ui(checks, answer=0) as box:
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        self.assertIn("Reinstall", box.shown[0][2])

    def test_conflict(self):
        root = reforged_mod(self.organizer)
        write_images(root / "menu/hi")
        self.organizer.add_mod("The Convergence", ["menu/hi/00_Solo.tpfbhd", "menu/hi/00_Solo.tpfbdt"])
        with quiet_ui(checks, answer=1) as box:
            self.assertFalse(self.game._reforged_ui_images_ready(self.app))
        self.assertIn("The Convergence", box.shown[0][2])
        with quiet_ui(checks, answer=0, check=True):
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(self.game._reforged_ui_images_ready(self.app))
        self.assertEqual(box.shown, [])

    def test_nightreign_asks_nothing(self):
        reforged_mod(self.organizer)
        nightreign = game_for(FakeOrganizer(), nightreign=True)
        with quiet_ui(checks) as box:
            self.assertTrue(nightreign._reforged_ui_images_ready("nightreign.exe"))
        self.assertEqual(box.shown, [])


class ToolAndNotices(UiCase):
    def setUp(self):
        super().setUp()
        self.game = game_for(self.organizer)
        self.tool, self.diagnose = reforged_ui_tool.createPlugins()
        self.assertTrue(self.tool.init(self.organizer))
        self.assertTrue(self.diagnose.init(self.organizer))

    def test_names(self):
        self.assertEqual(self.tool.name(), "Elden Ring MO2 Reforged UI Images")
        self.assertEqual(self.tool.displayName(), "Reforged UI images...")
        self.assertTrue(self.tool.requirements())
        self.assertEqual(self.diagnose.localizedName(), "Reforged UI image checks")

    def test_notices(self):
        self.assertEqual(self.diagnose.activeProblems(), [])
        root = reforged_mod(self.organizer)
        self.assertEqual(self.diagnose.activeProblems(), [self.diagnose.Missing])
        self.assertTrue(self.diagnose.hasGuidedFix(self.diagnose.Missing))
        self.assertIn("Gideon", self.diagnose.fullDescription(self.diagnose.Missing))
        write_images(root / "menu/hi")
        self.assertEqual(self.diagnose.activeProblems(), [])
        self.organizer.add_mod("The Convergence", ["menu/hi/00_Solo.tpfbdt"])
        self.assertEqual(self.diagnose.activeProblems(), [self.diagnose.Conflict])
        self.assertIn("The Convergence", self.diagnose.fullDescription(self.diagnose.Conflict))
        self.assertFalse(self.diagnose.hasGuidedFix(self.diagnose.Conflict))

    def test_notice_old_install(self):
        reforged_mod(self.organizer, tools=False)
        self.assertEqual(self.diagnose.activeProblems(), [self.diagnose.NoTool])
        self.assertIn("Reinstall", self.diagnose.fullDescription(self.diagnose.NoTool))

    def test_tool_makes_them(self):
        root = reforged_mod(self.organizer)
        self.organizer.refresh = mock.Mock()
        self.diagnose._invalidate = mock.Mock()
        with quiet_ui(reforged_ui_tool, reforged_ui, answer=0) as box:
            self.tool.display()
        self.assertTrue(reforged_ui.up_to_date(root))
        self.assertEqual(box.shown[-1][0], "information")
        self.diagnose._invalidate.assert_called_once()

    def test_tool_without_reforged(self):
        with quiet_ui(reforged_ui_tool) as box:
            self.tool.display()
        self.assertEqual(box.shown[0][0], "information")
        self.assertEqual(FakeRun.instances, [])


class AfterInstall(UiCase):
    def test_runs_unless_up_to_date(self):
        self.game = game_for(self.organizer)
        installer = Installer()
        self.assertTrue(installer.init(self.organizer))
        root = reforged_mod(self.organizer)
        with mock.patch.object(installer, "_parentWidget", return_value=None), quiet_ui(reforged_ui):
            installer._create_reforged_ui_images("ELDEN RING Reforged", str(root))
            self.assertEqual(len(FakeRun.instances), 1)
            installer._create_reforged_ui_images("ELDEN RING Reforged", str(root))
        self.assertEqual(len(FakeRun.instances), 1)
        self.assertTrue(reforged_ui.up_to_date(root))


class PluginFile(unittest.TestCase):
    def load(self, game):
        import importlib.util
        import sys

        from PyQt6.QtCore import QCoreApplication

        from harness import PLUGINS

        app = QCoreApplication.instance()
        before = app.property("revampedInstanceGame")
        self.addCleanup(lambda: app.setProperty("revampedInstanceGame", before))
        app.setProperty("revampedInstanceGame", game)
        path = PLUGINS / "eldenring_mo2_reforged_ui.py"
        spec = importlib.util.spec_from_file_location("cov_reforged_ui_file", path)
        module = importlib.util.module_from_spec(spec)
        saved = list(sys.path)
        try:
            spec.loader.exec_module(module)
        finally:
            sys.path[:] = saved
        return module.createPlugins()

    def test_stand_ins_in_another_game(self):
        plugins = self.load("Skyrim Special Edition")
        self.assertEqual([type(p).__name__ for p in plugins], ["ToolStub", "DiagnoseStub"])
        self.assertEqual(plugins[0].name(), "Elden Ring MO2 Reforged UI Images")
        self.assertEqual(plugins[1].name(), "Elden Ring MO2 Reforged UI Diagnose")

    def test_real_ones_in_elden_ring(self):
        plugins = self.load("ELDEN RING")
        self.assertEqual([type(p).__name__ for p in plugins],
                         ["EldenRingMo2ReforgedUiTool", "EldenRingMo2ReforgedUiDiagnose"])


if __name__ == "__main__":
    unittest.main()
