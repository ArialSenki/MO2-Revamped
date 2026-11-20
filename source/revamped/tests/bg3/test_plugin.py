"""The Baldur's Gate 3 game plugin over a fake game folder, mods, Overwrite
and documents folder."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_baldursgate3 as plugin_module
from basic_games.games.baldursgate3 import launch as launch_module
from basic_games.games.baldursgate3 import modsettings
from basic_games.games.baldursgate3.layout import BG3DataContent, BG3ModDataChecker, Content
from basic_games.games.baldursgate3.saves import list_saves, save_metadata
from fake_organizer import FakeOrganizer
from harness import make_tree
from paktools import mod_pak, write_pak

Game = plugin_module.BG3Game
NAME = Game.Name
UA, UB, UC = (f"10000000-0000-0000-0000-00000000000{i}" for i in (1, 2, 3))


class FakeProgress:
    def __init__(self, cancel_after: int | None = None):
        self.cancel_after = cancel_after
        self.calls = 0

    def setValue(self, value):
        pass

    def wasCanceled(self):
        self.calls += 1
        return self.cancel_after is not None and self.calls > self.cancel_after

    def close(self):
        pass

    def show(self):
        pass


class PluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg3-test-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))
        self.docs = self.tmp / "Local" / "Larian Studios" / "Baldur's Gate 3"
        self.game.documentsDirectory = lambda: QDir(str(self.docs))
        self.game.progress_window = lambda *a, **k: FakeProgress()
        # no download, no network, no window in these tests
        self.game.lslib = lambda: self.fail("LSLib was asked for")

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self, extra=()) -> Path:
        game_dir = self.tmp / "Baldurs Gate 3"
        for relative in ["bin/bg3.exe", "bin/bg3_dx11.exe", "bin/bink2w64.dll", "bin/Osiris.dll", *extra]:
            file = game_dir / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"game")
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def pak_mod(self, name: str, uuid: str, active=True, **meta) -> Path:
        root = self.organizer.add_mod(name, [], active)
        root.mkdir(parents=True, exist_ok=True)
        mod_pak(root / f"{name}.pak", name, uuid, **meta)
        return root

    def run_game(self) -> bool:
        exe = Path(self.game.gameDirectory().absolutePath(), "bin/bg3.exe")
        return self.game._on_about_to_run(str(exe), QDir(str(exe.parent)), "")

    def written(self) -> list[str]:
        return [m["name"] for m in modsettings.read(self.game.modsettings_path).modules]

    # identity

    def test_identity_is_basic_games(self):
        self.assertEqual(self.game.name(), "Baldur's Gate 3 Plugin")
        self.assertEqual(self.game.gameName(), "Baldur's Gate 3")
        self.assertEqual(self.game.gameShortName(), "baldursgate3")
        self.assertEqual(self.game.gameNexusName(), "baldursgate3")
        self.assertIn("bg3", self.game.validShortNames())
        self.assertEqual(self.game.nexusGameID(), 3474)
        self.assertEqual(self.game.steamAPPId(), "1086940")
        self.assertEqual(self.game.gogAPPId(), "1456460669")
        keys = {s.key for s in self.game.settings()}
        for old in ("force_load_dlls", "log_diff", "delete_levelcache_folders_older_than_x_days",
                    "autobuild_paks", "convert_yamls_to_json"):
            self.assertIn(old, keys)

    def test_not_installed_is_quiet(self):
        self.assertEqual(self.game.executables(), [])
        self.assertEqual(self.game.executableForcedLoads(), [])
        self.assertEqual(self.game.gameVersion(), "")
        self.assertEqual(self.game.mappings(), [])

    def test_executables_keep_their_titles(self):
        self.install(["Launcher/LariLauncher.exe"])
        titles = [e.title() for e in self.game.executables()]
        self.assertEqual(titles, ["Baldur's Gate 3: DX11", "Baldur's Gate 3: Vulkan", "Larian Launcher"])

    def test_import_is_light(self):
        code = (
            "import sys, time; sys.path[:0] = %r; import harness; harness.setup_mobase();"
            "import basic_games; before = set(sys.modules);"
            "import basic_games.games.game_baldursgate3 as g;"
            "g.BG3Game(); heavy = [m for m in ('basic_games.games.baldursgate3.loadorder',"
            "'basic_games.games.baldursgate3.lslib','basic_games.games.baldursgate3.launch',"
            "'urllib.request','xml.etree.ElementTree','xml.sax') if m in sys.modules and m not in before];"
            "print(heavy)" % [str(Path(__file__).parent), sys.path[0]]
        )
        out = subprocess.run([sys.executable, "-I", "-c", code], capture_output=True, text=True, timeout=120)
        self.assertEqual(out.stdout.strip().splitlines()[-1], "[]", out.stderr)

    # DLLs

    def test_forced_loads(self):
        self.install()
        self.organizer.add_mod("SE", ["bin/DWrite.dll"])
        self.organizer.add_mod("WASD", ["bin/NativeMods/BG3WASD.dll", "bin/NativeMods/BG3WASD.toml"])
        self.organizer.add_mod("Bink", ["bin/Bink2W64.dll"])
        # (the fake VFS tree has lower-case names)
        dlls = sorted(d.casefold() for d in self.game.forced_dlls())
        self.assertEqual(dlls, ["dwrite.dll", "nativemods/bg3wasd.dll"])
        self.organizer.add_mod("NML", ["bin/bink2w64.dll", "bin/bink2w64_original.dll"])
        self.assertEqual([d.casefold() for d in self.game.forced_dlls()], ["dwrite.dll"])
        loads = self.game.executableForcedLoads()
        self.assertEqual(sorted(l.process() for l in loads), ["bg3.exe", "bg3_dx11.exe"])
        self.organizer.setPluginSetting(NAME, "force_load_dlls", False)
        self.assertEqual(self.game.executableForcedLoads(), [])

    def test_script_extender_found(self):
        self.install()
        self.assertFalse(self.game.has_script_extender())
        self.organizer.add_mod("SE", ["bin/DWrite.dll"])
        self.assertTrue(self.game.has_script_extender())

    # load order at start

    def test_start_writes_the_order_and_reads_each_pak_once(self):
        self.install()
        self.pak_mod("Low", UA)
        self.pak_mod("High", UB, deps=[("Low", UA)])
        self.pak_mod("Off", UC, active=False)
        self.assertTrue(self.run_game())
        self.assertEqual(self.written(), ["GustavX", "Low", "High"])
        cache = json.loads((self.game.plugin_data_path / "pak_cache.json").read_text(encoding="utf-8"))
        self.assertEqual(len(cache["paks"]), 2)
        mapped = {Path(m.destination).name for m in self.game.mappings()}
        self.assertIn("Low.pak", mapped)
        self.assertIn("modsettings.lsx", mapped)

    def test_cancel_writes_nothing_and_stops_the_start(self):
        self.install()
        for i in range(3):
            self.pak_mod(f"M{i}", f"20000000-0000-0000-0000-00000000000{i}")
        self.game.progress_window = lambda *a, **k: FakeProgress(cancel_after=0)
        launch = self.game.launch()
        real = launch_module.loadorder.read_mods

        def slow(*args, **kwargs):
            import time

            time.sleep(0.2)
            return real(*args, **kwargs)

        launch_module.loadorder.read_mods = slow
        try:
            self.assertFalse(self.run_game())
        finally:
            launch_module.loadorder.read_mods = real
        self.assertFalse(self.game.modsettings_path.exists())

    def test_missing_dependency_asks_and_cancel_keeps_the_old_file(self):
        self.install()
        self.pak_mod("Needs", UA, deps=[("Lib", UB)])
        self.game.modsettings_path.write_text("old order", encoding="utf-8")
        asked = []
        launch = self.game.launch()
        launch._ask_missing = lambda plan: asked.append(plan.missing) or False
        self.assertFalse(self.run_game())
        self.assertEqual(asked, [[("Needs", "Lib")]])
        self.assertEqual(self.game.modsettings_path.read_text(encoding="utf-8"), "old order")
        self.organizer.setPluginSetting(NAME, "warn_missing_dependencies", False)
        self.assertTrue(self.run_game())
        backups = list(self.game.modsettings_backups.glob("*.lsx"))
        self.assertEqual([b.read_text(encoding="utf-8") for b in backups], ["old order"])

    def test_unreadable_pak_is_named_and_others_load(self):
        self.install()
        self.pak_mod("Good", UA)
        bad = self.organizer.add_mod("Bad", [])
        bad.mkdir(parents=True, exist_ok=True)
        (bad / "Bad.pak").write_bytes(b"LSPK\x12\x00\x00\x00 cut")
        warned = []
        self.game.launch()._warn_left_out = lambda plan: warned.append(plan.left_out)
        self.assertTrue(self.run_game())
        self.assertEqual(self.written(), ["GustavX", "Good"])
        self.assertEqual(warned[0][0][0], "Bad")

    def test_mods_enabled_outside_mo2_are_kept(self):
        self.install()
        self.pak_mod("Mine", UA)
        mod_pak(self.docs / "Mods" / "ModIo_x.pak", "ModIo", UB)
        game_file = self.docs / "PlayerProfiles" / "Public" / "modsettings.lsx"
        game_file.parent.mkdir(parents=True, exist_ok=True)
        game_file.write_text(
            modsettings.render(
                [modsettings.GUSTAVX,
                 {"name": "ModIo", "folder": "ModIo", "uuid": UB, "version64": 1, "publish_handle": 77},
                 {"name": "Gone", "folder": "Gone", "uuid": UC, "version64": 1}],
                (4, 8, 0, 700),
            ),
            encoding="utf-8",
        )
        self.assertTrue(self.run_game())
        self.assertEqual(self.written(), ["GustavX", "Mine", "ModIo"])
        self.assertEqual(modsettings.read(self.game.modsettings_path).version, (4, 8, 0, 700))

    def test_game_turning_mods_off_is_noticed(self):
        self.install()
        self.pak_mod("A", UA)
        self.pak_mod("B", UB)
        self.assertTrue(self.run_game())
        text = self.game.modsettings_path.read_text(encoding="utf-8")
        start = text.index(f'value="{UB}"')
        node_start = text.rindex('<node id="ModuleShortDesc">', 0, start)
        node_end = text.index("</node>", start) + len("</node>")
        self.game.modsettings_path.write_text(text[:node_start] + text[node_end:], encoding="utf-8")
        exe = Path(self.game.gameDirectory().absolutePath(), "bin/bg3.exe")
        self.game._on_finished_run(str(exe), 0)
        self.assertEqual(self.game.launch().dropped_last_run, [UB])

    def test_crash_check_folder_is_cleared(self):
        self.install()
        marker = self.docs / "ModCrashSanityCheck" / "x.lsx"
        marker.parent.mkdir(parents=True)
        marker.write_text("crash")
        self.assertTrue(self.run_game())
        self.assertTrue(marker.parent.is_dir())
        self.assertFalse(marker.exists())

    def test_other_programs_dont_touch_the_order(self):
        self.install()
        self.pak_mod("A", UA)
        self.assertTrue(self.game._on_about_to_run(str(self.tmp / "BG3ModManager.exe"), QDir(), ""))
        self.assertFalse(self.game.modsettings_path.exists())

    # layout

    def test_checker_and_content(self):
        checker = BG3ModDataChecker()
        content = BG3DataContent()
        valid = make_tree(["Mods/x.pak", "readme.txt"])
        self.assertEqual(checker.dataLooksValid(valid), mobase.ModDataChecker.VALID)
        self.assertEqual(checker.dataLooksValid(make_tree(["x.dll"])), mobase.ModDataChecker.FIXABLE)
        self.assertEqual(checker.dataLooksValid(make_tree(["what.exe"])), mobase.ModDataChecker.INVALID)
        self.assertEqual(
            content.getContentsFor(make_tree(["bin/DWrite.dll"])), [Content.SCRIPT_EXTENDER]
        )
        self.assertEqual(
            content.getContentsFor(make_tree(["bin/bink2w64.dll", "bin/bink2w64_original.dll"])),
            [Content.NATIVE_MOD_LOADER],
        )
        self.assertEqual(
            content.getContentsFor(make_tree(["bin/NativeMods/BG3WASD.dll", "a.pak", "Data/Public/x"])),
            sorted([Content.NATIVE, Content.PAK, Content.LOOSE_FILES]),
        )
        self.assertEqual(len(content.getAllContents()), 7)

    # saves

    def test_saves(self):
        story = self.tmp / "Story"
        info = {"Save Name": "Before the goblins", "Game Version": "4.1.1.7631656", "Difficulty": ["Balanced"],
                "Current Level": "WLD_Main_A",
                "Active Party": {"Characters": [{"Origin": "Tav", "Level": 4, "Classes": [{"Main": "Wizard"}]}]}}
        write_pak(story / "Tav-1__QuickSave_1" / "Tav-1__QuickSave_1.lsv", {"SaveInfo.json": json.dumps(info).encode()})
        (story / "Tav-1__QuickSave_1" / "Tav-1__QuickSave_1.webp").write_bytes(b"img")
        odd = story / "Odd" / "Odd.lsv"
        odd.parent.mkdir(parents=True)
        odd.write_bytes(b"not a package")
        saves = list_saves(story)
        self.assertEqual(len(saves), 2)
        named = next(s for s in saves if "QuickSave" in s.getFilepath())
        self.assertEqual(named.getName(), "Before the goblins")
        self.assertEqual(len(named.allFiles()), 2)
        meta = save_metadata(Path(named.getFilepath()), named)
        self.assertIn("Tav Wizard (level 4)", meta.values())
        self.assertIn("Balanced", meta.values())
        odd_save = next(s for s in saves if "Odd" in s.getFilepath())
        self.assertEqual(odd_save.getName(), "Odd")
