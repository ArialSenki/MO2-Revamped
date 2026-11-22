"""The Elden Ring and Nightreign game plugin outside MO2: its identity, the
mod data checker, the save recovery and swap, backups, the native DLL
profile, forced loads, the launch environment and the online programs."""

from __future__ import annotations

import configparser
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import mobase
from basic_games.games import game_eldenring_mo2 as er

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from harness import make_tree


def tree_paths(tree) -> list[str]:
    paths: list[str] = []

    def visit(path: str, entry) -> mobase.IFileTree.WalkReturn:
        full = (path + entry.name()).replace("\\", "/")
        if isinstance(entry, mobase.IFileTree):
            if len(entry) == 0:
                paths.append(full + "/")
        else:
            paths.append(full)
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(visit, "/")
    return sorted(paths)


class Identity(unittest.TestCase):
    """What instances, settings and Nexus know the plugins by."""

    def test_elden_ring(self):
        game = er.EldenRingMo2Game()
        self.assertEqual(game.name(), "Elden Ring MO2 Support")
        self.assertEqual(game.gameName(), "ELDEN RING")
        self.assertEqual(game.gameShortName(), "eldenring")
        self.assertEqual(game.gameNexusName(), "eldenring")
        self.assertEqual(game.GameSteamId, 1245620)
        keys = [s.key for s in game.settings()]
        self.assertIn("load_native_dlls", keys)
        self.assertIn("save_backups_kept", keys)
        self.assertIs(game.LocalSavegamesClass, er.EldenRingLocalSavegames)

    def test_nightreign(self):
        game = er.EldenRingNightreignMo2Game()
        self.assertEqual(game.name(), "Elden Ring Nightreign MO2 Support")
        self.assertEqual(game.gameName(), "ELDEN RING NIGHTREIGN")
        self.assertEqual(game.gameShortName(), "nightreign")
        self.assertEqual(game.GameProcess, "nightreign.exe")
        self.assertIs(game.LocalSavegamesClass, er.NightreignLocalSavegames)
        self.assertEqual(game.SteamCloudSaves, ("NR0000.sl2", "NR0000.sl2.bak"))

    def test_names_the_others_import(self):
        # the archive installer and the native DLL profile tool import these
        for name in ("EldenRingModDataChecker", "EldenRingMo2Game", "EldenRingLocalSavegames"):
            self.assertTrue(hasattr(er, name), name)
        # MO2 Revamped's About window reads these from this file
        text = Path(er.__file__).read_text(encoding="utf-8")
        self.assertRegex(text, r'NativeBridgeVersion = "[^"]+"')
        self.assertRegex(text, r'    Version = "[^"]+"')

    def test_init(self):
        organizer = FakeOrganizer()
        appdata_folder()
        game = game_for(organizer)
        self.assertTrue(any(isinstance(f, er.EldenRingModDataChecker) for f in organizer.features.features))
        self.assertEqual(len(organizer.callbacks["run"]), 1)
        self.assertEqual(len(organizer.callbacks["finished"]), 1)
        self.assertTrue(game.isActive())


class Checker(unittest.TestCase):
    def setUp(self):
        self.organizer = FakeOrganizer()
        self.checker = er.EldenRingModDataChecker(self.organizer, "Elden Ring MO2 Support")
        self.checker.previewing = True

    def normalize(self, paths, **options):
        return tree_paths(self.checker.normalize_standard_layout(make_tree(paths), **options))

    def test_layouts(self):
        cases = {
            ("MyMod.dll", "MyMod.ini", "readme.txt"): ["DLLs/MyMod.dll", "DLLs/MyMod.ini"],
            ("MyMod v1.2/MyMod.dll", "MyMod v1.2/MyMod.ini"): ["DLLs/MyMod.dll", "DLLs/MyMod.ini"],
            ("MyMod/mod/parts/a.partsbnd.dcx", "MyMod/mod/regulation.bin",
             "MyMod/launchmod_eldenring.bat", "MyMod/modengine2/bin/x.dll"):
                ["parts/a.partsbnd.dcx", "regulation.bin"],
            ("A/a.dll", "A/config.ini", "B/b.dll", "B/config.ini"):
                ["DLLs/A/a.dll", "DLLs/A/config.ini", "DLLs/B/b.dll", "DLLs/B/config.ini"],
            ("FreecamMod.dll", "Freecam/settings.ini"):
                ["DLLs/Freecam/settings.ini", "DLLs/FreecamMod.dll"],
            ("Main/parts/a.dcx", "Optional - Extra/parts/b.dcx"): ["parts/a.dcx"],
        }
        for paths, expected in cases.items():
            with self.subTest(paths=paths):
                self.assertEqual(self.normalize(list(paths)), expected)

    def test_reforged_online(self):
        paths = ["ERR/mod/regulation.bin", "ERR/mod/parts/a.dcx",
                 "ERR/internals/modengine/dll/reforged.dll",
                 "ERR/internals/launcher/ReforgedLauncher.exe", "ERR/README.md"]
        online = self.normalize(paths, keep_reforged_online=True)
        self.assertIn("DLLs/reforged.dll", online)
        self.assertIn("_ReforgedOnline/internals/launcher/ReforgedLauncher.exe", online)
        offline = self.normalize(paths)
        self.assertFalse(any(p.startswith("_ReforgedOnline") for p in offline))

    def test_validity(self):
        valid, fixable, invalid = self.checker.VALID, self.checker.FIXABLE, self.checker.INVALID
        cases = [
            (["SeamlessCoop/ersc.dll", "ersc_launcher.exe"], valid),
            (["parts/a.dcx", "notes.txt"], valid),
            (["Wrapper/parts/a.dcx"], fixable),
            (["parts/sub/x.dll", "parts/a.dcx"], valid),  # assets are never walked
            (["Wrap/inner/x.dll", "chr/a.dcx"], fixable),
            (["notes.txt"], invalid),
        ]
        for paths, expected in cases:
            with self.subTest(paths=paths):
                self.assertEqual(self.checker.dataLooksValid(make_tree(paths)), expected)


class SaveRecovery(unittest.TestCase):
    """Steam's account folders set aside for a launch, and put back."""

    def setUp(self):
        self.appdata = appdata_folder()
        self.steam = self.appdata / "EldenRing"
        self.account = self.steam / "76561198000000001"
        self.account.mkdir(parents=True)
        (self.account / "ER0000.sl2").write_bytes(b"steam save")
        (self.steam / "GraphicsConfig.xml").write_text("steam")
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)

    def test_hide_and_restore(self):
        session = self.game._hide_existing_global_save_directories()
        self.assertIsNotNone(session)
        self.assertFalse(self.account.exists())
        self.assertTrue((session / self.account.name / "ER0000.sl2").is_file())
        from basic_games.games.eldenring import recovery_probe

        self.assertTrue(recovery_probe.pending(str(self.appdata)))
        self.assertTrue(self.game._restore_global_save_quarantine(session, self.steam))
        self.game._active_save_quarantine = None
        self.assertEqual((self.account / "ER0000.sl2").read_bytes(), b"steam save")
        self.assertFalse(session.exists())
        self.assertFalse(recovery_probe.pending(str(self.appdata)))

    def test_recovery_at_start(self):
        session = self.game._hide_existing_global_save_directories()
        self.game._active_save_quarantine = None
        self.game._release_save_quarantine(session)
        # the next MO2 puts them back from init(); it waits while a game
        # runs, so this machine's own Elden Ring must not count
        from unittest import mock

        cls = type(self.game)
        with mock.patch.object(cls, "_elden_ring_process_state", return_value=False),                 mock.patch.object(cls, "_launcher_running", return_value=False):
            other = game_for(FakeOrganizer())
        self.assertEqual(other._recovered_sessions, 1)
        self.assertTrue((self.account / "ER0000.sl2").is_file())

    def test_swap_round_trip(self):
        route = self.organizer.profile_dir / "saves"
        (route / self.account.name).mkdir(parents=True)
        (route / self.account.name / "ER0000.sl2").write_bytes(b"route save")
        session = self.game._hide_existing_global_save_directories(swap_route=route)
        # the route's save is where the game looks, Steam's is set aside
        self.assertEqual((self.account / "ER0000.sl2").read_bytes(), b"route save")
        (self.account / "ER0000.sl2").write_bytes(b"played")
        self.assertTrue(self.game._restore_global_save_quarantine(session, self.steam))
        self.assertEqual((route / self.account.name / "ER0000.sl2").read_bytes(), b"played")
        self.assertEqual((self.account / "ER0000.sl2").read_bytes(), b"steam save")
        self.assertEqual((self.steam / "GraphicsConfig.xml").read_text(), "steam")


class Backups(unittest.TestCase):
    def test_backup_once_per_change(self):
        game = er.EldenRingMo2Game()
        route = Path(tempfile.mkdtemp(prefix="er-route-"))
        (route / "76561198000000001").mkdir()
        save = route / "76561198000000001" / "ER0000.sl2"
        save.write_bytes(b"1")
        backups = route.parent / f"{route.name} Backups" / "auto"
        game._backup_route_saves(route, backups, 2)
        game._backup_route_saves(route, backups, 2)
        self.assertEqual(len(list(backups.iterdir())), 1)
        for content in (b"2", b"3"):
            save.write_bytes(content)
            os.utime(save, ns=(save.stat().st_mtime_ns + 10**9, save.stat().st_mtime_ns + 10**9))
            game._backup_route_saves(route, backups, 2)
        self.assertEqual(len(list(backups.iterdir())), 2)


class LaunchHelpers(unittest.TestCase):
    def test_split_arguments(self):
        split = er.EldenRingMo2Game._split_arguments
        self.assertEqual(split('-a "b c" d'), ["-a", "b c", "d"])
        self.assertEqual(split(r'x \"y\"'), ["x", '"y"'])
        self.assertEqual(split(""), [])

    def test_launch_methods(self):
        cls = er.EldenRingMo2Game
        self.assertEqual(cls.launch_method_for(r"C:\x\ReforgedLauncher.exe", {}), ("swap", False))
        self.assertEqual(cls.launch_method_for(r"C:\x\eldenring.exe", {}), ("vfs", False))
        key = os.path.normcase(os.path.abspath(r"C:\x\tool.exe"))
        self.assertEqual(cls.launch_method_for(r"C:\x\tool.exe", {key: "swap"}), ("swap", True))


class NativeDlls(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll", "SeamlessCoop/ersc_settings.ini"])
        self.organizer.add_mod("A", ["DLLs/a.dll", "DLLs/b.dll", "parts/x.dcx"])
        self.game = game_for(self.organizer)

    def loads(self):
        return [Path(load.library()).name for load in self.game.executableForcedLoads()
                if Path(load.library()).name not in ("eldenring_mo2_bridge.dll",
                                                     "eldenring_mo2_me3_compat.dll")]

    def test_default_order(self):
        self.assertEqual(self.loads(), ["a.dll", "b.dll", "ersc.dll"])

    def test_profile_order_and_switch_off(self):
        self.organizer.write_profile_ini(
            "[RevampedNativeProfile]\nversion = 2\n"
            "native_01 = DLLs/b.dll\nmod_01 = A\nenabled_01 = true\n"
            "native_02 = DLLs/a.dll\nmod_02 = A\nenabled_02 = false\n"
            "native_03 = SeamlessCoop/ersc.dll\nmod_03 = Seamless\nenabled_03 = true\n"
        )
        self.assertEqual(self.loads(), ["b.dll", "ersc.dll"])

    def test_convergence_seamless_copy_replaces_the_profile_entry(self):
        # the native profile lists the mod's ersc.dll; with The Convergence
        # its copy loaded as well, two Seamless Co-op DLLs in one game
        root = self.organizer.add_mod("The Convergence", ["DLLs/ErdTools.dll"])
        (root / "meta.ini").write_text("[General]\ngameName=ELDEN RING\nmodid=3419\n", encoding="utf-8")
        self.organizer.write_profile_ini(
            "[RevampedNativeProfile]\nversion = 2\n"
            "native_01 = SeamlessCoop/ersc.dll\nmod_01 = Seamless\nenabled_01 = true\n"
            "native_02 = DLLs/b.dll\nmod_02 = A\nenabled_02 = true\n"
        )
        game = game_for(self.organizer)
        loads = [Path(load.library()) for load in game.executableForcedLoads()]
        seamless = [load for load in loads if load.name == "ersc.dll"]
        self.assertEqual(len(seamless), 1)
        self.assertEqual(seamless[0].parent.parent.name, game.ConvergenceSeamlessFolder)
        names = [load.name for load in loads if load.name in ("ersc.dll", "b.dll", "ErdTools.dll")]
        self.assertLess(names.index("ersc.dll"), names.index("b.dll"))

    def test_initializers_name_the_forced_loads(self):
        self.organizer.write_profile_ini(
            "[RevampedNativeProfile]\nversion = 2\n"
            "native_01 = DLLs/a.dll\nmod_01 = A\nenabled_01 = true\ninitializer_01 = Init\n"
        )
        data = self.game._resolve_revamped_native_profile(self.organizer.profile())
        entries = [e for e in data["entries"] if e.get("initializer")]
        entries += self.game._seamless_initializer()
        ini = self.organizer.profile_dir / "eldenring_mo2_startup.ini"
        self.game._activate_profile_config_environment(ini, entries)
        try:
            value = os.environ.get(self.game.NativeInitializerEnvironment, "")
            loads = {os.path.normcase(os.path.abspath(l.library())) for l in self.game.executableForcedLoads()}
            for line in value.splitlines():
                self.assertIn(os.path.normcase(line.split("|")[0]), loads)
            self.assertEqual(os.environ.get(self.game.ProfileLaunchConfigEnvironment), str(ini))
        finally:
            self.game._restore_profile_config_environment()
        self.assertNotIn(self.game.NativeInitializerEnvironment, os.environ)
        self.assertNotIn(self.game.ProfileLaunchConfigEnvironment, os.environ)


class ConvergenceWithSeamless(unittest.TestCase):
    """Seamless Co-op reads its settings next to its DLL, so with The
    Convergence it runs from a copy whose settings save to cnvco2: their
    co-op saves no longer share ER0000.co2 with Seamless without Convergence."""

    SEAMLESS_INI = (
        "[GAMEPLAY]\nallow_invaders = 1\n[PASSWORD]\ncooppassword = secret\n"
        "[SAVE]\nsave_file_extension = co2\n[LANGUAGE]\nmod_language_override =\n"
    )

    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        root = self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll", "SeamlessCoop/locale/en.json"])
        (root / "SeamlessCoop" / "ersc_settings.ini").write_text(self.SEAMLESS_INI, encoding="utf-8")
        self.seamless_root = root

    def add_convergence(self, extension: str | None = "cnvco2", active: bool = True) -> None:
        root = self.organizer.add_mod("The Convergence", ["DLLs/ErdTools.dll", "regulation.bin"], active)
        (root / "meta.ini").write_text("[General]\ngameName=ELDEN RING\nmodid=3419\n", encoding="utf-8")
        if extension is not None:
            (root / "DLLs" / "ersc_settings.ini").write_text(
                f"[PASSWORD]\ncooppassword =\n[SAVE]\nsave_file_extension = {extension}\n", encoding="utf-8")

    def seamless_load(self, game) -> Path:
        loads = [Path(l.library()) for l in game.executableForcedLoads() if Path(l.library()).name == "ersc.dll"]
        self.assertEqual(len(loads), 1)
        return loads[0]

    def test_without_convergence_the_mod_dll(self):
        game = game_for(self.organizer)
        self.assertEqual(self.seamless_load(game), self.seamless_root / "SeamlessCoop" / "ersc.dll")
        self.add_convergence(active=False)
        game = game_for(self.organizer)
        self.assertEqual(self.seamless_load(game), self.seamless_root / "SeamlessCoop" / "ersc.dll")

    def test_with_convergence_a_copy_saving_to_cnvco2(self):
        self.add_convergence()
        game = game_for(self.organizer)
        dll = self.seamless_load(game)
        self.assertNotEqual(dll.parent, self.seamless_root / "SeamlessCoop")
        self.assertTrue(str(dll).startswith(str(self.organizer.base)))
        self.assertTrue(dll.is_file())
        self.assertTrue((dll.parent / "locale" / "en.json").is_file())
        settings = configparser.ConfigParser(interpolation=None)
        settings.read(dll.parent / "ersc_settings.ini", encoding="utf-8")
        self.assertEqual(settings.get("SAVE", "save_file_extension"), "cnvco2")
        # the user's own settings stay, the mod's file isn't touched
        self.assertEqual(settings.get("PASSWORD", "cooppassword"), "secret")
        self.assertEqual(settings.get("GAMEPLAY", "allow_invaders"), "1")
        self.assertEqual(
            (self.seamless_root / "SeamlessCoop" / "ersc_settings.ini").read_text(encoding="utf-8"),
            self.SEAMLESS_INI)
        # the bridge starts the copy, the DLL usvfs loads
        self.assertEqual([e["library"] for e in game._seamless_initializer()], [dll])

    def test_settings_follow_the_user_and_convergence(self):
        self.add_convergence(extension=None)
        game = game_for(self.organizer)
        dll = self.seamless_load(game)
        self.assertIn("save_file_extension = cnvco2", (dll.parent / "ersc_settings.ini").read_text(encoding="utf-8"))
        # a password changed in the mod reaches the copy on the next launch
        (self.seamless_root / "SeamlessCoop" / "ersc_settings.ini").write_text(
            self.SEAMLESS_INI.replace("secret", "other"), encoding="utf-8")
        dll = self.seamless_load(game_for(self.organizer))
        text = (dll.parent / "ersc_settings.ini").read_text(encoding="utf-8")
        self.assertIn("cooppassword = other", text)
        self.assertIn("save_file_extension = cnvco2", text)

    def test_settings_in_the_system_code_page(self):
        # a password with "ñ" saved by Notepad in Windows-1252 isn't UTF-8:
        # the copy failed and Seamless saved to the shared ER0000.co2
        self.add_convergence()
        own = self.seamless_root / "SeamlessCoop" / "ersc_settings.ini"
        own.write_bytes(self.SEAMLESS_INI.replace("secret", "contraseña").encode("cp1252"))
        dll = self.seamless_load(game_for(self.organizer))
        self.assertNotEqual(dll.parent, self.seamless_root / "SeamlessCoop")
        data = (dll.parent / "ersc_settings.ini").read_bytes()
        self.assertIn("cooppassword = contraseña".encode("cp1252"), data)
        self.assertIn(b"save_file_extension = cnvco2", data)

    def test_other_save_extensions_from_a_mod_are_refused(self):
        # a Convergence settings file naming the game's own save (or Reforged's,
        # or a path) would aim the co-op copy at that save
        for bad in ("sl2", "SL2", "err", "cnv", "co2", "x/../y", "**", "a" * 20, "ｃｎｖ"):
            with self.subTest(extension=bad):
                self.organizer = FakeOrganizer()
                root = self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
                (root / "SeamlessCoop" / "ersc_settings.ini").write_text(self.SEAMLESS_INI, encoding="utf-8")
                self.seamless_root = root
                self.add_convergence(extension=bad)
                game = game_for(self.organizer)
                self.assertEqual(game._convergence_coop_extension(), game.ConvergenceCoopSaveExtension)
        self.assertTrue(type(game)._usable_coop_extension("cnvco2"))
        self.assertTrue(type(game)._usable_coop_extension("co3"))

    def test_extension_read_as_the_save_tool_reads_it(self):
        # "; note", quotes and a leading dot: the same value in both
        self.add_convergence(extension='".cnvx" ; Convergence co-op')
        self.assertEqual(game_for(self.organizer)._convergence_coop_extension(), "cnvx")

    def test_no_launch_stop_without_native_dlls(self):
        # with the mods' DLLs not loaded, a copy that can't be made stops nothing
        self.add_convergence()
        game = game_for(self.organizer)
        blocker = Path(self.organizer.base) / game.ConvergenceSeamlessFolder
        blocker.parent.mkdir(parents=True, exist_ok=True)
        blocker.write_text("not a folder", encoding="utf-8")
        saved = self.organizer.pluginSetting
        self.organizer.pluginSetting = lambda plugin, key: (
            False if key == game.NativeDllSetting else saved(plugin, key))
        try:
            self.assertTrue(game._convergence_seamless_ready(str(Path("eldenring.exe"))))
        finally:
            self.organizer.pluginSetting = saved

    def test_a_failed_copy_loads_no_seamless(self):
        # with the copy impossible, the mod's own DLL would save Convergence's
        # characters into ER0000.co2: no Seamless DLL, and the launch says why
        self.add_convergence()
        game = game_for(self.organizer)
        blocker = Path(self.organizer.base) / game.ConvergenceSeamlessFolder
        blocker.parent.mkdir(parents=True, exist_ok=True)
        blocker.write_text("not a folder", encoding="utf-8")
        loads = [Path(l.library()).name for l in game.executableForcedLoads()]
        self.assertNotIn("ersc.dll", loads)
        self.assertTrue(game._convergence_seamless_error)

    def test_password_characters_and_utf16_survive(self):
        cls = type(game_for(self.organizer))
        text = cls._with_save_extension("[PASSWORD]\r\ncooppassword = a\x0bb c\r\n[SAVE]\r\n", "cnvco2")
        self.assertIn("cooppassword = a\x0bb c", text)
        self.add_convergence()
        own = self.seamless_root / "SeamlessCoop" / "ersc_settings.ini"
        own.write_bytes(self.SEAMLESS_INI.replace("secret", "ü").encode("utf-16"))
        dll = self.seamless_load(game_for(self.organizer))
        data = (dll.parent / "ersc_settings.ini").read_bytes().decode("utf-16")
        self.assertIn("cooppassword = ü", data)
        self.assertIn("save_file_extension = cnvco2", data)

    def test_links_in_seamless_folder_are_not_followed(self):
        outside = Path(tempfile.mkdtemp(prefix="er-outside-"))
        (outside / "secret.txt").write_text("x", encoding="utf-8")
        link = self.seamless_root / "SeamlessCoop" / "elsewhere"
        if subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(outside)],
                          capture_output=True).returncode != 0:
            self.skipTest("no junction")
        self.add_convergence()
        dll = self.seamless_load(game_for(self.organizer))
        self.assertTrue((dll.parent / "locale" / "en.json").is_file())
        self.assertFalse((dll.parent / "elsewhere" / "secret.txt").exists())

    def test_save_section_added_when_missing(self):
        cls = type(game_for(self.organizer))
        text = cls._with_save_extension("[PASSWORD]\ncooppassword = x\n", "cnvco2")
        self.assertIn("[SAVE]\nsave_file_extension = cnvco2", text)
        text = cls._with_save_extension("[SAVE]\n; comment\n[OTHER]\n", "cnvco2")
        self.assertEqual(text.splitlines()[:2], ["[SAVE]", "save_file_extension = cnvco2"])


class LinkedModsFolder(unittest.TestCase):
    """Through a junction, every DLL loads by its path in the mods folder."""

    def test_junction(self):
        appdata_folder()
        store = Path(tempfile.mkdtemp(prefix="er-store-"))
        base = Path(tempfile.mkdtemp(prefix="er-linked-"))
        link = base / "mods"
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(store)], check=True,
                       capture_output=True)
        try:
            organizer = FakeOrganizer(base=base, mods_folder=link)
            organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
            organizer.write_profile_ini(
                "[RevampedNativeProfile]\nversion = 2\n"
                "native_01 = SeamlessCoop/ersc.dll\nmod_01 = Seamless\nenabled_01 = true\n")
            game = game_for(organizer)
            loads = [l.library() for l in game.executableForcedLoads() if "ersc" in l.library()]
            self.assertEqual(len(loads), 1)
            self.assertIn(str(link).casefold(), loads[0].casefold())
        finally:
            subprocess.run(["cmd", "/c", "rmdir", str(link)], capture_output=True)


class OnlinePrograms(unittest.TestCase):
    def test_reforged_launcher(self):
        appdata_folder()
        organizer = FakeOrganizer()
        organizer.add_mod("Reforged", ["_ReforgedOnline/internals/launcher/ReforgedLauncher.exe"])
        game = game_for(organizer)
        titles = [e.title() for e in game.executables()]
        self.assertIn("ELDEN RING Reforged (online)", titles)
        nightreign = game_for(FakeOrganizer(), nightreign=True)
        self.assertNotIn("ELDEN RING Reforged (online)", [e.title() for e in nightreign.executables()])


class Environment(unittest.TestCase):
    def test_boot_boost_and_cleanup(self):
        appdata_folder()
        organizer = FakeOrganizer()
        game = game_for(organizer)
        ini = organizer.write_profile_ini("[Startup]\nboot_boost = 0\n")
        before = dict(os.environ)
        game._activate_profile_config_environment(ini, [])
        self.assertEqual(os.environ.get(game.BootBoostEnvironment), "0")
        game._restore_profile_config_environment()
        self.assertEqual(dict(os.environ), before)

    @staticmethod
    def add_convergence(organizer: FakeOrganizer, active: bool = True) -> None:
        root = organizer.add_mod("The Convergence", ["DLLs/ErdTools.dll", "regulation.bin"], active)
        (root / "meta.ini").write_text(
            "[General]\ngameName=ELDEN RING\nmodid=3419\nversion=3.0.4\n", encoding="utf-8")

    def save_extension(self, organizer: FakeOrganizer, nightreign: bool = False) -> str | None:
        game = game_for(organizer, nightreign)
        ini = organizer.write_profile_ini("[Startup]\n")
        game._activate_profile_config_environment(ini, [])
        try:
            return os.environ.get(game.SaveExtensionEnvironment)
        finally:
            game._restore_profile_config_environment()

    def test_convergence_saves_to_cnv(self):
        # its own launcher (convergence.me3) saves to ER0000.cnv; without the
        # bridge renaming it, it played and overwrote ER0000.sl2
        appdata_folder()
        organizer = FakeOrganizer()
        self.add_convergence(organizer)
        self.assertEqual(self.save_extension(organizer), "cnv")

        by_hand = FakeOrganizer()
        by_hand.add_mod("Convergence", ["me3/convergence.me3", "regulation.bin"])
        self.assertEqual(self.save_extension(by_hand), "cnv")

    def test_convergence_extension_only_when_it_names_the_save(self):
        appdata_folder()
        inactive = FakeOrganizer()
        self.add_convergence(inactive, active=False)
        self.assertIsNone(self.save_extension(inactive))
        # Seamless Co-op names the saves itself (cnvco2 in Convergence's settings)
        seamless = FakeOrganizer()
        self.add_convergence(seamless)
        seamless.add_mod("Seamless", ["SeamlessCoop/ersc.dll", "SeamlessCoop/ersc_settings.ini"])
        self.assertIsNone(self.save_extension(seamless))
        nightreign = FakeOrganizer()
        self.add_convergence(nightreign)
        self.assertIsNone(self.save_extension(nightreign, nightreign=True))

    def test_regulation_fix_with_a_mod_regulation(self):
        # Convergence's regulation.bin doesn't fit in a save: the game said
        # "Failed to save game. Save data is corrupted" without the fix
        appdata_folder()
        organizer = FakeOrganizer()
        self.add_convergence(organizer)
        game = game_for(organizer)
        ini = organizer.write_profile_ini("[Startup]\n")
        game._activate_profile_config_environment(ini, [])
        self.assertEqual(os.environ.get(game.RegulationFixEnvironment), "1")
        game._restore_profile_config_environment()
        self.assertIsNone(os.environ.get(game.RegulationFixEnvironment))

        plain = FakeOrganizer()
        plain.add_mod("Loose", ["parts/am_m_0000.partsbnd.dcx"])
        game = game_for(plain)
        game._activate_profile_config_environment(plain.write_profile_ini("[Startup]\n"), [])
        try:
            self.assertIsNone(os.environ.get(game.RegulationFixEnvironment))
        finally:
            game._restore_profile_config_environment()

    def test_reforged_still_saves_to_err(self):
        appdata_folder()
        organizer = FakeOrganizer()
        organizer.add_mod("Reforged", ["DLLs/reforged.dll"])
        self.add_convergence(organizer)
        self.assertEqual(self.save_extension(organizer), "err")


if __name__ == "__main__":
    unittest.main()
